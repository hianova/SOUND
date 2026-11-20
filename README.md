# SOUND

**意圖 × 事實 × 推理（Intent × Fact × Reasoning）**

`SOUND` 是建構於 [`FLOW v8.0.0`](../FLOW/README.md) SSOT 基底之上的**認識論與規範健全性驗證基礎庫（Epistemic & Normative Soundness Kernel）**。基礎庫本身**不耦合任何上層應用、不編解碼自然語言明文、也不內建領域業務邏輯**：所有企業業務規則、單據解析與流程自動化（如 [`../routine/`](../routine/README.md)）均留在外部專案，透過 Merkle 封印的 `16,384B`（`256 × 64B`）`SoundArena`、Pure-GPR C17 ABI 與領域無關 Python SDK 執行驗證。

---

## 一、架構分層（FLOW SSOT × SOUND Kernel × 領域無關 SDK）

| 層級 | 所在模組 | 核心職責與邊界 |
| :--- | :--- | :--- |
| **數學基底（SSOT）** | [`../FLOW/`](../FLOW/README.md)（`Flow.Core`、`Flow.Schema`、`Flow.Morphism`、`searu.h`、`flow_ir_wire.h`）＋ [`Sound/Bilattice.lean`](Sound/Bilattice.lean) | 領域無關的 Lean 4 機器驗證（`46` 定理、`0 sorryAx`、`leanchecker`）：四態雙格交替不動點 $\Gamma / \Gamma^2$、統一 HoTT Fallback Fiber 總遷移（`SoundFiber` / `potestativeFiberStepBV64` / `batchAdmit2BV64`）、Lex Specialis 加權能量與 KanLock 支配定理（`lexSpecialisPairEnergy`）、Cross-64-Node Sheaf Chart Gluing（`chartGlueValidBV64`）、Prepared-Statement 政策／資料隔離謂詞（`preparedPolicyAdmitBV64`）、512-bit 零 `memcpy` C17 Arena Co-Generator（`Flow.Schema`）與 `"FIR1"` 通用線性態射編碼。 |
| **Layer 0（零信任 Prepared-Statement × SipHash × HoTT 入口閘門）** | [`sound.h`](sound.h) `sound_policy_seal_keyed` / `sound_policy_verify_keyed` / `sound_admit_forest_batch_keyed` / `sound_admit_forest_with_utterance` ＋ `Sound.Bilattice` | 以統一全函數遷移 `sound_fiber_t`（`COMMITTED` 或 1..13 `HOLD/FALLBACK` 纖維，記錄於 `forest_ctrl.last_fiber` / `fiber_epoch`）取代傳統 OOP 錯誤碼；支援 Pure-GPR SipHash-2-4 金鑰 Policy MAC（含降級攻擊防禦）、2-Phase 原子批次繫結（全過或零寫入回滾）、Layer-0 原文 Span Hash 比對與 `PROPOSER_LOW_EVID` 傳聞證據上限；將不可信提議者嚴格限制在預編譯參數槽（`bind_mask ⊥ policy_mask`），攔截免疫／政策／未綁定槽位、跨層 `sort`、特權升級與拓撲注入。 |
| **Layer 1（單調日誌 × Hohfeld 守恆 × Cross-Chart Gluing）** | [`sound.h`](sound.h) `PhysLog` ＋ `SoundNormative64` ＋ `SoundChartGlue64` | 物理事件只增不減進入 64-slot 環形 `PhysLog`（FNV-1a 鏈）；規範層維護 Hohfeld `Duty+/Duty-/Power/Immunity/Voidable/RescissionPower`；並提供 Cross-64-Node Sheaf Chart Gluing（`sound_chart_export_port` / `sound_chart_glue_import` / `sound_chart_glue_verify`，持久化於 `flow_aux[37]`），將已成立無衝突出口以 Merkle 憑證貼合至下游 Arena 事實繫結槽。 |
| **Layer 2（四態雙格核心 × 加權 SEARU × 雙路徑 FIR1 升格）** | [`Sound.lean`](Sound.lean) ≡ [`sound.h`](sound.h) | `Sound.lean` 以 `0` 本地定理配置 256-slot `soundArenaSchema`，於 Elaboration-Time 透過 `sound_lean_jsi.c` 驗證 10 組 golden `16,384B` `.snd` 與 `2,144B` `"FIR1"` 位元等價。`sound_eval` 計算四態雙格與雙向休謨防火牆；`sound_searu_repair_conflict_weighted` 支援 Lex Specialis 法規優先權重（KanLock 免疫無條件優先）；`sound_lift_to_fir`（權限閘門）與 `sound_lift_policy_eval_to_fir`（將封裝政策直接編譯為 combinational `FLOW_IR_MULT_LINEAR` `.fir` 電路）皆維持 Pure-GPR Ring-0 潔淨。 |
| **領域無關 Python SDK** | [`modules/`](modules/README.md) | 提供 [`arena_codec.py`](modules/common/arena_codec.py)（`SoundFiber` FFI／CLI 雙路橋接）、[`prepared_policy.py`](modules/common/prepared_policy.py)（SQLite 式 `PreparedPolicy` SDK）、[`compute_bridge.py`](modules/common/compute_bridge.py)（精確 `Fraction` DAG／`pro_rata`／`CB_AUX01`／Lean 4 證明／`FIR1` 降階）與 [`spatial_bridge.py`](modules/common/spatial_bridge.py)（平面深度／遮擋／運動學邊際檢定），與 `SOUND` 核心完全解耦。 |

