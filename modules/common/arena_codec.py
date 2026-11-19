"""Pure-stdlib encoder, in-memory `libsound` ctypes FFI, and `build/sound` CLI bridge."""

from __future__ import annotations

import ctypes
import re
import struct
import subprocess
import tempfile
from dataclasses import dataclass, field
from enum import IntEnum
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

SOUND_MAGIC = 0x32444E53  # "SND2"
SOUND_VERSION = 2
SOUND_ARENA_CELLS = 256
SOUND_ARENA_BYTES = 16384
SOUND_FIR_BYTES = 2144
SOUND_MAX_NODES = 64
SOUND_MAX_CLAUSES = 8
SOUND_LABEL_BYTES = 32
SOUND_FLOW_AUX_SLOT = 218
SOUND_FLOW_AUX_CELLS = 38
SOUND_FLOW_AUX_OFFSET = SOUND_FLOW_AUX_SLOT * 64
SOUND_FLOW_AUX_WORDS = SOUND_FLOW_AUX_CELLS * 8

FNV1A_OFFSET = 0xCBF29CE484222325
FNV1A_PRIME = 0x00000100000001B3
FALLBACK_SCHEMA_HASH = 0x1F627D29685823B6

NODE_FLAG_CONSTITUTIVE = 1
SOUND_EVAL_FLAGS_REQ = 0x0F
SOUND_FIR_FLAG_USE_REPAIRED = 1
DEFAULT_SEARU_SEED = 0x53454152555F3031
_SOUND_FIR_MULT_BITS = 575
_SOUND_FIR_MULT_DEPTH = 10

STATUS_CODE_MAP: Dict[int, str] = {
    0: "established",
    1: "defeated",
    2: "conflictUndecided",
    3: "penumbraOpen",
}

class SoundFiber(IntEnum):
    """Canonical 14-branch HoTT fallback fiber replacing split OOP error/verdict codes."""

    COMMITTED = 0
    FALLBACK_NULL = 1
    FALLBACK_BOUNDS = 2
    FALLBACK_FORMAT = 3
    FALLBACK_MAC = 4
    HOLD_IMMUNITY = 5
    HOLD_NO_POWER = 6
    HOLD_MISSING_CONTEXT = 7
    HOLD_INCOHERENT = 8
    FALLBACK_POLICY_TAMPER = 9
    FALLBACK_CLARIFY = 10
    FALLBACK_IRREALIS = 11
    FALLBACK_SPAN = 12
    FALLBACK_POLICY_VIOLATION = 13


SOUND_FIBER_MAP: Dict[int, str] = {f.value: f"SOUND_FIBER_{f.name}" for f in SoundFiber}


def fiber_name(code: int) -> str:
    """Return canonical `SOUND_FIBER_*` name for integer fiber code."""
    return SOUND_FIBER_MAP.get(int(code), f"SOUND_FIBER_UNKNOWN({code})")

SORT_MAP: Dict[str, int] = {
    "factual": 0,
    "normative": 1,
    "institutional": 2,
}

EVID_MAP: Dict[str, int] = {
    "direct": 0,
    "sensory": 1,
    "enacted_norm": 2,
    "inferential": 3,
    "reportative": 4,
    "assumed": 5,
}

ASPECT_MAP: Dict[str, int] = {
    "state": 0,
    "activity": 1,
    "accomplishment": 2,
    "achievement": 3,
}

REASON_FLAGS: Dict[int, str] = {
    0x01: "PENUMBRA",
    0x02: "REVOKED",
    0x04: "EVIDENTIAL_LEAF",
    0x08: "HUME_IS_TO_OUGHT",
    0x10: "HUME_OUGHT_TO_IS",
    0x20: "UNVERIFIED_PRESUP",
    0x40: "ATTACKED",
    0x80: "NO_ACTIVE_CLAUSE",
}

SOUND_FOREST_CTRL_PREPARED_SEALED = 1 << 0
SOUND_FOREST_CTRL_NO_DIRECT_CLAIM = 1 << 1
SOUND_FOREST_CTRL_KEYED_SIPHASH = 1 << 2
SOUND_FOREST_CTRL_PROPOSER_LOW_EVID = 1 << 3
SOUND_MAX_CANDIDATES = 16
SOUND_FLOW_AUX_PRIORITY_CELL = 36
SOUND_FLOW_AUX_GLUE_CELL = 37
SOUND_GLUE_MAGIC_VER = 0x00010000314C4753

FORCE_MAP: Dict[str, int] = {
    "assertion": 0,
    "query": 1,
    "directive": 2,
    "declarative": 3,
    "irrealis": 4,
}

SOUND_INJECT_REASON_FLAGS: Dict[int, str] = {
    1 << 0: "IMMUNITY_SLOT",
    1 << 1: "POLICY_SLOT",
    1 << 2: "UNBOUND_SLOT",
    1 << 3: "CROSS_PLANE_SORT",
    1 << 4: "PRIV_ESCALATION",
    1 << 5: "TOPOLOGY_MUTATION",
    1 << 6: "POLICY_TAMPER",
    1 << 7: "DIRECT_CLAIM_FORBID",
    1 << 8: "EVID_CEILING",
    1 << 9: "SPAN_HASH_MISMATCH",
    1 << 10: "DUPLICATE_TARGET",
}

_SCHEMA_HASH_RE = re.compile(
    r"SOUND_SCHEMA_SCHEMA_HASH\s+UINT64_C\((0x[0-9A-Fa-f]+)\)"
)
_SUMMARY_RE = re.compile(
    r"^\[verify\]\s+\S+\s+\(16384B,\s+nodes=(\d+)\)\s+\|\s+"
    r"I\*=(0x[0-9a-fA-F]+)\s+P\*=(0x[0-9a-fA-F]+)\s+"
    r"est=(0x[0-9a-fA-F]+)\s+def=(0x[0-9a-fA-F]+)\s+"
    r"conf=(0x[0-9a-fA-F]+)\s+pen=(0x[0-9a-fA-F]+)\s+\|\s+invariants=OK\s*$"
)
_NODE_LINE_RE = re.compile(
    r"^\s*node\[(\d+)\]\s+(\S+)\s+->\s+(\w+)\s+\(reasons=(0x[0-9a-fA-F]+)\)\s*$"
)
_REPAIR_RE = re.compile(
    r"^\[repair\]\s+\S+\s+\|\s+conf_in=(0x[0-9a-fA-F]+)\s+->\s+"
    r"cand=(0x[0-9a-fA-F]+)\s+retract=(0x[0-9a-fA-F]+)\s+"
    r"pinned=(0x[0-9a-fA-F]+)\s+energy=(\d+)\s+iters=(\d+)\s+conv=(\d+)\s*$"
)
_LIFT_FIR_RE = re.compile(
    r"^\[lift-fir\]\s+\S+\s+->\s+\S+\s+\((\d+)B,\s+name=(\S+),\s+active=(0x[0-9a-fA-F]+)\)\s+\|\s+"
    r"mult_bits=(\d+)\s+mult_depth=(\d+)\s+unitary=OK\s*$"
)
_CLI_FIBER_RE = re.compile(r"fiber=(\d+)")


class SoundVerifyError(RuntimeError):
    """Raised when `build/sound` fails to verify or repair a SoundArena image."""


@dataclass(frozen=True)
class ArenaNodeSpec:
    """Typed node specification matching SoundNodeMeta64 + SoundNodeClauses64."""

    label: str
    sort: str
    evid: str
    aspect: str = "state"
    constitutive: bool = False
    revoked: bool = False
    penumbra: bool = False
    presup_mask: int = 0
    attack_mask: int = 0
    clauses: List[int] = field(default_factory=list)
    span_start: int = 0
    span_end: int = 0


@dataclass(frozen=True)
class NodeEvalResult:
    """Kernel verification status & reason bitmask for one SoundArena node."""

    index: int
    label: str
    status: str
    reasons: int

    @property
    def reason_names(self) -> List[str]:
        return decode_reasons(self.reasons)


@dataclass(frozen=True)
class ArenaEvalResult:
    """Parsed output of `build/sound verify`."""

    num_nodes: int
    i_star: int
    p_star: int
    established_mask: int
    defeated_mask: int
    conflict_mask: int
    penumbra_mask: int
    nodes: List[NodeEvalResult]
    raw_output: str


@dataclass(frozen=True)
class ArenaRepairResult:
    """Parsed output of `build/sound repair`."""

    conf_in: int
    cand: int
    retract: int
    pinned: int
    energy: int
    iters: int
    converged: bool
    raw_output: str


