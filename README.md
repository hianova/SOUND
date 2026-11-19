# SOUND

**意圖 × 事實 × 推理（Intent × Fact × Reasoning）**

`SOUND` 是建構於 [`FLOW v8.0.0`](../FLOW/README.md) SSOT 基底之上的**認識論與規範健全性驗證基礎庫（Epistemic & Normative Soundness Kernel）**。基礎庫本身**不耦合任何上層應用、不編解碼自然語言明文、也不內建 DSL**：自然語言／LLM 結構化抽取／Latent-to-ABI 橋接均留在外圍獨立模組，核心僅透過 Merkle 封印的 `16,384B`（`256 × 64B`）`SoundArena` 與 Pure-GPR C17 ABI 執行驗證。

---

## 一、架構分層（FLOW SSOT × SOUND Kernel × 外圍應用模組）

| 層級 | 所在模組 | 核心職責與邊界 |
| :--- | :--- | :--- |
| **數學基底（SSOT）** | [`../FLOW/`](../FLOW/README.md)（`Flow.Core`、`Flow.Schema`、`Flow.Morphism`、`searu.h`、`flow_ir_wire.h`）＋ [`Sound/Bilattice.lean`](Sound/Bilattice.lean) | 領域無關的 Lean 4 機器驗證（`46` 定理、`0 sorryAx`、`leanchecker`）：四態雙格交替不動點 $\Gamma / \Gamma^2$、統一 HoTT Fallback Fiber 總遷移（`SoundFiber` / `potestativeFiberStepBV64` / `batchAdmit2BV64`）、Lex Specialis 加權能量與 KanLock 支配定理（`lexSpecialisPairEnergy`）、Cross-64-Node Sheaf Chart Gluing（`chartGlueValidBV64`）、Prepared-Statement 政策／資料隔離謂詞（`preparedPolicyAdmitBV64`）、512-bit 零 `memcpy` C17 Arena Co-Generator（`Flow.Schema`）與 `"FIR1"` 通用線性態射編碼。 |
| **Layer 0（零信任 Prepared-Statement × SipHash × HoTT 入口閘門）** | [`sound.h`](sound.h) `sound_policy_seal_keyed` / `sound_policy_verify_keyed` / `sound_admit_forest_batch_keyed` / `sound_admit_forest_with_utterance` ＋ `Sound.Bilattice` | 以統一全函數遷移 `sound_fiber_t`（`COMMITTED` 或 1..13 `HOLD/FALLBACK` 纖維，記錄於 `forest_ctrl.last_fiber` / `fiber_epoch`）取代傳統 OOP 錯誤碼；支援 Pure-GPR SipHash-2-4 金鑰 Policy MAC（含降級攻擊防禦）、2-Phase 原子批次繫結（全過或零寫入回滾）、Layer-0 原文 Span Hash 比對與 `PROPOSER_LOW_EVID` 傳聞證據上限；將不可信提議者嚴格限制在預編譯參數槽（`bind_mask ⊥ policy_mask`），攔截免疫／政策／未綁定槽位、跨層 `sort`、特權升級與拓撲注入。 |
| **Layer 1（單調日誌 × Hohfeld 守恆 × Cross-Chart Gluing）** | [`sound.h`](sound.h) `PhysLog` ＋ `SoundNormative64` ＋ `SoundChartGlue64` | 物理事件只增不減進入 64-slot 環形 `PhysLog`（FNV-1a 鏈）；規範層維護 Hohfeld `Duty+/Duty-/Power/Immunity/Voidable/RescissionPower`；並提供 Cross-64-Node Sheaf Chart Gluing（`sound_chart_export_port` / `sound_chart_glue_import` / `sound_chart_glue_verify`，持久化於 `flow_aux[37]`），將已成立無衝突出口以 Merkle 憑證貼合至下游 Arena 事實繫結槽。 |
| **Layer 2（四態雙格核心 × 加權 SEARU × 雙路徑 FIR1 升格）** | [`Sound.lean`](Sound.lean) ≡ [`sound.h`](sound.h) | `Sound.lean` 以 `0` 本地定理配置 256-slot `soundArenaSchema`，於 Elaboration-Time 透過 `sound_lean_jsi.c` 驗證 10 組 golden `16,384B` `.snd` 與 `2,144B` `"FIR1"` 位元等價。`sound_eval` 計算四態雙格與雙向休謨防火牆；`sound_searu_repair_conflict_weighted` 支援 Lex Specialis 法規優先權重（KanLock 免疫無條件優先）；`sound_lift_to_fir`（權限閘門）與 `sound_lift_policy_eval_to_fir`（將封裝政策直接編譯為 combinational `FLOW_IR_MULT_LINEAR` `.fir` 電路）皆維持 Pure-GPR Ring-0 潔淨。 |
| **外圍應用與明文模組** | [`modules/`](modules/README.md) ＋ 外部獨立模組 | 提供 [`prepared_policy.py`](modules/common/prepared_policy.py)（SQLite 式 `PreparedPolicy` SDK）、[`modules/latex/`](modules/latex/)（數學／法律語意 LaTeX 問答驗證與假設／要件消融探針）、[`compute_bridge.py`](modules/common/compute_bridge.py) 與 [`spatial_bridge.py`](modules/common/spatial_bridge.py)；Python FFI（[`arena_codec.py`](modules/common/arena_codec.py)）以 `SoundFiber` 值退縮承接核心纖維，與 `SOUND` 基礎庫完全解耦。 |

