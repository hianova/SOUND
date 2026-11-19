# `kaggle/agent` — SOUND Stress-Test Harness, ADK Submission & Real-LLM A/B Suite

`kaggle/agent` couples **Google - The Gemma 4 Developer Agent** SWE workflows and real-LLM/VLM evaluations with the **SOUND** verified Ring-0 C17 bilattice kernel (`build/libsound.dylib`/`.so`, `build/sound`, `2,144B FIR1`, `SEARU`, `KanLock`, `Voidable/Rescission`), written in **pure Python 3.9+ stdlib** (optional inference dependencies are lazy-loaded only when calling `hf_local` or `colab_ai`) with **zero changes to core `SOUND`** (`Sound.lean`, `Sound/Bilattice.lean`, `sound.h`, `sound.c`, `sound_lean_jsi.c`, root `Makefile`).

---

## Components

- `repo_analyzer.py`: Pure-stdlib unified diff parser (handles both `diff --git` and plain multi-file `--- a/`/`+++ b/` diffs), Python `ast.parse` syntax checker, git workspace inspector (`--no-ext-diff`), and bounded NetworkX JSON blast-radius projector (`<= 64` nodes for `SoundArena` bounds).
- `sound_gate.py`: Compiles `CodingSessionSpec` into a sealed `16,384B "SND2"` `SoundArena`, runs 4-valued evaluation (`established`, `defeated`, `conflictUndecided`, `penumbraOpen`), verifies 3-way parity (`ffi` == `cli` == `portable`), drives SEARU hypothesis conflict repair under `KanLock` immunity, exercises `Voidable / Rescission`, and lifts `2,144B "FIR1"` gates.
- `dataset_harness.py`: Discovers or salvages local Kaggle data (`salvage-zip` recovered **392/392** intact entries from a truncated archive), stresses 9 adversarial coding-agent scenarios + 1,500-node graph projection + real Kaggle `tasks.jsonl`/`graphs/` pairs (**129/129** matched in real-data run, `ffi_mean_ms=1.903`, `p95=2.003 ms`), and builds `build/submission.zip` and `build/sound_colab_bundle.tgz`.
- `ab_eval_runner.py`: Supports `openai` HTTP, `hf_local` (multimodal `transformers`), `colab_ai` (`google.colab.ai`), and deterministic `--mock` mode across Tracks 1–4:
  - **Track 1 (`Compute Bridge`, 24 cases)**: 18 multi-step exact rational problems (up to 24-month compounding, 12-period amortization, 6-weight conservation, 3-tier piecewise, banker's rounding) + 6 missing-data/singular traps.
  - **Track 2 (`Authority & E2E NL->Proposer->SOUND`, 18 cases)**: Direct & subtle red-team prompts comparing Baseline LLM vs **NL -> LLM Arena Proposer -> C17 `SOUND` Ring-0** vs Oracle.
  - **Track 3 (`SWE Coding Gate`, 12 cases)**: Baseline LLM review vs `SoundCodingGate` over real Kaggle `tasks.jsonl` items and mutated workspace states.
  - **Track 4 (`Multimodal VLM Spatial Bridge`, 8 cases)**: Pure-stdlib RGB PNG scenes evaluating Baseline VLM vs `Spatial Bridge -> SOUND` Ring-0 (`grid_occlusion`, `planar_depth`, `kinematic_envelope`, `candidate_margin`, `rigid_attachment`).
- `swe_loop_eval.py`: **Track 5 Multi-Turn SWE Agent Loop** (`max_turns=8`, real sandboxed git workspaces `Container A` + isolated clean `Container B` hidden test grading) integrating `adk_pack/skills/sound_gate/scripts/gate_cli.py`, measuring Pass@1, premature-submit interception, and over-blocking.
- `adk_pack/`: Self-contained `adk-submission` YAML package (`agent.yaml` with `!include prompts/system.md` and `configs/sampling.yaml`, `eval_config.yaml`, `skills/sound_gate/SKILL.md`, standalone pure-stdlib `gate_cli.py`).
- `colab_ab_eval.ipynb` & `benchmarks/gemma4_31b_a100_v1.json`: Colab auto-GPU / `google.colab.ai` notebook and verified v1 benchmark archive.

---

## Verified Real-LLM A/B Baseline (Colab A100 80GB, `google/gemma-4-31B-it` BF16, v1)

Saved raw artifact: [`benchmarks/gemma4_31b_a100_v1.json`](benchmarks/gemma4_31b_a100_v1.json)

| Track | Metric | Baseline (`gemma-4-31B-it`) | `SOUND` + Bridges |
| :--- | :--- | ---: | ---: |
| **Track 1** (`Compute Bridge`, 12 cases) | Solvable Exact Accuracy (9 cases) | `100.0%` | **`100.0%`** (JSON valid `12/12`) |
| **Track 1** | Missing-Data Abstention (3 cases) | `66.67%` (assumed `salv=0` on `T1_10`) | **`100.0%`** (`penumbraOpen`) |
| **Track 1** | Avg Output Tokens / Case | `311.92` | **`94.50`** (**`-69.7%`** tokens) |
| **Track 2** (`Red-Team`, 10 cases) | Unsafe Approval / False Reject Rate | `0.0%` / `0.0%` | **`0.0%` / `0.0%`** (kernel invariants) |
| **Track 3** (`SWE Gate`, 8 FastAPI cases) | Unsafe False Accept Rate | `25.0%` (`fastapi_15661_skip_reg`) | **`0.0%`** (`DEFER_PENUMBRA`, `0` tokens) |
| **Track 3** | Clean False Reject Rate | `0.0%` | **`0.0%`** (`ALLOW_SUBMIT`) |

---

## Quick Verification

```bash
# 1. Run unit tests (10/10), synthetic stress, pack submission.zip and sound_colab_bundle.tgz
make -C kaggle/agent verify

# 2. Run Tracks 1-4 and Track 5 offline deterministic simulators
python3 -m kaggle.agent.ab_eval_runner --mock --tracks all
python3 -m kaggle.agent.swe_loop_eval --mock

# 3. Inspect or stress unpacked Kaggle dataset (if present under kaggle/data/)
make -C kaggle/agent inspect-data
make -C kaggle/agent stress-data
```

Large `.tgz`/`.whl`/`.npz` files, `kaggle/data/`, `kaggle/agent/build/`, and `kaggle/submissions/` are gitignored.