@dataclass(frozen=True)
class ArenaFirResult:
    """Parsed output and 2,144B `"FIR1"` QTT authority circuit from `build/sound lift-fir`."""

    fir_bytes: bytes
    prog_name: str
    active_mask: int
    mult_bits: int
    mult_depth: int
    raw_output: str


@dataclass(frozen=True)
class NormativeStateSnapshot:
    """Header PhysLog chain & SoundNormative64 masks read from cells 0..1."""

    num_nodes: int
    phys_len: int
    phys_head: int
    phys_hash: int
    merkle_root: int
    revoked_mask: int
    penumbra_mask: int
    duty_pos_mask: int
    duty_neg_mask: int
    power_mask: int
    immunity_mask: int
    voidable_mask: int
    rescission_power_mask: int


def decode_reasons(reasons: int) -> List[str]:
    """Decode SOUND_REASON_* bitmask into ordered human-readable flag names."""
    out: List[str] = []
    for bit, name in sorted(REASON_FLAGS.items()):
        if reasons & bit:
            out.append(name)
    return out


@dataclass(frozen=True)
class ForestCtrlSnapshot:
    """Typed view of SoundForestCtrl64 stored at cell 216 (offset 13,824)."""

    utterance_bytes: int
    ambiguity_flags: int
    unresolved_anaphora: int
    num_candidates: int
    last_fiber: int
    last_admitted_node: int
    ctrl_flags: int
    last_reject_reason: int
    policy_mask: int
    bind_mask: int
    policy_hash: int
    fiber_epoch: int
    reserved: int


@dataclass(frozen=True)
class ChartGlueSnapshot:
    """Typed view of `SoundChartGlue64` stored in `flow_aux[37]` (cell 255)."""

    magic_ver: int
    src_merkle_root: int
    src_policy_hash: int
    src_export_mask: int
    dst_import_mask: int
    glue_hash: int
    epoch_flags: int
    reserved: int


@dataclass(frozen=True)
class CandidateSpec:
    """Typed Layer-0 candidate slot specification for `SoundCandidate64`."""

    target_node: int
    force: str = "assertion"
    sort: str = "factual"
    evid: str = "sensory"
    aspect: str = "state"
    span_start: int = 0
    span_end: int = 16
    node_flags: int = 0
    presup_mask: int = 0
    premise_mask: int = 0
    attack_mask: int = 0
    payload_hash: Optional[int] = None


@dataclass(frozen=True)
class ArenaVerifyOutcome:
    """Value-first HoTT verification outcome returning landing SoundFiber without throwing."""

    fiber: SoundFiber
    eval_result: Optional[ArenaEvalResult]
    forest_ctrl: Optional[ForestCtrlSnapshot] = None

    @property
    def committed(self) -> bool:
        return self.fiber == SoundFiber.COMMITTED


def decode_inject_reasons(mask: int) -> List[str]:
    """Decode SOUND_INJECT_* bitmask into ordered human-readable reason names."""
    out: List[str] = []
    for bit, name in sorted(SOUND_INJECT_REASON_FLAGS.items()):
        if mask & bit:
            out.append(name)
    return out


def read_forest_ctrl(arena_bytes: bytes) -> ForestCtrlSnapshot:
    """Unpack `SoundForestCtrl64` from cell 216 (offset `216 * 64 = 13824`)."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B SoundArena, got {len(arena_bytes)}B")
    vals = struct.unpack_from("<8I3Q2I", arena_bytes, 216 * 64)
    return ForestCtrlSnapshot(
        utterance_bytes=vals[0],
        ambiguity_flags=vals[1],
        unresolved_anaphora=vals[2],
        num_candidates=vals[3],
        last_fiber=vals[4],
        last_admitted_node=vals[5],
        ctrl_flags=vals[6],
        last_reject_reason=vals[7],
        policy_mask=vals[8],
        bind_mask=vals[9],
        policy_hash=vals[10],
        fiber_epoch=vals[11],
        reserved=vals[12],
    )


def read_chart_glue(arena_bytes: bytes) -> ChartGlueSnapshot:
    """Unpack `SoundChartGlue64` from cell 255 (`flow_aux[37]`, offset `255 * 64 = 16320`)."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B SoundArena, got {len(arena_bytes)}B")
    vals = struct.unpack_from("<8Q", arena_bytes, (218 + SOUND_FLOW_AUX_GLUE_CELL) * 64)
    return ChartGlueSnapshot(
        magic_ver=vals[0],
        src_merkle_root=vals[1],
        src_policy_hash=vals[2],
        src_export_mask=vals[3],
        dst_import_mask=vals[4],
        glue_hash=vals[5],
        epoch_flags=vals[6],
        reserved=vals[7],
    )


def fnv1a_64(data: bytes, seed: int = FNV1A_OFFSET) -> int:
    """Compute exact 64-bit FNV-1a matching `sound_fnv1a_bytes` in `sound.h`."""
    h = seed & 0xFFFFFFFFFFFFFFFF
    for b in data:
        h ^= b
        h = (h * FNV1A_PRIME) & 0xFFFFFFFFFFFFFFFF
    return h


def _rotl64(x: int, bits: int) -> int:
    x &= 0xFFFFFFFFFFFFFFFF
    return ((x << bits) | (x >> (64 - bits))) & 0xFFFFFFFFFFFFFFFF


def _sipround(v0: int, v1: int, v2: int, v3: int) -> Tuple[int, int, int, int]:
    m = 0xFFFFFFFFFFFFFFFF
    v0 = (v0 + v1) & m
    v1 = _rotl64(v1, 13) ^ v0
    v0 = _rotl64(v0, 32)
    v2 = (v2 + v3) & m
    v3 = _rotl64(v3, 16) ^ v2
    v0 = (v0 + v3) & m
    v3 = _rotl64(v3, 21) ^ v0
    v2 = (v2 + v1) & m
    v1 = _rotl64(v1, 17) ^ v2
    v2 = _rotl64(v2, 32)
    return v0, v1, v2, v3


def siphash24_u64(k0: int, k1: int, data: bytes) -> int:
    """Pure-Python SipHash-2-4 matching `sound_siphash24_bytes` bit-for-bit."""
    m = 0xFFFFFFFFFFFFFFFF
    kk0 = k0 & m
    kk1 = k1 & m
    v0 = 0x736F6D6570736575 ^ kk0
    v1 = 0x646F72616E646F6D ^ kk1
    v2 = 0x6C7967656E657261 ^ kk0
    v3 = 0x7465646279746573 ^ kk1
    buf = 0
    buflen = 0
    for byte in data:
        buf |= (byte & 0xFF) << (buflen * 8)
        buflen += 1
        if buflen == 8:
            v3 ^= buf
            v0, v1, v2, v3 = _sipround(v0, v1, v2, v3)
            v0, v1, v2, v3 = _sipround(v0, v1, v2, v3)
            v0 ^= buf
            buf = 0
            buflen = 0
    b = ((len(data) & 0xFF) << 56) | buf
    v3 ^= b
    v0, v1, v2, v3 = _sipround(v0, v1, v2, v3)
    v0, v1, v2, v3 = _sipround(v0, v1, v2, v3)
    v0 ^= b
    v2 ^= 0xFF
    for _ in range(4):
        v0, v1, v2, v3 = _sipround(v0, v1, v2, v3)
    return (v0 ^ v1 ^ v2 ^ v3) & m


def load_schema_hash(repo_root: Path) -> int:
    """Read `SOUND_SCHEMA_SCHEMA_HASH` from `build/sound_schema_abi.h` if present."""
    hdr = repo_root / "build" / "sound_schema_abi.h"
    if not hdr.is_file():
        return FALLBACK_SCHEMA_HASH
    text = hdr.read_text(encoding="utf-8", errors="replace")
    m = _SCHEMA_HASH_RE.search(text)
    if not m:
        return FALLBACK_SCHEMA_HASH
    return int(m.group(1), 16)


_LIBSOUND_CACHE: Dict[Path, ctypes.CDLL] = {}


class _AlignedArenaBuffer:
    """64-byte aligned 16,384B host scratch buffer for zero-I/O `libsound` FFI."""

    def __init__(self, initial: Optional[bytes] = None) -> None:
        self._raw = ctypes.create_string_buffer(SOUND_ARENA_BYTES + 64)
        base = ctypes.addressof(self._raw)
        self._offset = (64 - (base & 63)) & 63
        self.ptr = ctypes.c_void_p(base + self._offset)
        assert (self.ptr.value & 63) == 0
        if initial is not None:
            self.write(initial)

    def write(self, data: bytes) -> None:
        if len(data) != SOUND_ARENA_BYTES:
            raise ValueError(f"Expected {SOUND_ARENA_BYTES}B arena, got {len(data)}B")
        ctypes.memmove(self.ptr, data, SOUND_ARENA_BYTES)

    def read(self) -> bytes:
        return bytes(self._raw[self._offset : self._offset + SOUND_ARENA_BYTES])


