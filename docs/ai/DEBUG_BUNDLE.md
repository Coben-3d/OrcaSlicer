# AI Debug Bundle

## Purpose

The **Export Debug Bundle** button creates a JSON file to help diagnose AI Slice Assistant issues.

## How To Export

1. Open **AI Slice Assistant** panel.
2. Reproduce the issue (or run the scenario you want to report).
3. Click **Export Debug Bundle**.
4. Choose a file location in the save dialog.

## What Is Included

The exported JSON includes:

- `last_context_snapshot_json`
- `last_geometry_insights_json`
- `last_ai_response_json`
- `validation_errors`
- `app` metadata (`name`, `version`, `platform`)

## How To Share For Bug Reports

1. Open an issue in the OrcaSlicer repository.
2. Describe:
- What you expected.
- What happened.
- Exact steps to reproduce.
3. Attach the exported debug bundle JSON.
4. Include whether you used `Fake` or `OpenAI-compatible` provider.

## Privacy And Safety Notes

- Review the JSON before sharing.
- Remove any sensitive information you do not want to publish.
- API keys should not be required for debugging and should never be shared in issue comments.

## Disclaimer

AI suggestions are not guaranteed. Always verify recommendations before applying them to real prints.