---

## 二、檔案結構

* [**`Sound.lean`**](Sound.lean)：按需引入 `Flow.Core` / `Flow.Schema` / `Flow.Morphism` ＋ [`Sound/Bilattice.lean`](Sound/Bilattice.lean) 的純配置與 AOT `SoundArena` / `Flow.Morphism` `.fir` 編譯器（`0` 本地 `theorem/lemma/axiom`、通過 `leanchecker`），負責產出 `build/sound_schema_abi.h` 並執行 `#eval Sound.compileTimeBoundaryGate`。
* [**`sound.h`**](sound.h)：單標頭 Pure-GPR C17 核心（零堆積、零浮點、`-ffreestanding -nostdlib -fno-builtin -mgeneral-regs-only` Ring-0 潔淨、相容 C++20），含 SipHash-2-4 HMAC、原子批次入口閘門、Lex Specialis 加權 SEARU、Cross-Chart Gluing、`sound_lift_to_fir` 與 `sound_lift_policy_eval_to_fir`。
* [**`sound_lean_jsi.c`**](sound_lean_jsi.c)：Lean 4 動態外掛 JSI 橋接（僅編入 `libsound_jsi`，絕不進入 `libsound.a` 或 `build/sound`）。
* [**`sound.c`**](sound.c) ＋ [**`Makefile`**](Makefile) ＋ [**`tools/sound_sabotage_check.py`**](tools/sound_sabotage_check.py)：零堆積、零 `leanshared` 獨立 CLI（`selftest` T1..T10 / `verify` / `repair` / `lift-fir` / `lift-policy-fir`）與 18-mutant 單點破壞變異測試閘門（`make sabotage-check`）。
* [**`modules/`**](modules/README.md)：領域無關 Python 3.9+ stdlib SDK（`arena_codec.py`、`prepared_policy.py`、`compute_bridge.py`、`spatial_bridge.py`）。

---

## 三、一鍵驗證閘門

```bash
make fmt && make clean && make verify && make sabotage-check && make -C modules verify
```

驗證內容涵蓋：
1. **Gate 0 / Gate 1（Lean 4 SSOT + `leanchecker` + ABI）**：驗證 `Sound/Bilattice.lean` 全 `46` 條定理（`0 sorryAx`、`leanchecker Sound.Bilattice`）與 `Sound.lean` 零本地定理／零佔位符、通過 `leanchecker Sound`，並產出零 `memcpy` 的 `build/sound_schema_abi.h`。
2. **Gate 2（Ring-0 Pure-GPR + C++20 + `clang-tidy`）**：驗證 `0 undef`、`0 udiv/sdiv`、`0 blr/br`、`0 FPU/SIMD`、`16,384B / 64B` C++20 ABI 與靜態分析零警告。
3. **Gate 3（Zero-Lean-Runtime 獨立執行檔）**：驗證 `libsound.a`、`libsound.dylib` 與 `build/sound` 零堆積、零浮點、零 `leanshared`。
4. **Gate 4（JSI Elaboration-Time 閘門）**：驗證 10 組 `16,384B` `.snd` 與 `2,144B` `"FIR1"` 電路 Lean↔C17 位元對等、`flow_ir_eval_u64` 執行、`SEARU` 衝突修復、Layer 0 Prepared-Statement 政策／資料隔離、Layer 1 測試與 4,096 組隨機圖不變量 Fuzz。
5. **Gate 5（CLI 實測與 `clang-format`）**：執行 `sound selftest`（`T1..T10` 含 SipHash-2-4、原子批次、`LOW_EVID`、Lex Specialis、Chart Gluing 與 Policy-Eval FIR 編譯）、`verify`、`repair`、`lift-fir`、`cmp -s` 位元比對與格式檢查。
6. **Gate 6（單點破壞變異測試 `make sabotage-check`）**：注入 18 項跨全層單點語義破壞並以 `sound selftest` 實測擊殺率（`18/18 = 100.0%`）。
7. **SDK 驗證（`make -C modules verify`）**：執行領域無關 SDK 單元測試（編解碼防篡改、FFI/CLI 一致性、`PreparedPolicy` 隔離、SipHash-2-4、加權 SEARU、Chart Gluing、`ComputeBridge` 與 `SpatialBridge`）。