def _configure_libsound(lib: ctypes.CDLL) -> ctypes.CDLL:
    lib.sound_ring0_arena_verify.argtypes = [ctypes.c_void_p]
    lib.sound_ring0_arena_verify.restype = ctypes.c_uint32

    lib.sound_ring0_arena_seal.argtypes = [ctypes.c_void_p]
    lib.sound_ring0_arena_seal.restype = None

    lib.sound_ring0_eval.argtypes = [ctypes.c_void_p]
    lib.sound_ring0_eval.restype = ctypes.c_uint32

    lib.sound_ring0_searu_repair.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint32]
    lib.sound_ring0_searu_repair.restype = ctypes.c_uint32

    lib.sound_ring0_apply_potestative.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint32,
        ctypes.c_bool,
        ctypes.c_bool,
        ctypes.c_bool,
        ctypes.c_bool,
    ]
    lib.sound_ring0_apply_potestative.restype = ctypes.c_uint32

    lib.sound_ring0_apply_declarative.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_bool,
        ctypes.c_bool,
        ctypes.c_uint64,
    ]
    lib.sound_ring0_apply_declarative.restype = ctypes.c_uint32

    lib.sound_ring0_exercise_rescission.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_uint64,
    ]
    lib.sound_ring0_exercise_rescission.restype = ctypes.c_uint32

    lib.sound_ring0_lift_to_fir.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_uint32,
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_size_t),
    ]
    lib.sound_ring0_lift_to_fir.restype = ctypes.c_uint32

    lib.sound_ring0_policy_seal.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint64,
        ctypes.c_uint64,
        ctypes.c_uint32,
    ]
    lib.sound_ring0_policy_seal.restype = ctypes.c_uint32

    lib.sound_ring0_policy_verify.argtypes = [ctypes.c_void_p]
    lib.sound_ring0_policy_verify.restype = ctypes.c_uint32

    lib.sound_ring0_admit_forest.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_uint32),
    ]
    lib.sound_ring0_admit_forest.restype = ctypes.c_uint32

    lib.sound_ring0_siphash24.argtypes = [
        ctypes.c_uint64,
        ctypes.c_uint64,
        ctypes.c_void_p,
        ctypes.c_size_t,
    ]
    lib.sound_ring0_siphash24.restype = ctypes.c_uint64

    lib.sound_ring0_policy_seal_keyed.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint64,
        ctypes.c_uint64,
        ctypes.c_uint32,
        ctypes.c_uint64,
        ctypes.c_uint64,
    ]
    lib.sound_ring0_policy_seal_keyed.restype = ctypes.c_uint32

    lib.sound_ring0_policy_verify_keyed.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint64,
        ctypes.c_uint64,
    ]
    lib.sound_ring0_policy_verify_keyed.restype = ctypes.c_uint32

    lib.sound_ring0_admit_forest_with_utterance.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_uint32),
    ]
    lib.sound_ring0_admit_forest_with_utterance.restype = ctypes.c_uint32

    lib.sound_ring0_admit_forest_batch.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_uint32),
    ]
    lib.sound_ring0_admit_forest_batch.restype = ctypes.c_uint32

    lib.sound_ring0_admit_forest_batch_keyed.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.c_uint64,
        ctypes.c_uint64,
        ctypes.POINTER(ctypes.c_uint32),
    ]
    lib.sound_ring0_admit_forest_batch_keyed.restype = ctypes.c_uint32

    lib.sound_ring0_searu_repair_weighted.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint64,
        ctypes.c_uint32,
        ctypes.c_void_p,
    ]
    lib.sound_ring0_searu_repair_weighted.restype = ctypes.c_uint32

    lib.sound_ring0_chart_glue_import.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_uint64,
        ctypes.c_void_p,
    ]
    lib.sound_ring0_chart_glue_import.restype = ctypes.c_uint32

    lib.sound_ring0_chart_glue_import_keyed.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_uint64,
        ctypes.c_void_p,
        ctypes.c_uint64,
        ctypes.c_uint64,
    ]
    lib.sound_ring0_chart_glue_import_keyed.restype = ctypes.c_uint32

    lib.sound_ring0_chart_glue_verify.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
    ]
    lib.sound_ring0_chart_glue_verify.restype = ctypes.c_uint32

    lib.sound_ring0_lift_policy_eval_to_fir.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_uint32,
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_size_t),
    ]
    lib.sound_ring0_lift_policy_eval_to_fir.restype = ctypes.c_uint32
    return lib


def load_libsound(sound_bin: Path) -> Optional[ctypes.CDLL]:
    """Load pure-GPR `build/libsound.dylib` / `libsound.so` beside `sound_bin` if present."""
    for ext in ("dylib", "so"):
        candidate = sound_bin.parent / f"libsound.{ext}"
        if candidate.is_file():
            resolved = candidate.resolve()
            if resolved not in _LIBSOUND_CACHE:
                _LIBSOUND_CACHE[resolved] = _configure_libsound(ctypes.CDLL(str(resolved)))
            return _LIBSOUND_CACHE[resolved]
    return None


def read_normative_state(arena_bytes: bytes) -> NormativeStateSnapshot:
    """Unpack SoundImageHeader64 (cell 0) and SoundNormative64 (cell 1) from raw 16KB bytes."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B arena, got {len(arena_bytes)}B")
    (
        _magic,
        _ver,
        num_nodes,
        _fflags,
        phys_len,
        phys_head,
        _rhdr,
        phys_hash,
        _shash,
        merkle_root,
        _rtail,
    ) = struct.unpack_from("<IIIIIIQQQQQ", arena_bytes, 0)
    (
        revoked_mask,
        penumbra_mask,
        duty_pos_mask,
        duty_neg_mask,
        power_mask,
        immunity_mask,
        voidable_mask,
        rescission_power_mask,
    ) = struct.unpack_from("<8Q", arena_bytes, 64)
    return NormativeStateSnapshot(
        num_nodes=num_nodes,
        phys_len=phys_len,
        phys_head=phys_head,
        phys_hash=phys_hash,
        merkle_root=merkle_root,
        revoked_mask=revoked_mask,
        penumbra_mask=penumbra_mask,
        duty_pos_mask=duty_pos_mask,
        duty_neg_mask=duty_neg_mask,
        power_mask=power_mask,
        immunity_mask=immunity_mask,
        voidable_mask=voidable_mask,
        rescission_power_mask=rescission_power_mask,
    )


def read_flow_aux_words(
    arena_bytes: bytes, count: int = SOUND_FLOW_AUX_WORDS
) -> List[int]:
    """Read `count` little-endian `uint64` words from cells 218..255 (`flow_aux`)."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B arena, got {len(arena_bytes)}B")
    if not (0 <= count <= SOUND_FLOW_AUX_WORDS):
        raise ValueError(f"flow_aux count must be in 0..{SOUND_FLOW_AUX_WORDS}, got {count}")
    if count == 0:
        return []
    return list(struct.unpack_from(f"<{count}Q", arena_bytes, SOUND_FLOW_AUX_OFFSET))


def _parse_seed_u64(seed: Optional[str]) -> int:
    if seed is None:
        return DEFAULT_SEARU_SEED
    s = str(seed).strip()
    if s.startswith(("0x", "0X")):
        return int(s, 16) & 0xFFFFFFFFFFFFFFFF
    return int(s, 10) & 0xFFFFFFFFFFFFFFFF


def _parse_eval_from_arena(arena_bytes: bytes) -> ArenaEvalResult:
    num_nodes = struct.unpack_from("<I", arena_bytes, 8)[0]
    (
        _node_mask,
        i_star,
        p_star,
        established_mask,
        defeated_mask,
        conflict_mask,
        penumbra_mask,
        eval_flags,
        _eval_epoch,
    ) = struct.unpack_from("<7QII", arena_bytes, 128)
    if (eval_flags & SOUND_EVAL_FLAGS_REQ) != SOUND_EVAL_FLAGS_REQ:
        raise SoundVerifyError(f"Kernel invariant violation (eval_flags=0x{eval_flags:x})")

    nodes: List[NodeEvalResult] = []
    for u in range(num_nodes):
        st_code = arena_bytes[192 + u]
        reasons = struct.unpack_from("<I", arena_bytes, 256 + (u * 4))[0]
        raw_lbl = arena_bytes[512 + (u * 64) + 32 : 512 + (u * 64) + 64]
        label = raw_lbl.split(b"\x00", 1)[0].decode("ascii", errors="replace")
        nodes.append(
            NodeEvalResult(
                index=u,
                label=label,
                status=STATUS_CODE_MAP.get(st_code, "unknown"),
                reasons=reasons,
            )
        )

    return ArenaEvalResult(
        num_nodes=num_nodes,
        i_star=i_star,
        p_star=p_star,
        established_mask=established_mask,
        defeated_mask=defeated_mask,
        conflict_mask=conflict_mask,
        penumbra_mask=penumbra_mask,
        nodes=nodes,
        raw_output=(
            f"[ffi verify] nodes={num_nodes} I*=0x{i_star:016x} P*=0x{p_star:016x} "
            f"est=0x{established_mask:016x} def=0x{defeated_mask:016x} "
            f"conf=0x{conflict_mask:016x} pen=0x{penumbra_mask:016x}"
        ),
    )


