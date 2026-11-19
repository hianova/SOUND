# SOUND Domain Modules（稅務 & 會計基礎選擇題無運算陷阱辨識）

本資料夾為 [`SOUND`](../README.md) 核心之外的**領域應用模組層**，採單向零耦合邊界：
> 相關模組：需要小說設定除錯、魂系道具敘事生成與永續多 NPC 互動原型，另見 [`modules/narrative/README.md`](narrative/README.md)。
- 核心程式（`Sound.lean`、`sound.h`、`sound.c`、`sound_lean_jsi.c`、`Makefile`）**完全不引用** `modules/`、任何自然語言詞庫或會計／稅法領域名詞。
- `modules/` 僅透過標準 `python3`（純 stdlib，零第三方套件）把宣告式領域 Topic Pack 與題目實例打包成 `16,384B` `"SND2"` `SoundArena`，單向呼叫 `./build/sound verify` 與 `./build/sound repair` 取得正式 4 值 bilattice 判定。

---

## 目錄結構

```text
modules/
├── README.md
├── common/
│   ├── __init__.py
│   ├── arena_codec.py          # 16,384B SoundArena 二進位編碼、FNV-1a Merkle 封印與 build/sound CLI 橋接
│   ├── tripwires.py            # 輕量高精準多語詞面引線（題幹極性反轉、絕對詞、未成就/假設詞、但書範圍詞）
│   ├── mcq_engine.py           # Topic Pack 驗證、選項編譯、Hume/Presup/Exception/Penumbra 陷阱分類與作答
│   ├── cli.py                  # 命令列工具（packs / benchmark / solve）
│   └── proposer_contract.json  # Stage 2 外部 LLM Proposer 輸出 JSON Schema 契約（無廠商綁定）
├── taxation/
│   ├── packs/
│   │   ├── vat_zero_vs_exempt.json      # 台灣營業稅：零稅率 vs 免稅、放棄免稅 3 年鎖定、進項扣抵與退稅（26/64 節點）
│   │   └── tax_roles_and_remedy.json    # 所得稅扣繳 vs 納稅義務人、復查前置與暫緩強制執行（22/64 節點）
│   └── benchmarks/
│       └── tw_tax_mcq.json              # 10 題台灣稅法基礎無運算選擇題（40 選項）
├── accounting/
│   ├── packs/
│   │   ├── revenue_accrual_timing.json  # 權責發生制、IFRS 15 控制權移轉與 FOB 在途存貨歸屬（22/64 節點）
│   │   └── provision_vs_contingent.json # IAS 37 負債準備、或有負債/資產門檻與 IAS 16 資收支出（28/64 節點）
│   └── benchmarks/
│       └── tw_accounting_mcq.json       # 10 題台灣初會 / IFRS 基礎無運算選擇題（40 選項）
└── tests/
    └── test_modules.py         # 端到端編解碼防篡改、20 題／80 選項基準測試與核心零耦合檢查
```

---

## 選擇題非計算陷阱與 `SOUND` 核心狀態對照表

| 考題陷阱分類 (`trap_type`) | 考題常見手法（稅務 / 會計） | `SOUND` 核心判定 (`status` / `reasons`) |
| :--- | :--- | :--- |
| `TRAP_EXCEPTION_IGNORED` | 選項使用「一律 / 皆 / 所有 / 無例外」忽略法條但書或準則例外（如境內銷售皆課 5%、所有支出列費用、逾期皆移送強制執行） | 展開 `universal_counterexamples` 後例外出線攻破原則 → `defeated` (`ATTACKED = 0x40`) |
| `TRAP_CONCEPT_OR_ROLE_SWAP` | 偷換互斥概念或主體角色（如免稅誤作零稅率退稅、扣繳義務人誤作最終納稅人、FOB 起運點賣方負擔、極微流出強制附註揭露） | 互斥對偶概念已 `established` 並擊敗誤植主張 → `defeated` (`ATTACKED` 或 `NO_ACTIVE_CLAUSE`) |
| `TRAP_UNVERIFIED_PRESUP` | 時點／條件未成就先推結論（如僅口頭約定就退稅、未經復查直接訴願、未移轉控制權認收入、僅「有可能」或金額不可靠估計就認負債準備） | 必要前提缺席或被標為 `evid=assumed` → `defeated` (`UNVERIFIED_PRESUP = 0x20` / `EVIDENTIAL_LEAF = 0x04`) |
| `TRAP_HUME_IS_TO_OUGHT` | 把單純物理／簽約／付款／起訴「實然」事實直接說成公法課稅或會計認列「應然」義務，未經過任何法規／準則根節點 | 純 factual 前提直連 `normative` 非構成性節點 → `defeated` (`HUME_IS_TO_OUGHT = 0x08`) |
| `TRAP_PENUMBRA_OVERCLAIM` | 把實質認定之灰色半影區（跨境數位使用地、租稅規避實質課稅、高度客製化收款權、低單價工具重大性）硬寫成「必定／一律成立」 | 核心輸出 `penumbraOpen` (`PENUMBRA = 0x01`) 而選項期望 `established` |
| `TRAP_CONFLICT_UNRESOLVED` | 規範互斥對攻無法收斂之雙向衝突 | 核心輸出 `conflictUndecided`（可再經 `./build/sound repair` 求極大無衝突修復子集） |

---

## 快速執行

請先確保核心 `build/sound` 已編譯（若尚未編譯，在倉庫根目錄執行 `make all`）：

```bash
# 1. 列出並驗證全部 Topic Pack（含 64 節點預算檢查）
python3 -m modules.common.cli packs

# 2. 執行全部 20 題（80 選項）稅務與會計基準測試（逐題送入 build/sound verify）
python3 -m modules.common.cli benchmark --all --verbose

# 3. 單題除錯檢視每個節點之 SOUND 狀態與 reason 位元遮罩
python3 -m modules.common.cli solve modules/taxation/benchmarks/tw_tax_mcq.json TW_TAX_VAT_01

# 4. 執行完整 unittest 套件
python3 -m unittest discover -s modules/tests -v
```

---

## Stage 2：如何對接外部 LLM Proposer

在雙層架構中，`modules/common/proposer_contract.json` 定義了不綁定特定廠商 SDK 的結構化意圖契約：
1. 外部 LLM 只負責讀取純文字題幹與某個 Topic Pack 的 `nodes` / `aliases` 清單，將題幹與四個選項映射為節點 ID、`unverified_facts`、`universal_scope` 與 `claim` 結構（嚴禁由 LLM 自行猜答案或進行數值計算）。
2. 所有邏輯推導、例外擊敗、休謨防火牆與前提檢查統一交由 `modules/common/mcq_engine.py` 編譯成 `16,384B` `.snd` 並交給 `./build/sound verify` 做出可稽核、可重現的形式判決。
