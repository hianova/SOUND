"""Ergonomic SQLite-style Prepared-Statement SDK over SOUND Ring-0 C17 FFI."""

from __future__ import annotations

from dataclasses import dataclass, replace
from pathlib import Path
from typing import Any, Dict, Iterable, Mapping, Optional, Sequence, Tuple, Union

from modules.common.arena_codec import (
    FALLBACK_SCHEMA_HASH,
    SOUND_MAX_NODES,
    ArenaEvalResult,
    ArenaNodeSpec,
    ArenaRepairResult,
    CandidateSpec,
    ChartGlueSnapshot,
    ForestCtrlSnapshot,
    SoundFiber,
    SoundVerifyError,
    _AlignedArenaBuffer,
    _parse_eval_from_arena,
    _use_ffi_backend,
    _verify_arena_buf_with_optional_key,
    admit_batch_bytes,
    chart_glue_import_bytes,
    chart_glue_verify_bytes,
    encode_arena,
    fiber_name,
    lift_policy_eval_fir_bytes,
    load_schema_hash,
    policy_seal_bytes,
    policy_seal_keyed_bytes,
    policy_verify_bytes,
    policy_verify_keyed_bytes,
    repair_weighted_arena_bytes,
    verify_arena_bytes,
)

NodeRef = Union[int, str]


def _resolve_node_index(label_to_idx: Mapping[str, int], num_nodes: int, ref: NodeRef) -> int:
    if isinstance(ref, int):
        if not (0 <= ref < num_nodes):
            raise ValueError(f"Node index {ref} out of bounds 0..{num_nodes - 1}")
        return ref
    if ref not in label_to_idx:
        raise ValueError(f"Unknown node label: {ref!r}")
    return label_to_idx[ref]


def _resolve_node_mask(
    label_to_idx: Mapping[str, int], num_nodes: int, refs: Iterable[NodeRef]
) -> int:
    mask = 0
    for ref in refs:
        mask |= 1 << _resolve_node_index(label_to_idx, num_nodes, ref)
    return mask


def _coerce_candidate(
    item: Union[CandidateSpec, Mapping[str, Any]],
    label_to_idx: Mapping[str, int],
    num_nodes: int,
) -> CandidateSpec:
    if isinstance(item, CandidateSpec):
        if not (0 <= item.target_node < num_nodes):
            raise ValueError(f"Candidate target_node {item.target_node} out of bounds")
        return item

    if "target_node" in item:
        target_idx = _resolve_node_index(label_to_idx, num_nodes, item["target_node"])
    elif "target" in item:
        target_idx = _resolve_node_index(label_to_idx, num_nodes, item["target"])
    else:
        raise ValueError("Candidate mapping must specify 'target' or 'target_node'")

    return CandidateSpec(
        target_node=target_idx,
        force=str(item.get("force", "assertion")),
        sort=str(item.get("sort", "factual")),
        evid=str(item.get("evid", "sensory")),
        aspect=str(item.get("aspect", "state")),
        span_start=int(item.get("span_start", 0)),
        span_end=int(item.get("span_end", 16)),
        node_flags=int(item.get("node_flags", 0)),
        presup_mask=int(item.get("presup_mask", 0)),
        premise_mask=int(item.get("premise_mask", 0)),
        attack_mask=int(item.get("attack_mask", 0)),
        payload_hash=(int(item["payload_hash"]) if item.get("payload_hash") is not None else None),
    )