def _parse_repair_from_arena(arena_bytes: bytes) -> ArenaRepairResult:
    conf_in = struct.unpack_from("<Q", arena_bytes, 128 + 40)[0]
    pinned, cand, retract, energy, iters, converged, _seed, _r0, _r1 = struct.unpack_from(
        "<QQQQIIQQQ", arena_bytes, 217 * 64
    )
    return ArenaRepairResult(
        conf_in=conf_in,
        cand=cand,
        retract=retract,
        pinned=pinned,
        energy=energy,
        iters=iters,
        converged=(converged == 1),
        raw_output=(
            f"[ffi repair] conf_in=0x{conf_in:016x} cand=0x{cand:016x} "
            f"retract=0x{retract:016x} energy={energy} conv={converged}"
        ),
    )


def _encode_label32(label: str) -> bytes:
    raw = label.encode("ascii", errors="strict")
    if len(raw) == 0 or len(raw) > 31 or any(b in (0x20, 0x09, 0x0A, 0x0D) for b in raw):
        raise ValueError(f"Node label must be 1..31 non-whitespace ASCII bytes: {label!r}")
    return raw.ljust(SOUND_LABEL_BYTES, b"\x00")


def encode_arena(
    nodes: Sequence[ArenaNodeSpec],
    *,
    schema_hash: int = FALLBACK_SCHEMA_HASH,
    frame_flags: int = 0,
    power_mask: int = 0,
    immunity_mask: int = 0,
    duty_pos_mask: int = 0,
    duty_neg_mask: int = 0,
    voidable_mask: int = 0,
    rescission_power_mask: int = 0,
    flow_aux_words: Optional[Sequence[int]] = None,
) -> bytes:
    """Serialize and FNV-1a Merkle-seal a 16,384B `"SND2"` SoundArena."""
    num_nodes = len(nodes)
    if num_nodes > SOUND_MAX_NODES:
        raise ValueError(f"SoundArena allows at most {SOUND_MAX_NODES} nodes, got {num_nodes}")

    node_mask = (1 << num_nodes) - 1 if num_nodes < 64 else 0xFFFFFFFFFFFFFFFF
    buf = bytearray(SOUND_ARENA_BYTES)

    revoked_mask = 0
    penumbra_mask = 0

    for idx, node in enumerate(nodes):
        if node.sort not in SORT_MAP:
            raise ValueError(f"Invalid sort {node.sort!r} on node {node.label!r}")
        if node.evid not in EVID_MAP:
            raise ValueError(f"Invalid evid {node.evid!r} on node {node.label!r}")
        if node.aspect not in ASPECT_MAP:
            raise ValueError(f"Invalid aspect {node.aspect!r} on node {node.label!r}")
        if len(node.clauses) > SOUND_MAX_CLAUSES:
            raise ValueError(
                f"Node {node.label!r} has {len(node.clauses)} clauses (> {SOUND_MAX_CLAUSES})"
            )
        if (node.presup_mask & ~node_mask) != 0:
            raise ValueError(f"Node {node.label!r} presup_mask out of bounds")
        if (node.attack_mask & ~node_mask) != 0:
            raise ValueError(f"Node {node.label!r} attack_mask out of bounds")

        bit = 1 << idx
        if node.revoked:
            revoked_mask |= bit
        if node.penumbra:
            penumbra_mask |= bit

        flags = NODE_FLAG_CONSTITUTIVE if node.constitutive else 0
        label32 = _encode_label32(node.label)

        # SoundNodeMeta64 at cells[8 + idx] (offset 512 + idx*64)
        struct.pack_into(
            "<BBBBIQQII32s",
            buf,
            512 + idx * 64,
            SORT_MAP[node.sort],
            EVID_MAP[node.evid],
            ASPECT_MAP[node.aspect],
            len(node.clauses),
            flags,
            node.presup_mask & 0xFFFFFFFFFFFFFFFF,
            node.attack_mask & 0xFFFFFFFFFFFFFFFF,
            node.span_start,
            node.span_end,
            label32,
        )

        # SoundNodeClauses64 at cells[72 + idx] (offset 4608 + idx*64)
        padded_clauses = [0] * SOUND_MAX_CLAUSES
        for c_idx, cmask in enumerate(node.clauses):
            if (cmask & ~node_mask) != 0:
                raise ValueError(f"Node {node.label!r} clause[{c_idx}] out of bounds")
            padded_clauses[c_idx] = cmask & 0xFFFFFFFFFFFFFFFF
        struct.pack_into("<8Q", buf, 4608 + idx * 64, *padded_clauses)

    # SoundNormative64 at cell 1 (offset 64)
    struct.pack_into(
        "<8Q",
        buf,
        64,
        revoked_mask & node_mask,
        penumbra_mask & node_mask,
        duty_pos_mask & 0xFFFFFFFFFFFFFFFF,
        duty_neg_mask & 0xFFFFFFFFFFFFFFFF,
        power_mask & 0xFFFFFFFFFFFFFFFF,
        immunity_mask & 0xFFFFFFFFFFFFFFFF,
        voidable_mask & 0xFFFFFFFFFFFFFFFF,
        rescission_power_mask & 0xFFFFFFFFFFFFFFFF,
    )

    if flow_aux_words is not None:
        if len(flow_aux_words) > SOUND_FLOW_AUX_WORDS:
            raise ValueError(
                f"flow_aux allows at most {SOUND_FLOW_AUX_WORDS} uint64 words, got {len(flow_aux_words)}"
            )
        for w_idx, word in enumerate(flow_aux_words):
            struct.pack_into(
                "<Q",
                buf,
                SOUND_FLOW_AUX_OFFSET + w_idx * 8,
                int(word) & 0xFFFFFFFFFFFFFFFF,
            )

    # FNV-1a Merkle over cells 1..255 (bytes 64..16384)
    merkle_root = fnv1a_64(bytes(buf[64:SOUND_ARENA_BYTES]))

    # SoundImageHeader64 at cell 0 (offset 0)
    struct.pack_into(
        "<IIIIIIQQQQQ",
        buf,
        0,
        SOUND_MAGIC,
        SOUND_VERSION,
        num_nodes,
        frame_flags,
        0,  # phys_len
        0,  # phys_head
        0,  # reserved_hdr
        FNV1A_OFFSET,  # phys_hash
        schema_hash & 0xFFFFFFFFFFFFFFFF,
        merkle_root,
        0,  # reserved_tail
    )

    return bytes(buf)


def _use_ffi_backend(sound_bin: Path, backend: str) -> Optional[ctypes.CDLL]:
    if backend not in ("auto", "ffi", "cli"):
        raise ValueError(f"Unknown backend {backend!r} (expected 'auto', 'ffi', or 'cli')")
    if backend == "cli":
        return None
    lib = load_libsound(sound_bin)
    if backend == "ffi" and lib is None:
        raise FileNotFoundError(f"Requested 'ffi' backend, but libsound not beside {sound_bin}")
    return lib


