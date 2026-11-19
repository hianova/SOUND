# SOUND Narrative Module（小說世界觀稽核、碎片化道具敘事投影與永續 NPC 互動原型）

本模組示範如何把長篇設定／小說劇本編譯成 **固定 `16,384B` 記憶體足跡、無 Token 膨脹、無吃書風險** 的遊戲敘事與 NPC 權限核心，且與 [`SOUND`](../../README.md) 核心維持嚴格單向零耦合（所有驗證、SEARU 修復與 `.fir` 權限閘門皆透過 `./build/sound` 完成）。

---

## 核心管線

1. **編譯期世界觀防吃書稽核 (`audit_lore_pack`)**
   * 將章節／小鎮設定編碼為 `<= 62` 節點之 `SoundArena`（[ashen_bell_chronicle.json](packs/ashen_bell_chronicle.json) 共 `31/64` 節點）。
   * 區分「刻意設計的羅生門對峙 (`intentional_rashomon`)」與「意外劇情矛盾 (`accidental_contradictions`)」；遇意外矛盾自動呼叫 `./build/sound repair`（SEARU）求取最小修補子集，並驗證全真相解鎖後可收斂無衝突。
2. **魂系碎片化道具故事投影 (`compile_item_cards`)**
   * 每個道具不憑空編造文案，而是對 `public_facts` 基底做真實 **SOUND 差分驗證**，精確計算：
     * 新成立證詞 (`newly_established`)
     * 被物證 `ATTACKED (0x40)` 擊敗的舊指控／神話 (`defeated_beliefs`)
     * 被打破的羅生門爭議 (`resolved_rashomon`)
     * 受 `penumbraOpen` 保護、嚴禁說破結局的謎霧鎖 (`CRYPTIC_HINT_ONLY` / `forbidden_spoilers`)
3. **執行期永續多 NPC 認知遮罩與權限閘門 (`NarrativeWorldRuntime`)**
   * 每個 NPC 只維護極小狀態：`(known_facts, rumor_facts, power_nodes, immunity_nodes, searu_seed, phys_history)`。
   * **資訊不對稱與傳聞隔離**：未實證傳聞編碼為 `evid=reportative`，遭 `EVIDENTIAL_LEAF / UNVERIFIED_PRESUP` 擋下（`SKEPTICAL_RUMOR`），拒絕輕信或劇透。
   * **防嘴砲詐唬 (`Irrealis`)**：玩家「假設／如果…」誘導對話直接隔離（`BLUFF_QUARANTINED`），前後 `SoundArena` 位元完全不變。
   * **羅生門 SEARU 性格偏向**：兩派證詞對咬（`conflictUndecided`）時呼叫 `./build/sound repair` 依 NPC `searu_seed` 算出傾向立場。
   * **Hohfeld 權限、`Voidable / Rescission` 與 `2,144B` `.fir` 硬體閘門**：缺權限（`ACTION_NO_POWER`）、觸犯主線鐵律（`ACTION_IMMUNITY_LOCKED`）或缺關鍵前置（`ACTION_PREREQ_MISSING`）一律攔截；若受偽證欺騙簽約（`internal_will_matches=false`），契約進 `voidable` 狀態並在揭穿後由 C17 `sound_ring0_exercise_rescission` 合法撤銷翻臉；集齊條件後執行 `lift-fir` 匯出 `2,144B` `"FIR1"` QTT 靜態權限電路。
4. **跨場景 `WorldLedger` 接力（[world_ledger.py](engine/world_ledger.py) ↔ [royal_court_hearing.json](packs/royal_court_hearing.json)）**
   * 只把第一章證成之少數全域真相旗標（`wl_vane_innocent`、`wl_abbot_culprit`、`wl_crypt_key_held`）映射為第二章《王都聽證會》初始 `fact_slot`，每場仍鎖死於 `16KB / <= 64 節點`。

---

## 快速執行

```bash
# 0. 一鍵執行全部模組驗證
make -C modules verify

# 1. 稽核《灰鐘鎮編年史》世界觀連續性、羅生門與全真相收斂
python3 -m modules.narrative.engine.cli audit

# 2. 編譯 5 件碎片化道具敘事卡（含物證反轉證明與防劇透半影鎖）
python3 -m modules.narrative.engine.cli items

# 3. 執行三名 NPC 互動劇本與 2,144B FIR1 權限閘門匯出示範
python3 -m modules.narrative.engine.cli demo

# 4. 執行雙幕跨場景 WorldLedger + 欺騙簽約 Voidable/Rescission + 永久特赦示範
python3 -m modules.narrative.engine.cli campaign

# 5. 執行完整 narrative unittest 套件
python3 -m unittest discover -s modules/narrative/tests -v
```

---

## Stage 2 外部 LLM 雙契約（可選，離線零依賴即可運行）

* [lore_extractor_contract.json](contracts/lore_extractor_contract.json)：規範離線製作期將小說章節抽取為合法 Lore Pack JSON。
* [dialogue_realizer_contract.json](contracts/dialogue_realizer_contract.json)：規範執行期輕量小模型只依 `SpeechAct`（`stance`、`allowed_nodes`、`forbidden_spoilers`）潤飾角色口癖，嚴禁越權篡改真相或發放道具。
