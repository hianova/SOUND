"""End-to-end verification of SOUND 6-direction hardening via Ring-0 FFI, PreparedPolicy SDK, and CLI."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

from modules.common.arena_codec import (
    SOUND_FOREST_CTRL_NO_DIRECT_CLAIM,
    SOUND_FOREST_CTRL_PROPOSER_LOW_EVID,
    SOUND_GLUE_MAGIC_VER,
    ArenaNodeSpec,
    CandidateSpec,
    SoundFiber,
    admit_candidate_with_utterance_bytes,
    decode_inject_reasons,
    encode_arena,
    fnv1a_64,
    load_schema_hash,
    policy_seal_bytes,
    policy_seal_keyed_bytes,
    policy_verify_bytes,
    policy_verify_keyed_bytes,
    siphash24_bytes,
    siphash24_u64,
)
from modules.common.prepared_policy import PreparedPolicy

REPO_ROOT = Path(__file__).resolve().parents[2]
SOUND_BIN = REPO_ROOT / "build" / "sound"


class SoundHardeningTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not SOUND_BIN.is_file():
            raise RuntimeError(f"Missing {SOUND_BIN}; run `make all` in repo root first.")
        cls.schema_hash = load_schema_hash(REPO_ROOT)

    def test_1_siphash24_parity_and_keyed_policy_downgrade_guard(self) -> None:
        # Official Aumasson-Bernstein 15-byte vector: key 00..0f, msg 00..0e -> 0xa129ca6149be45e5
        k0 = 0x0706050403020100
        k1 = 0x0F0E0D0C0B0A0908
        msg15 = bytes(range(15))
        expected = 0xA129CA6149BE45E5
        self.assertEqual(siphash24_u64(k0, k1, msg15), expected)
        self.assertEqual(siphash24_bytes(k0, k1, msg15, SOUND_BIN), expected)

        # Also test multi-block + empty bytes + NUL bytes parity
        for sample in (b"", b"a", b"abc\x00def\x01\xff" * 3, bytes(range(64))):
            self.assertEqual(
                siphash24_u64(0x1122334455667788, 0x99AABBCCDDEEFF00, sample),
                siphash24_bytes(0x1122334455667788, 0x99AABBCCDDEEFF00, sample, SOUND_BIN),
            )

        nodes = [
            ArenaNodeSpec("statute", "normative", "enacted_norm", clauses=[0]),
            ArenaNodeSpec("fact_a", "factual", "assumed", clauses=[0]),
        ]
        arena = encode_arena(nodes, schema_hash=self.schema_hash)
        sealed, fiber, ctrl = policy_seal_keyed_bytes(
            arena,
            SOUND_BIN,
            policy_mask=0x1,
            bind_mask=0x2,
            k0=k0,
            k1=k1,
            ctrl_flags=SOUND_FOREST_CTRL_NO_DIRECT_CLAIM,
        )
        self.assertEqual(fiber, SoundFiber.COMMITTED)
        self.assertTrue(ctrl.ctrl_flags & 0x4)
        self.assertEqual(policy_verify_keyed_bytes(sealed, SOUND_BIN, k0=k0, k1=k1), SoundFiber.COMMITTED)
        self.assertEqual(
            policy_verify_keyed_bytes(sealed, SOUND_BIN, k0=k0 ^ 1, k1=k1),
            SoundFiber.FALLBACK_POLICY_TAMPER,
        )
        # Downgrade guard: unkeyed policy_verify must reject keyed arena even before touching bad keys
        self.assertEqual(policy_verify_bytes(sealed, SOUND_BIN), SoundFiber.FALLBACK_POLICY_TAMPER)

    def test_2_utterance_span_hash_and_atomic_batch_and_low_evid_ceiling(self) -> None:
        utt = b"export_ok=yes;invoice_valid=yes;zero_tax=no;"
        nodes = [
            ArenaNodeSpec("statute_vat", "normative", "enacted_norm", clauses=[0]),
            ArenaNodeSpec("export_ok", "factual", "assumed", clauses=[0]),
            ArenaNodeSpec("invoice_ok", "factual", "assumed", clauses=[0]),
            ArenaNodeSpec("derived_zero_rate", "normative", "inferential", clauses=[0b0111]),
        ]

        # Single-candidate Ring-0 utterance span-hash verification
        base = encode_arena(nodes, schema_hash=self.schema_hash)
        sealed, f0, _ = policy_seal_bytes(
            base,
            SOUND_BIN,
            policy_mask=0b1001,
            bind_mask=0b0110,
            ctrl_flags=SOUND_FOREST_CTRL_NO_DIRECT_CLAIM,
        )
        self.assertEqual(f0, SoundFiber.COMMITTED)

        bad_utt, f_bad, node_bad, ctrl_bad = admit_candidate_with_utterance_bytes(
            sealed,
            SOUND_BIN,
            candidate=CandidateSpec(
                target_node=1,
                evid="sensory",
                span_start=0,
                span_end=13,
                payload_hash=0x12345678,
            ),
            utterance=utt,
        )
        self.assertEqual(f_bad, SoundFiber.FALLBACK_SPAN)
        self.assertEqual(node_bad, 0xFFFFFFFF)
        self.assertIn("SPAN_HASH_MISMATCH", decode_inject_reasons(ctrl_bad.last_reject_reason))

        ok_utt, f_ok, node_ok, _ = admit_candidate_with_utterance_bytes(
            sealed,
            SOUND_BIN,
            candidate=CandidateSpec(
                target_node=1,
                evid="sensory",
                span_start=0,
                span_end=13,
                payload_hash=fnv1a_64(utt[0:13]),
            ),
            utterance=utt,
        )
        self.assertEqual(f_ok, SoundFiber.COMMITTED)
        self.assertEqual(node_ok, 1)

        # PreparedPolicy keyed batch + PROPOSER_LOW_EVID + duplicate target rollback + clean commit
        key = (0x534F554E445F4B30, 0x5052455041524531)
        pol_low = PreparedPolicy.prepare(
            nodes,
            policy_nodes=["statute_vat", "derived_zero_rate"],
            bind_nodes=["export_ok", "invoice_ok"],
            sound_bin=SOUND_BIN,
            key=key,
            ctrl_flags=SOUND_FOREST_CTRL_NO_DIRECT_CLAIM | SOUND_FOREST_CTRL_PROPOSER_LOW_EVID,
        )
        self.assertEqual(pol_low.verify_policy(), SoundFiber.COMMITTED)

        # Reject if slot 2 claims SENSORY under PROPOSER_LOW_EVID (slot 1 must NOT be mutated)
        pol_rej, f_rej, cnt_rej, ctrl_rej = pol_low.bind_batch(
            [
                {"target": "export_ok", "evid": "reportative", "span_start": 0, "span_end": 13},
                {"target": "invoice_ok", "evid": "sensory", "span_start": 14, "span_end": 31},
            ],
            utterance=utt,
        )
        self.assertEqual(f_rej, SoundFiber.FALLBACK_POLICY_VIOLATION)
        self.assertEqual(cnt_rej, 0)
        self.assertIs(pol_rej, pol_low)
        self.assertEqual(pol_low.eval().established_mask, 0b0001)  # atomic: nothing bound

        # Reject duplicate target_node atomically
        pol_dup, f_dup, cnt_dup, _ = pol_low.bind_batch(
            [
                {"target": "export_ok", "evid": "reportative", "span_start": 0, "span_end": 13},
                {"target": "export_ok", "evid": "reportative", "span_start": 14, "span_end": 31},
            ],
            utterance=utt,
        )
        self.assertEqual(f_dup, SoundFiber.FALLBACK_POLICY_VIOLATION)
        self.assertEqual(cnt_dup, 0)
        self.assertIs(pol_dup, pol_low)

        # Commit both REPORTATIVE -> hearsay leaves quarantined in eval (established_mask stays 0b0001)
        pol_rep, f_rep, cnt_rep, _ = pol_low.bind_batch(
            [
                {"target": "export_ok", "evid": "reportative", "span_start": 0, "span_end": 13},
                {"target": "invoice_ok", "evid": "reportative", "span_start": 14, "span_end": 31},
            ],
            utterance=utt,
        )
        self.assertEqual(f_rep, SoundFiber.COMMITTED)
        self.assertEqual(cnt_rep, 2)
        ev_rep = pol_rep.eval()
        self.assertEqual(ev_rep.established_mask, 0b0001)
        self.assertIn("EVIDENTIAL_LEAF", ev_rep.nodes[1].reason_names)

        # Trusted tool-receipt channel (without PROPOSER_LOW_EVID): SENSORY batch establishes all 4
        pol_tool = PreparedPolicy.prepare(
            nodes,
            policy_nodes=["statute_vat", "derived_zero_rate"],
            bind_nodes=["export_ok", "invoice_ok"],
            sound_bin=SOUND_BIN,
            key=key,
            ctrl_flags=SOUND_FOREST_CTRL_NO_DIRECT_CLAIM,
        )
        pol_bound, f_tool, cnt_tool, _ = pol_tool.bind_batch(
            [
                {"target": "export_ok", "evid": "sensory", "span_start": 0, "span_end": 13},
                {"target": "invoice_ok", "evid": "sensory", "span_start": 14, "span_end": 31},
            ],
            utterance=utt,
        )
        self.assertEqual(f_tool, SoundFiber.COMMITTED)
        self.assertEqual(cnt_tool, 2)
        self.assertEqual(pol_bound.eval().established_mask, 0b1111)

    def test_3_lex_specialis_weighted_searu_and_kanlock_dominance(self) -> None:
        nodes = [
            ArenaNodeSpec("general_rule", "normative", "enacted_norm", attack_mask=(1 << 1), clauses=[0]),
            ArenaNodeSpec("special_rule", "normative", "enacted_norm", attack_mask=(1 << 0), clauses=[0]),
        ]
        pol = PreparedPolicy.prepare(
            nodes,
            policy_nodes=["general_rule", "special_rule"],
            bind_nodes=(),
            sound_bin=SOUND_BIN,
        )
        # Prefer special_rule (weight 100 > weight 10)
        _, f1, rep1 = pol.repair_weighted({"general_rule": 10, "special_rule": 100})
        self.assertEqual(f1, SoundFiber.COMMITTED)
        self.assertTrue(rep1.converged)
        self.assertEqual(rep1.cand, 0b10)
        self.assertEqual(rep1.energy, 10)

        # Pin general_rule with KanLock immunity: dominates special_rule even with w=(1, 255)
        pol_imm = PreparedPolicy.prepare(
            nodes,
            policy_nodes=["general_rule", "special_rule"],
            bind_nodes=(),
            immunity_nodes=["general_rule"],
            sound_bin=SOUND_BIN,
        )
        _, f2, rep2 = pol_imm.repair_weighted({"general_rule": 1, "special_rule": 255})
        self.assertEqual(f2, SoundFiber.COMMITTED)
        self.assertTrue(rep2.converged)
        self.assertEqual(rep2.cand, 0b01)

    def test_4_cross_64node_chart_gluing_sdk_and_tamper_detection(self) -> None:
        src_nodes = [
            ArenaNodeSpec("root_a", "normative", "enacted_norm", clauses=[0]),
            ArenaNodeSpec("export_ok", "normative", "inferential", presup_mask=0b01, clauses=[0b01]),
            ArenaNodeSpec("conflicted", "normative", "enacted_norm", attack_mask=0b100, clauses=[0]),
        ]
        dst_nodes = [
            ArenaNodeSpec("dst_law", "normative", "enacted_norm", clauses=[0]),
            ArenaNodeSpec("imp_fact", "factual", "assumed", clauses=[0]),
            ArenaNodeSpec("dst_out", "normative", "inferential", presup_mask=0b10, clauses=[0b11]),
        ]
        src_pol_uneval = PreparedPolicy.prepare(
            src_nodes,
            policy_nodes=["root_a", "export_ok", "conflicted"],
            bind_nodes=(),
            sound_bin=SOUND_BIN,
        )
        dst_pol = PreparedPolicy.prepare(
            dst_nodes,
            policy_nodes=["dst_law", "dst_out"],
            bind_nodes=["imp_fact"],
            sound_bin=SOUND_BIN,
        )
        # Un-evaluated src chart must fail incoherently
        _, f_uneval, _ = dst_pol.glue_from(src_pol_uneval, export_map={"export_ok": "imp_fact"})
        self.assertEqual(f_uneval, SoundFiber.HOLD_INCOHERENT)

        src_evald, _ = src_pol_uneval.materialize_eval()
        # Exporting conflicted node must fail
        _, f_conf, _ = dst_pol.glue_from(src_evald, export_map={"conflicted": "imp_fact"})
        self.assertEqual(f_conf, SoundFiber.HOLD_INCOHERENT)
        # Importing into policy slot must fail policy isolation
        _, f_slot, _ = dst_pol.glue_from(src_evald, export_map={"export_ok": "dst_out"})
        self.assertEqual(f_slot, SoundFiber.FALLBACK_POLICY_VIOLATION)

        # Clean gluing: commits and downstream dst_out becomes established!
        dst_glued, f_glued, glue = dst_pol.glue_from(
            src_evald, export_map={"export_ok": "imp_fact"}
        )
        self.assertEqual(f_glued, SoundFiber.COMMITTED)
        self.assertEqual(glue.magic_ver, SOUND_GLUE_MAGIC_VER)
        self.assertEqual(dst_glued.verify_glue(src_evald), SoundFiber.COMMITTED)
        self.assertEqual(dst_glued.eval().established_mask, 0b111)

    def test_5_policy_eval_fir_lift_ffi_and_cli_bit_identical(self) -> None:
        nodes = [
            ArenaNodeSpec("statute", "normative", "enacted_norm", clauses=[0]),
            ArenaNodeSpec("bind_main", "factual", "assumed", clauses=[0]),
            ArenaNodeSpec("bind_exc", "factual", "assumed", clauses=[0]),
            ArenaNodeSpec("proviso", "normative", "inferential", clauses=[0b0101]),
            ArenaNodeSpec(
                "effect",
                "normative",
                "inferential",
                presup_mask=0b0010,
                attack_mask=0b1000,
                clauses=[0b0011],
            ),
        ]
        pol = PreparedPolicy.prepare(
            nodes,
            policy_nodes=["statute", "proviso", "effect"],
            bind_nodes=["bind_main", "bind_exc"],
            sound_bin=SOUND_BIN,
        )
        fir_ffi = pol.lift_policy_fir(prog_name="policy_test")
        self.assertTrue(fir_ffi.startswith(b"FIR1"))

        with tempfile.TemporaryDirectory(prefix="snd_pol_fir_") as td:
            snd_p = Path(td) / "policy_test.snd"
            fir_p = Path(td) / "policy_test.fir"
            snd_p.write_bytes(pol.arena_bytes)
            proc = subprocess.run(
                [str(SOUND_BIN), "lift-policy-fir", str(snd_p), str(fir_p)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(proc.returncode, 0, proc.stderr or proc.stdout)
            fir_cli = fir_p.read_bytes()

        # Bit-for-bit parity between Ring-0 FFI and standalone CLI
        self.assertEqual(fir_ffi, fir_cli)


if __name__ == "__main__":
    unittest.main()