def try_verify_arena_bytes(
    arena_bytes: bytes, sound_bin: Path, *, backend: str = "auto"
) -> ArenaVerifyOutcome:
    """Value-first HoTT verifier returning landing `SoundFiber` without throwing on fallbacks."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        return ArenaVerifyOutcome(
            fiber=SoundFiber.FALLBACK_BOUNDS,
            eval_result=None,
            forest_ctrl=None,
        )

    lib = _use_ffi_backend(sound_bin, backend)
    if lib is not None:
        ctrl_pre = read_forest_ctrl(arena_bytes)
        buf = _AlignedArenaBuffer(arena_bytes)
        rc_ver = int(lib.sound_ring0_arena_verify(buf.ptr))
        if rc_ver != 0:
            return ArenaVerifyOutcome(
                fiber=SoundFiber(rc_ver),
                eval_result=None,
                forest_ctrl=ctrl_pre,
            )
        rc_ev = int(lib.sound_ring0_eval(buf.ptr))
        cur = buf.read()
        ctrl_post = read_forest_ctrl(cur)
        if rc_ev != 0:
            return ArenaVerifyOutcome(
                fiber=SoundFiber(rc_ev),
                eval_result=None,
                forest_ctrl=ctrl_post,
            )
        try:
            eval_res = _parse_eval_from_arena(cur)
        except SoundVerifyError:
            return ArenaVerifyOutcome(
                fiber=SoundFiber.HOLD_INCOHERENT,
                eval_result=None,
                forest_ctrl=ctrl_post,
            )
        return ArenaVerifyOutcome(
            fiber=SoundFiber.COMMITTED,
            eval_result=eval_res,
            forest_ctrl=ctrl_post,
        )

    try:
        ev = verify_arena_bytes(arena_bytes, sound_bin, backend="cli")
        return ArenaVerifyOutcome(
            fiber=SoundFiber.COMMITTED,
            eval_result=ev,
            forest_ctrl=read_forest_ctrl(arena_bytes),
        )
    except SoundVerifyError as exc:
        m = _CLI_FIBER_RE.search(str(exc))
        fb = SoundFiber(int(m.group(1))) if m else SoundFiber.FALLBACK_FORMAT
        return ArenaVerifyOutcome(
            fiber=fb,
            eval_result=None,
            forest_ctrl=read_forest_ctrl(arena_bytes),
        )


def _verify_arena_buf_with_optional_key(
    lib: ctypes.CDLL, buf: _AlignedArenaBuffer, key: Optional[Tuple[int, int]]
) -> int:
    """Verify 16KB arena structure + Merkle, honoring keyed SipHash policy if `key` is supplied."""
    rc = int(lib.sound_ring0_arena_verify(buf.ptr))
    if key is None:
        return rc
    if rc not in (0, SoundFiber.FALLBACK_POLICY_TAMPER.value):
        return rc
    k0, k1 = key
    return int(
        lib.sound_ring0_policy_verify_keyed(
            buf.ptr, k0 & 0xFFFFFFFFFFFFFFFF, k1 & 0xFFFFFFFFFFFFFFFF
        )
    )


def verify_arena_bytes(
    arena_bytes: bytes,
    sound_bin: Path,
    *,
    backend: str = "auto",
    key: Optional[Tuple[int, int]] = None,
) -> ArenaEvalResult:
    """Verify & evaluate a 16,384B arena via in-memory `libsound` FFI or `build/sound` CLI."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B SoundArena, got {len(arena_bytes)}B")

    lib = _use_ffi_backend(sound_bin, backend)
    if lib is not None:
        buf = _AlignedArenaBuffer(arena_bytes)
        rc = _verify_arena_buf_with_optional_key(lib, buf, key)
        if rc != 0:
            raise SoundVerifyError(
                f"SoundArena verify failed (fiber={rc} {fiber_name(rc)})"
            )
        rc = int(lib.sound_ring0_eval(buf.ptr))
        if rc != 0:
            raise SoundVerifyError(f"sound_eval failed (fiber={rc} {fiber_name(rc)})")
        return _parse_eval_from_arena(buf.read())

    if not sound_bin.is_file():
        raise FileNotFoundError(f"SOUND CLI binary not found: {sound_bin}")

    with tempfile.TemporaryDirectory(prefix="sound_mod_") as tmp_dir:
        snd_path = Path(tmp_dir) / "arena.snd"
        snd_path.write_bytes(arena_bytes)
        proc = subprocess.run(
            [str(sound_bin), "verify", str(snd_path)],
            capture_output=True,
            text=True,
            check=False,
        )

    if proc.returncode != 0:
        raise SoundVerifyError(
            f"`sound verify` failed (rc={proc.returncode}): {proc.stderr.strip() or proc.stdout.strip()}"
        )

    lines = [ln for ln in proc.stdout.splitlines() if ln.strip()]
    if not lines:
        raise SoundVerifyError("`sound verify` produced empty stdout")

    m_sum = _SUMMARY_RE.match(lines[0])
    if not m_sum:
        raise SoundVerifyError(f"Unexpected `sound verify` header: {lines[0]!r}")

    num_nodes = int(m_sum.group(1))
    nodes: List[NodeEvalResult] = []
    for ln in lines[1:]:
        m_nd = _NODE_LINE_RE.match(ln)
        if m_nd:
            nodes.append(
                NodeEvalResult(
                    index=int(m_nd.group(1)),
                    label=m_nd.group(2),
                    status=m_nd.group(3),
                    reasons=int(m_nd.group(4), 16),
                )
            )

    if len(nodes) != num_nodes:
        raise SoundVerifyError(
            f"Parsed {len(nodes)} node lines from `sound verify`, expected {num_nodes}"
        )

    return ArenaEvalResult(
        num_nodes=num_nodes,
        i_star=int(m_sum.group(2), 16),
        p_star=int(m_sum.group(3), 16),
        established_mask=int(m_sum.group(4), 16),
        defeated_mask=int(m_sum.group(5), 16),
        conflict_mask=int(m_sum.group(6), 16),
        penumbra_mask=int(m_sum.group(7), 16),
        nodes=nodes,
        raw_output=proc.stdout,
    )


def repair_arena_bytes(
    arena_bytes: bytes,
    sound_bin: Path,
    seed: Optional[str] = None,
    *,
    backend: str = "auto",
) -> ArenaRepairResult:
    """Run SEARU repair via in-memory `libsound` FFI or `build/sound repair` CLI."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B SoundArena, got {len(arena_bytes)}B")

    lib = _use_ffi_backend(sound_bin, backend)
    if lib is not None:
        buf = _AlignedArenaBuffer(arena_bytes)
        rc = int(lib.sound_ring0_arena_verify(buf.ptr))
        if rc != 0:
            raise SoundVerifyError(
                f"SoundArena verify failed (fiber={rc} {fiber_name(rc)})"
            )
        rc = int(lib.sound_ring0_searu_repair(buf.ptr, _parse_seed_u64(seed), 2048))
        if rc != 0:
            raise SoundVerifyError(f"SEARU repair failed (fiber={rc} {fiber_name(rc)})")
        return _parse_repair_from_arena(buf.read())

    if not sound_bin.is_file():
        raise FileNotFoundError(f"SOUND CLI binary not found: {sound_bin}")

    with tempfile.TemporaryDirectory(prefix="sound_mod_") as tmp_dir:
        snd_path = Path(tmp_dir) / "arena.snd"
        snd_path.write_bytes(arena_bytes)
        cmd = [str(sound_bin), "repair", str(snd_path)]
        if seed is not None:
            cmd.append(seed)
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)

    stdout = proc.stdout.strip()
    m = _REPAIR_RE.match(stdout)
    if not m:
        raise SoundVerifyError(
            f"`sound repair` unexpected output (rc={proc.returncode}): "
            f"{proc.stderr.strip() or stdout}"
        )

    return ArenaRepairResult(
        conf_in=int(m.group(1), 16),
        cand=int(m.group(2), 16),
        retract=int(m.group(3), 16),
        pinned=int(m.group(4), 16),
        energy=int(m.group(5)),
        iters=int(m.group(6)),
        converged=(int(m.group(7)) == 1),
        raw_output=proc.stdout,
    )


def lift_fir_arena_bytes(
    arena_bytes: bytes,
    sound_bin: Path,
    *,
    prog_name: str = "npc_gate",
    repaired: bool = False,
    seed: Optional[str] = None,
    backend: str = "auto",
) -> ArenaFirResult:
    """Lift a 16,384B arena to a verified 2,144B FIR1 gate via `libsound` FFI or CLI."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B SoundArena, got {len(arena_bytes)}B")
    if not prog_name or "/" in prog_name or "\\" in prog_name:
        raise ValueError(f"Invalid FIR program basename: {prog_name!r}")

    lib = _use_ffi_backend(sound_bin, backend)
    if lib is not None:
        buf = _AlignedArenaBuffer(arena_bytes)
        rc = int(lib.sound_ring0_arena_verify(buf.ptr))
        if rc != 0:
            raise SoundVerifyError(
                f"SoundArena verify failed (fiber={rc} {fiber_name(rc)})"
            )
        if repaired:
            rc = int(lib.sound_ring0_searu_repair(buf.ptr, _parse_seed_u64(seed), 2048))
        else:
            rc = int(lib.sound_ring0_eval(buf.ptr))
        if rc != 0:
            raise SoundVerifyError(f"Kernel eval failed (fiber={rc} {fiber_name(rc)})")

        fir_out = ctypes.create_string_buffer(SOUND_FIR_BYTES)
        out_len = ctypes.c_size_t(0)
        eff_name = prog_name.encode("ascii", errors="strict")[:23]
        fir_flags = SOUND_FIR_FLAG_USE_REPAIRED if repaired else 0
        rc = int(
            lib.sound_ring0_lift_to_fir(
                buf.ptr,
                eff_name,
                fir_flags,
                ctypes.cast(fir_out, ctypes.c_void_p),
                SOUND_FIR_BYTES,
                ctypes.byref(out_len),
            )
        )
        if rc != 0:
            raise SoundVerifyError(
                f"sound_ring0_lift_to_fir failed (fiber={rc} {fiber_name(rc)})"
            )
        fir_bytes = bytes(fir_out[: out_len.value])
        if len(fir_bytes) != SOUND_FIR_BYTES or not fir_bytes.startswith(b"FIR1"):
            raise SoundVerifyError("Malformed FFI FIR1 output")
        cur_bytes = buf.read()
        if repaired:
            active_mask = _parse_repair_from_arena(cur_bytes).cand
        else:
            active_mask = _parse_eval_from_arena(cur_bytes).established_mask
        eff_str = eff_name.decode("ascii")
        return ArenaFirResult(
            fir_bytes=fir_bytes,
            prog_name=eff_str,
            active_mask=active_mask,
            mult_bits=_SOUND_FIR_MULT_BITS,
            mult_depth=_SOUND_FIR_MULT_DEPTH,
            raw_output=f"[ffi lift-fir] name={eff_str} active=0x{active_mask:016x} ({SOUND_FIR_BYTES}B)",
        )

    if not sound_bin.is_file():
        raise FileNotFoundError(f"SOUND CLI binary not found: {sound_bin}")

    with tempfile.TemporaryDirectory(prefix="sound_fir_") as tmp_dir:
        snd_path = Path(tmp_dir) / f"{prog_name}.snd"
        fir_path = Path(tmp_dir) / f"{prog_name}.fir"
        snd_path.write_bytes(arena_bytes)
        cmd = [str(sound_bin), "lift-fir", str(snd_path), str(fir_path)]
        if repaired:
            cmd.append("--repaired")
            if seed is not None:
                cmd.append(seed)
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)

        if proc.returncode != 0:
            raise SoundVerifyError(
                f"`sound lift-fir` failed (rc={proc.returncode}): "
                f"{proc.stderr.strip() or proc.stdout.strip()}"
            )
        stdout = proc.stdout.strip()
        m = _LIFT_FIR_RE.match(stdout)
        if not m:
            raise SoundVerifyError(f"`sound lift-fir` unexpected output: {stdout!r}")
        fir_bytes = fir_path.read_bytes()

    if len(fir_bytes) != SOUND_FIR_BYTES or not fir_bytes.startswith(b"FIR1"):
        raise SoundVerifyError(
            f"Malformed lifted FIR1 artifact: len={len(fir_bytes)} magic={fir_bytes[:4]!r}"
        )

    return ArenaFirResult(
        fir_bytes=fir_bytes,
        prog_name=m.group(2),
        active_mask=int(m.group(3), 16),
        mult_bits=int(m.group(4)),
        mult_depth=int(m.group(5)),
        raw_output=proc.stdout,
    )


