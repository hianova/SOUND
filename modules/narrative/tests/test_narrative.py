"""End-to-end tests for SOUND Narrative Lore Audit, Item Cards, NPC Runtime, and FIR1 Gates."""

from __future__ import annotations

import copy
import json
import unittest
from pathlib import Path

from modules.common.arena_codec import load_schema_hash
from modules.narrative.engine.lore_engine import (
    audit_lore_pack,
    compile_item_cards,
    load_lore_pack,
)
from modules.narrative.engine.npc_runtime import NarrativeWorldRuntime

REPO_ROOT = Path(__file__).resolve().parents[3]
NARRATIVE_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PACK = NARRATIVE_ROOT / "packs" / "ashen_bell_chronicle.json"
SOUND_BIN = REPO_ROOT / "build" / "sound"


class SoundNarrativeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not SOUND_BIN.is_file():
            raise RuntimeError(f"Missing {SOUND_BIN}; run `make all` in repo root first.")
        cls.schema_hash = load_schema_hash(REPO_ROOT)
        cls.pack = load_lore_pack(DEFAULT_PACK)

    def test_lore_pack_audit_clean_and_rashomon(self) -> None:
        self.assertLessEqual(len(self.pack["nodes"]), 62)
        rep = audit_lore_pack(self.pack, SOUND_BIN, self.schema_hash)
        self.assertTrue(rep.clean)
        self.assertEqual(
            rep.intentional_rashomon_active,
            [("claim_vane_arsonist", "claim_abbot_staged")],
        )
        self.assertEqual(rep.accidental_contradictions, [])
        self.assertTrue(rep.full_truth_conflict_free)
        self.assertEqual(rep.active_penumbra_hooks, ["pen_sleeper_alive", "pen_bell_origin"])

    def test_accidental_contradiction_detected_and_repaired_by_searu(self) -> None:
        broken = copy.deepcopy(self.pack)
        broken["nodes"].extend(
            [
                {
                    "id": "plot_hole_a",
                    "role": "rule",
                    "sort": "normative",
                    "evid": "direct",
                    "attacked_by": ["plot_hole_b"],
                    "clauses": [["law_chronicle_record", "ev_tower_burned"]],
                },
                {
                    "id": "plot_hole_b",
                    "role": "rule",
                    "sort": "normative",
                    "evid": "direct",
                    "attacked_by": ["plot_hole_a"],
                    "clauses": [["law_chronicle_record", "ev_tower_burned"]],
                },
            ]
        )
        rep = audit_lore_pack(broken, SOUND_BIN, self.schema_hash)
        self.assertFalse(rep.clean)
        self.assertIn("plot_hole_a", rep.accidental_contradictions)
        self.assertIn("plot_hole_b", rep.accidental_contradictions)
        self.assertIsNotNone(rep.searu_repair)
        assert rep.searu_repair is not None
        self.assertTrue(rep.searu_repair.converged)
        self.assertEqual(rep.searu_repair.energy, 0)

    def test_item_lore_projection_and_reversal_certificates(self) -> None:
        cards = {c.item_id: c for c in compile_item_cards(self.pack, SOUND_BIN, self.schema_hash)}
        self.assertEqual(
            set(cards.keys()),
            {
                "item_church_lantern",
                "item_charred_oil_flask",
                "item_vane_rescue_order",
                "item_abbot_confession",
                "item_cracked_bell_shard",
            },
        )

        # Standalone oil flask is partial: must NOT establish truth_abbot_culprit alone
        oil_card = cards["item_charred_oil_flask"]
        self.assertEqual(oil_card.projected_facts, ["ev_charred_oil_flask"])
        self.assertNotIn("truth_abbot_culprit", oil_card.newly_established)

        # Vane rescue order formally defeats arsonist claim and resolves Rashomon to abbot_staged
        vane_card = cards["item_vane_rescue_order"]
        self.assertEqual(vane_card.defeated_beliefs, ["claim_vane_arsonist"])
        self.assertEqual(vane_card.resolved_rashomon, ["claim_abbot_staged"])
        self.assertEqual(vane_card.style_constraint, "CONCRETE_ALLOWED")

        # Paired abbot confession proves truth_abbot_culprit and shatters church hero myth
        abbot_card = cards["item_abbot_confession"]
        self.assertIn("truth_abbot_culprit", abbot_card.newly_established)
        self.assertIn("lore_church_hero_myth", abbot_card.defeated_beliefs)

        # Cracked bell shard triggers penumbra spoiler lock
        bell_card = cards["item_cracked_bell_shard"]
        self.assertEqual(bell_card.style_constraint, "CRYPTIC_HINT_ONLY")
        self.assertEqual(bell_card.forbidden_spoilers, ["pen_sleeper_alive", "pen_bell_origin"])

    def test_npc_epistemic_fog_bluff_quarantine_and_reversal(self) -> None:
        rt = NarrativeWorldRuntime(self.pack, SOUND_BIN, self.schema_hash)

        act_conv = rt.ask_topic("npc_archivist", "claim_vane_arsonist")
        self.assertEqual(act_conv.stance, "CONVINCED")
        self.assertEqual(act_conv.sound_status, "established")

        act_rumor = rt.ask_topic("npc_archivist", "rumor_guild_treason")
        self.assertEqual(act_rumor.stance, "SKEPTICAL_RUMOR")
        self.assertEqual(act_rumor.sound_status, "defeated")

        act_pen = rt.ask_topic("npc_archivist", "pen_sleeper_alive")
        self.assertEqual(act_pen.stance, "CRYPTIC_EVADE")
        self.assertEqual(act_pen.sound_status, "penumbraOpen")
        self.assertIn("pen_sleeper_alive", act_pen.forbidden_spoilers)

        act_bluff = rt.hypothetical_bluff(
            "npc_archivist",
            "假設國王已經簽了特赦令，你先放了范恩",
            "claim_vane_arsonist",
        )
        self.assertEqual(act_bluff.stance, "BLUFF_QUARANTINED")
        self.assertTrue(act_bluff.state_delta["arena_digest_unchanged"])
        self.assertTrue(act_bluff.state_delta["phys_len_unchanged"])

        act_flip = rt.present_item("npc_archivist", "item_vane_rescue_order")
        self.assertEqual(act_flip.stance, "BELIEF_SHATTERED")
        self.assertIn("claim_vane_arsonist", act_flip.state_delta["defeated_beliefs"])
        self.assertEqual(rt.ask_topic("npc_archivist", "claim_vane_arsonist").sound_status, "defeated")

    def test_npc_rashomon_searu_immunity_and_power_gates(self) -> None:
        rt = NarrativeWorldRuntime(self.pack, SOUND_BIN, self.schema_hash)

        act_torn = rt.ask_topic("npc_blacksmith", "claim_vane_arsonist")
        self.assertEqual(act_torn.stance, "RASHOMON_TORN")
        self.assertEqual(act_torn.sound_status, "conflictUndecided")
        self.assertIn(act_torn.searu_leaning_node, ("claim_vane_arsonist", "claim_abbot_staged"))

        act_imm = rt.request_action("npc_blacksmith", "act_royal_pardon")
        self.assertEqual(act_imm.stance, "ACTION_IMMUNITY_LOCKED")

        act_nopow = rt.request_action("npc_blacksmith", "act_grant_crypt_key")
        self.assertEqual(act_nopow.stance, "ACTION_NO_POWER")

        act_missing = rt.request_action("npc_blacksmith", "act_smith_forge_key")
        self.assertEqual(act_missing.stance, "ACTION_PREREQ_MISSING")
        self.assertEqual(act_missing.state_delta["missing_prereqs"], ["ev_player_helped_smith"])

        act_ok = rt.request_action(
            "npc_blacksmith",
            "act_smith_forge_key",
            extra_facts=["ev_player_helped_smith"],
        )
        self.assertEqual(act_ok.stance, "ACTION_GRANTED")
        self.assertEqual(act_ok.sound_status, "established")

        # Blacksmith still holds unresolved Rashomon -> requires SEARU `--repaired` to emit FIR1
        smith_fir = rt.export_npc_fir_gate("npc_blacksmith", repaired=True)
        self.assertEqual(len(smith_fir.fir_bytes), 2144)
        self.assertTrue(smith_fir.fir_bytes.startswith(b"FIR1"))
        self.assertEqual(smith_fir.prog_name, "npc_blacksmith")

    def test_warden_multi_item_deduction_and_fir1_export(self) -> None:
        rt = NarrativeWorldRuntime(self.pack, SOUND_BIN, self.schema_hash)
        id_to_idx = {nd["id"]: idx for idx, nd in enumerate(self.pack["nodes"])}

        act0 = rt.request_action("npc_warden", "act_grant_crypt_key")
        self.assertEqual(act0.stance, "ACTION_PREREQ_MISSING")
        self.assertEqual(
            set(act0.state_delta["missing_prereqs"]),
            {"truth_abbot_culprit", "ev_warden_oath_signed"},
        )

        # Presenting oil flask alone leaves truth_abbot_culprit unproven
        act1 = rt.present_item("npc_warden", "item_charred_oil_flask")
        self.assertNotIn("truth_abbot_culprit", act1.state_delta["newly_established"])

        # Completing with abbot confession proves truth_abbot_culprit and shatters hero myth
        act2 = rt.present_item("npc_warden", "item_abbot_confession")
        self.assertEqual(act2.stance, "BELIEF_SHATTERED")
        self.assertIn("truth_abbot_culprit", act2.state_delta["newly_established"])
        self.assertIn("lore_church_hero_myth", act2.state_delta["defeated_beliefs"])

        # Signing oath satisfies final precondition and grants crypt key
        act3 = rt.request_action(
            "npc_warden",
            "act_grant_crypt_key",
            extra_facts=["ev_warden_oath_signed"],
        )
        self.assertEqual(act3.stance, "ACTION_GRANTED")

        # Export verified 2,144B FIR1 QTT gate with act_grant_crypt_key bit active
        warden_fir = rt.export_npc_fir_gate("npc_warden", repaired=False)
        self.assertEqual(len(warden_fir.fir_bytes), 2144)
        self.assertTrue(warden_fir.fir_bytes.startswith(b"FIR1"))
        self.assertEqual(warden_fir.prog_name, "npc_warden")
        self.assertNotEqual(warden_fir.active_mask & (1 << id_to_idx["act_grant_crypt_key"]), 0)

    def test_contracts_and_core_zero_coupling(self) -> None:
        ext_contract = json.loads(
            (NARRATIVE_ROOT / "contracts/lore_extractor_contract.json").read_text(encoding="utf-8")
        )
        dlg_contract = json.loads(
            (NARRATIVE_ROOT / "contracts/dialogue_realizer_contract.json").read_text(encoding="utf-8")
        )
        self.assertIn("intentional_rashomon", ext_contract["required"])
        self.assertIn("in_character_line", dlg_contract["required"])

        core_files = ("Sound.lean", "sound.h", "sound.c", "sound_lean_jsi.c", "Makefile")
        forbidden = ("narrative", "ashen_bell", "modules/")
        for fname in core_files:
            content = (REPO_ROOT / fname).read_text(encoding="utf-8")
            for term in forbidden:
                self.assertNotIn(term, content, msg=f"Core file {fname} leaked {term!r}")


if __name__ == "__main__":
    unittest.main()
