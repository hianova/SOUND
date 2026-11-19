"""Pure-stdlib encoder, in-memory `libsound` ctypes FFI, and `build/sound` CLI bridge."""

from __future__ import annotations

import ctypes
import re
import struct
import subprocess
import tempfile
from dataclasses import dataclass, field
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
FALLBACK_SCHEMA_HASH = 0x9A13D2D8CB8DE1E4

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

SOUND_ERR_MAP: Dict[int, str] = {
    0: "SOUND_OK",
    1: "SOUND_ERR_NULL",
    2: "SOUND_ERR_BOUNDS",
    3: "SOUND_ERR_FORMAT",
    4: "SOUND_ERR_MAC",
    5: "SOUND_ERR_IMMUNITY_LOCKED",
    6: "SOUND_ERR_NO_POWER",
    7: "SOUND_ERR_MISSING_CONTEXT",
    8: "SOUND_ERR_INCOHERENT",
}

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


def fnv1a_64(data: bytes, seed: int = FNV1A_OFFSET) -> int:
    """Compute exact 64-bit FNV-1a matching `sound_fnv1a_bytes` in `sound.h`."""
    h = seed & 0xFFFFFFFFFFFFFFFF
    for b in data:
        h ^= b
        h = (h * FNV1A_PRIME) & 0xFFFFFFFFFFFFFFFF
    return h


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


def verify_arena_bytes(
    arena_bytes: bytes, sound_bin: Path, *, backend: str = "auto"
) -> ArenaEvalResult:
    """Verify & evaluate a 16,384B arena via in-memory `libsound` FFI or `build/sound` CLI."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B SoundArena, got {len(arena_bytes)}B")

    lib = _use_ffi_backend(sound_bin, backend)
    if lib is not None:
        buf = _AlignedArenaBuffer(arena_bytes)
        rc = lib.sound_ring0_arena_verify(buf.ptr)
        if rc != 0:
            raise SoundVerifyError(
                f"SoundArena verify failed (err={rc} {SOUND_ERR_MAP.get(rc, '')})"
            )
        rc = lib.sound_ring0_eval(buf.ptr)
        if rc != 0:
            raise SoundVerifyError(f"sound_eval failed (err={rc} {SOUND_ERR_MAP.get(rc, '')})")
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
        rc = lib.sound_ring0_arena_verify(buf.ptr)
        if rc != 0:
            raise SoundVerifyError(
                f"SoundArena verify failed (err={rc} {SOUND_ERR_MAP.get(rc, '')})"
            )
        rc = lib.sound_ring0_searu_repair(buf.ptr, _parse_seed_u64(seed), 2048)
        if rc != 0:
            raise SoundVerifyError(f"SEARU repair failed (err={rc} {SOUND_ERR_MAP.get(rc, '')})")
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
        rc = lib.sound_ring0_arena_verify(buf.ptr)
        if rc != 0:
            raise SoundVerifyError(
                f"SoundArena verify failed (err={rc} {SOUND_ERR_MAP.get(rc, '')})"
            )
        if repaired:
            rc = lib.sound_ring0_searu_repair(buf.ptr, _parse_seed_u64(seed), 2048)
        else:
            rc = lib.sound_ring0_eval(buf.ptr)
        if rc != 0:
            raise SoundVerifyError(f"Kernel eval failed (err={rc} {SOUND_ERR_MAP.get(rc, '')})")

        fir_out = ctypes.create_string_buffer(SOUND_FIR_BYTES)
        out_len = ctypes.c_size_t(0)
        eff_name = prog_name.encode("ascii", errors="strict")[:23]
        fir_flags = SOUND_FIR_FLAG_USE_REPAIRED if repaired else 0
        rc = lib.sound_ring0_lift_to_fir(
            buf.ptr,
            eff_name,
            fir_flags,
            ctypes.cast(fir_out, ctypes.c_void_p),
            SOUND_FIR_BYTES,
            ctypes.byref(out_len),
        )
        if rc != 0:
            raise SoundVerifyError(
                f"sound_ring0_lift_to_fir failed (err={rc} {SOUND_ERR_MAP.get(rc, '')})"
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
) -> Tuple[bytes, int, NormativeStateSnapshot]:
    """Execute C17 `sound_ring0_apply_potestative` + re-seal via in-memory `libsound` FFI."""
    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    buf = _AlignedArenaBuffer(arena_bytes)
    rc_ver = lib.sound_ring0_arena_verify(buf.ptr)
    if rc_ver != 0:
        raise SoundVerifyError(
            f"SoundArena verify failed (err={rc_ver} {SOUND_ERR_MAP.get(rc_ver, '')})"
        )
    rc = int(
        lib.sound_ring0_apply_potestative(
            buf.ptr,
            slot,
            bool(grant_pos),
            bool(revoke_pos),
            bool(grant_neg),
            bool(revoke_neg),
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
) -> Tuple[bytes, int, NormativeStateSnapshot]:
    """Execute C17 `sound_ring0_apply_declarative` + re-seal via in-memory `libsound` FFI."""
    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    buf = _AlignedArenaBuffer(arena_bytes)
    rc_ver = lib.sound_ring0_arena_verify(buf.ptr)
    if rc_ver != 0:
        raise SoundVerifyError(
            f"SoundArena verify failed (err={rc_ver} {SOUND_ERR_MAP.get(rc_ver, '')})"
        )
    rc = int(
        lib.sound_ring0_apply_declarative(
            buf.ptr,
            intent_node,
            duty_slot,
            bool(in_constitutive_context),
            bool(internal_will_matches),
            utterance_hash & 0xFFFFFFFFFFFFFFFF,
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
) -> Tuple[bytes, int, ArenaEvalResult, NormativeStateSnapshot]:
    """Execute C17 `sound_ring0_exercise_rescission` + re-seal via in-memory `libsound` FFI."""
    lib = _use_ffi_backend(sound_bin, "ffi")
    assert lib is not None
    buf = _AlignedArenaBuffer(arena_bytes)
    rc_ver = lib.sound_ring0_arena_verify(buf.ptr)
    if rc_ver != 0:
        raise SoundVerifyError(
            f"SoundArena verify failed (err={rc_ver} {SOUND_ERR_MAP.get(rc_ver, '')})"
        )
    rc = int(
        lib.sound_ring0_exercise_rescission(
            buf.ptr,
            target_intent_node,
            duty_slot,
            rescission_hash & 0xFFFFFFFFFFFFFFFF,
        )
    )
    if rc == 0:
        eval_res = _parse_eval_from_arena(buf.read())
    else:
        rc_ev = lib.sound_ring0_eval(buf.ptr)
        if rc_ev != 0:
            raise SoundVerifyError(f"sound_eval failed (err={rc_ev})")
        eval_res = _parse_eval_from_arena(buf.read())
    lib.sound_ring0_arena_seal(buf.ptr)
    sealed = buf.read()
    return sealed, rc, eval_res, read_normative_state(sealed)
