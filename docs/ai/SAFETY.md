# AI Slice Assistant Safety

## Scope And Guardrails

AI Slice Assistant is designed for **slicer settings assistance only**.
It is not a general chatbot and should not be used for non-slicing topics.

Core safety boundaries:

- JSON contract responses only.
- Contract/schema validation before UI use.
- Business-rule checks (version, limits, value ranges).
- Allowlist-based setting filtering and value validation.
- Apply pipeline is controlled and reversible with undo.

## Safe Mode (Default ON)

Safe Mode is enabled by default and adds stricter runtime limits:

- Maximum of 10 recommended changes shown/applied.
- High-risk keys are blocked.
- Temperature/flow/speed related changes are marked as requiring user confirmation.

## Validation And Repair

Before recommendations are accepted:

1. JSON parsing must succeed.
2. Schema validation must pass.
3. Business rules must pass.

If validation fails, a repair request is sent with validation errors, and retries are limited.
If still invalid, output is rejected.

## Apply Safety

When applying recommendations:

- Only allowlisted keys are eligible.
- Values must match expected type and bounds.
- Apply is atomic (all-or-nothing).
- Original values are captured for undo.

## Operator Responsibility

AI suggestions can be wrong or suboptimal for your setup.
You are responsible for reviewing and validating each proposed change before printing.

## Disclaimer

AI suggestions are not guaranteed. Always verify recommendations before applying them to real prints.