---

## 二、檔案結構

* [**`Sound.lean`**](Sound.lean)：按需引入 `Flow.Core` / `Flow.Schema` / `Flow.Morphism` ＋ [`Sound/Bilattice.lean`](Sound/Bilattice.lean) 的純配置與 AOT `SoundArena` / `Flow.Morphism` `.fir` 編譯器（`0` 本地 `theorem/lemma/axiom`、通過 `leanchecker`），負責產出 `build/sound_schema_abi.h` 並執行 `#eval Sound.compileTimeBoundaryGate`。
* [**`sound.h`**](sound.h)：單標頭 Pure-GPR C17 核心（零堆積、零浮點、`-ffreestanding -nostdlib -fno-builtin -mgeneral-regs-only` Ring-0 潔淨、相容 C++20），含 SipHash-2-4 HMAC、原子批次入口閘門、Lex Specialis 加權 SEARU、Cross-Chart Gluing、`sound_lift_to_fir` 與 `sound_lift_policy_eval_to_fir`。
* [**`sound_lean_jsi.c`**](sound_lean_jsi.c)：Lean 4 動態外掛 JSI 橋接（僅編入 `libsound_jsi`，絕不進入 `libsound.a` 或 `build/sound`）。
* [**`sound.c`**](sound.c) ＋ [**`Makefile`**](Makefile) ＋ [**`tools/sound_sabotage_check.py`**](tools/sound_sabotage_check.py)：零堆積、零 `leanshared` 獨立 CLI（`selftest` T1..T10 / `verify` / `repair` / `lift-fir` / `lift-policy-fir`）與 18-mutant 單點破壞變異測試閘門（`make sabotage-check`）。

---

## 三、一鍵驗證閘門

```bash
make fmt && make clean && make verify && make sabotage-check
```

驗證內容涵蓋：
1. **Gate 0 / Gate 1（Lean 4 SSOT + `leanchecker` + ABI）**：驗證 `Sound/Bilattice.lean` 全 `46` 條定理（`0 sorryAx`、`leanchecker Sound.Bilattice`）與 `Sound.lean` 零本地定理／零佔位符、通過 `leanchecker Sound`，並產出零 `memcpy` 的 `build/sound_schema_abi.h`。
2. **Gate 2（Ring-0 Pure-GPR + C++20 + `clang-tidy`）**：驗證 `0 undef`、`0 udiv/sdiv`、`0 blr/br`、`0 FPU/SIMD`、`16,384B / 64B` C++20 ABI 與靜態分析零警告。
3. **Gate 3（Zero-Lean-Runtime 獨立執行檔）**：驗證 `libsound.a`、`libsound.dylib` 與 `build/sound` 零堆積、零浮點、零 `leanshared`。
4. **Gate 4（JSI Elaboration-Time 閘門）**：驗證 10 組 `16,384B` `.snd` 與 `2,144B` `"FIR1"` 電路 Lean↔C17 位元對等、`flow_ir_eval_u64` 執行、`SEARU` 衝突修復、Layer 0 Prepared-Statement 政策／資料隔離、Layer 1 測試與 4,096 組隨機圖不變量 Fuzz。
5. **Gate 5（CLI 實測與 `clang-format`）**：執行 `sound selftest`（`T1..T10` 含 SipHash-2-4、原子批次、`LOW_EVID`、Lex Specialis、Chart Gluing 與 Policy-Eval FIR 編譯）、`verify`、`repair`、`lift-fir`、`cmp -s` 位元比對與格式檢查。
6. **Gate 6（單點破壞變異測試 `make sabotage-check`）**：注入 18 項跨全層（雙格／休謨／Hohfeld／Prepared-Statement／SipHash 批次／加權 SEARU／Chart Gluing & FIR）單點語義破壞並以 `sound selftest` 實測擊殺率（`18/18 = 100.0%`）。

