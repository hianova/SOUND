"""End-to-end tests for SOUND domain-free Python SDK (`arena_codec`, FFI/CLI parity, policy isolation)."""

from __future__ import annotations

import unittest
from pathlib import Path

from modules.common.arena_codec import (
    NODE_FLAG_CONSTITUTIVE,
    SOUND_ARENA_BYTES,
    SOUND_FIR_BYTES,
    SOUND_FOREST_CTRL_NO_DIRECT_CLAIM,
    SOUND_INJECT_REASON_FLAGS,
    ArenaNodeSpec,
    SoundFiber,
    SoundVerifyError,
    admit_candidate_bytes,
    decode_inject_reasons,
    encode_arena,
    lift_fir_arena_bytes,
    load_libsound,
    load_schema_hash,
    policy_seal_bytes,
    policy_verify_bytes,
    repair_arena_bytes,
    try_verify_arena_bytes,
    verify_arena_bytes,
)

REPO_ROOT = Path(__file__).resolve().parents[2]
SOUND_BIN = REPO_ROOT / "build" / "sound"


class SoundModulesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not SOUND_BIN.is_file():
            raise RuntimeError(f"Missing {SOUND_BIN}; run `make all` in repo root first.")
        cls.schema_hash = load_schema_hash(REPO_ROOT)

    def test_arena_codec_roundtrip_and_tamper(self) -> None:
        # Case-3-equivalent 5-node graph encoded cleanly in Python and verified by C17 `sound verify`
        nodes = [
            ArenaNodeSpec(
                label="park_rule",
                sort="normative",
                evid="enacted_norm",
                clauses=[0],
            ),
            ArenaNodeSpec(
                label="sedan_ban",
                sort="normative",
                evid="direct",
                clauses=[1 << 0],
            ),
            ArenaNodeSpec(
                label="emergency_exc",
                sort="normative",
                evid="enacted_norm",
                clauses=[0],
            ),
            ArenaNodeSpec(
                label="ambulance_ban",
                sort="normative",
                evid="direct",
                attack_mask=(1 << 2),
                clauses=[1 << 0],
            ),
            ArenaNodeSpec(
                label="escooter_ban",
                sort="normative",
                evid="direct",
                penumbra=True,
                clauses=[1 << 0],
            ),
        ]
        arena = encode_arena(nodes, schema_hash=self.schema_hash)
        self.assertEqual(len(arena), SOUND_ARENA_BYTES)

        res = verify_arena_bytes(arena, SOUND_BIN)
        self.assertEqual(res.nodes[1].status, "established")
        self.assertEqual(res.nodes[3].status, "defeated")
        self.assertEqual(res.nodes[3].reasons, 0x40)
        self.assertEqual(res.nodes[4].status, "penumbraOpen")
        self.assertEqual(res.nodes[4].reasons, 0x01)

        # Flip 1 byte in cell 1 (covered by FNV-1a Merkle root) -> fallback MAC fiber
        tampered = bytearray(arena)
        tampered[64] ^= 0x01
        outcome = try_verify_arena_bytes(bytes(tampered), SOUND_BIN)
        self.assertFalse(outcome.committed)
        self.assertEqual(outcome.fiber, SoundFiber.FALLBACK_MAC)
        with self.assertRaises(SoundVerifyError):
            verify_arena_bytes(bytes(tampered), SOUND_BIN)

    def test_conflict_undecided_and_searu_repair(self) -> None:
        # Mutually attacking norms -> conflictUndecided + SEARU repair convergence
        conflict_nodes = [
            ArenaNodeSpec(label="norm_a", sort="normative", evid="enacted_norm", attack_mask=(1 << 1), clauses=[0]),
            ArenaNodeSpec(label="norm_b", sort="normative", evid="enacted_norm", attack_mask=(1 << 0), clauses=[0]),
        ]
        conflict_arena = encode_arena(conflict_nodes, schema_hash=self.schema_hash)
        c_eval = verify_arena_bytes(conflict_arena, SOUND_BIN)
        self.assertEqual(c_eval.nodes[0].status, "conflictUndecided")
        self.assertEqual(c_eval.nodes[1].status, "conflictUndecided")
        rep = repair_arena_bytes(conflict_arena, SOUND_BIN)
        self.assertTrue(rep.converged)
        self.assertEqual(rep.energy, 0)

    def test_ffi_cli_bit_exact_parity(self) -> None:
        lib = load_libsound(SOUND_BIN)
        self.assertIsNotNone(lib, "Expected pure-GPR build/libsound.dylib/.so to exist")

        # Park Case-3-like non-conflict graph -> verify + lift-fir FFI vs CLI parity
        park_nodes = [
            ArenaNodeSpec(label="park_rule", sort="normative", evid="enacted_norm", clauses=[0]),
            ArenaNodeSpec(label="sedan_ban", sort="normative", evid="direct", clauses=[1 << 0]),
            ArenaNodeSpec(label="emergency_exc", sort="normative", evid="enacted_norm", clauses=[0]),
            ArenaNodeSpec(label="ambulance_ban", sort="normative", evid="direct", attack_mask=(1 << 2), clauses=[1 << 0]),
            ArenaNodeSpec(label="escooter_ban", sort="normative", evid="direct", penumbra=True, clauses=[1 << 0]),
        ]
        park_arena = encode_arena(park_nodes, schema_hash=self.schema_hash)
        ev_ffi = verify_arena_bytes(park_arena, SOUND_BIN, backend="ffi")
        ev_cli = verify_arena_bytes(park_arena, SOUND_BIN, backend="cli")
        self.assertEqual(ev_ffi.i_star, ev_cli.i_star)
        self.assertEqual(ev_ffi.p_star, ev_cli.p_star)
        self.assertEqual(ev_ffi.established_mask, ev_cli.established_mask)
        self.assertEqual(ev_ffi.defeated_mask, ev_cli.defeated_mask)
        self.assertEqual(ev_ffi.penumbra_mask, ev_cli.penumbra_mask)
        self.assertEqual(
            [(n.status, n.reasons) for n in ev_ffi.nodes],
            [(n.status, n.reasons) for n in ev_cli.nodes],
        )

        fir_ffi = lift_fir_arena_bytes(park_arena, SOUND_BIN, prog_name="parity_gate", backend="ffi")
        fir_cli = lift_fir_arena_bytes(park_arena, SOUND_BIN, prog_name="parity_gate", backend="cli")
        self.assertEqual(len(fir_ffi.fir_bytes), SOUND_FIR_BYTES)
        self.assertEqual(fir_ffi.fir_bytes, fir_cli.fir_bytes)
        self.assertEqual(fir_ffi.active_mask, fir_cli.active_mask)

        # Nixon diamond -> SEARU repair + --repaired lift-fir FFI vs CLI parity
        conf_nodes = [
            ArenaNodeSpec(label="quaker", sort="normative", evid="enacted_norm", attack_mask=(1 << 1), clauses=[0]),
            ArenaNodeSpec(label="republican", sort="normative", evid="enacted_norm", attack_mask=(1 << 0), clauses=[0]),
        ]
        conf_arena = encode_arena(conf_nodes, schema_hash=self.schema_hash)
        rep_ffi = repair_arena_bytes(conf_arena, SOUND_BIN, seed="0x1234", backend="ffi")
        rep_cli = repair_arena_bytes(conf_arena, SOUND_BIN, seed="0x1234", backend="cli")
        self.assertEqual(rep_ffi.cand, rep_cli.cand)
        self.assertEqual(rep_ffi.retract, rep_cli.retract)
        self.assertEqual(rep_ffi.energy, rep_cli.energy)
        self.assertEqual(rep_ffi.converged, rep_cli.converged)

        rfir_ffi = lift_fir_arena_bytes(
            conf_arena, SOUND_BIN, prog_name="nixon_rep", repaired=True, seed="0x1234", backend="ffi"
        )
        rfir_cli = lift_fir_arena_bytes(
            conf_arena, SOUND_BIN, prog_name="nixon_rep", repaired=True, seed="0x1234", backend="cli"
        )
        self.assertEqual(rfir_ffi.fir_bytes, rfir_cli.fir_bytes)
        self.assertEqual(rfir_ffi.active_mask, rfir_cli.active_mask)

    def test_prepared_statement_policy_isolation_ffi(self) -> None:
        self.assertEqual(len(SOUND_INJECT_REASON_FLAGS), 11)
        nodes = [
            ArenaNodeSpec(
                label="rule_node",
                sort="institutional",
                evid="enacted_norm",
                constitutive=True,
                clauses=[1 << 1],
            ),
            ArenaNodeSpec(
                label="bind_leaf",
                sort="factual",
                evid="assumed",
                clauses=[0],
            ),
            ArenaNodeSpec(
                label="imm_node",
                sort="factual",
                evid="direct",
                clauses=[0],
            ),
            ArenaNodeSpec(
                label="unbound_leaf",
                sort="factual",
                evid="assumed",
                clauses=[0],
            ),
        ]
        raw_arena = encode_arena(
            nodes,
            schema_hash=self.schema_hash,
            immunity_mask=(1 << 2),
        )

        # Misconfigured seal retracts cleanly (total HoTT fallback fiber)
        _, rc_overlap, _ = policy_seal_bytes(
            raw_arena, SOUND_BIN, policy_mask=0x3, bind_mask=0x2
        )
        self.assertEqual(rc_overlap, SoundFiber.HOLD_INCOHERENT)
        _, rc_imm, _ = policy_seal_bytes(
            raw_arena, SOUND_BIN, policy_mask=0x1, bind_mask=0x4
        )
        self.assertEqual(rc_imm, SoundFiber.HOLD_IMMUNITY)

        # Valid Prepared-Statement policy seal
        sealed, rc_ok, ctrl = policy_seal_bytes(
            raw_arena,
            SOUND_BIN,
            policy_mask=0x1,
            bind_mask=0x2,
            ctrl_flags=SOUND_FOREST_CTRL_NO_DIRECT_CLAIM,
        )
        self.assertEqual(rc_ok, SoundFiber.COMMITTED)
        self.assertEqual(ctrl.last_fiber, int(SoundFiber.COMMITTED))
        ep_seal = ctrl.fiber_epoch
        self.assertGreaterEqual(ep_seal, 1)
        self.assertNotEqual(ctrl.policy_hash, 0)
        self.assertEqual(policy_verify_bytes(sealed, SOUND_BIN), SoundFiber.COMMITTED)

        # Injection block 1: immunity slot (node 2)
        sealed_imm, v_imm, adm_imm, c_imm = admit_candidate_bytes(
            sealed, SOUND_BIN, target_node=2, evid="sensory"
        )
        self.assertEqual(v_imm, SoundFiber.FALLBACK_POLICY_VIOLATION)
        self.assertEqual(c_imm.last_fiber, int(SoundFiber.FALLBACK_POLICY_VIOLATION))
        self.assertGreater(c_imm.fiber_epoch, ep_seal)
        self.assertEqual(adm_imm, 0xFFFFFFFF)
        self.assertIn("IMMUNITY_SLOT", decode_inject_reasons(c_imm.last_reject_reason))

        # Injection block 2: policy slot (node 0)
        _, v_pol, adm_pol, c_pol = admit_candidate_bytes(
            sealed_imm, SOUND_BIN, target_node=0, evid="sensory"
        )
        self.assertEqual(v_pol, SoundFiber.FALLBACK_POLICY_VIOLATION)
        self.assertEqual(adm_pol, 0xFFFFFFFF)
        self.assertIn("POLICY_SLOT", decode_inject_reasons(c_pol.last_reject_reason))

        # Injection block 3: unbound slot (node 3)
        _, v_unb, adm_unb, c_unb = admit_candidate_bytes(
            sealed, SOUND_BIN, target_node=3, evid="sensory"
        )
        self.assertEqual(v_unb, SoundFiber.FALLBACK_POLICY_VIOLATION)
        self.assertEqual(adm_unb, 0xFFFFFFFF)
        self.assertEqual(decode_inject_reasons(c_unb.last_reject_reason), ["UNBOUND_SLOT"])

        # Injection block 4: cross-plane sort & privilege escalation on bind slot (node 1)
        _, v_esc, adm_esc, c_esc = admit_candidate_bytes(
            sealed,
            SOUND_BIN,
            target_node=1,
            force="declarative",
            sort="institutional",
            evid="enacted_norm",
            node_flags=NODE_FLAG_CONSTITUTIVE,
        )
        self.assertEqual(v_esc, SoundFiber.FALLBACK_POLICY_VIOLATION)
        self.assertEqual(adm_esc, 0xFFFFFFFF)
        esc_reasons = decode_inject_reasons(c_esc.last_reject_reason)
        self.assertIn("CROSS_PLANE_SORT", esc_reasons)
        self.assertIn("PRIV_ESCALATION", esc_reasons)

        # Injection block 5: topology mutation on bind slot (node 1)
        _, v_topo, adm_topo, c_topo = admit_candidate_bytes(
            sealed, SOUND_BIN, target_node=1, evid="sensory", attack_mask=0x1
        )
        self.assertEqual(v_topo, SoundFiber.FALLBACK_POLICY_VIOLATION)
        self.assertEqual(adm_topo, 0xFFFFFFFF)
        self.assertEqual(
            decode_inject_reasons(c_topo.last_reject_reason), ["TOPOLOGY_MUTATION"]
        )

        # Injection block 6: NO_DIRECT_CLAIM enforcement on bind slot (node 1)
        _, v_dir, adm_dir, c_dir = admit_candidate_bytes(
            sealed, SOUND_BIN, target_node=1, evid="direct"
        )
        self.assertEqual(v_dir, SoundFiber.FALLBACK_POLICY_VIOLATION)
        self.assertEqual(adm_dir, 0xFFFFFFFF)
        self.assertEqual(
            decode_inject_reasons(c_dir.last_reject_reason), ["DIRECT_CLAIM_FORBID"]
        )

        # Valid parameter binding on bind slot (node 1) -> committed & established propagation
        bound, v_ok, adm_ok, c_ok = admit_candidate_bytes(
            sealed, SOUND_BIN, target_node=1, sort="factual", evid="sensory"
        )
        self.assertEqual(v_ok, SoundFiber.COMMITTED)
        self.assertEqual(c_ok.last_fiber, int(SoundFiber.COMMITTED))
        self.assertEqual(adm_ok, 1)
        self.assertEqual(c_ok.last_reject_reason, 0)
        self.assertEqual(policy_verify_bytes(bound, SOUND_BIN), SoundFiber.COMMITTED)
        ev_bound = verify_arena_bytes(bound, SOUND_BIN, backend="ffi")
        self.assertEqual(ev_bound.nodes[1].status, "established")
        self.assertEqual(ev_bound.nodes[0].status, "established")

        # Post-seal policy tamper at cell 72 -> value-first FALLBACK_POLICY_TAMPER without throwing
        tampered = bytearray(bound)
        tampered[72 * 64 : 72 * 64 + 8] = b"\x00" * 8
        tampered_bytes = bytes(tampered)
        self.assertEqual(
            policy_verify_bytes(tampered_bytes, SOUND_BIN), SoundFiber.FALLBACK_POLICY_TAMPER
        )
        tampered_admit, v_tamper, adm_tamper, c_tamper = admit_candidate_bytes(
            tampered_bytes, SOUND_BIN, target_node=1, sort="factual", evid="sensory"
        )
        self.assertEqual(v_tamper, SoundFiber.FALLBACK_POLICY_TAMPER)
        self.assertEqual(c_tamper.last_fiber, int(SoundFiber.FALLBACK_POLICY_TAMPER))
        self.assertGreater(c_tamper.fiber_epoch, c_ok.fiber_epoch)
        self.assertEqual(adm_tamper, 0xFFFFFFFF)
        self.assertEqual(
            decode_inject_reasons(c_tamper.last_reject_reason), ["POLICY_TAMPER"]
        )
        tamper_outcome = try_verify_arena_bytes(tampered_admit, SOUND_BIN, backend="ffi")
        self.assertFalse(tamper_outcome.committed)
        self.assertEqual(tamper_outcome.fiber, SoundFiber.FALLBACK_POLICY_TAMPER)

        # Bit-flip tamper at cell 1 -> value-first FALLBACK_MAC without throwing
        mac_bad = bytearray(bound)
        mac_bad[64] ^= 0x01
        mac_outcome = try_verify_arena_bytes(bytes(mac_bad), SOUND_BIN, backend="ffi")
        self.assertFalse(mac_outcome.committed)
        self.assertEqual(mac_outcome.fiber, SoundFiber.FALLBACK_MAC)

    def test_core_zero_coupling(self) -> None:
        core_files = ("Sound.lean", "Sound/Bilattice.lean", "sound.h", "sound.c", "sound_lean_jsi.c", "Makefile")
        existing = [REPO_ROOT / fname for fname in core_files if (REPO_ROOT / fname).is_file()]
        self.assertTrue(existing, msg="Expected at least core C sources (sound.h / sound.c) to exist")
        forbidden = ("modules/", "routine/", "taxation", "accounting", "營業稅", "會計")
        for fpath in existing:
            content = fpath.read_text(encoding="utf-8")
            for term in forbidden:
                self.assertNotIn(term, content, msg=f"Core file {fpath.name} leaked module ref {term!r}")


if __name__ == "__main__":
    unittest.main()
