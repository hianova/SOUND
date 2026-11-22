#!/usr/bin/env python3
"""
SOUND v3.0.0 — Deterministic Fail-Closed Coverage Gate & Untrusted Proposer Wrapper
=====================================================================================
Bridges raw natural-language consumer contracts (Traditional Chinese + English) and an
untrusted proposer extraction JSON (`source_spans` + `clauses` + optional `edges`) into
the formally verified Ring-0 C17 verifier `./build/sound audit-contract`.

Critical safety guarantee (where LLMs fail):
  The proposer NEVER gets to sign off a contract directly. Every sentence in the raw
  contract containing monetary figures, percentages, or high-risk legal powers/triggers is
  deterministically checked against `source_spans`. Any uncovered risky sentence (or mapped
  to an empty `base_loss=0, extra_drift=0, exit_fee=0` placeholder) increments
  `coverage_gaps` and injects a `B4_BOT` ambiguity clause, forcing `./build/sound` to
  fail closed (`VETOED` with `SOUND_CVIOL_COVERAGE_GAP`).
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import tempfile
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SOUND_BIN = ROOT / "build" / "sound"
DEFAULT_INFO_BIN = ROOT.parent / "INFO" / "build" / "info"

RISK_TRIGGERS = (
    # Traditional Chinese legal/financial risk triggers
    "單方",
    "逕行",
    "酌收",
    "拋棄",
    "放棄",
    "連帶",
    "自動續約",
    "加速到期",
    "優先於主約",
    "沒收",
    "另行規定",
    "全權決定",
    "不在此限",
    "違約金",
    "解約金",
    "附件",
    # English legal/financial risk triggers
    "sole discretion",
    "unilateral",
    "without notice",
    "waive",
    "waives",
    "jointly and severally",
    "accelerate",
    "acceleration",
    "automatically renew",
    "notwithstanding",
    "forfeit",
    "forfeiture",
    "surrender charge",
    "early termination",
    "prevail over",
    "annex",
)

HOSTILE_AFFIRMATIVE_TRIGGERS = (
    "單方",
    "逕行",
    "拋棄",
    "放棄",
    "連帶",
    "加速到期",
    "優先於主約",
    "沒收",
    "強制仲裁",
    "全權決定",
    "sole discretion",
    "unilateral",
    "waive",
    "waives",
    "jointly and severally",
    "accelerate",
    "prevail over",
    "forfeit",
    "mandatory arbitration",
    "notwithstanding",
)

NEGATION_GUARDS = (
    "不得",
    "禁止",
    "絕不",
    "無權",
    "不可",
    "無單方",
    "免除",
    "不會",
    "not ",
    "no ",
    "never ",
    "prohibit",
    "without ",
)

PENALTY_FEE_WORDS = (
    "罰金",
    "違約金",
    "解約金",
    "酌收",
    "加收",
    "penalty",
    "surcharge",
    "multiplier",
    "forfeit",
    "liquidated",
)

HOSTILE_CLAUSE_KINDS = frozenset(
    ("unilateral_power", "chisholm_penalty", "hidden_fine_print", "ambiguous_obligation", "exit_trap")
)
HOSTILE_EDGE_KINDS = frozenset(("override", "chisholm_trigger", "sheaf_twist"))

MONEY_OR_PCT_RE = re.compile(
    r"(?:"
    r"[\$NT\s]?\d{1,3}(?:,\d{3})+(?:\.\d+)?"
    r"|\d+(?:\.\d+)?\s*(?:%|％|萬|元|個月|倍|USD|NTD|TWD)"
    r")",
    re.IGNORECASE,
)

MONEY_WAN_RE = re.compile(r"(\d[\d,]*(?:\.\d+)?)\s*萬\s*(?:元)?")
MONEY_UNIT_RE = re.compile(
    r"(?:[\$]|USD|NTD|TWD)\s*(\d[\d,]*(?:\.\d+)?)"
    r"|(\d[\d,]*(?:\.\d+)?)\s*(?:元|dollars?|bucks?)",
    re.IGNORECASE,
)
RATE_OR_MULT_RE = re.compile(r"\d+(?:\.\d+)?\s*(?:%|％|倍|x\b)", re.IGNORECASE)


def split_sentences_with_spans(text: str) -> List[Tuple[int, int, str]]:
    spans: List[Tuple[int, int, str]] = []
    start = 0
    for idx, ch in enumerate(text):
        if ch in "。；;\n!！?？":
            seg = text[start : idx + 1].strip()
            if seg:
                raw_seg_start = text.find(seg, start, idx + 1)
                spans.append((raw_seg_start, raw_seg_start + len(seg), seg))
            start = idx + 1
    if start < len(text):
        seg = text[start:].strip()
        if seg:
            raw_seg_start = text.find(seg, start)
            spans.append((raw_seg_start, raw_seg_start + len(seg), seg))
    return spans


def is_risky_sentence(sentence: str) -> Tuple[bool, List[str]]:
    lower = sentence.lower()
    reasons: List[str] = []
    for kw in RISK_TRIGGERS:
        if kw.lower() in lower:
            reasons.append(f"trigger:{kw}")
    if MONEY_OR_PCT_RE.search(sentence):
        reasons.append("monetary_or_rate")
    return (len(reasons) > 0), reasons


def _spans_overlap(a0: int, a1: int, b0: int, b1: int) -> bool:
    return max(a0, b0) < min(a1, b1)


def _extract_proposer_span_links(
    raw_len: int, proposer: dict
) -> List[Tuple[int, int, int, Optional[dict]]]:
    cid_map: Dict[int, dict] = {}
    clause_links: List[Tuple[int, int, int, Optional[dict]]] = []
    for cl in proposer.get("clauses", []):
        cid = int(cl.get("clause_id", 0))
        if cid > 0:
            cid_map[cid] = cl
        sp = cl.get("source_span")
        if isinstance(sp, list) and len(sp) == 2:
            s0, s1 = int(sp[0]), int(sp[1])
            if 0 <= s0 < s1 <= raw_len:
                clause_links.append((s0, s1, cid, cl))

    for sp in proposer.get("source_spans", []):
        if isinstance(sp, dict) and "start" in sp and "end" in sp:
            s0, s1 = int(sp["start"]), int(sp["end"])
            cid = int(sp.get("clause_id", 0))
            if 0 <= s0 < s1 <= raw_len:
                clause_links.append((s0, s1, cid, cid_map.get(cid)))
    return clause_links


def _extract_money_values(sentence: str) -> List[int]:
    vals: List[int] = []
    used_ranges: List[Tuple[int, int]] = []
    for m in MONEY_WAN_RE.finditer(sentence):
        used_ranges.append((m.start(), m.end()))
        vals.append(int(round(float(m.group(1).replace(",", "")) * 10000.0)))
    for m in MONEY_UNIT_RE.finditer(sentence):
        if any(_spans_overlap(m.start(), m.end(), u0, u1) for u0, u1 in used_ranges):
            continue
        raw_num = m.group(1) or m.group(2)
        if raw_num is not None:
            vals.append(int(round(float(raw_num.replace(",", "")))))
    return vals


def _is_negated_before(lower_text: str, pos: int) -> bool:
    window = lower_text[max(0, pos - 10) : pos]
    return any(g in window for g in NEGATION_GUARDS)


def _find_affirmative_hostile_triggers(sentence: str) -> List[str]:
    lower = sentence.lower()
    found: List[str] = []
    for kw in HOSTILE_AFFIRMATIVE_TRIGGERS:
        kw_low = kw.lower()
        idx = lower.find(kw_low)
        while idx != -1:
            if not _is_negated_before(lower, idx):
                found.append(kw)
                break
            idx = lower.find(kw_low, idx + len(kw_low))
    return found


def _has_affirmative_rate_penalty(sentence: str) -> bool:
    if not RATE_OR_MULT_RE.search(sentence):
        return False
    lower = sentence.lower()
    for kw in PENALTY_FEE_WORDS:
        kw_low = kw.lower()
        idx = lower.find(kw_low)
        while idx != -1:
            if not _is_negated_before(lower, idx):
                return True
            idx = lower.find(kw_low, idx + len(kw_low))
    return False


def _clause_modeled_amount(cl: dict) -> int:
    base = int(cl.get("base_loss", 0))
    drift = int(cl.get("extra_drift", 0))
    exit_fee = int(cl.get("exit_fee", 0))
    cons_pen = int(cl.get("cons_penalty", 0))
    return max(base, drift, exit_fee, cons_pen, base + drift)


def _clause_is_flagged(cl: dict, hostile_edge_cids: Set[int]) -> bool:
    cid = int(cl.get("clause_id", 0))
    if cid in hostile_edge_cids:
        return True
    if str(cl.get("kind", "")) in HOSTILE_CLAUSE_KINDS:
        return True
    if int(cl.get("extra_drift", 0)) > 0:
        return True
    if not bool(cl.get("disclosed", True)):
        return True
    return str(cl.get("b4_truth", "TRUE")) in ("BOT", "TOP")


def _inject_gap_clause(
    clauses: List[dict], next_id: int, title: str, protected_mask: int
) -> Tuple[dict, int]:
    cl = {
        "clause_id": next_id,
        "title": title[:62],
        "kind": "ambiguous_obligation",
        "disclosed": False,
        "b4_truth": "BOT",
        "target_mask": protected_mask,
        "base_loss": 0,
        "extra_drift": 0,
        "exit_fee": 0,
        "cons_penalty": 0,
        "inst_penalty": 0,
        "sheaf_cycle": False,
    }
    return cl, next_id + 1


def apply_coverage_gate(raw_text: str, proposer: dict) -> Tuple[dict, List[dict], List[dict]]:
    sentences = split_sentences_with_spans(raw_text)
    span_links = _extract_proposer_span_links(len(raw_text), proposer)

    hostile_edge_cids: Set[int] = set()
    for edge in proposer.get("edges", []):
        ek = str(edge.get("kind", edge.get("edge_kind", "")))
        if ek in HOSTILE_EDGE_KINDS:
            hostile_edge_cids.add(int(edge.get("src_id", 0)))
            hostile_edge_cids.add(int(edge.get("dst_id", 0)))

    uncovered: List[dict] = []
    fidelity_mismatches: List[dict] = []

    for s0, s1, seg in sentences:
        covering_clauses: List[dict] = [
            cl for c0, c1, _, cl in span_links if _spans_overlap(s0, s1, c0, c1) and cl is not None
        ]
        has_span_cover = any(_spans_overlap(s0, s1, c0, c1) for c0, c1, _, _ in span_links)
        risky, reasons = is_risky_sentence(seg)
        if risky and not has_span_cover:
            uncovered.append({"start": s0, "end": s1, "text": seg, "reasons": reasons})
            continue
        if not covering_clauses:
            continue

        fid_reasons: List[str] = []
        money_vals = _extract_money_values(seg)
        if money_vals:
            raw_floor = max(money_vals)
            modeled_peak = max(_clause_modeled_amount(cl) for cl in covering_clauses)
            if modeled_peak < raw_floor:
                fid_reasons.append(f"money_downplay:{modeled_peak}<{raw_floor}")

        aff_triggers = _find_affirmative_hostile_triggers(seg)
        if aff_triggers and not any(_clause_is_flagged(cl, hostile_edge_cids) for cl in covering_clauses):
            fid_reasons.append(f"power_downplay:{','.join(aff_triggers[:3])}")

        if _has_affirmative_rate_penalty(seg):
            total_exp = sum(
                int(cl.get("base_loss", 0))
                + int(cl.get("extra_drift", 0))
                + int(cl.get("exit_fee", 0))
                + int(cl.get("cons_penalty", 0))
                for cl in covering_clauses
            )
            if total_exp == 0 and not any(_clause_is_flagged(cl, hostile_edge_cids) for cl in covering_clauses):
                fid_reasons.append("rate_downplay:zero_exposure")

        if fid_reasons:
            fidelity_mismatches.append({"start": s0, "end": s1, "text": seg, "reasons": fid_reasons})

    gated = json.loads(json.dumps(proposer))
    prev_gaps = int(gated.get("coverage_gaps", 0))
    gated["coverage_gaps"] = prev_gaps + len(uncovered) + len(fidelity_mismatches)

    clauses = list(gated.get("clauses", []))
    next_id = max((int(c.get("clause_id", 0)) for c in clauses), default=0) + 1
    pmask = int(gated.get("protected_mask", 1))
    for gap in uncovered:
        cl, next_id = _inject_gap_clause(
            clauses, next_id, f"UNCOVERED_RAW_SENTENCE ({', '.join(gap['reasons'])})", pmask
        )
        clauses.append(cl)
    for mm in fidelity_mismatches:
        cl, next_id = _inject_gap_clause(
            clauses, next_id, f"FACT_FIDELITY_MISMATCH ({', '.join(mm['reasons'])})", pmask
        )
        clauses.append(cl)
    gated["clauses"] = clauses
    return gated, uncovered, fidelity_mismatches


def compute_cognitive_opacity(
    raw_txt_path: Path,
    proposer: dict,
    summary_text: Optional[str] = None,
    info_bin: Optional[Path] = DEFAULT_INFO_BIN,
) -> Optional[dict]:
    """Optional INFO MDL Cognitive Opacity score; returns None cleanly if INFO CLI is absent."""
    if info_bin is None or not info_bin.is_file():
        return None
    if summary_text is None:
        summary_text = (
            f"Plain-Language Consumer Contract Disclosure: {proposer.get('contract_name', 'Contract')}. "
            f"Maximum customer liability cap: {int(proposer.get('max_loss_cap', 0))}. "
            f"Maximum exit/termination fee: {int(proposer.get('max_exit_fee', 0))}. "
            f"All core consumer protections remain intact without hidden overrides or undisclosed charges.\n"
        )
    with tempfile.TemporaryDirectory(prefix="sound_info_ncd_") as td:
        td_p = Path(td)
        sum_p = td_p / "summary.txt"
        ncd_p = td_p / "ncd.json"
        sum_p.write_text(summary_text, encoding="utf-8")
        proc = subprocess.run(
            [str(info_bin), "ncd", str(sum_p), str(raw_txt_path), "--out-json", str(ncd_p)],
            capture_output=True,
            text=True,
            check=False,
        )
        if proc.returncode != 0 or not ncd_p.is_file():
            return None
        d = json.loads(ncd_p.read_text(encoding="utf-8"))

    la = int(d["la_bits_q12"])
    lb = int(d["lb_bits_q12"])
    lb_given_a = int(d["lb_given_a_bits_q12"])
    la_given_b = int(d["la_given_b_bits_q12"])
    c_joint = min(la + lb_given_a, lb + la_given_b)
    ncd = float(c_joint - min(la, lb)) / float(max(la, lb)) if max(la, lb) > 0 else 0.0
    cond_ratio = float(lb_given_a) / float(lb) if lb > 0 else 0.0
    tier = "HIGH_OPACITY" if (ncd >= 0.85 or cond_ratio >= 0.85) else (
        "ELEVATED" if (ncd >= 0.68 or cond_ratio >= 0.72) else "LOW"
    )
    return {
        "summary_bytes": int(d["len_a"]),
        "contract_bytes": int(d["len_b"]),
        "summary_mdl_bytes": round(la / (4096.0 * 8.0), 1),
        "contract_mdl_bytes": round(lb / (4096.0 * 8.0), 1),
        "cond_mdl_bytes": round(lb_given_a / (4096.0 * 8.0), 1),
        "cond_ratio": round(cond_ratio, 4),
        "ncd": round(ncd, 4),
        "opacity_tier": tier,
    }


def _append_intake_receipt_sections(
    out_receipt: Path,
    uncovered: List[dict],
    fidelity_mismatches: List[dict],
    opacity: Optional[dict],
) -> None:
    lines: List[str] = []
    if uncovered or fidelity_mismatches:
        lines.append("\n## Intake & Fact-Fidelity Gate Findings\n")
        for g in uncovered:
            lines.append(f"- **UNCOVERED_SENTENCE** [{g['start']}..{g['end']}] ({', '.join(g['reasons'])}): `{g['text']}`")
        for m in fidelity_mismatches:
            lines.append(f"- **FACT_FIDELITY_MISMATCH** [{m['start']}..{m['end']}] ({', '.join(m['reasons'])}): `{m['text']}`")
    if opacity is not None:
        lines.append("\n## Cognitive Opacity Audit (INFO MDL Bridge)\n")
        lines.append(
            f"- **Complexity**: `C(Summary)={opacity['summary_mdl_bytes']} B`, "
            f"`C(Contract)={opacity['contract_mdl_bytes']} B`, "
            f"`C(Contract|Summary)={opacity['cond_mdl_bytes']} B` "
            f"(`cond_ratio={opacity['cond_ratio']:.4f}`, `NCD={opacity['ncd']:.4f}`, tier=`{opacity['opacity_tier']}`)"
        )
    if lines:
        prev = out_receipt.read_text(encoding="utf-8")
        out_receipt.write_text(prev + "\n".join(lines) + "\n", encoding="utf-8")


def run_intake_audit(
    raw_txt_path: Path,
    proposer_json_path: Path,
    sound_bin: Path = DEFAULT_SOUND_BIN,
    out_lean: Optional[Path] = None,
    out_fir: Optional[Path] = None,
    out_json: Optional[Path] = None,
    out_receipt: Optional[Path] = None,
    summary_txt_path: Optional[Path] = None,
    info_bin: Optional[Path] = DEFAULT_INFO_BIN,
) -> Tuple[dict, List[dict]]:
    raw_text = raw_txt_path.read_text(encoding="utf-8")
    proposer = json.loads(proposer_json_path.read_text(encoding="utf-8"))
    gated, uncovered, fidelity_mismatches = apply_coverage_gate(raw_text, proposer)
    summary_str = summary_txt_path.read_text(encoding="utf-8") if summary_txt_path is not None else None
    opacity = compute_cognitive_opacity(raw_txt_path, proposer, summary_text=summary_str, info_bin=info_bin)

    with tempfile.TemporaryDirectory(prefix="sound_intake_") as td:
        td_p = Path(td)
        spec_p = td_p / "gated_contract.json"
        spec_p.write_text(json.dumps(gated, ensure_ascii=False, indent=2), encoding="utf-8")
        json_out_p = out_json if out_json is not None else (td_p / "audit.json")

        cmd = [str(sound_bin), "audit-contract", str(spec_p), "--out-json", str(json_out_p)]
        if out_lean is not None:
            cmd += ["--out-lean", str(out_lean)]
        if out_fir is not None:
            cmd += ["--out-fir", str(out_fir)]
        if out_receipt is not None:
            cmd += ["--out-receipt", str(out_receipt)]

        proc = subprocess.run(cmd, cwd=str(ROOT), capture_output=True, text=True, check=False)
        if proc.returncode != 0:
            raise RuntimeError(f"audit-contract failed: {proc.stderr}\n{proc.stdout}")

        audit_res = json.loads(json_out_p.read_text(encoding="utf-8"))
        audit_res["uncovered_sentences"] = uncovered
        audit_res["fidelity_mismatches"] = fidelity_mismatches
        audit_res["cognitive_opacity"] = opacity
        if out_receipt is not None and out_receipt.is_file():
            _append_intake_receipt_sections(out_receipt, uncovered, fidelity_mismatches, opacity)
        if out_json is not None:
            out_json.write_text(json.dumps(audit_res, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        return audit_res, uncovered


def main() -> int:
    ap = argparse.ArgumentParser(description="SOUND Deterministic Fail-Closed Contract Intake Gate")
    ap.add_argument("--raw-text", required=True, type=Path, help="Raw natural-language contract (.txt)")
    ap.add_argument("--proposer-json", required=True, type=Path, help="Untrusted LLM extraction JSON")
    ap.add_argument("--summary-text", type=Path, default=None, help="Optional plain-language summary (.txt) for INFO NCD audit")
    ap.add_argument("--sound-bin", default=DEFAULT_SOUND_BIN, type=Path)
    ap.add_argument("--info-bin", default=DEFAULT_INFO_BIN, type=Path)
    ap.add_argument("--out-lean", type=Path, default=None)
    ap.add_argument("--out-fir", type=Path, default=None)
    ap.add_argument("--out-json", type=Path, default=None)
    ap.add_argument("--out-receipt", type=Path, default=None)
    args = ap.parse_args()

    res, uncovered = run_intake_audit(
        args.raw_text,
        args.proposer_json,
        sound_bin=args.sound_bin,
        out_lean=args.out_lean,
        out_fir=args.out_fir,
        out_json=args.out_json,
        out_receipt=args.out_receipt,
        summary_txt_path=args.summary_text,
        info_bin=args.info_bin,
    )
    print(
        f"[intake-gate] verdict={res['verdict']} coverage_gaps={res['coverage_gaps']} "
        f"uncovered={len(uncovered)} fidelity_mismatches={len(res['fidelity_mismatches'])} "
        f"viol=0x{res['viol_flags']:03x} worst={res['worst_case_loss']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
