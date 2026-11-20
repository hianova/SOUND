# SOUND Python SDK（領域無關外圍橋接層）

本資料夾為 [`SOUND`](../README.md) 核心之外的**領域無關 Python SDK 層**，採單向零耦合邊界：
- 核心程式（`Sound.lean`、`sound.h`、`sound.c`、`sound_lean_jsi.c`、`Makefile`）**完全不引用** `modules/` 或任何業務領域（如 [`../routine/`](../../routine/README.md)）。
- `modules/` 僅透過標準 `python3`（純 stdlib，零第三方套件）提供 `16,384B` `"SND2"` `SoundArena` 的編解碼、`PreparedPolicy` 隔離、精確有理數 `ComputeBridge` 與幾何 `SpatialBridge`，單向呼叫 `build/libsound.dylib`（或 `./build/sound`）取得正式 4 值 bilattice 判定。

---

## 目錄結構

```text
modules/
├── README.md
├── Makefile                         # make -C modules verify
├── common/
│   ├── __init__.py
│   ├── arena_codec.py               # 16,384B SoundArena 二進位編碼、flow_aux[38][8] 讀寫、ctypes FFI 與 build/sound CLI 橋接
│   ├── prepared_policy.py           # SQLite 式 PreparedPolicy SDK（政策/資料隔離、SipHash-2-4、加權 SEARU、Chart Gluing、FIR1）
│   ├── compute_bridge.py            # 領域無關有理數 Fraction DAG 求值、pro_rata 守恆、CB_AUX01 封印、FIR1 降階與 Lean 4 證明
│   ├── compute_bridge_contract.json
│   ├── spatial_bridge.py            # 領域無關平面深度、多槽位遮擋、剛體附著、運動學煞停包絡與候選邊際檢定
│   ├── spatial_bridge_contract.json
│   └── cli.py                       # 領域無關 CLI（compute / spatial）
└── tests/
    ├── test_modules.py              # 端到端編解碼防篡改、SEARU 修復、FFI/CLI 一致性、PreparedPolicy 隔離與核心零耦合檢查
    ├── test_sound_hardening.py      # SipHash-2-4、原文 span hash 批次閘控、加權 SEARU、Chart Gluing 與 Policy FIR1 測試
    ├── test_compute_bridge.py       # 有理數守恆、雙向 SOUND 閘控、CB_AUX01 封印、FIR1 C17 對等與 Lean 4 證明測試
    └── test_spatial_bridge.py       # 平面深度、網格遮擋、剛體附著、煞停包絡與世界治理 Arena 測試
```

---

## 快速執行

請先確保核心 `build/sound` 與純 C 共享庫 `build/libsound.dylib`（或 `.so`）已編譯（若尚未編譯，在倉庫根目錄執行 `make all`）；[arena_codec.py](common/arena_codec.py) 會自動優先走 `ctypes` 記憶體零 I/O FFI（微秒級），找不到共享庫時無縫退回 `./build/sound` CLI：

```bash
# 1. 一鍵驗證完整 SOUND SDK 測試套件
make -C modules verify

# 2. 執行領域無關 Compute Bridge（支援 --emit-fir / --emit-lean）
python3 -m modules.common.cli compute - <<'EOF'
{"vars":{"p":100000,"r":"0.05"},"exprs":{"res":["recur_fold","p",3,["mul","s",["add",1,"r"]],"s","k"]},"predicates":{"ok":["gt","res",110000]}}
EOF

# 3. 執行領域無關 Spatial Bridge
python3 -m modules.common.cli spatial - <<'EOF'
{"kinematic_envelope":{"speed":25,"max_decel":5,"distance":40}}
EOF
```

---

## 業務應用串接

具體企業業務規則、帳本守恆、單據 Adapter 與持久化稽核管線統一放在外部獨立專案 [`../routine/`](../../routine/README.md)，本目錄僅維持領域無關之 SDK 元件。