def apply_potestative_arena_bytes(
    arena_bytes: bytes,
    sound_bin: Path,
    *,
    slot: int,
    grant_pos: bool = False,
    revoke_pos: bool = False,
    grant_neg: bool = False,
    revoke_neg: bool = False,
) -> Tuple[bytes, SoundFiber, NormativeStateSnapshot]:
    """Execute C17 `sound_ring0_apply_potestative` + re-seal via in-memory `libsound` FFI."""
    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    buf = _AlignedArenaBuffer(arena_bytes)
    rc_ver = int(lib.sound_ring0_arena_verify(buf.ptr))
    if rc_ver != 0:
        return arena_bytes, SoundFiber(rc_ver), read_normative_state(arena_bytes)
    rc = SoundFiber(
        int(
            lib.sound_ring0_apply_potestative(
                buf.ptr,
                slot,
                bool(grant_pos),
                bool(revoke_pos),
                bool(grant_neg),
                bool(revoke_neg),
            )
        )
    )
    lib.sound_ring0_arena_seal(buf.ptr)
    sealed = buf.read()
    return sealed, rc, read_normative_state(sealed)


def apply_declarative_arena_bytes(
    arena_bytes: bytes,
    sound_bin: Path,
    *,
    intent_node: int,
    duty_slot: int,
    in_constitutive_context: bool,
    internal_will_matches: bool,
    utterance_hash: int,
) -> Tuple[bytes, SoundFiber, NormativeStateSnapshot]:
    """Execute C17 `sound_ring0_apply_declarative` + re-seal via in-memory `libsound` FFI."""
    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    buf = _AlignedArenaBuffer(arena_bytes)
    rc_ver = int(lib.sound_ring0_arena_verify(buf.ptr))
    if rc_ver != 0:
        return arena_bytes, SoundFiber(rc_ver), read_normative_state(arena_bytes)
    rc = SoundFiber(
        int(
            lib.sound_ring0_apply_declarative(
                buf.ptr,
                intent_node,
                duty_slot,
                bool(in_constitutive_context),
                bool(internal_will_matches),
                utterance_hash & 0xFFFFFFFFFFFFFFFF,
            )
        )
    )
    lib.sound_ring0_arena_seal(buf.ptr)
    sealed = buf.read()
    return sealed, rc, read_normative_state(sealed)


def exercise_rescission_arena_bytes(
    arena_bytes: bytes,
    sound_bin: Path,
    *,
    target_intent_node: int,
    duty_slot: int,
    rescission_hash: int,
) -> Tuple[bytes, SoundFiber, Optional[ArenaEvalResult], NormativeStateSnapshot]:
    """Execute C17 `sound_ring0_exercise_rescission` + re-seal via in-memory `libsound` FFI."""
    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    buf = _AlignedArenaBuffer(arena_bytes)
    rc_ver = int(lib.sound_ring0_arena_verify(buf.ptr))
    if rc_ver != 0:
        return arena_bytes, SoundFiber(rc_ver), None, read_normative_state(arena_bytes)
    rc = SoundFiber(
        int(
            lib.sound_ring0_exercise_rescission(
                buf.ptr,
                target_intent_node,
                duty_slot,
                rescission_hash & 0xFFFFFFFFFFFFFFFF,
            )
        )
    )
    eval_res: Optional[ArenaEvalResult]
    if rc == SoundFiber.COMMITTED:
        eval_res = _parse_eval_from_arena(buf.read())
    else:
        rc_ev = int(lib.sound_ring0_eval(buf.ptr))
        eval_res = _parse_eval_from_arena(buf.read()) if rc_ev == 0 else None
    lib.sound_ring0_arena_seal(buf.ptr)
    sealed = buf.read()
    return sealed, rc, eval_res, read_normative_state(sealed)


def policy_seal_bytes(
    arena_bytes: bytes,
    sound_bin: Path,
    *,
    policy_mask: int,
    bind_mask: int,
    ctrl_flags: int = 0,
) -> Tuple[bytes, SoundFiber, ForestCtrlSnapshot]:
    """Seal Zero-Trust Prepared-Statement policy/bind planes via `sound_ring0_policy_seal`."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B SoundArena, got {len(arena_bytes)}B")
    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    buf = _AlignedArenaBuffer(arena_bytes)
    rc_ver = int(lib.sound_ring0_arena_verify(buf.ptr))
    if rc_ver != 0:
        return arena_bytes, SoundFiber(rc_ver), read_forest_ctrl(arena_bytes)
    rc = SoundFiber(
        int(
            lib.sound_ring0_policy_seal(
                buf.ptr,
                policy_mask & 0xFFFFFFFFFFFFFFFF,
                bind_mask & 0xFFFFFFFFFFFFFFFF,
                ctrl_flags & 0xFFFFFFFF,
            )
        )
    )
    lib.sound_ring0_arena_seal(buf.ptr)
    sealed = buf.read()
    return sealed, rc, read_forest_ctrl(sealed)


def policy_verify_bytes(arena_bytes: bytes, sound_bin: Path) -> SoundFiber:
    """Verify Prepared-Statement policy digest directly via `sound_ring0_policy_verify`."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        return SoundFiber.FALLBACK_BOUNDS
    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    buf = _AlignedArenaBuffer(arena_bytes)
    return SoundFiber(int(lib.sound_ring0_policy_verify(buf.ptr)))


