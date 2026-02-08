#include "openai_compat_provider.h"

#include "nlohmann/json.hpp"
#include "slic3r/Utils/Http.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <utility>

#include <wx/secretstore.h>

namespace Slic3r {
namespace AI {
namespace Providers {

namespace {

using nlohmann::json;

constexpr const char* k_secret_store_service = "orcaslicer.ai.openai_compat";
constexpr const char* k_secret_store_user = "api_key";

std::string trim_copy(const std::string& value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
        ++start;

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;

    return value.substr(start, end - start);
}

std::string strip_code_fence(std::string text)
{
    text = trim_copy(text);
    if (text.rfind("```", 0) != 0)
        return text;

    const size_t first_newline = text.find('\n');
    if (first_newline == std::string::npos)
        return text;

    const size_t closing_fence = text.rfind("```");
    if (closing_fence == std::string::npos || closing_fence <= first_newline)
        return text;

    return trim_copy(text.substr(first_newline + 1, closing_fence - first_newline - 1));
}

std::string normalize_endpoint(const std::string& base_url)
{
    std::string normalized = trim_copy(base_url);
    if (normalized.empty())
        normalized = "https://api.openai.com/v1";

    while (!normalized.empty() && normalized.back() == '/')
        normalized.pop_back();

    if (normalized.size() >= 17 && normalized.compare(normalized.size() - 17, 17, "/chat/completions") == 0)
        return normalized;

    return normalized + "/chat/completions";
}

std::string build_error_contract(const std::string& summary, const std::string& refusal, const std::string& warning)
{
    json output = json::object();
    output["contract_version"] = "0.1.0";
    output["summary"] = summary;
    output["recommended_changes"] = json::array();
    output["questions"] = json::array();
    output["refusals"] = json::array({refusal});
    output["warnings"] = json::array({warning});
    return output.dump(2);
}

std::string extract_content(const json& response)
{
    if (response.contains("choices") && response.at("choices").is_array() && !response.at("choices").empty()) {
        const json& first = response.at("choices").at(0);

        if (first.contains("message") && first.at("message").is_object()) {
            const json& message = first.at("message");

            if (message.contains("content") && message.at("content").is_string())
                return message.at("content").get<std::string>();

            if (message.contains("content") && message.at("content").is_array()) {
                std::string merged;
                for (const json& item : message.at("content")) {
                    if (item.is_object() && item.contains("text") && item.at("text").is_string()) {
                        if (!merged.empty())
                            merged.push_back('\n');
                        merged += item.at("text").get<std::string>();
                    }
                }
                if (!merged.empty())
                    return merged;
            }
        }

        if (first.contains("text") && first.at("text").is_string())
            return first.at("text").get<std::string>();
    }

    if (response.contains("output_text") && response.at("output_text").is_string())
        return response.at("output_text").get<std::string>();

    return {};
}

std::string extract_first_json_object(std::string text)
{
    text = trim_copy(text);
    const size_t start = text.find('{');
    const size_t end   = text.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end <= start)
        return {};