@dataclass(frozen=True)
class PreparedPolicy:
    """Compiled, tamper-sealed SOUND Prepared-Statement arena with strict data/policy isolation."""

    arena_bytes: bytes
    sound_bin: Path
    label_to_idx: Dict[str, int]
    num_nodes: int
    key: Optional[Tuple[int, int]] = None

    @classmethod
    def prepare(
        cls,
        nodes: Sequence[ArenaNodeSpec],
        *,
        policy_nodes: Iterable[NodeRef],
        bind_nodes: Iterable[NodeRef],
        sound_bin: Path,
        key: Optional[Tuple[int, int]] = None,
        ctrl_flags: int = 0,
        immunity_nodes: Iterable[NodeRef] = (),
        power_nodes: Iterable[NodeRef] = (),
        duty_pos_nodes: Iterable[NodeRef] = (),
        duty_neg_nodes: Iterable[NodeRef] = (),
        schema_hash: Optional[int] = None,
    ) -> PreparedPolicy:
        """Build, Merkle-seal, and lock a 16KB Prepared-Statement `SoundArena` policy."""
        num_nodes = len(nodes)
        label_to_idx: Dict[str, int] = {}
        for i, nd in enumerate(nodes):
            if nd.label in label_to_idx:
                raise ValueError(f"Duplicate node label: {nd.label!r}")
            label_to_idx[nd.label] = i

        pol_mask = _resolve_node_mask(label_to_idx, num_nodes, policy_nodes)
        bnd_mask = _resolve_node_mask(label_to_idx, num_nodes, bind_nodes)
        imm_mask = _resolve_node_mask(label_to_idx, num_nodes, immunity_nodes)
        pow_mask = _resolve_node_mask(label_to_idx, num_nodes, power_nodes)
        dpos_mask = _resolve_node_mask(label_to_idx, num_nodes, duty_pos_nodes)
        dneg_mask = _resolve_node_mask(label_to_idx, num_nodes, duty_neg_nodes)

        eff_hash = schema_hash
        if eff_hash is None:
            repo_candidate = sound_bin.resolve().parent.parent
            eff_hash = load_schema_hash(repo_candidate) if (repo_candidate / "build").is_dir() else FALLBACK_SCHEMA_HASH

        arena = encode_arena(
            nodes,
            schema_hash=eff_hash,
            power_mask=pow_mask,
            immunity_mask=imm_mask,
            duty_pos_mask=dpos_mask,
            duty_neg_mask=dneg_mask,
        )

        if key is not None:
            k0, k1 = key
            sealed, fiber, _ctrl = policy_seal_keyed_bytes(
                arena,
                sound_bin,
                policy_mask=pol_mask,
                bind_mask=bnd_mask,
                k0=k0,
                k1=k1,
                ctrl_flags=ctrl_flags,
            )
        else:
            sealed, fiber, _ctrl = policy_seal_bytes(
                arena,
                sound_bin,
                policy_mask=pol_mask,
                bind_mask=bnd_mask,
                ctrl_flags=ctrl_flags,
            )
        if fiber != SoundFiber.COMMITTED:
            raise SoundVerifyError(f"PreparedPolicy.prepare failed ({fiber_name(fiber.value)})")

        return cls(
            arena_bytes=sealed,
            sound_bin=sound_bin,
            label_to_idx=label_to_idx,
            num_nodes=num_nodes,
            key=key,
        )

    def node_mask(self, refs: Iterable[NodeRef]) -> int:
        return _resolve_node_mask(self.label_to_idx, self.num_nodes, refs)

    def verify_policy(self) -> SoundFiber:
        """Verify structural policy isolation & digest (or keyed SipHash-2-4 HMAC)."""
        if self.key is not None:
            k0, k1 = self.key
            return policy_verify_keyed_bytes(self.arena_bytes, self.sound_bin, k0=k0, k1=k1)
        return policy_verify_bytes(self.arena_bytes, self.sound_bin)

    def bind_batch(
        self,
        bindings: Sequence[Union[CandidateSpec, Mapping[str, Any]]],
        *,
        utterance: Optional[bytes] = None,
    ) -> Tuple[PreparedPolicy, SoundFiber, int, ForestCtrlSnapshot]:
        """Atomically bind 1..16 factual slots; returns unchanged `self` on rollback."""
        cands = [_coerce_candidate(b, self.label_to_idx, self.num_nodes) for b in bindings]
        next_bytes, verdict, count, ctrl = admit_batch_bytes(
            self.arena_bytes,
            self.sound_bin,
            candidates=cands,
            utterance=utterance,
            key=self.key,
        )
        if verdict == SoundFiber.COMMITTED:
            return replace(self, arena_bytes=next_bytes), verdict, count, ctrl
        return self, verdict, 0, ctrl

    def eval(self) -> ArenaEvalResult:
        """Verify & evaluate the 4-valued bilattice kernel via in-memory `libsound` FFI."""
        return verify_arena_bytes(self.arena_bytes, self.sound_bin, backend="ffi", key=self.key)

    def materialize_eval(self) -> Tuple[PreparedPolicy, ArenaEvalResult]:
        """Evaluate and persist `eval_out` / Merkle seal into a new `PreparedPolicy` snapshot."""
        lib = _use_ffi_backend(self.sound_bin, "ffi")
        assert lib is not None
        buf = _AlignedArenaBuffer(self.arena_bytes)
        rc = _verify_arena_buf_with_optional_key(lib, buf, self.key)
        if rc != 0:
            raise SoundVerifyError(f"SoundArena verify failed ({fiber_name(rc)})")
        rc_ev = int(lib.sound_ring0_eval(buf.ptr))
        if rc_ev != 0:
            raise SoundVerifyError(f"sound_eval failed ({fiber_name(rc_ev)})")
        lib.sound_ring0_arena_seal(buf.ptr)
        sealed = buf.read()
        return replace(self, arena_bytes=sealed), _parse_eval_from_arena(sealed)

    def repair_weighted(
        self,
        weights: Union[Mapping[NodeRef, int], Sequence[int]],
        *,
        seed: Optional[str] = None,
    ) -> Tuple[PreparedPolicy, SoundFiber, ArenaRepairResult]:
        """Run Lex Specialis priority-weighted SEARU repair respecting KanLock immunity."""
        if isinstance(weights, Mapping):
            arr = [0] * SOUND_MAX_NODES
            for ref, w in weights.items():
                idx = _resolve_node_index(self.label_to_idx, self.num_nodes, ref)
                arr[idx] = int(w)
            weight_seq: Sequence[int] = arr
        else:
            weight_seq = weights
        next_bytes, fiber, rep = repair_weighted_arena_bytes(
            self.arena_bytes,
            self.sound_bin,
            priority_weights=weight_seq,
            seed=seed,
            key=self.key,
        )
        return replace(self, arena_bytes=next_bytes), fiber, rep

    def glue_from(
        self,
        src: PreparedPolicy,
        *,
        export_map: Mapping[NodeRef, NodeRef],
    ) -> Tuple[PreparedPolicy, SoundFiber, ChartGlueSnapshot]:
        """Import established witnesses from another verified chart into local bind slots."""
        src_mask = 0
        slot_map: Dict[int, int] = {}
        for s_ref, d_ref in export_map.items():
            s_idx = _resolve_node_index(src.label_to_idx, src.num_nodes, s_ref)
            d_idx = _resolve_node_index(self.label_to_idx, self.num_nodes, d_ref)
            src_mask |= 1 << s_idx
            slot_map[s_idx] = d_idx
        next_bytes, fiber, glue = chart_glue_import_bytes(
            self.arena_bytes,
            src.arena_bytes,
            self.sound_bin,
            src_export_mask=src_mask,
            slot_map=slot_map,
            key=self.key,
        )
        if fiber == SoundFiber.COMMITTED:
            return replace(self, arena_bytes=next_bytes), fiber, glue
        return self, fiber, glue

    def verify_glue(self, src: PreparedPolicy) -> SoundFiber:
        """Verify stored `SoundChartGlue64` certificate against source chart `src`."""
        return chart_glue_verify_bytes(self.arena_bytes, src.arena_bytes, self.sound_bin)

    def lift_policy_fir(
        self,
        *,
        prog_name: str = "sound_policy_eval",
        unroll_alt_steps: int = 0,
    ) -> bytes:
        """Compile this sealed policy into a combinational FLOW_IR linear `.fir` circuit."""
        return lift_policy_eval_fir_bytes(
            self.arena_bytes,
            self.sound_bin,
            prog_name=prog_name,
            unroll_alt_steps=unroll_alt_steps,
            key=self.key,
        )
