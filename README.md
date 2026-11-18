# SOUND

**意圖 × 事實 × 推理（Intent × Fact × Reasoning）**

`SOUND` 是建構於 [`FLOW v8.0.0`](../flow/README.md) SSOT 基底之上的**認識論與規範健全性驗證基礎庫（Epistemic & Normative Soundness Kernel）**。基礎庫本身**不耦合任何上層應用、不編解碼自然語言明文、也不內建 DSL**：自然語言／LLM 結構化抽取／Latent-to-ABI 橋接均留在外圍獨立模組，核心僅透過 Merkle 封印的 `16,384B`（`256 × 64B`）`SoundArena` 與 Pure-GPR C17 ABI 執行驗證。

---

## 一、架構分層（FLOW SSOT × SOUND Kernel × 外圍應用模組）

| 層級 | 所在模組 | 核心職責與邊界 |
| :--- | :--- | :--- |
| **數學基底（SSOT）** | [`../flow/Flow/`](../flow/Flow/)（`Flow.Bilattice`、`Flow.BitAlgebra`、`Flow.Schema`、`searu.h`） | 領域無關的 Lean 4 機器驗證（`0 sorryAx`）：四態雙格交替不動點 $\Gamma / \Gamma^2$、單一固定點與雙格互為不動點定理、64-bit `KanLock` 與雙極性閘控位元板、512-bit C17 Arena Co-Generator，以及 Pure-GPR `SEARU` 離散修復求解器。 |
| **Layer 0（零信任入口閘門）** | [`sound.h`](sound.h) `sound_admit_forest` ＋ `Flow.Bilattice.admitGuard` | 只驗結構化 `SoundForestCtrl64` / `SoundCandidate64`：檢查 Byte-Span 邊界、歧義／未消解指代旗標，並隔離 `Irrealis` 假設；嚴禁基礎庫內嵌自然語言詞典或明文剖析。 |
| **Layer 1（單調日誌 × Hohfeld 守恆）** | [`sound.h`](sound.h) `PhysLog` ＋ `SoundNormative64` | 物理事件只增不減進入 64-slot 環形 `PhysLog`（FNV-1a 鏈）；規範層維護 Hohfeld `Duty+/Duty-/Power/Immunity/Voidable/RescissionPower`（全部持久化於 16KB `SoundArena`）。受 `Immunity`（`KanLock`）鎖定槽位對賦權與撤銷嚴格免疫；撤銷意思表示後重算不動點並同步清理衍生義務與撤銷權，同時保留多重獨立支撐。 |
| **Layer 2（四態雙格核心 × SEARU × FIR1 升格）** | [`Sound.lean`](Sound.lean) ≡ [`sound.h`](sound.h) | `Sound.lean` 以 `0` 本地定理配置 256-slot `soundArenaSchema`，於 Elaboration-Time 透過 `sound_lean_jsi.c` 驗證 10 組 golden `16,384B` `.snd` 與 `2,144B` `"FIR1"`（`Flow.Morphism`）位元等價。`sound_eval` 計算四態雙格與雙向休謨防火牆；`sound_searu_repair_conflict` 呼叫 `SEARU_SOLVE_INLINE` 求解極大無衝突擴充集；`sound_lift_to_fir`（Ring-0 Pure-GPR）將已驗證 `SoundArena` 直接升格為 `FLOW_IR_MULT_LINEAR` QTT 權限閘門電路。 |
| **外圍應用與明文模組** | 外部獨立模組 | 負責自然語言／LLM／對話／法規文字與 `SoundArena` 二進位結構之間的雙向轉譯、文字層 `.lean` 產生與人話解釋，與 `SOUND` 基礎庫完全解耦。 |

---

## 二、檔案結構

* [**`Sound.lean`**](Sound.lean)：`import Flow` 的純配置與 AOT `SoundArena` / `Flow.Morphism` `.fir` 編譯器（`0` 本地 `theorem/lemma/axiom`、通過 `leanchecker`），負責產出 `build/sound_schema_abi.h` 並執行 `#eval Sound.compileTimeBoundaryGate`。
* [**`sound.h`**](sound.h)：單標頭 Pure-GPR C17 核心（零堆積、零浮點、`-ffreestanding -nostdlib -fno-builtin -mgeneral-regs-only` Ring-0 潔淨、相容 C++20），含 `sound_lift_to_fir` 與 `sound_ring0_lift_to_fir`。
* [**`sound_lean_jsi.c`**](sound_lean_jsi.c)：Lean 4 動態外掛 JSI 橋接（僅編入 `libsound_jsi`，絕不進入 `libsound.a` 或 `build/sound`）。
* [**`sound.c`**](sound.c) ＋ [**`Makefile`**](Makefile)：零堆積、零 `leanshared` 獨立 CLI（`selftest` / `verify <arena.snd>` / `repair <arena.snd> [seed]` / `lift-fir <arena.snd> <out.fir> [--repaired [seed]]`）。

---

## 三、一鍵五閘門驗證

```bash
make fmt && make clean && make verify
```

驗證內容涵蓋：
1. **Gate 1（Lean 4 SSOT + `leanchecker` + ABI）**：驗證 `Sound.lean` 零本地定理與零佔位符、通過 `leanchecker Sound` 並產出零 `memcpy` 的 `build/sound_schema_abi.h`。
2. **Gate 2（Ring-0 Pure-GPR + C++20 + `clang-tidy`）**：驗證 `0 undef`、`0 udiv/sdiv`、`0 blr/br`、`0 FPU/SIMD`、`16,384B / 64B` C++20 ABI 與靜態分析零警告。
3. **Gate 3（Zero-Lean-Runtime 獨立執行檔）**：驗證 `libsound.a` 與 `build/sound` 零堆積、零浮點、零 `leanshared`。
4. **Gate 4（JSI Elaboration-Time 閘門）**：驗證 10 組 `16,384B` `.snd` 與 `2,144B` `"FIR1"` 電路 Lean↔C17 位元對等、`flow_ir_eval_u64` 執行、`SEARU` 衝突修復、Layer 0/1 測試與 4,096 組隨機圖不變量 Fuzz。
5. **Gate 5（CLI 實測與 `clang-format`）**：執行 `sound selftest`（含 `T7(fir)`）、`verify`、`repair`、`lift-fir`、`cmp -s` 位元比對與格式檢查。
