"""CLI and interactive Socratic REPL for Semantic LaTeX <-> SOUND verification."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import List, Optional, Sequence

from ..common.arena_codec import SoundFiber
from .latex_compiler import (
    CompiledLatexDoc,
    LatexCompileError,
    compile_latex_file,
    export_topic_pack,
)
from .latex_emitter import emit_latex_report, validate_emitted_latex_structure
from .qa_verifier import (
    QAVerdict,
    probe_premise_necessity,
    run_embedded_qa_suite,
    verify_scenario,
)


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _default_sound_bin(repo_root: Path) -> Path:
    return repo_root / "build" / "sound"


def _split_csv(val: Optional[str]) -> List[str]:
    if not val:
        return []
    return [tok for tok in re.split(r"[,\s]+", val.strip()) if tok]


def _print_verdict(v: QAVerdict) -> None:
    st = v.actual_status or "-"
    tag = "PASS" if v.comprehension_pass else "FAIL"
    print(
        f"  [{v.qa_id}] {tag} | fiber={v.landing_fiber_name} status={st:<17} "
        f"reasons={v.reason_names or v.inject_reasons or []}"
    )
    if v.repair_result is not None:
        print(
            f"      SEARU repair: energy={v.repair_result.energy} conv={int(v.repair_result.converged)} "
            f"retract={v.retracted_nodes}"
        )
    print(f"      diag: {v.diagnosis_zh}")


def _cmd_compile(args: argparse.Namespace, sound_bin: Path) -> int:
    doc = compile_latex_file(Path(args.tex), sound_bin)
    print(
        f"[latex compile] pack={doc.pack_id} domain={doc.domain}/{doc.jurisdiction} "
        f"nodes={len(doc.nodes)}/64 probes={len(doc.qa_probes)} "
        f"policy=0x{doc.policy_mask:016x} bind=0x{doc.bind_mask:016x} "
        f"imm=0x{doc.immunity_mask:016x} policy_hash=0x{doc.forest_ctrl.policy_hash:016x}"
    )
    if args.out_snd:
        Path(args.out_snd).write_bytes(doc.arena_bytes)
        print(f"  wrote {len(doc.arena_bytes)}B SoundArena -> {args.out_snd}")
    if args.out_pack:
        pack = export_topic_pack(doc)
        Path(args.out_pack).write_text(
            json.dumps(pack, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        print(f"  wrote validated TopicPack JSON -> {args.out_pack}")
    return 0


def _cmd_verify(args: argparse.Namespace, sound_bin: Path) -> int:
    doc = compile_latex_file(Path(args.tex), sound_bin)
    verdicts = run_embedded_qa_suite(doc, sound_bin)
    print(f"[latex verify] {args.tex} (pack={doc.pack_id}, probes={len(verdicts)})")
    fails = 0
    for v in verdicts:
        _print_verdict(v)
        if not v.comprehension_pass:
            fails += 1
    if not verdicts:
        print("  FAIL: no embedded \\begin{soundqa} probes found")
        return 1
    print(f"Summary: {len(verdicts) - fails}/{len(verdicts)} probes PASS (fails={fails})")
    return 0 if fails == 0 else 1


def _cmd_ablate(args: argparse.Namespace, sound_bin: Path) -> int:
    doc = compile_latex_file(Path(args.tex), sound_bin)
    facts = _split_csv(args.facts)
    rows = probe_premise_necessity(
        doc, sound_bin, base_facts=facts, target_node=args.target
    )
    print(f"[latex ablate] target={args.target} | base_facts={facts}")
    for r in rows:
        nec = "NECESSARY" if r["necessary"] else "redundant"
        print(
            f"  drop {r['dropped_fact']:<22} (L{r['line_start']}): "
            f"{r['baseline_status']} -> {r['resulting_status']} [{nec}] reasons={r['reason_names']}"
        )
    return 0


def _cmd_ask(args: argparse.Namespace, sound_bin: Path) -> int:
    doc = compile_latex_file(Path(args.tex), sound_bin)
    facts = _split_csv(args.bind)
    verdict = verify_scenario(
        doc,
        sound_bin,
        bind_facts=facts,
        target_node=args.target,
        claim_status=args.claim,
        force=args.force,
        evid=args.evid,
        tamper_target=args.tamper,
    )
    print(f"[latex ask] pack={doc.pack_id} target={args.target}")
    _print_verdict(verdict)
    if args.claim and not verdict.comprehension_pass:
        return 1
    return 0


def _cmd_emit_report(args: argparse.Namespace, sound_bin: Path) -> int:
    doc = compile_latex_file(Path(args.tex), sound_bin)
    verdicts = run_embedded_qa_suite(doc, sound_bin)
    ablations = None
    if args.ablate_target and args.ablate_facts:
        ablations = probe_premise_necessity(
            doc,
            sound_bin,
            base_facts=_split_csv(args.ablate_facts),
            target_node=args.ablate_target,
        )
    tex_out = emit_latex_report(doc, verdicts, ablations=ablations, standalone=True)
    struct_errs = validate_emitted_latex_structure(tex_out)
    if struct_errs:
        print(f"ERROR: emitted LaTeX structure invalid: {struct_errs}", file=sys.stderr)
        return 1
    if args.out:
        Path(args.out).write_text(tex_out, encoding="utf-8")
        print(f"[latex emit-report] wrote {len(tex_out)} chars (0 struct errors) -> {args.out}")
    else:
        sys.stdout.write(tex_out)
    return 0


def _run_interactive_session(doc: CompiledLatexDoc, sound_bin: Path) -> int:
    bound: List[str] = []
    history: List[QAVerdict] = []
    print(
        f"SOUND LaTeX Interactive Verifier | pack={doc.pack_id} "
        f"(nodes={len(doc.nodes)}, bind_slots={bin(doc.bind_mask).count('1')}, "
        f"kanlock={bin(doc.immunity_mask).count('1')})"
    )
    print("Commands: help | list | bind <ids...> | clear | ask <target> [claim] | ablate <target> <f1,f2> | tamper <slot> [evid] | emit <out.tex> | quit")
    while True:
        try:
            raw = input("sound-latex> ")
        except EOFError:
            break
        line = raw.strip()
        if not line:
            continue
        parts = line.split()
        cmd = parts[0].lower()
        rest = parts[1:]

        if cmd in ("quit", "exit", "q"):
            break
        if cmd == "help":
            print(
                "  list                         list policy/bind/kanlock nodes & line ranges\n"
                "  bind <id1> [id2...]          add case/hypothesis bind slots\n"
                "  clear                        clear current bound facts\n"
                "  ask <target> [claim_status]  evaluate target under bound facts\n"
                "  ablate <target> <f1,f2,...>  Socratic necessity check\n"
                "  tamper <slot> [evid]         test Ring-0 Prepared-Statement guard\n"
                "  emit <out.tex>               write auditable LaTeX report"
            )
        elif cmd == "list":
            for nd in doc.nodes:
                plane = "BIND" if nd.is_bind_slot else ("KANLOCK" if nd.kanlock else "POLICY")
                print(
                    f"  [{plane:<7}] {nd.node_id:<22} (L{nd.line_start:>3}) {nd.title}"
                )
        elif cmd == "bind":
            for tok in rest:
                for fid in _split_csv(tok):
                    if fid not in doc.node_index:
                        print(f"  ! Unknown node: {fid}")
                    elif fid not in bound:
                        bound.append(fid)
            print(f"  bound = {bound}")
        elif cmd == "clear":
            bound.clear()
            print("  bound = []")
        elif cmd == "ask":
            if not rest:
                print("  Usage: ask <target_node> [established|defeated|conflictUndecided|penumbraOpen]")
                continue
            target = rest[0]
            claim = rest[1] if len(rest) > 1 else None
            try:
                v = verify_scenario(
                    doc, sound_bin, bind_facts=bound, target_node=target, claim_status=claim
                )
                history.append(v)
                _print_verdict(v)
            except Exception as exc:
                print(f"  ! Error: {exc}")
        elif cmd == "ablate":
            if len(rest) < 2:
                print("  Usage: ablate <target_node> <fact1,fact2,...>")
                continue
            target = rest[0]
            facts = _split_csv(" ".join(rest[1:]))
            try:
                rows = probe_premise_necessity(
                    doc, sound_bin, base_facts=facts, target_node=target
                )
                for r in rows:
                    nec = "NECESSARY" if r["necessary"] else "redundant"
                    print(
                        f"  drop {r['dropped_fact']:<22} -> {r['resulting_status']} [{nec}] {r['reason_names']}"
                    )
            except Exception as exc:
                print(f"  ! Error: {exc}")
        elif cmd == "tamper":
            if not rest:
                print("  Usage: tamper <slot_id> [evid]")
                continue
            slot = rest[0]
            t_evid = rest[1] if len(rest) > 1 else "sensory"
            try:
                v = verify_scenario(
                    doc,
                    sound_bin,
                    bind_facts=bound,
                    target_node=None,
                    expected_fiber=SoundFiber.FALLBACK_POLICY_VIOLATION,
                    tamper_target=slot,
                    tamper_evid=t_evid,
                )
                history.append(v)
                _print_verdict(v)
            except Exception as exc:
                print(f"  ! Error: {exc}")
        elif cmd == "emit":
            if not rest:
                print("  Usage: emit <out.tex>")
                continue
            use_v = history or run_embedded_qa_suite(doc, sound_bin)
            tex_out = emit_latex_report(doc, use_v, standalone=True)
            errs = validate_emitted_latex_structure(tex_out)
            if errs:
                print(f"  ! Structure error: {errs}")
                continue
            Path(rest[0]).write_text(tex_out, encoding="utf-8")
            print(f"  wrote {len(tex_out)} chars -> {rest[0]}")
        else:
            print(f"  Unknown command {cmd!r}; type 'help'")
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="python3 -m modules.latex.cli",
        description="Semantic LaTeX <-> SOUND Ring-0 Comprehension Verifier CLI",
    )
    p.add_argument(
        "--sound-bin",
        default=None,
        help="Path to built `sound` binary (defaults to `<repo>/build/sound`)",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("compile", help="Compile & seal semantic .tex into a 16KB SoundArena")
    c.add_argument("tex", help="Input .tex path")
    c.add_argument("--out-snd", default=None, help="Optional output 16KB .snd path")
    c.add_argument("--out-pack", default=None, help="Optional output TopicPack .json path")

    v = sub.add_parser("verify", help="Run all embedded \\begin{soundqa} probes in .tex")
    v.add_argument("tex", help="Input .tex path")

    ab = sub.add_parser("ablate", help="Run Socratic premise necessity ablation")
    ab.add_argument("tex", help="Input .tex path")
    ab.add_argument("--target", required=True, help="Target theorem/effect node ID")
    ab.add_argument("--facts", required=True, help="Comma-separated bound fact IDs")

    ak = sub.add_parser("ask", help="Verify a claim under bound facts via Ring-0 FFI")
    ak.add_argument("tex", help="Input .tex path")
    ak.add_argument("--target", required=True, help="Target node ID")
    ak.add_argument("--bind", default="", help="Comma-separated bind fact IDs")
    ak.add_argument("--claim", default=None, help="Optional expected status")
    ak.add_argument("--force", default="assertion", help="Utterance force")
    ak.add_argument("--evid", default="sensory", help="Evidence level for bound facts")
    ak.add_argument("--tamper", default=None, help="Optional slot to test tamper guard")

    em = sub.add_parser("emit-report", help="Emit auditable LaTeX report from .tex probes")
    em.add_argument("tex", help="Input .tex path")
    em.add_argument("--out", default=None, help="Output .tex path (stdout if omitted)")
    em.add_argument("--ablate-target", default=None, help="Optional ablation target node")
    em.add_argument("--ablate-facts", default=None, help="Optional comma-separated facts")

    it = sub.add_parser("interactive", help="Start interactive Socratic verification REPL")
    it.add_argument("tex", help="Input .tex path")

    return p


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(list(argv) if argv is not None else None)
    sound_bin = Path(args.sound_bin) if args.sound_bin else _default_sound_bin(_repo_root())
    try:
        if args.cmd == "compile":
            return _cmd_compile(args, sound_bin)
        if args.cmd == "verify":
            return _cmd_verify(args, sound_bin)
        if args.cmd == "ablate":
            return _cmd_ablate(args, sound_bin)
        if args.cmd == "ask":
            return _cmd_ask(args, sound_bin)
        if args.cmd == "emit-report":
            return _cmd_emit_report(args, sound_bin)
        if args.cmd == "interactive":
            doc = compile_latex_file(Path(args.tex), sound_bin)
            return _run_interactive_session(doc, sound_bin)
    except LatexCompileError as exc:
        print(f"ERROR (LatexCompileError): {exc}", file=sys.stderr)
        return 1
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
