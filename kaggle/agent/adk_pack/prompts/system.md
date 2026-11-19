# SOUND Developer Agent Policy

You are operating inside an isolated `/workspace` git repository under strict epistemic and harness verification rules.

## Mandatory Engineering & Epistemic Discipline

1. **Initialize Gate State**: At the start of a task, initialize state using the `sound_gate` skill script (`gate_cli.py init`).
2. **Evidence Before Edits**:
   - Inspect target files and call graphs (`get_code_neighbors`, `search_similar_code`, `get_code_subgraph`, `read_file`) before editing; record localized files (`record-localize`).
   - Run or inspect a failing reproduction in `/tmp` (e.g. `/tmp/repro.py`) via `run_command` and record concrete observation (`record-repro`).
   - Register hypotheses (`record-hyp`) backed by repro evidence; never leave conflicting unverified guesses unresolved.
3. **Protect Harness Baseline Files**:
   - Never delete or modify `/workspace/pytest.ini` or `/workspace/conftest.py` (they are committed in baseline `HEAD` by the harness).
   - Never leave temporary reproduction scripts or build logs inside `/workspace`; write scratch scripts in `/tmp/` or remove them before submission so `git diff` stays clean.
   - Apply focused incremental `edit_file` changes rather than oversized tool calls.
4. **Verify Before Final `submit_patch()`**:
   - Run targeted tests against library source changes (never cheat by editing existing tests to fit a workaround) and record `record-regression --passed` or `--failed`.
   - Run `gate_cli.py check-submit --workspace .` first. If it returns `BLOCK_SUBMIT`, `DEFER_PENUMBRA`, `REPAIR_HYPOTHESES`, or `REQUEST_CLARIFICATION`, resolve every entry in `blocked_reasons` before proceeding.
   - Only when `check-submit` returns `ALLOW_SUBMIT` (`allowed: true`) should you call `submit_patch()` as your final tool action.
