"""Pure-stdlib encoder and `build/sound` CLI bridge for 16,384B SoundArena images."""

from __future__ import annotations

import re
import struct
import subprocess
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence

SOUND_MAGIC = 0x32444E53  # "SND2"
SOUND_VERSION = 2
SOUND_ARENA_CELLS = 256
SOUND_ARENA_BYTES = 16384
SOUND_MAX_NODES = 64
SOUND_MAX_CLAUSES = 8
SOUND_LABEL_BYTES = 32

FNV1A_OFFSET = 0xCBF29CE484222325
FNV1A_PRIME = 0x00000100000001B3
FALLBACK_SCHEMA_HASH = 0x9A13D2D8CB8DE1E4

NODE_FLAG_CONSTITUTIVE = 1

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


def verify_arena_bytes(arena_bytes: bytes, sound_bin: Path) -> ArenaEvalResult:
    """Run `build/sound verify` on an in-memory 16,384B arena and parse the verdict."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B SoundArena, got {len(arena_bytes)}B")
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
    arena_bytes: bytes, sound_bin: Path, seed: Optional[str] = None
) -> ArenaRepairResult:
    """Run `build/sound repair` on an in-memory 16,384B arena and parse SEARU repair metrics."""
    if len(arena_bytes) != SOUND_ARENA_BYTES:
        raise ValueError(f"Expected {SOUND_ARENA_BYTES}B SoundArena, got {len(arena_bytes)}B")
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
