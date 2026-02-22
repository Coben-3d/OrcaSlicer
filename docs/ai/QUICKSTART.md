# AI Slice Assistant Quickstart

## What It Does

AI Slice Assistant helps you review proposed **slicer/print setting** changes for the current project.
It builds a context snapshot, sends it to the selected provider, validates the JSON response, and shows a diff you can apply or undo.

## What It Does Not Do

- No general-purpose chat assistant.
- No direct printer control or machine actions.
- No guarantee that suggestions are correct for your printer/material/part.

## Enable The Panel

1. Open OrcaSlicer.
2. Use the **View** menu and enable **AI Slice Assistant**.

## Configure Provider

1. Open **Preferences**.
2. Go to **Online > AI**.
3. Select provider:
- `Fake` for deterministic offline testing.
- `OpenAI-compatible` for HTTP provider testing.
4. If using OpenAI-compatible, set:
- Base URL
- API key
- Model
- Timeout / Max tokens / Temperature
5. Click **Test** to verify connection.

## First Test (Recommended)

1. In the AI panel, click **Copy Context**.
2. Enter a focused prompt, for example:
- `Improve bridging quality while keeping print time reasonable.`
3. Click **Send**.
4. Check status in chat:
- `Valid`, `Repaired`, or `Rejected`.
5. Review `recommended_changes` and details.
6. Optionally click **Apply Selected**.
7. If needed, click **Undo Last Apply**.

## Safe Mode

Safe Mode is enabled by default in **Preferences > Online > AI**.
When enabled, it limits risk by:

- Capping recommendations to 10.
- Blocking high-risk keys.
- Requiring user confirmation for temperature/flow/speed-related changes.

## Important Disclaimer

AI suggestions are not guaranteed. Always verify recommendations before applying them to real prints.