def admit_candidate_bytes(
    arena_bytes: bytes,
    sound_bin: Path,
    *,
    target_node: int,
    force: str = "assertion",
    sort: str = "factual",
    evid: str = "sensory",
    aspect: str = "state",
    span_start: int = 0,
    span_end: int = 16,
    utterance_bytes: int = 32,
    node_flags: int = 0,
    presup_mask: int = 0,
    premise_mask: int = 0,
    attack_mask: int = 0,
    payload_hash: int = 0,
) -> Tuple[bytes, SoundFiber, int, ForestCtrlSnapshot]:
    """Write `SoundCandidate64` + Layer-0 utterance header and invoke `sound_ring0_admit_forest`."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B SoundArena, got {len(arena_bytes)}B")
    if force not in FORCE_MAP:
        raise ValueError(f"Invalid force: {force!r}")
    if sort not in SORT_MAP:
        raise ValueError(f"Invalid sort: {sort!r}")
    if evid not in EVID_MAP:
        raise ValueError(f"Invalid evid: {evid!r}")
    if aspect not in ASPECT_MAP:
        raise ValueError(f"Invalid aspect: {aspect!r}")

    raw = bytearray(arena_bytes)
    struct.pack_into(
        "<IBBBBIIIIQQQQQ",
        raw,
        200 * 64,
        target_node & 0xFFFFFFFF,
        FORCE_MAP[force],
        SORT_MAP[sort],
        EVID_MAP[evid],
        ASPECT_MAP[aspect],
        span_start & 0xFFFFFFFF,
        span_end & 0xFFFFFFFF,
        node_flags & 0xFFFFFFFF,
        0,
        presup_mask & 0xFFFFFFFFFFFFFFFF,
        premise_mask & 0xFFFFFFFFFFFFFFFF,
        attack_mask & 0xFFFFFFFFFFFFFFFF,
        payload_hash & 0xFFFFFFFFFFFFFFFF,
        0,
    )
    struct.pack_into(
        "<4I",
        raw,
        216 * 64,
        utterance_bytes & 0xFFFFFFFF,
        0,
        0,
        1,
    )

    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    buf = _AlignedArenaBuffer(bytes(raw))
    admitted = ctypes.c_uint32(0xFFFFFFFF)
    verdict = SoundFiber(int(lib.sound_ring0_admit_forest(buf.ptr, ctypes.byref(admitted))))
    lib.sound_ring0_arena_seal(buf.ptr)
    sealed = buf.read()
    return sealed, verdict, int(admitted.value), read_forest_ctrl(sealed)


def _c_bytes_arg(data: Optional[bytes]) -> Tuple[Optional[ctypes.Array[ctypes.c_char]], ctypes.c_void_p, int]:
    """Create a ctypes buffer for arbitrary binary bytes (safe for empty bytes and NUL bytes)."""
    if data is None:
        return None, ctypes.c_void_p(0), 0
    if len(data) == 0:
        arr = ctypes.create_string_buffer(1)
        return arr, ctypes.cast(arr, ctypes.c_void_p), 0
    arr = ctypes.create_string_buffer(data, len(data))
    return arr, ctypes.cast(arr, ctypes.c_void_p), len(data)


def _pack_candidate(
    raw: bytearray,
    slot_idx: int,
    cand: CandidateSpec,
    utterance: Optional[bytes] = None,
) -> None:
    if not (0 <= slot_idx < SOUND_MAX_CANDIDATES):
        raise ValueError(f"Candidate slot index out of range 0..15: {slot_idx}")
    if cand.force not in FORCE_MAP:
        raise ValueError(f"Invalid force: {cand.force!r}")
    if cand.sort not in SORT_MAP:
        raise ValueError(f"Invalid sort: {cand.sort!r}")
    if cand.evid not in EVID_MAP:
        raise ValueError(f"Invalid evid: {cand.evid!r}")
    if cand.aspect not in ASPECT_MAP:
        raise ValueError(f"Invalid aspect: {cand.aspect!r}")

    if cand.payload_hash is not None:
        phash = cand.payload_hash & 0xFFFFFFFFFFFFFFFF
    elif utterance is not None and 0 <= cand.span_start < cand.span_end <= len(utterance):
        phash = fnv1a_64(utterance[cand.span_start : cand.span_end])
    else:
        phash = 0

    struct.pack_into(
        "<IBBBBIIIIQQQQQ",
        raw,
        (200 + slot_idx) * 64,
        cand.target_node & 0xFFFFFFFF,
        FORCE_MAP[cand.force],
        SORT_MAP[cand.sort],
        EVID_MAP[cand.evid],
        ASPECT_MAP[cand.aspect],
        cand.span_start & 0xFFFFFFFF,
        cand.span_end & 0xFFFFFFFF,
        cand.node_flags & 0xFFFFFFFF,
        0,
        cand.presup_mask & 0xFFFFFFFFFFFFFFFF,
        cand.premise_mask & 0xFFFFFFFFFFFFFFFF,
        cand.attack_mask & 0xFFFFFFFFFFFFFFFF,
        phash,
        0,
    )


def siphash24_bytes(k0: int, k1: int, data: bytes, sound_bin: Path) -> int:
    """Compute Ring-0 Pure-GPR SipHash-2-4 tag via `sound_ring0_siphash24`."""
    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    _keepalive, ptr, n = _c_bytes_arg(data)
    return int(
        lib.sound_ring0_siphash24(
            k0 & 0xFFFFFFFFFFFFFFFF,
            k1 & 0xFFFFFFFFFFFFFFFF,
            ptr,
            n,
        )
    )


def policy_seal_keyed_bytes(
    arena_bytes: bytes,
    sound_bin: Path,
    *,
    policy_mask: int,
    bind_mask: int,
    k0: int,
    k1: int,
    ctrl_flags: int = 0,
) -> Tuple[bytes, SoundFiber, ForestCtrlSnapshot]:
    """Seal Zero-Trust Prepared-Statement policy/bind planes with keyed SipHash-2-4 HMAC."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B SoundArena, got {len(arena_bytes)}B")
    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    buf = _AlignedArenaBuffer(arena_bytes)
    rc_ver = int(lib.sound_ring0_arena_verify(buf.ptr))
    if rc_ver not in (0, SoundFiber.FALLBACK_POLICY_TAMPER.value):
        return arena_bytes, SoundFiber(rc_ver), read_forest_ctrl(arena_bytes)
    rc = SoundFiber(
        int(
            lib.sound_ring0_policy_seal_keyed(
                buf.ptr,
                policy_mask & 0xFFFFFFFFFFFFFFFF,
                bind_mask & 0xFFFFFFFFFFFFFFFF,
                ctrl_flags & 0xFFFFFFFF,
                k0 & 0xFFFFFFFFFFFFFFFF,
                k1 & 0xFFFFFFFFFFFFFFFF,
            )
        )
    )
    lib.sound_ring0_arena_seal(buf.ptr)
    sealed = buf.read()
    return sealed, rc, read_forest_ctrl(sealed)


def policy_verify_keyed_bytes(
    arena_bytes: bytes, sound_bin: Path, *, k0: int, k1: int
) -> SoundFiber:
    """Verify keyed SipHash-2-4 Prepared-Statement policy MAC via `sound_ring0_policy_verify_keyed`."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        return SoundFiber.FALLBACK_BOUNDS
    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    buf = _AlignedArenaBuffer(arena_bytes)
    return SoundFiber(
        int(
            lib.sound_ring0_policy_verify_keyed(
                buf.ptr,
                k0 & 0xFFFFFFFFFFFFFFFF,
                k1 & 0xFFFFFFFFFFFFFFFF,
            )
        )
    )


def admit_candidate_with_utterance_bytes(
    arena_bytes: bytes,
    sound_bin: Path,
    *,
    candidate: CandidateSpec,
    utterance: bytes,
) -> Tuple[bytes, SoundFiber, int, ForestCtrlSnapshot]:
    """Admit a single candidate with Ring-0 utterance slice FNV-1a span-hash verification."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B SoundArena, got {len(arena_bytes)}B")
    raw = bytearray(arena_bytes)
    _pack_candidate(raw, 0, candidate, utterance)
    struct.pack_into("<4I", raw, 216 * 64, len(utterance) & 0xFFFFFFFF, 0, 0, 1)

    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    buf = _AlignedArenaBuffer(bytes(raw))
    _keepalive, utt_ptr, utt_len = _c_bytes_arg(utterance)
    admitted = ctypes.c_uint32(0xFFFFFFFF)
    verdict = SoundFiber(
        int(
            lib.sound_ring0_admit_forest_with_utterance(
                buf.ptr, utt_ptr, utt_len, ctypes.byref(admitted)
            )
        )
    )
    lib.sound_ring0_arena_seal(buf.ptr)
    sealed = buf.read()
    return sealed, verdict, int(admitted.value), read_forest_ctrl(sealed)