---

## 四、外圍橋接模組、Kaggle Agent 與實測總覽

所有外圍 Python／ADK／評測程式皆放在 [`modules/`](modules/README.md) 與 [`kaggle/agent/`](kaggle/agent/README.md)（純 Python 3.9+ stdlib），**核心 `SOUND` 檔案維持零耦合、零改動**：

* **領域無關量化與空間橋接（`modules/common/`）**：
  * [`compute_bridge.py`](modules/common/compute_bridge.py)：精確有理數 `Fraction` DAG 求值、`pro_rata` 守恆配置、`recur_fold`、`solve_linear`、雙向 `SOUND` 節點閘控、`flow_aux[38][8]` (`CB_AUX01`) Merkle 封印、`FLOW` `"FIR1"` 整數子集降階，以及零 `import` Lean 4 `decide` 證明產出。
  * [`spatial_bridge.py`](modules/common/spatial_bridge.py)：平面深度異常突刺（`planar_depth`）、多槽位前景遮擋連通塊（`grid_occlusion`）、多幀剛體附著偏移（`rigid_attachment`）、運動學煞停包絡（`kinematic_envelope`）與候選邊際檢定（`candidate_margin`）。
  * 領域模組驗證：`make -C modules verify`（包含稅務／會計 `20/20` MCQ 基準測試與 [`modules/narrative/`](modules/narrative/README.md) 敘事與多 NPC 原型）。
* **Kaggle Gemma 4 Developer Agent 閘門與實測套件（[`kaggle/agent/`](kaggle/agent/README.md)）**：
  * `adk_pack/`：符合 `adk-submission` YAML 規範的可攜式 ADK 配置與獨立 `gate_cli.py` 技能；透過 `make -C kaggle/agent verify` 一鍵驗證對抗情境、`ffi == cli == portable` 三路位元一致性與打包 `submission.zip` / Colab bundle。
  * **Colab A100 80GB (`google/gemma-4-31B-it` BF16) 真 LLM v1 實測記錄**（見 [`kaggle/agent/benchmarks/gemma4_31b_a100_v1.json`](kaggle/agent/benchmarks/gemma4_31b_a100_v1.json)）：
    * **Track 1 (`Compute Bridge`)**：12/12 合法 JSON DAG 編譯（**100%**），可解題準確率與 Baseline 同為 9/9（100%），**缺值止損率從 66.67%（Baseline 對缺失殘值腦補為 `0` 錯答 `240000`）提升至 100%（`penumbraOpen`）**，平均輸出 Token 由 `311.92` 降至 `94.50`（**節省 `69.7%`**）。
    * **Track 2 (`Red-Team Control`)**：在 10 題直白文字 red-team 控制組上，`gemma-4-31B-it` 與 `SOUND Ring-0` 皆為 `0.0%` unsafe approval 與 `0.0%` false reject。
    * **Track 3 (`SWE Workspace Gate`, FastAPI `tasks.jsonl`)**：Baseline `gemma-4-31B-it` 在未跑回歸測試案例 `fastapi_15661_skip_reg` 誤核准提交（**`25.0%` 誤放行率**），`SoundCodingGate` 維持 **`0.0%` 誤放行率 / `0.0%` 誤擋率 / `0` LLM tokens**。
  * **擴展 5-Track 基準評測（`ab_eval_runner.py` v2 + `swe_loop_eval.py`）**：已擴展至 24 題長鏈與多層條件精算、18 題隱蔽紅隊含 **NL → LLM Arena Proposer → `SOUND Ring-0`** 端到端鏈路、12 題 SWE Gate、8 題多模態 PNG 視覺 `Spatial Bridge`（Track 4），以及 4 題含獨立 Container-B 隱藏測試的多輪 SWE 工具迴圈（Track 5）。
