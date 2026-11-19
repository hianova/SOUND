"""End-to-end verification of Spatial Bridge, physical occlusion, and legal authority boundaries."""

from __future__ import annotations

import json
import re
import subprocess
import sys
import time
import unittest
from fractions import Fraction
from pathlib import Path
from typing import List

from modules.common.arena_codec import (
    SOUND_FIBER_MAP,
    SOUND_FLOW_AUX_OFFSET,
    ArenaNodeSpec,
    SoundFiber,
    SoundVerifyError,
    apply_declarative_arena_bytes,
    apply_potestative_arena_bytes,
    decode_reasons,
    encode_arena,
    load_schema_hash,
    read_flow_aux_words,
    try_verify_arena_bytes,
    verify_arena_bytes,
)
from modules.common.compute_bridge import unpack_compute_flow_aux
from modules.common.spatial_bridge import (
    check_planar_depth,
    check_rigid_attachment,
    evaluate_spatial_spec,
    evaluate_world_governed_arena,
)

REPO_ROOT = Path(__file__).resolve().parents[2]
MODULES_ROOT = Path(__file__).resolve().parents[1]
SOUND_BIN = REPO_ROOT / "build" / "sound"


def _make_clean_bitmap(height: int = 12, width: int = 28) -> List[List[int]]:
    """Build a 12x28 4-slot rectilinear bitmap with crisp vertical glyphs inside each slot."""
    grid = [[240 for _ in range(width)] for _ in range(height)]
    # 4 slots: [0..6], [7..13], [14..20], [21..27]. Keep outer 1px border & slot seams clean!
    for slot_idx in range(4):
        c_mid = slot_idx * 7 + 3
        for r in range(2, height - 2):
            grid[r][c_mid] = 20
            if r in (2, height // 2, height - 3):
                grid[r][c_mid - 1] = 20
                grid[r][c_mid + 1] = 20
    return grid


class SpatialBridgeTestSuite(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.schema_hash = load_schema_hash(REPO_ROOT)

    def test_01_domain_free_purity(self) -> None:
        forbidden_re = re.compile(
            r"\b(?:plate|license|car|leaf|trolley|train|murder|homicide|"
            r"parking|vehicle|accounting|tax|taxation|kaggle)\b"
        )
        for fname in ("spatial_bridge.py", "spatial_bridge_contract.json"):
            fpath = MODULES_ROOT / "common" / fname
            text = fpath.read_text(encoding="utf-8", errors="replace").lower()
            m = forbidden_re.search(text)
            self.assertIsNone(
                m,
                msg=f"Domain term {m.group(0)!r} leaked into {fpath}" if m else "",
            )

        core_files = (
            REPO_ROOT / "Sound.lean",
            REPO_ROOT / "Sound" / "Bilattice.lean",
            REPO_ROOT / "sound.h",
            REPO_ROOT / "sound.c",
            REPO_ROOT / "sound_lean_jsi.c",
            REPO_ROOT / "Makefile",
        )
        existing = [f for f in core_files if f.is_file()]
        self.assertTrue(existing, msg="Expected core C sources (sound.h / sound.c) to exist")
        for fpath in existing:
            text = fpath.read_text(encoding="utf-8", errors="replace").lower()
            self.assertNotIn("spatial_bridge", text, msg=f"Bridge leaked into {fpath}")

    def test_02_occlusion_depth_and_rigid_attachment_scenario(self) -> None:
        # Scenario: deliberate leaf/foreign object attached across a license plate crop
        bmp = _make_clean_bitmap(12, 28)
        # Overlay a foreign blob spanning slot 1 -> slot 2 and protruding into top border row 0
        for r in range(0, 9):
            for c in range(8, 19):
                bmp[r][c] = 15

        # Tilted planar background depth (z = 1000 + 2*x - 3*y) with protruding near leaf at z=420
        samples = []
        for y in range(0, 12, 2):
            for x in range(0, 28, 3):
                z = 1000 + 2 * x - 3 * y
                if 2 <= y <= 6 and 9 <= x <= 18:
                    z = 420
                samples.append([x, y, z])

        attached_frames = [
            {"target_box": [100 + 20 * k, 50 + 5 * k, 200 + 20 * k, 100 + 5 * k],
             "anomaly_box": [135 + 20 * k, 52 + 5 * k, 165 + 20 * k, 88 + 5 * k]}
            for k in range(5)
        ]
        drifting_frames = [
            {"target_box": [100 + 20 * k, 50, 200 + 20 * k, 100],
             "anomaly_box": [120 + 5 * k, 52 + 8 * k, 150 + 5 * k, 88 + 8 * k]}
            for k in range(5)
        ]

        spatial_spec = {
            "planar_depth": {
                "samples": samples,
                "max_plane_dev": 10,
                "near_occlusion_delta": 150,
                "min_occluder_samples": 3,
            },
            "grid_occlusion": {
                "bitmap": bmp,
                "num_slots": 4,
                "fg_threshold": 128,
                "dark_is_fg": True,
                "expected_ink_min": "0.05",
                "expected_ink_max": "0.55",
                "border_band_px": 1,
            },
            "rigid_attachment": {
                "frames": attached_frames,
                "max_rel_jitter": "0.03",
                "min_frames": 4,
                "min_target_motion": 40,
            },
            "candidate_margin": {
                "candidates": [{"id": "ABC1234", "score": "0.41", "format_valid": True}],
                "min_top_score": "0.7",
            },
            "predicate_bindings": {
                "fact_occluded": "any_occluded",
                "fact_rigid_attach": "rigidly_attached",
                "fact_read_clear": "all_clear",
            },
        }

        t0 = time.perf_counter()
        s_rep = evaluate_spatial_spec(spatial_spec)
        elapsed_ms = (time.perf_counter() - t0) * 1000.0
        self.assertLess(elapsed_ms, 20.0)
        self.assertIsNotNone(s_rep.planar_depth)
        self.assertTrue(s_rep.planar_depth.near_occluded)  # type: ignore[union-attr]
        self.assertIsNotNone(s_rep.grid_occlusion)
        self.assertTrue(s_rep.grid_occlusion.is_occluded)  # type: ignore[union-attr]
        self.assertTrue(s_rep.grid_occlusion.border_breached)  # type: ignore[union-attr]
        self.assertTrue(s_rep.grid_occlusion.cross_slot_blob)  # type: ignore[union-attr]
        self.assertIsNotNone(s_rep.rigid_attachment)
        self.assertTrue(s_rep.rigid_attachment.rigidly_attached)  # type: ignore[union-attr]

        drift_rep = check_rigid_attachment(drifting_frames, max_rel_jitter="0.03", min_frames=4, min_target_motion=40)
        self.assertFalse(drift_rep.rigidly_attached)
        self.assertTrue(drift_rep.drifting)

        # SOUND governance: physical occlusion attacks normal CRUD pass; rigid tamper intercept fires
        base_nodes = [
            ArenaNodeSpec(label="fact_occluded", sort="factual", evid="direct", clauses=[0]),
            ArenaNodeSpec(label="fact_rigid_attach", sort="factual", evid="direct", clauses=[0]),
            ArenaNodeSpec(label="fact_read_clear", sort="factual", evid="direct", clauses=[0]),
            ArenaNodeSpec(label="std_crud_rule", sort="normative", evid="enacted_norm", clauses=[0]),
            ArenaNodeSpec(
                label="claim_normal_pass",
                sort="normative",
                evid="direct",
                constitutive=True,
                presup_mask=(1 << 2) | (1 << 3),
                attack_mask=(1 << 0),
                clauses=[1 << 3],
            ),
            ArenaNodeSpec(
                label="norm_tamper_intercept",
                sort="normative",
                evid="enacted_norm",
                constitutive=True,
                presup_mask=(1 << 0) | (1 << 1),
                clauses=[(1 << 0) | (1 << 1)],
            ),
        ]

        res_ffi = evaluate_world_governed_arena(
            base_nodes, spatial_spec=spatial_spec, sound_bin=SOUND_BIN, schema_hash=self.schema_hash, backend="ffi"
        )
        res_cli = evaluate_world_governed_arena(
            base_nodes, spatial_spec=spatial_spec, sound_bin=SOUND_BIN, schema_hash=self.schema_hash, backend="cli"
        )
        self.assertEqual(res_ffi.eval_result.established_mask, res_cli.eval_result.established_mask)
        self.assertEqual(res_ffi.eval_result.nodes[0].status, "established")
        self.assertEqual(res_ffi.eval_result.nodes[1].status, "established")
        self.assertEqual(res_ffi.eval_result.nodes[2].status, "defeated")
        self.assertEqual(res_ffi.eval_result.nodes[4].status, "defeated")
        self.assertIn("ATTACKED", decode_reasons(res_ffi.eval_result.nodes[4].reasons))
        self.assertEqual(res_ffi.eval_result.nodes[5].status, "established")

    def test_03_clean_planar_grid_passes_and_ambiguous_margin_penumbra(self) -> None:
        clean_bmp = _make_clean_bitmap(12, 28)
        clean_samples = [
            [x, y, 1000 + 2 * x - 3 * y]
            for y in range(0, 12, 2)
            for x in range(0, 28, 3)
        ]

        def _nodes() -> List[ArenaNodeSpec]:
            return [
                ArenaNodeSpec(label="fact_read_clear", sort="factual", evid="direct", clauses=[0]),
                ArenaNodeSpec(label="fact_cand_clear", sort="factual", evid="direct", clauses=[0]),
                ArenaNodeSpec(label="std_crud_rule", sort="normative", evid="enacted_norm", clauses=[0]),
                ArenaNodeSpec(
                    label="claim_normal_pass",
                    sort="normative",
                    evid="enacted_norm",
                    presup_mask=(1 << 0) | (1 << 1),
                    clauses=[1 << 2],
                ),
            ]

        clear_spec = {
            "planar_depth": {"samples": clean_samples, "max_plane_dev": 5, "near_occlusion_delta": 50},
            "grid_occlusion": {"bitmap": clean_bmp, "num_slots": 4, "border_band_px": 1},
            "candidate_margin": {
                "candidates": [
                    {"id": "KJA8899", "score": "0.94", "format_valid": True},
                    {"id": "KJA8898", "score": "0.51", "format_valid": True},
                    {"id": "123456", "score": "0.99", "format_valid": False},
                ],
                "min_top_score": "0.70",
                "min_margin": "0.15",
            },
            "predicate_bindings": {
                "fact_read_clear": "all_clear",
                "fact_cand_clear": "candidate_clear",
            },
        }
        ok_res = evaluate_world_governed_arena(
            _nodes(), spatial_spec=clear_spec, sound_bin=SOUND_BIN, schema_hash=self.schema_hash
        )
        self.assertEqual(ok_res.eval_result.nodes[3].status, "established")
        self.assertEqual(ok_res.spatial.candidate_margin.winner_id, "KJA8899")  # type: ignore[union-attr]

        # Narrow margin (0.82 vs 0.79, margin=0.03 < 0.15) -> candidate becomes penumbraOpen, blocks CRUD
        ambig_spec = dict(clear_spec)
        ambig_spec["candidate_margin"] = {
            "candidates": [
                {"id": "B8899", "score": "0.82", "format_valid": True},
                {"id": "88899", "score": "0.79", "format_valid": True},
            ],
            "min_top_score": "0.70",
            "min_margin": "0.15",
        }
        pen_res = evaluate_world_governed_arena(
            _nodes(), spatial_spec=ambig_spec, sound_bin=SOUND_BIN, schema_hash=self.schema_hash
        )
        self.assertEqual(pen_res.eval_result.nodes[1].status, "penumbraOpen")
        self.assertIn("PENUMBRA", decode_reasons(pen_res.eval_result.nodes[1].reasons))
        self.assertEqual(pen_res.eval_result.nodes[3].status, "defeated")
        self.assertIn("UNVERIFIED_PRESUP", decode_reasons(pen_res.eval_result.nodes[3].reasons))

    def test_04_trolley_legal_authority_hume_and_kanlock_firewall(self) -> None:
        # Kinematic inevitability + arithmetic headcount (5 > 1) CANNOT authorize sacrificing a bystander
        spatial_spec = {
            "kinematic_envelope": {
                "speed": 25,
                "max_decel": 5,
                "distance": 40,
                "reaction_time": 0,
            },
            "predicate_bindings": {
                "fact_cannot_stop": "cannot_stop",
            },
        }
        compute_spec = {
            "vars": {"count_main": 5, "count_side": 1},
            "exprs": {"net_diff": ["sub", "count_main", "count_side"]},
            "predicates": {"fact_more_on_main": ["gt", "count_main", "count_side"]},
        }
        nodes = [
            ArenaNodeSpec(label="fact_cannot_stop", sort="factual", evid="direct", clauses=[0]),
            ArenaNodeSpec(label="fact_more_on_main", sort="factual", evid="direct", clauses=[0]),
            # Attempting utilitarian leap from pure facts -> C17 blocks via HUME_IS_TO_OUGHT (0x08)
            ArenaNodeSpec(
                label="norm_utilitarian_divert",
                sort="normative",
                evid="inferential",
                constitutive=False,
                clauses=[(1 << 0) | (1 << 1)],
            ),
            ArenaNodeSpec(label="act_max_brake", sort="normative", evid="enacted_norm", clauses=[0]),
            ArenaNodeSpec(label="act_divert_bystander", sort="normative", evid="enacted_norm", clauses=[0]),
        ]

        # act_max_brake (idx 3) has lawful power_mask; act_divert_bystander (idx 4) lacks power and is KanLock-immune
        res = evaluate_world_governed_arena(
            nodes,
            spatial_spec=spatial_spec,
            compute_spec=compute_spec,
            sound_bin=SOUND_BIN,
            schema_hash=self.schema_hash,
            power_mask=(1 << 3),
            immunity_mask=(1 << 4),
        )
        self.assertEqual(res.spatial.kinematic_envelope.stopping_distance, Fraction(125, 2))  # type: ignore[union-attr]
        self.assertEqual(res.eval_result.nodes[0].status, "established")
        self.assertEqual(res.eval_result.nodes[1].status, "established")
        self.assertEqual(res.eval_result.nodes[2].status, "defeated")
        self.assertIn("HUME_IS_TO_OUGHT", decode_reasons(res.eval_result.nodes[2].reasons))

        # 1) Authorized maximum braking executes cleanly -> SOUND_FIBER_COMMITTED (0)
        _s1, rc_brake, snap_brake = apply_potestative_arena_bytes(
            res.arena_bytes, SOUND_BIN, slot=3, grant_pos=True
        )
        self.assertEqual(SOUND_FIBER_MAP[int(rc_brake)], "SOUND_FIBER_COMMITTED")
        self.assertNotEqual(snap_brake.duty_pos_mask & (1 << 3), 0)

        # 2) Without KanLock immunity, missing legal power retracts cleanly -> SOUND_FIBER_HOLD_NO_POWER (6)
        no_pow_arena = encode_arena(
            nodes, schema_hash=self.schema_hash, power_mask=(1 << 3), immunity_mask=0
        )
        _s2, rc_nopow, _ = apply_potestative_arena_bytes(
            no_pow_arena, SOUND_BIN, slot=4, grant_pos=True
        )
        self.assertEqual(SOUND_FIBER_MAP[int(rc_nopow)], "SOUND_FIBER_HOLD_NO_POWER")

        # 3) Under KanLock immunity, EVEN if power bit is set -> SOUND_FIBER_HOLD_IMMUNITY (5)
        locked_arena = encode_arena(
            nodes, schema_hash=self.schema_hash, power_mask=(1 << 3) | (1 << 4), immunity_mask=(1 << 4)
        )
        _s3, rc_pot_lock, _ = apply_potestative_arena_bytes(
            locked_arena, SOUND_BIN, slot=4, grant_pos=True
        )
        self.assertEqual(SOUND_FIBER_MAP[int(rc_pot_lock)], "SOUND_FIBER_HOLD_IMMUNITY")

        _s4, rc_dec_lock, _ = apply_declarative_arena_bytes(
            locked_arena,
            SOUND_BIN,
            intent_node=4,
            duty_slot=4,
            in_constitutive_context=True,
            internal_will_matches=True,
            utterance_hash=0x12345678,
        )
        self.assertEqual(SOUND_FIBER_MAP[int(rc_dec_lock)], "SOUND_FIBER_HOLD_IMMUNITY")

    def test_05_tilted_plane_invariance_and_missing_depth_penumbra(self) -> None:
        tilted = [[x, y, 1000 + 2 * x - 3 * y] for y in range(4) for x in range(4)]
        ok_depth = check_planar_depth(tilted, max_plane_dev=0, near_occlusion_delta=10)
        self.assertFalse(ok_depth.undetermined)
        self.assertTrue(ok_depth.plane_consistent)
        self.assertFalse(ok_depth.near_occluded)
        self.assertEqual(ok_depth.plane_coeffs, (Fraction(2), Fraction(-3), Fraction(1000)))

        sparse = [[0, 0, 1000], [1, 0, None], [0, 1, None], [1, 1, None]]
        nodes = [ArenaNodeSpec(label="fact_depth_clean", sort="factual", evid="direct", clauses=[0])]
        sparse_res = evaluate_world_governed_arena(
            nodes,
            spatial_spec={
                "planar_depth": {"samples": sparse, "min_coverage": "0.5"},
                "predicate_bindings": {"fact_depth_clean": "plane_consistent"},
            },
            sound_bin=SOUND_BIN,
            schema_hash=self.schema_hash,
        )
        self.assertTrue(sparse_res.spatial.planar_depth.undetermined)  # type: ignore[union-attr]
        self.assertEqual(sparse_res.eval_result.nodes[0].status, "penumbraOpen")

    def test_06_combined_spatial_compute_flow_aux_merkle(self) -> None:
        nodes = [
            ArenaNodeSpec(label="fact_cannot_stop", sort="factual", evid="direct", clauses=[0]),
            ArenaNodeSpec(label="fact_energy_high", sort="factual", evid="direct", clauses=[0]),
        ]
        res = evaluate_world_governed_arena(
            nodes,
            spatial_spec={
                "kinematic_envelope": {"speed": 20, "max_decel": 5, "distance": 30},
                "predicate_bindings": {"fact_cannot_stop": "cannot_stop"},
            },
            compute_spec={
                "aux_scale": 2,
                "vars": {"excess": 5},
                "exprs": {"overshoot": ["sub", "stopping_distance", 30], "total_risk": ["mul", "overshoot", "excess"]},
                "predicates": {"fact_energy_high": ["gt", "overshoot", 0]},
            },
            sound_bin=SOUND_BIN,
            schema_hash=self.schema_hash,
        )
        self.assertEqual(res.compute.values["overshoot"].value, Fraction(10))  # type: ignore[union-attr]
        self.assertEqual(res.compute.values["total_risk"].value, Fraction(50))  # type: ignore[union-attr]
        words = read_flow_aux_words(res.arena_bytes)
        unpacked = unpack_compute_flow_aux(words)
        self.assertEqual(unpacked["num_entries"], 2)

        tampered = bytearray(res.arena_bytes)
        tampered[SOUND_FLOW_AUX_OFFSET + 8] ^= 0x01
        outcome = try_verify_arena_bytes(bytes(tampered), SOUND_BIN, backend="ffi")
        self.assertFalse(outcome.committed)
        self.assertEqual(outcome.fiber, SoundFiber.FALLBACK_MAC)
        with self.assertRaises(SoundVerifyError) as ctx:
            verify_arena_bytes(bytes(tampered), SOUND_BIN, backend="ffi")
        self.assertIn("SOUND_FIBER_FALLBACK_MAC", str(ctx.exception))

    def test_07_cli_spatial_subcommand(self) -> None:
        spec = {
            "kinematic_envelope": {"speed": 10, "max_decel": 5, "distance": 12},
            "predicate_bindings": {"fact_stop": "can_stop"},
        }
        proc = subprocess.run(
            [sys.executable, "-m", "modules.common.cli", "spatial", "-"],
            cwd=str(REPO_ROOT),
            input=json.dumps(spec),
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(proc.returncode, 0, msg=proc.stderr)
        out = json.loads(proc.stdout)
        self.assertTrue(out["flags"]["can_stop"])
        self.assertEqual(out["metrics"]["stopping_distance"], "10")
        self.assertTrue(out["predicates"]["fact_stop"]["truth"])