def admit_batch_bytes(
    arena_bytes: bytes,
    sound_bin: Path,
    *,
    candidates: Sequence[CandidateSpec],
    utterance: Optional[bytes] = None,
    key: Optional[Tuple[int, int]] = None,
) -> Tuple[bytes, SoundFiber, int, ForestCtrlSnapshot]:
    """Run 2-phase atomic multi-slot admission across `1..16` candidates via Ring-0 FFI."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B SoundArena, got {len(arena_bytes)}B")
    if len(candidates) > SOUND_MAX_CANDIDATES:
        raise ValueError(
            f"Batch allows at most {SOUND_MAX_CANDIDATES} candidates, got {len(candidates)}"
        )

    raw = bytearray(arena_bytes)
    raw[200 * 64 : 216 * 64] = b"\x00" * (SOUND_MAX_CANDIDATES * 64)
    for idx, cand in enumerate(candidates):
        _pack_candidate(raw, idx, cand, utterance)

    utt_bytes = (
        len(utterance)
        if utterance is not None
        else max((cand.span_end for cand in candidates), default=0)
    )
    struct.pack_into(
        "<4I",
        raw,
        216 * 64,
        utt_bytes & 0xFFFFFFFF,
        0,
        0,
        len(candidates) & 0xFFFFFFFF,
    )

    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    buf = _AlignedArenaBuffer(bytes(raw))
    _keepalive, utt_ptr, utt_len = _c_bytes_arg(utterance)
    out_count = ctypes.c_uint32(0)
    if key is not None:
        k0, k1 = key
        verdict = SoundFiber(
            int(
                lib.sound_ring0_admit_forest_batch_keyed(
                    buf.ptr,
                    utt_ptr,
                    utt_len,
                    k0 & 0xFFFFFFFFFFFFFFFF,
                    k1 & 0xFFFFFFFFFFFFFFFF,
                    ctypes.byref(out_count),
                )
            )
        )
    else:
        verdict = SoundFiber(
            int(
                lib.sound_ring0_admit_forest_batch(
                    buf.ptr, utt_ptr, utt_len, ctypes.byref(out_count)
                )
            )
        )
    lib.sound_ring0_arena_seal(buf.ptr)
    sealed = buf.read()
    return sealed, verdict, int(out_count.value), read_forest_ctrl(sealed)


def repair_weighted_arena_bytes(
    arena_bytes: bytes,
    sound_bin: Path,
    *,
    priority_weights: Sequence[int],
    seed: Optional[str] = None,
    key: Optional[Tuple[int, int]] = None,
) -> Tuple[bytes, SoundFiber, ArenaRepairResult]:
    """Run Lex Specialis priority-weighted SEARU conflict repair via Ring-0 FFI."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B SoundArena, got {len(arena_bytes)}B")
    if len(priority_weights) > SOUND_MAX_NODES:
        raise ValueError(
            f"Expected at most {SOUND_MAX_NODES} weights, got {len(priority_weights)}"
        )

    padded = [0] * SOUND_MAX_NODES
    for i, w in enumerate(priority_weights):
        wi = int(w)
        if not (0 <= wi <= 255):
            raise ValueError(f"Priority weight[{i}] out of uint8 range 0..255: {wi}")
        padded[i] = wi
    w_arr = (ctypes.c_uint8 * SOUND_MAX_NODES)(*padded)

    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    buf = _AlignedArenaBuffer(arena_bytes)
    rc_ver = _verify_arena_buf_with_optional_key(lib, buf, key)
    if rc_ver != 0:
        raise SoundVerifyError(
            f"SoundArena verify failed (fiber={rc_ver} {fiber_name(rc_ver)})"
        )
    rc = SoundFiber(
        int(
            lib.sound_ring0_searu_repair_weighted(
                buf.ptr,
                _parse_seed_u64(seed),
                2048,
                ctypes.cast(w_arr, ctypes.c_void_p),
            )
        )
    )
    lib.sound_ring0_arena_seal(buf.ptr)
    sealed = buf.read()
    return sealed, rc, _parse_repair_from_arena(sealed)


def chart_glue_import_bytes(
    dst_arena_bytes: bytes,
    src_arena_bytes: bytes,
    sound_bin: Path,
    *,
    src_export_mask: int,
    slot_map: Dict[int, int],
    key: Optional[Tuple[int, int]] = None,
) -> Tuple[bytes, SoundFiber, ChartGlueSnapshot]:
    """Glue established source chart ports into destination factual bind slots via Ring-0 FFI."""
    if len(dst_arena_bytes) != SOUND_ARENA_BYTES or len(src_arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B SoundArena inputs")
    c_map = [0] * SOUND_MAX_NODES
    for s_idx, d_idx in slot_map.items():
        if not (0 <= s_idx < SOUND_MAX_NODES and 0 <= d_idx < SOUND_MAX_NODES):
            raise ValueError(f"Slot map indices must be in 0..63, got {s_idx} -> {d_idx}")
        c_map[s_idx] = d_idx
    map_arr = (ctypes.c_uint8 * SOUND_MAX_NODES)(*c_map)

    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    dst_buf = _AlignedArenaBuffer(dst_arena_bytes)
    src_buf = _AlignedArenaBuffer(src_arena_bytes)
    if key is not None:
        k0, k1 = key
        rc = SoundFiber(
            int(
                lib.sound_ring0_chart_glue_import_keyed(
                    dst_buf.ptr,
                    src_buf.ptr,
                    src_export_mask & 0xFFFFFFFFFFFFFFFF,
                    ctypes.cast(map_arr, ctypes.c_void_p),
                    k0 & 0xFFFFFFFFFFFFFFFF,
                    k1 & 0xFFFFFFFFFFFFFFFF,
                )
            )
        )
    else:
        rc = SoundFiber(
            int(
                lib.sound_ring0_chart_glue_import(
                    dst_buf.ptr,
                    src_buf.ptr,
                    src_export_mask & 0xFFFFFFFFFFFFFFFF,
                    ctypes.cast(map_arr, ctypes.c_void_p),
                )
            )
        )
    lib.sound_ring0_arena_seal(dst_buf.ptr)
    sealed = dst_buf.read()
    return sealed, rc, read_chart_glue(sealed)


def chart_glue_verify_bytes(
    dst_arena_bytes: bytes, src_arena_bytes: bytes, sound_bin: Path
) -> SoundFiber:
    """Verify destination chart glue certificate against source chart via Ring-0 FFI."""
    if len(dst_arena_bytes) != SOUND_ARENA_BYTES or len(src_arena_bytes) != SOUND_ARENA_BYTES:
        return SoundFiber.FALLBACK_BOUNDS
    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    dst_buf = _AlignedArenaBuffer(dst_arena_bytes)
    src_buf = _AlignedArenaBuffer(src_arena_bytes)
    return SoundFiber(int(lib.sound_ring0_chart_glue_verify(dst_buf.ptr, src_buf.ptr)))


def lift_policy_eval_fir_bytes(
    arena_bytes: bytes,
    sound_bin: Path,
    *,
    prog_name: str = "sound_policy_eval",
    unroll_alt_steps: int = 0,
    key: Optional[Tuple[int, int]] = None,
) -> bytes:
    """Compile a sealed SoundArena policy directly into a FLOW_IR linear `.fir` circuit."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B SoundArena, got {len(arena_bytes)}B")
    if not prog_name or "/" in prog_name or "\\" in prog_name:
        raise ValueError(f"Invalid FIR program basename: {prog_name!r}")

    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    buf = _AlignedArenaBuffer(arena_bytes)
    rc_ver = _verify_arena_buf_with_optional_key(lib, buf, key)
    if rc_ver != 0:
        raise SoundVerifyError(
            f"SoundArena verify failed (fiber={rc_ver} {fiber_name(rc_ver)})"
        )

    out_cap = 65536
    fir_out = ctypes.create_string_buffer(out_cap)
    out_len = ctypes.c_size_t(0)
    eff_name = prog_name.encode("ascii", errors="strict")[:23]
    rc = int(
        lib.sound_ring0_lift_policy_eval_to_fir(
            buf.ptr,
            eff_name,
            unroll_alt_steps & 0xFFFFFFFF,
            ctypes.cast(fir_out, ctypes.c_void_p),
            out_cap,
            ctypes.byref(out_len),
        )
    )
    if rc != 0:
        raise SoundVerifyError(
            f"sound_ring0_lift_policy_eval_to_fir failed (fiber={rc} {fiber_name(rc)})"
        )
    fir_bytes = bytes(fir_out[: out_len.value])
    if len(fir_bytes) < 64 or not fir_bytes.startswith(b"FIR1"):
        raise SoundVerifyError("Malformed policy-eval FIR1 output")
    return fir_bytes
