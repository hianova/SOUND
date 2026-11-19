"""Deterministic semantic LaTeX -> Prepared-Statement SoundArena compiler."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

from ..common.arena_codec import (
    FALLBACK_SCHEMA_HASH,
    SOUND_FOREST_CTRL_NO_DIRECT_CLAIM,
    SOUND_MAX_CLAUSES,
    SOUND_MAX_NODES,
    ArenaNodeSpec,
    ForestCtrlSnapshot,
    NormativeStateSnapshot,
    SoundFiber,
    encode_arena,
    fiber_name,
    fnv1a_64,
    load_schema_hash,
    policy_seal_bytes,
    policy_verify_bytes,
    read_forest_ctrl,
    read_normative_state,
)
from ..common.mcq_engine import ALLOWED_ROLES, NODE_ID_RE, validate_topic_pack

_VALID_STATUSES = {
    "established",
    "defeated",
    "conflictUndecided",
    "penumbraOpen",
}
_VALID_FORCES = {"assertion", "query", "directive", "declarative", "irrealis"}
_VALID_EVIDS = {
    "direct",
    "sensory",
    "enacted_norm",
    "inferential",
    "reportative",
    "assumed",
}

# env_name -> (role, sort, default_evid, default_constitutive, is_bind_slot, default_kanlock, is_penumbra)
_NODE_ENV_DEFAULTS: Dict[str, Tuple[str, str, str, bool, bool, bool, bool]] = {
    # Abstract / general mathematics
    "soundaxiom": ("statute_root", "normative", "enacted_norm", False, False, True, False),
    "sounddef": ("rule", "institutional", "enacted_norm", True, False, False, False),
    "soundhyp": ("fact_slot", "factual", "assumed", False, True, False, False),
    "soundlemma": ("rule", "normative", "inferential", False, False, False, False),
    "soundtheorem": ("rule", "normative", "inferential", False, False, False, False),
    "soundcorollary": ("effect", "normative", "inferential", False, False, False, False),
    "soundcounterexample": ("rule", "normative", "inferential", True, False, False, False),
    "soundpenumbra": ("penumbra", "normative", "inferential", False, False, False, True),
    # Legal / statutory
    "soundstatute": ("statute_root", "normative", "enacted_norm", False, False, False, False),
    "soundarticle": ("statute_root", "normative", "enacted_norm", False, False, False, False),
    "soundfact": ("fact_slot", "factual", "assumed", False, True, False, False),
    "soundrule": ("rule", "normative", "inferential", False, False, False, False),
    "soundproviso": ("rule", "normative", "inferential", False, False, False, False),
    "soundeffect": ("effect", "normative", "inferential", False, False, False, False),
}

_BEGIN_ENV_RE = re.compile(r"\\begin\{([A-Za-z0-9_*]+)\}")
_CMD_CHECK_RE = re.compile(r"\\([A-Za-z]+)(?![A-Za-z])")
_KV_RE = re.compile(r"([A-Za-z0-9_]+)\s*=\s*([^,]+)")


class LatexCompileError(ValueError):
    """Raised when a semantic LaTeX file violates schema or SOUND invariants."""


@dataclass(frozen=True)
class LatexNodeSpec:
    """Parsed and resolved semantic LaTeX node with exact UTF-8 byte & line provenance."""

    index: int
    node_id: str
    env_name: str
    title: str
    tex_label: str
    role: str
    sort: str
    evid: str
    constitutive: bool
    is_bind_slot: bool
    kanlock: bool
    penumbra: bool
    revoked: bool
    clauses: List[List[str]]
    presup: List[str]
    attacked_by: List[str]
    aliases: List[str]
    span_start: int
    span_end: int
    line_start: int
    line_end: int
    payload_hash: int
    body_text: str


@dataclass(frozen=True)
class LatexQAProbe:
    """Embedded `\\begin{soundqa}` comprehension or adversarial probe in a `.tex` document."""

    qa_id: str
    kind: str
    title: str
    prompt: str
    bind_facts: List[str]
    target_node: Optional[str]
    expect_status: Optional[str]
    expect_reasons: List[str]
    expect_fiber: SoundFiber
    tamper_target: Optional[str]
    tamper_evid: Optional[str]
    force: str
    hint: str
    span_start: int
    span_end: int
    line_start: int
    line_end: int


@dataclass(frozen=True)
class CompiledLatexDoc:
    """Compiled & Ring-0 Policy-Sealed `SoundArena` artifact backed by semantic LaTeX provenance."""

    pack_id: str
    domain: str
    jurisdiction: str
    standard: str
    title: str
    source_path: Optional[Path]
    source_text: str
    source_bytes: bytes
    no_direct_claim: bool
    nodes: List[LatexNodeSpec]
    node_index: Dict[str, LatexNodeSpec]
    universal_counterexamples: Dict[str, List[str]]
    confusable_pairs: List[Tuple[str, str]]
    qa_probes: List[LatexQAProbe]
    policy_mask: int
    bind_mask: int
    immunity_mask: int
    schema_hash: int
    arena_bytes: bytes
    forest_ctrl: ForestCtrlSnapshot
    norm_state: NormativeStateSnapshot


def _build_line_and_byte_maps(text: str) -> Tuple[List[int], List[int]]:
    """Map every string char index 0..len(text) to its UTF-8 byte offset and 1-based line number."""
    n = len(text)
    byte_off = [0] * (n + 1)
    line_no = [1] * (n + 1)
    b_acc = 0
    ln = 1
    for i, ch in enumerate(text):
        byte_off[i] = b_acc
        line_no[i] = ln
        b_acc += len(ch.encode("utf-8"))
        if ch == "\n":
            ln += 1
    byte_off[n] = b_acc
    line_no[n] = ln
    return byte_off, line_no


def _mask_tex_comments(text: str) -> str:
    """Replace unescaped `%...` comments with equal-length spaces, preserving exact char indices."""
    chars = list(text)
    i = 0
    n = len(chars)
    while i < n:
        if chars[i] == "%":
            bs = 0
            j = i - 1
            while j >= 0 and chars[j] == "\\":
                bs += 1
                j -= 1
            if bs % 2 == 0:
                while i < n and chars[i] != "\n":
                    chars[i] = " "
                    i += 1
                continue
        i += 1
    return "".join(chars)


def _skip_hspace(text: str, pos: int) -> int:
    n = len(text)
    while pos < n and text[pos] in " \t\r\n":
        pos += 1
    return pos


def _read_balanced(text: str, pos: int, open_ch: str, close_ch: str) -> Tuple[str, int]:
    """Read a balanced `{...}` or `[...]` group starting at `text[pos] == open_ch`."""
    n = len(text)
    if pos >= n or text[pos] != open_ch:
        raise LatexCompileError(f"Expected {open_ch!r} at char index {pos}")
    depth = 0
    start = pos + 1
    i = pos
    while i < n:
        ch = text[i]
        if ch == "\\":
            i += 2
            continue
        if ch == open_ch:
            depth += 1
        elif ch == close_ch:
            depth -= 1
            if depth == 0:
                return text[start:i], i + 1
        i += 1
    raise LatexCompileError(f"Unclosed {open_ch!r}...{close_ch!r} starting at char index {pos}")


def _scan_command_calls(masked: str, raw: str, cmd_name: str, num_args: int) -> List[List[str]]:
    """Find every `\\cmd_name{arg1}...{argK}` with balanced braces, extracting from `raw`."""
    results: List[List[str]] = []
    pos = 0
    target_len = len(cmd_name)
    n = len(masked)
    while pos < n:
        idx = masked.find("\\" + cmd_name, pos)
        if idx < 0:
            break
        after = idx + 1 + target_len
        if after < n and masked[after].isalpha():
            pos = after
            continue
        cur = after
        args: List[str] = []
        ok = True
        for _ in range(num_args):
            cur = _skip_hspace(masked, cur)
            if cur >= n or masked[cur] != "{":
                ok = False
                break
            _, end_pos = _read_balanced(masked, cur, "{", "}")
            args.append(raw[cur + 1 : end_pos - 1].strip())
            cur = end_pos
        if ok:
            results.append(args)
            pos = cur
        else:
            pos = after
    return results


def _has_bare_command(masked: str, cmd_name: str) -> bool:
    for m in _CMD_CHECK_RE.finditer(masked):
        if m.group(1) == cmd_name:
            return True
    return False


def _parse_id_list(arg: str) -> List[str]:
    out: List[str] = []
    for part in arg.split(","):
        cleaned = part.strip().replace(r"\_", "_")
        if cleaned:
            out.append(cleaned)
    return out


def _parse_text_list(arg: str) -> List[str]:
    return [p.strip() for p in arg.split(",") if p.strip()]


def _parse_kv_options(opt_str: str) -> Dict[str, str]:
    opts: Dict[str, str] = {}
    for part in opt_str.split(","):
        part_s = part.strip()
        if not part_s:
            continue
        m = _KV_RE.fullmatch(part_s)
        if m:
            opts[m.group(1).strip()] = m.group(2).strip().replace(r"\_", "_")
        elif "id" not in opts:
            opts["id"] = part_s.replace(r"\_", "_")
    return opts


def _unescape_title(title: str) -> str:
    return title.replace(r"\_", "_").strip()


def _parse_fiber_spec(spec: str) -> SoundFiber:
    norm = spec.strip().upper()
    if norm.startswith("SOUND_FIBER_"):
        norm = norm[len("SOUND_FIBER_") :]
    if norm in SoundFiber.__members__:
        return SoundFiber[norm]
    try:
        return SoundFiber(int(spec.strip(), 0))
    except Exception as exc:
        raise LatexCompileError(f"Unknown SoundFiber specifier: {spec!r}") from exc


def compile_latex_text(
    source_text: str,
    sound_bin: Path,
    *,
    source_path: Optional[Path] = None,
    schema_hash: Optional[int] = None,
) -> CompiledLatexDoc:
    """Parse semantic LaTeX text, check graph/slot invariants, and seal a Ring-0 `SoundArena`."""
    source_bytes = source_text.encode("utf-8")
    byte_off, line_no = _build_line_and_byte_maps(source_text)
    masked = _mask_tex_comments(source_text)

    doc_calls = _scan_command_calls(masked, source_text, "sounddoc", 4)
    if len(doc_calls) != 1:
        raise LatexCompileError(
            f"Expected exactly 1 \\sounddoc{{pack_id}}{{domain}}{{jurisdiction}}{{standard}}, found {len(doc_calls)}"
        )
    pack_id_raw, domain_raw, jur_raw, standard_raw = doc_calls[0]
    pack_id = pack_id_raw.replace(r"\_", "_").strip()
    if not NODE_ID_RE.match(pack_id):
        raise LatexCompileError(f"Invalid pack_id {pack_id!r} (must match ^[A-Za-z0-9_]{{1,24}}$)")
    domain = domain_raw.strip() or "general"
    jurisdiction = jur_raw.strip() or "INTL"
    standard = _unescape_title(standard_raw)

    title_calls = _scan_command_calls(masked, source_text, "title", 1)
    doc_title = _unescape_title(title_calls[0][0]) if title_calls else standard
    no_direct_claim = _has_bare_command(masked, "soundnodirectclaim")

    raw_nodes: List[Dict[str, Any]] = []
    raw_qas: List[LatexQAProbe] = []

    pos = 0
    n_len = len(masked)
    while pos < n_len:
        m = _BEGIN_ENV_RE.search(masked, pos)
        if not m:
            break
        env_name = m.group(1)
        if env_name not in _NODE_ENV_DEFAULTS and env_name != "soundqa":
            pos = m.end()
            continue

        env_start = m.start()
        cur = _skip_hspace(masked, m.end())
        opt_str = ""
        if cur < n_len and masked[cur] == "[":
            _, next_c = _read_balanced(masked, cur, "[", "]")
            opt_str = source_text[cur + 1 : next_c - 1]
            cur = _skip_hspace(masked, next_c)
        title_str = ""
        if cur < n_len and masked[cur] == "{":
            _, next_c = _read_balanced(masked, cur, "{", "}")
            title_str = _unescape_title(source_text[cur + 1 : next_c - 1])
            cur = next_c

        end_marker = f"\\end{{{env_name}}}"
        end_idx = masked.find(end_marker, cur)
        if end_idx < 0:
            raise LatexCompileError(
                f"Missing {end_marker!r} for environment started at line {line_no[env_start]}"
            )
        env_end = end_idx + len(end_marker)
        body_masked = masked[cur:end_idx]
        body_raw = source_text[cur:end_idx]
        opts = _parse_kv_options(opt_str)

        span_start = byte_off[env_start]
        span_end = byte_off[env_end]
        ln_start = line_no[env_start]
        ln_end = line_no[max(env_start, env_end - 1)]

        if env_name == "soundqa":
            qa_id = opts.get("id", f"qa_{len(raw_qas) + 1}")
            kind = opts.get("kind", "scenario")
            prompts = _scan_command_calls(body_masked, body_raw, "qaprompt", 1)
            binds = _scan_command_calls(body_masked, body_raw, "qabind", 1)
            targets = _scan_command_calls(body_masked, body_raw, "qatarget", 1)
            expects = _scan_command_calls(body_masked, body_raw, "qaexpect", 1)
            exp_reasons = _scan_command_calls(body_masked, body_raw, "qaexpectreason", 1)
            exp_fibers = _scan_command_calls(body_masked, body_raw, "qaexpectfiber", 1)
            tamp_targets = _scan_command_calls(body_masked, body_raw, "qatampertarget", 1)
            tamp_evids = _scan_command_calls(body_masked, body_raw, "qatamperevid", 1)
            forces = _scan_command_calls(body_masked, body_raw, "qaforce", 1)
            hints = _scan_command_calls(body_masked, body_raw, "qahint", 1)

            bind_facts: List[str] = []
            for b_arg in binds:
                bind_facts.extend(_parse_id_list(b_arg[0]))
            target_node = _parse_id_list(targets[-1][0])[0] if targets else None
            expect_status = expects[-1][0].strip() if expects else None
            if expect_status is not None and expect_status not in _VALID_STATUSES:
                raise LatexCompileError(
                    f"QA probe {qa_id!r} invalid \\qaexpect status: {expect_status!r}"
                )
            reason_list: List[str] = []
            for r_arg in exp_reasons:
                reason_list.extend([x.strip().upper() for x in r_arg[0].split(",") if x.strip()])
            expect_fiber = (
                _parse_fiber_spec(exp_fibers[-1][0]) if exp_fibers else SoundFiber.COMMITTED
            )
            tamper_target = (
                _parse_id_list(tamp_targets[-1][0])[0] if tamp_targets else None
            )
            tamper_evid = tamp_evids[-1][0].strip() if tamp_evids else None
            if tamper_evid is not None and tamper_evid not in _VALID_EVIDS:
                raise LatexCompileError(
                    f"QA probe {qa_id!r} invalid \\qatamperevid: {tamper_evid!r}"
                )
            force = forces[-1][0].strip() if forces else "assertion"
            if force not in _VALID_FORCES:
                raise LatexCompileError(f"QA probe {qa_id!r} invalid \\qaforce: {force!r}")

            raw_qas.append(
                LatexQAProbe(
                    qa_id=qa_id,
                    kind=kind,
                    title=title_str or qa_id,
                    prompt=prompts[-1][0] if prompts else title_str,
                    bind_facts=bind_facts,
                    target_node=target_node,
                    expect_status=expect_status,
                    expect_reasons=reason_list,
                    expect_fiber=expect_fiber,
                    tamper_target=tamper_target,
                    tamper_evid=tamper_evid,
                    force=force,
                    hint=hints[-1][0] if hints else "",
                    span_start=span_start,
                    span_end=span_end,
                    line_start=ln_start,
                    line_end=ln_end,
                )
            )
            pos = env_end
            continue

        # Node environment
        labels = _scan_command_calls(body_masked, body_raw, "label", 1)
        tex_label = labels[0][0].strip() if labels else ""
        node_id = opts.get("id") or tex_label.replace(r"\_", "_")
        if not node_id or not NODE_ID_RE.match(node_id):
            raise LatexCompileError(
                f"Environment {env_name} at line {ln_start} missing valid [id=...] or \\label{{...}}: got {node_id!r}"
            )
        if not tex_label:
            tex_label = f"sound:{node_id}"

        def_role, def_sort, def_evid, def_const, is_bind, def_kan, is_pen = _NODE_ENV_DEFAULTS[
            env_name
        ]
        clauses_calls = _scan_command_calls(body_masked, body_raw, "soundclause", 1)
        clauses: List[List[str]] = [_parse_id_list(c[0]) for c in clauses_calls]

        presup_calls = _scan_command_calls(body_masked, body_raw, "soundpresup", 1)
        presup: List[str] = []
        for p_call in presup_calls:
            presup.extend(_parse_id_list(p_call[0]))

        atk_by_calls = _scan_command_calls(body_masked, body_raw, "soundattackedby", 1)
        attacked_by: List[str] = []
        for a_call in atk_by_calls:
            for item in _parse_id_list(a_call[0]):
                if item not in attacked_by:
                    attacked_by.append(item)

        atk_out_calls = _scan_command_calls(body_masked, body_raw, "soundattacks", 1)
        attacks_out: List[str] = []
        for o_call in atk_out_calls:
            for item in _parse_id_list(o_call[0]):
                if item not in attacks_out:
                    attacks_out.append(item)

        alias_calls = _scan_command_calls(body_masked, body_raw, "soundaliases", 1)
        aliases: List[str] = []
        for al in alias_calls:
            aliases.extend(_parse_text_list(al[0]))

        kanlock = def_kan or _has_bare_command(body_masked, "soundkanlock")
        constitutive = def_const or _has_bare_command(body_masked, "soundconstitutive")
        revoked = _has_bare_command(body_masked, "soundrevoked")

        eff_evid = def_evid
        if env_name == "sounddef" and len(clauses) > 0:
            eff_evid = "inferential"
        elif env_name in ("soundstatute", "soundarticle", "soundaxiom") and len(clauses) == 0:
            clauses = [[]]
        elif is_bind:
            if clauses or kanlock or constitutive:
                raise LatexCompileError(
                    f"Bind slot {node_id!r} ({env_name}) cannot declare \\soundclause, \\soundkanlock, or \\soundconstitutive"
                )
            clauses = [[]]

        if len(clauses) > SOUND_MAX_CLAUSES:
            raise LatexCompileError(
                f"Node {node_id!r} has {len(clauses)} clauses (max {SOUND_MAX_CLAUSES})"
            )

        raw_nodes.append(
            {
                "node_id": node_id,
                "env_name": env_name,
                "title": title_str or node_id,
                "tex_label": tex_label,
                "role": def_role,
                "sort": def_sort,
                "evid": eff_evid,
                "constitutive": constitutive,
                "is_bind_slot": is_bind,
                "kanlock": kanlock,
                "penumbra": is_pen,
                "revoked": revoked,
                "clauses": clauses,
                "presup": presup,
                "attacked_by": attacked_by,
                "attacks_out": attacks_out,
                "aliases": aliases,
                "span_start": span_start,
                "span_end": span_end,
                "line_start": ln_start,
                "line_end": ln_end,
                "payload_hash": fnv1a_64(source_bytes[span_start:span_end]),
                "body_text": body_raw.strip(),
            }
        )
        pos = env_end

    if not (1 <= len(raw_nodes) <= SOUND_MAX_NODES):
        raise LatexCompileError(
            f"Semantic LaTeX must define 1..{SOUND_MAX_NODES} SOUND nodes, got {len(raw_nodes)}"
        )

    id_to_idx: Dict[str, int] = {}
    for idx, nd in enumerate(raw_nodes):
        nid = nd["node_id"]
        if nid in id_to_idx:
            raise LatexCompileError(f"Duplicate SOUND node ID {nid!r} in semantic LaTeX")
        id_to_idx[nid] = idx

    # Resolve outbound \soundattacks{target} -> target.attacked_by
    for nd in raw_nodes:
        src_id = nd["node_id"]
        for dst_id in nd["attacks_out"]:
            if dst_id not in id_to_idx:
                raise LatexCompileError(
                    f"Node {src_id!r} \\soundattacks references unknown node {dst_id!r}"
                )
            dst_list = raw_nodes[id_to_idx[dst_id]]["attacked_by"]
            if src_id not in dst_list:
                dst_list.append(src_id)

    # Validate cross-node references
    for nd in raw_nodes:
        nid = nd["node_id"]
        for c_idx, clause in enumerate(nd["clauses"]):
            for ref in clause:
                if ref not in id_to_idx:
                    raise LatexCompileError(
                        f"Node {nid!r} clause[{c_idx}] references unknown node {ref!r}"
                    )
        for ref in nd["presup"]:
            if ref not in id_to_idx:
                raise LatexCompileError(f"Node {nid!r} \\soundpresup references unknown {ref!r}")
        for ref in nd["attacked_by"]:
            if ref not in id_to_idx:
                raise LatexCompileError(
                    f"Node {nid!r} \\soundattackedby references unknown {ref!r}"
                )

    uc_map: Dict[str, List[str]] = {}
    for tgt_raw, facts_raw in _scan_command_calls(masked, source_text, "soundcounterexamplefor", 2):
        tgt = _parse_id_list(tgt_raw)
        if len(tgt) != 1 or tgt[0] not in id_to_idx:
            raise LatexCompileError(
                f"\\soundcounterexamplefor unknown target: {tgt_raw!r}"
            )
        f_list = _parse_id_list(facts_raw)
        for fid in f_list:
            if fid not in id_to_idx:
                raise LatexCompileError(
                    f"\\soundcounterexamplefor[{tgt[0]!r}] unknown fact {fid!r}"
                )
        uc_map[tgt[0]] = f_list

    conf_pairs: List[Tuple[str, str]] = []
    for a_raw, b_raw in _scan_command_calls(masked, source_text, "soundconfusable", 2):
        a_l = _parse_id_list(a_raw)
        b_l = _parse_id_list(b_raw)
        if len(a_l) != 1 or len(b_l) != 1 or a_l[0] not in id_to_idx or b_l[0] not in id_to_idx:
            raise LatexCompileError(f"\\soundconfusable unknown ref: {a_raw!r}, {b_raw!r}")
        conf_pairs.append((a_l[0], b_l[0]))

    for qa in raw_qas:
        for fid in qa.bind_facts:
            if fid not in id_to_idx:
                raise LatexCompileError(f"QA probe {qa.qa_id!r} unknown bind fact {fid!r}")
        if qa.target_node is not None and qa.target_node not in id_to_idx:
            raise LatexCompileError(f"QA probe {qa.qa_id!r} unknown target {qa.target_node!r}")
        if qa.tamper_target is not None and qa.tamper_target not in id_to_idx:
            raise LatexCompileError(
                f"QA probe {qa.qa_id!r} unknown tamper target {qa.tamper_target!r}"
            )

    nodes: List[LatexNodeSpec] = []
    arena_specs: List[ArenaNodeSpec] = []
    bind_mask = 0
    policy_mask = 0
    immunity_mask = 0

    for idx, nd in enumerate(raw_nodes):
        bit = 1 << idx
        if nd["is_bind_slot"]:
            bind_mask |= bit
        else:
            policy_mask |= bit
        if nd["kanlock"]:
            immunity_mask |= bit

        presup_mask = 0
        for ref in nd["presup"]:
            presup_mask |= 1 << id_to_idx[ref]
        attack_mask = 0
        for ref in nd["attacked_by"]:
            attack_mask |= 1 << id_to_idx[ref]
        clause_masks: List[int] = []
        for clause in nd["clauses"]:
            cm = 0
            for ref in clause:
                cm |= 1 << id_to_idx[ref]
            clause_masks.append(cm)

        spec = LatexNodeSpec(
            index=idx,
            node_id=nd["node_id"],
            env_name=nd["env_name"],
            title=nd["title"],
            tex_label=nd["tex_label"],
            role=nd["role"],
            sort=nd["sort"],
            evid=nd["evid"],
            constitutive=nd["constitutive"],
            is_bind_slot=nd["is_bind_slot"],
            kanlock=nd["kanlock"],
            penumbra=nd["penumbra"],
            revoked=nd["revoked"],
            clauses=nd["clauses"],
            presup=nd["presup"],
            attacked_by=nd["attacked_by"],
            aliases=nd["aliases"],
            span_start=nd["span_start"],
            span_end=nd["span_end"],
            line_start=nd["line_start"],
            line_end=nd["line_end"],
            payload_hash=nd["payload_hash"],
            body_text=nd["body_text"],
        )
        nodes.append(spec)
        arena_specs.append(
            ArenaNodeSpec(
                label=spec.node_id,
                sort=spec.sort,
                evid=spec.evid,
                aspect="state",
                constitutive=spec.constitutive,
                revoked=spec.revoked,
                penumbra=spec.penumbra,
                presup_mask=presup_mask,
                attack_mask=attack_mask,
                clauses=clause_masks,
                span_start=spec.span_start,
                span_end=spec.span_end,
            )
        )

    eff_schema = (
        schema_hash
        if schema_hash is not None
        else load_schema_hash(Path(__file__).resolve().parents[2])
    )
    raw_arena = encode_arena(
        arena_specs,
        schema_hash=eff_schema if eff_schema != 0 else FALLBACK_SCHEMA_HASH,
        immunity_mask=immunity_mask,
    )
    ctrl_flags = SOUND_FOREST_CTRL_NO_DIRECT_CLAIM if no_direct_claim else 0
    sealed_arena, seal_fiber, ctrl_snap = policy_seal_bytes(
        raw_arena,
        sound_bin,
        policy_mask=policy_mask,
        bind_mask=bind_mask,
        ctrl_flags=ctrl_flags,
    )
    if seal_fiber != SoundFiber.COMMITTED:
        raise LatexCompileError(
            f"Ring-0 sound_policy_seal rejected document: {fiber_name(seal_fiber)}"
        )
    ver_fiber = policy_verify_bytes(sealed_arena, sound_bin)
    if ver_fiber != SoundFiber.COMMITTED:
        raise LatexCompileError(
            f"Ring-0 sound_policy_verify failed after seal: {fiber_name(ver_fiber)}"
        )

    return CompiledLatexDoc(
        pack_id=pack_id,
        domain=domain,
        jurisdiction=jurisdiction,
        standard=standard,
        title=doc_title,
        source_path=source_path,
        source_text=source_text,
        source_bytes=source_bytes,
        no_direct_claim=no_direct_claim,
        nodes=nodes,
        node_index={nd.node_id: nd for nd in nodes},
        universal_counterexamples=uc_map,
        confusable_pairs=conf_pairs,
        qa_probes=raw_qas,
        policy_mask=policy_mask,
        bind_mask=bind_mask,
        immunity_mask=immunity_mask,
        schema_hash=eff_schema,
        arena_bytes=sealed_arena,
        forest_ctrl=ctrl_snap,
        norm_state=read_normative_state(sealed_arena),
    )


def compile_latex_file(
    tex_path: Path, sound_bin: Path, *, schema_hash: Optional[int] = None
) -> CompiledLatexDoc:
    """Load UTF-8 `.tex` file from disk and compile into a sealed `CompiledLatexDoc`."""
    raw = tex_path.read_text(encoding="utf-8")
    return compile_latex_text(raw, sound_bin, source_path=tex_path, schema_hash=schema_hash)


def export_topic_pack(doc: CompiledLatexDoc) -> Dict[str, Any]:
    """Export a validated JSON TopicPack compatible with `modules.common.mcq_engine`."""
    if len(doc.nodes) > 62:
        raise LatexCompileError(
            f"TopicPack requires <= 62 nodes to leave dynamic slots, got {len(doc.nodes)}"
        )
    pack_nodes: List[Dict[str, Any]] = []
    for nd in doc.nodes:
        item: Dict[str, Any] = {
            "id": nd.node_id,
            "role": nd.role,
            "sort": nd.sort,
            "evid": "direct" if nd.is_bind_slot else nd.evid,
            "description": nd.title,
        }
        if nd.constitutive:
            item["constitutive"] = True
        if nd.penumbra:
            item["penumbra"] = True
        if nd.revoked:
            item["revoked"] = True
        if not nd.is_bind_slot and nd.clauses:
            item["clauses"] = [list(c) for c in nd.clauses]
        elif nd.is_bind_slot:
            item["clauses"] = []
        if nd.presup:
            item["presup"] = list(nd.presup)
        if nd.attacked_by:
            item["attacked_by"] = list(nd.attacked_by)
        if nd.aliases:
            item["aliases"] = list(nd.aliases)
        pack_nodes.append(item)

    pack = {
        "pack_id": doc.pack_id,
        "domain": doc.domain,
        "jurisdiction": doc.jurisdiction,
        "standard": doc.standard,
        "version": "1.0.0",
        "title": doc.title,
        "nodes": pack_nodes,
        "universal_counterexamples": {k: list(v) for k, v in doc.universal_counterexamples.items()},
        "confusable_pairs": [[a, b] for a, b in doc.confusable_pairs],
    }
    validate_topic_pack(pack)
    return pack