    return text.substr(start, end - start + 1);
}

json build_messages(const ProviderRequest& request)
{
    static const std::string system_prompt =
        "You are the OrcaSlicer AI Slice Assistant.\\n"
        "Scope: slicer and print settings only; refuse all non-slicing topics.\\n"
        "You MUST output JSON only and no markdown or code fences.\\n"
        "Use contract version 0.1.0 with required fields: contract_version, summary, recommended_changes, questions, refusals, warnings.\\n"
        "recommended_changes length must be <= 20.\\n"
        "Each recommended_changes item must include: id,key,value,value_type,reason,impact,confidence,applies_to,tags,requires_user_confirmation.\\n"
        "impact values are integers 1..10, confidence is 0..1.\\n"
        "When a repair request is provided, output corrected JSON only, preserving intent and fixing validation errors.";

    std::ostringstream user_payload;
    user_payload << "user_intent:\n" << request.user_message << "\n\n";
    user_payload << "context_snapshot_json:\n";
    user_payload << (request.context_snapshot_json.empty() ? "{}" : request.context_snapshot_json) << "\n\n";
    user_payload << "geometry_insights_json:\n";
    user_payload << (request.geometry_insights_json.empty() ? "{}" : request.geometry_insights_json) << "\n";

    json messages = json::array();
    messages.push_back(json{{"role", "system"}, {"content", system_prompt}});
    messages.push_back(json{{"role", "user"}, {"content", user_payload.str()}});
    return messages;
}

bool parse_json_only_response(const std::string& raw_content, std::string& normalized_json, std::string& error_message)
{
    normalized_json.clear();
    error_message.clear();

    std::string candidate = strip_code_fence(raw_content);
    if (candidate.empty()) {
        error_message = "empty model response";
        return false;
    }

    try {
        json parsed = json::parse(candidate);
        normalized_json = parsed.dump(2);
        return true;
    } catch (...) {
        // Continue with object extraction fallback below.
    }

    candidate = extract_first_json_object(candidate);
    if (candidate.empty()) {
        error_message = "response is not valid JSON";
        return false;
    }

    try {
        json parsed = json::parse(candidate);
        normalized_json = parsed.dump(2);
        return true;
    } catch (const std::exception& ex) {
        error_message = ex.what();
        return false;
    }
}

bool perform_chat_completion(const OpenAICompatConfig& config,
                             const ProviderRequest& request,
                             std::string& out_body,
                             std::string& out_error,
                             unsigned& out_status)
{
    out_body.clear();
    out_error.clear();
    out_status = 0;

    if (trim_copy(config.api_key).empty()) {
        out_error = "missing api_key";
        return false;
    }
    if (trim_copy(config.model).empty()) {
        out_error = "missing model";
        return false;
    }

    const std::string endpoint = normalize_endpoint(config.base_url);

    json payload = json::object();
    payload["model"] = config.model;
    payload["messages"] = build_messages(request);
    payload["temperature"] = config.temperature;
    payload["max_tokens"] = config.max_tokens;
    payload["response_format"] = json::object({{"type", "json_object"}});

    bool success = false;
    try {
        Slic3r::Http::post(endpoint)
            .header("Authorization", "Bearer " + config.api_key)
            .header("Content-Type", "application/json")
            .set_post_body(payload.dump())
            .timeout_max(config.timeout_seconds > 0 ? config.timeout_seconds : 30)
            .on_complete([&](std::string body, unsigned status) {
                success = (status >= 200 && status < 300);
                out_body = std::move(body);
                out_status = status;
            })
            .on_error([&](std::string body, std::string error, unsigned status) {
                success = false;
                out_body = std::move(body);
                out_error = std::move(error);
                out_status = status;
            })
            .perform_sync();
    } catch (const std::exception& ex) {
        out_error = ex.what();
        success = false;
    }

    return success;
}

} // namespace

OpenAICompatProvider::OpenAICompatProvider(OpenAICompatConfig config)
    : m_config(std::move(config))
{}

std::string OpenAICompatProvider::run(const ProviderRequest& request) const
{
    std::string body;
    std::string error;
    unsigned status = 0;

    if (!perform_chat_completion(m_config, request, body, error, status)) {
        std::ostringstream warning;
        warning << "http_error status=" << status;
        if (!error.empty())
            warning << " msg=" << error;
        return build_error_contract("Le provider OpenAI-compatible n'a pas repondu correctement.",
                                    "provider_request_failed",
                                    warning.str());
    }

    json parsed_response;
    try {
        parsed_response = json::parse(body);
    } catch (const std::exception& ex) {
        return build_error_contract("Reponse HTTP recue mais format JSON invalide.",
                                    "provider_response_not_json",
                                    ex.what());
    }

    std::string content = extract_content(parsed_response);
    if (content.empty()) {
        if (parsed_response.is_object() && parsed_response.contains("contract_version")) {
            return parsed_response.dump(2);
        }
        return build_error_contract("Reponse provider sans contenu exploitable.",
                                    "provider_response_missing_content",
                                    "empty_message_content");
    }

    std::string normalized_json;
    std::string parse_error;
    if (!parse_json_only_response(content, normalized_json, parse_error)) {
        return build_error_contract("Le provider n'a pas renvoye un JSON valide.",
                                    "provider_response_invalid_json",
                                    parse_error.empty() ? "json_parse_failed" : parse_error);
    }

    return normalized_json;
}

bool OpenAICompatProvider::test_connection(std::string& error_message) const
{
    error_message.clear();

    ProviderRequest req;
    req.user_message = "Connection test. Return JSON only: {\"ok\":true}.";

    std::string body;
    std::string error;
    unsigned status = 0;

    if (!perform_chat_completion(m_config, req, body, error, status)) {
        std::ostringstream oss;
        oss << "status=" << status;
        if (!error.empty())
            oss << " error=" << error;
        if (!body.empty())
            oss << " body=" << trim_copy(body).substr(0, 160);
        error_message = oss.str();
        return false;
    }

    if (body.empty()) {
        error_message = "empty response body";
        return false;
    }

    return true;
}

bool OpenAICompatProvider::save_api_key_securely(const std::string& api_key, std::string& error_message)
{
    error_message.clear();

#ifdef __APPLE__
    wxSecretStore store = wxSecretStore::GetDefault();
    if (!store.IsOk()) {
        error_message = "system keychain not available";
        return false;
    }

    wxSecretValue secret(wxString::FromUTF8(api_key.c_str()));
    if (!store.Save(k_secret_store_service, k_secret_store_user, secret)) {
        error_message = "failed to save api key to system keychain";
        return false;
    }

    return true;
#else
    (void) api_key;
    error_message = "secure key storage is unavailable on this platform";
    return false;
#endif
}

bool OpenAICompatProvider::load_api_key_securely(std::string& api_key, std::string& error_message)
{
    api_key.clear();
    error_message.clear();

#ifdef __APPLE__
    wxSecretStore store = wxSecretStore::GetDefault();
    if (!store.IsOk()) {
        error_message = "system keychain not available";
        return false;
    }

    wxString username;
    wxSecretValue secret;
    if (!store.Load(k_secret_store_service, username, secret) || !secret.IsOk()) {
        error_message = "api key not found in system keychain";
        return false;
    }

    api_key.assign(static_cast<const char*>(secret.GetData()), secret.GetSize());
    if (api_key.empty()) {
        error_message = "empty api key in system keychain";
        return false;
    }

    return true;
#else
    error_message = "secure key storage is unavailable on this platform";
    return false;
#endif
}

bool OpenAICompatProvider::clear_api_key_securely(std::string& error_message)
{
    error_message.clear();

#ifdef __APPLE__
    wxSecretStore store = wxSecretStore::GetDefault();
    if (!store.IsOk()) {
        error_message = "system keychain not available";
        return false;
    }

    if (!store.Delete(k_secret_store_service)) {
        error_message = "failed to clear api key from system keychain";
        return false;
    }

    return true;
#else
    error_message = "secure key storage is unavailable on this platform";
    return false;
#endif
}

} // namespace Providers
} // namespace AI
} // namespace Slic3r
