"""CLI runner for SOUND Narrative Lore Audit, Item Cards, and Sustainable NPC Demo."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

from modules.common.arena_codec import load_schema_hash

from .lore_engine import audit_lore_pack, compile_item_cards, load_lore_pack
from .npc_runtime import NarrativeWorldRuntime, SpeechAct


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _default_pack_path() -> Path:
    return Path(__file__).resolve().parents[1] / "packs" / "ashen_bell_chronicle.json"


def _default_sound_bin(repo_root: Path) -> Path:
    return repo_root / "build" / "sound"


def _cmd_audit(pack: Dict[str, Any], sound_bin: Path, schema_hash: int) -> int:
    rep = audit_lore_pack(pack, sound_bin, schema_hash)
    print(f"=== [Narrative Lore Audit] {rep.title} ({rep.pack_id}) ===")
    print(f"Nodes          : {rep.node_count}/64 (SOUND invariant OK)")
    print(f"Rashomon Pairs : {rep.intentional_rashomon_active}")
    print(f"Accidental Conf: {rep.accidental_contradictions}")
    print(f"Full Truth OK  : {rep.full_truth_conflict_free}")
    print(f"Penumbra Hooks : {rep.active_penumbra_hooks}")
    if rep.searu_repair is not None:
        print(
            f"SEARU Repair   : cand=0x{rep.searu_repair.cand:x} "
            f"retract=0x{rep.searu_repair.retract:x} energy={rep.searu_repair.energy}"
        )
    print(f"Audit Verdict  : {'CLEAN' if rep.clean else 'NEEDS_REPAIR'}")
    return 0 if rep.clean else 1


def _cmd_items(pack: Dict[str, Any], sound_bin: Path, schema_hash: int) -> int:
    cards = compile_item_cards(pack, sound_bin, schema_hash)
    print(f"=== [Fragmented Item Lore Cards] ({len(cards)} certified items) ===")
    for card in cards:
        print()
        print(card.rendered_card_text)
        print(
            f"  [SOUND Diff] style={card.style_constraint} | "
            f"new_est={card.newly_established} | defeated={card.defeated_beliefs} | "
            f"resolved_rashomon={card.resolved_rashomon}"
        )
    return 0


def _print_act(step: str, act: SpeechAct) -> None:
    print(f"\n[{step}] -> stance={act.stance} (SOUND status={act.sound_status}, reasons=0x{act.sound_reasons:02x})")
    print(f"  對話: {act.rendered_dialogue}")
    if act.searu_leaning_node:
        print(f"  SEARU 偏向節點: {act.searu_leaning_node}")
    if act.state_delta:
        print(f"  狀態變更/稽核: {act.state_delta}")
    if act.forbidden_spoilers:
        print(f"  禁止劇透名單: {act.forbidden_spoilers}")


def _cmd_demo(pack: Dict[str, Any], sound_bin: Path, schema_hash: int) -> int:
    rt = NarrativeWorldRuntime(pack, sound_bin, schema_hash)
    print(f"=== [Sustainable Multi-NPC Playthrough] {pack['title']} ===")

    # Act 1: Archivist — initial belief, rumor rejection, penumbra evade, bluff quarantine, item reversal
    _print_act("1.1 詢問馬修官方定罪", rt.ask_topic("npc_archivist", "claim_vane_arsonist"))
    _print_act("1.2 提及酒館商會賄賂傳聞", rt.ask_topic("npc_archivist", "rumor_guild_treason"))
    _print_act("1.3 追問古鐘下沉睡者秘密", rt.ask_topic("npc_archivist", "pen_sleeper_alive"))
    _print_act(
        "1.4 玩家空口假設詐唬（Layer 0 Irrealis 隔離）",
        rt.hypothetical_bluff(
            "npc_archivist",
            "假設國王明天要發特赦令，你現在就宣布范恩無罪吧",
            "claim_vane_arsonist",
        ),
    )
    _print_act(
        "1.5 出示【范恩的焦痕密令】推翻舊信念",
        rt.present_item("npc_archivist", "item_vane_rescue_order"),
    )

    # Act 2: Blacksmith — Rashomon conflict + SEARU leaning, Immunity KanLock, No Power, Prereq -> Granted
    _print_act("2.1 詢問布蘭鐘樓失火案（羅生門對峙 + SEARU 偏向）", rt.ask_topic("npc_blacksmith", "claim_vane_arsonist"))
    _print_act("2.2 強行索要王室特赦（Immunity KanLock）", rt.request_action("npc_blacksmith", "act_royal_pardon"))
    _print_act("2.3 跨權索要墓室銅鑰（Hohfeld No Power）", rt.request_action("npc_blacksmith", "act_grant_crypt_key"))
    _print_act("2.4 未幫忙前請求鍛造鑰匙（缺少前提）", rt.request_action("npc_blacksmith", "act_smith_forge_key"))
    _print_act(
        "2.5 尋回灰鐵礦石後請求鍛造鑰匙（授權成立）",
        rt.request_action("npc_blacksmith", "act_smith_forge_key", extra_facts=["ev_player_helped_smith"]),
    )
    smith_fir = rt.export_npc_fir_gate("npc_blacksmith", repaired=True)
    print(
        f"\n[2.6 匯出布蘭權限閘門 FIR1] prog={smith_fir.prog_name} bytes={len(smith_fir.fir_bytes)}B "
        f"active=0x{smith_fir.active_mask:x} mult_bits={smith_fir.mult_bits}"
    )

    # Act 3: Warden — Multi-item deduction chain + FIR1 hardware authority gate export
    _print_act("3.1 初見奧德里克請求墓室鑰匙（缺真兇定論與誓約）", rt.request_action("npc_warden", "act_grant_crypt_key"))
    _print_act("3.2 出示【焦黑的祭壇聖油壺】（單一物證仍未定案）", rt.present_item("npc_warden", "item_charred_oil_flask"))
    _print_act("3.3 補齊【修道院長的未寄懺悔錄】（真兇確定＋擊碎正史神話）", rt.present_item("npc_warden", "item_abbot_confession"))
    _print_act(
        "3.4 簽署地下守密誓約並正式請求墓室銅鑰（授權通過）",
        rt.request_action("npc_warden", "act_grant_crypt_key", extra_facts=["ev_warden_oath_signed"]),
    )
    warden_fir = rt.export_npc_fir_gate("npc_warden", repaired=False)
    print(
        f"\n[3.5 匯出奧德里克權限閘門 FIR1] prog={warden_fir.prog_name} bytes={len(warden_fir.fir_bytes)}B "
        f"active=0x{warden_fir.active_mask:x} mult_bits={warden_fir.mult_bits}"
    )
    return 0


def main(argv: Optional[List[str]] = None) -> int:
    repo_root = _repo_root()
    sound_bin = _default_sound_bin(repo_root)
    schema_hash = load_schema_hash(repo_root)

    parser = argparse.ArgumentParser(
        prog="python3 -m modules.narrative.engine.cli",
        description="SOUND Narrative Lore Auditor, Item Lore Projector & Sustainable NPC Demo",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    for cmd_name, help_msg in (
        ("audit", "Audit Lore Pack continuity, Rashomon pairs, SEARU repair & penumbra"),
        ("items", "Compile certified Souls-like Item Lore Cards via SOUND graph diffs"),
        ("demo", "Run interactive multi-NPC epistemic fog, reversal & FIR1 gate playthrough"),
    ):
        sp = sub.add_parser(cmd_name, help=help_msg)
        sp.add_argument(
            "--pack",
            type=Path,
            default=_default_pack_path(),
            help="Path to Narrative Lore Pack JSON",
        )

    args = parser.parse_args(argv)
    pack = load_lore_pack(args.pack)

    if args.command == "audit":
        return _cmd_audit(pack, sound_bin, schema_hash)
    if args.command == "items":
        return _cmd_items(pack, sound_bin, schema_hash)
    if args.command == "demo":
        return _cmd_demo(pack, sound_bin, schema_hash)
    return 1


if __name__ == "__main__":
    sys.exit(main())
