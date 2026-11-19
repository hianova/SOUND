---
name: sound_gate
description: Deterministic SOUND epistemic & normative submission gate (`established`, `defeated`, `conflictUndecided`, `penumbraOpen`) for SWE tasks.
scripts:
  - scripts/gate_cli.py
---

# `sound_gate` Skill

Use `scripts/gate_cli.py` (pure Python 3 stdlib, zero external dependencies) via `run_skill_script` or `run_command` to audit task progress before committing a fix or calling `submit_patch`.

## Commands

- `python3 scripts/gate_cli.py init --workspace . --task-id <id>`
- `python3 scripts/gate_cli.py record-localize --workspace . --files path/a.py path/b.py --blast-checked`
- `python3 scripts/gate_cli.py record-repro --workspace . --observed`
- `python3 scripts/gate_cli.py record-hyp --workspace . --id h1 --description "..." [--supported-by-repro] [--contradicts h2]`
- `python3 scripts/gate_cli.py record-regression --workspace . --passed` (or `--failed`)
- `python3 scripts/gate_cli.py check-submit --workspace . [--strict]`
- `python3 scripts/gate_cli.py status --workspace .`

## Actions Returned

- `ALLOW_SUBMIT`: All preconditions (`fact_patch_nonempty`, `fact_syntax_ok`, `fact_localized`, `fact_repro_obs`, `fact_blast_ok`, `fact_reg_pass`, `norm_spec_clear`, hypothesis convergence) are `established` in `I*`. Safe to call `submit_patch`.
- `DEFER_PENUMBRA`: Targeted regression suite has not run yet (`fact_reg_pass` is `penumbraOpen`). Run tests before submitting.
- `REPAIR_HYPOTHESES`: Competing mutually attacking root-cause hypotheses are in `conflictUndecided`. Disprove or confirm with targeted checks before editing/submitting.
- `REQUEST_CLARIFICATION`: Ambiguous task specification (`norm_spec_clear` is `penumbraOpen`).
- `BLOCK_SUBMIT`: Empty patch, Python syntax error, missing localization/repro, or failed regression (`defeated`).
