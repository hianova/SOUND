"""CLI runner for SOUND Taxation & Accounting non-computation MCQ trap detection."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

from .arena_codec import load_schema_hash
from .mcq_engine import (
    discover_benchmarks,
    discover_packs,
    solve_question,
)


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _modules_root() -> Path:
    return Path(__file__).resolve().parents[1]


def _default_sound_bin(repo_root: Path) -> Path:
    return repo_root / "build" / "sound"


def _cmd_packs(modules_root: Path) -> int:
    packs = discover_packs(modules_root)
    print(f"{'pack_id':<26} {'domain':<12} {'jur':<5} {'nodes':<7} {'standard'}")
    print("-" * 92)
    for pid, (_, pack) in packs.items():
        node_str = f"{len(pack['nodes'])}/64"
        print(
            f"{pid:<26} {pack['domain']:<12} {pack['jurisdiction']:<5} "
            f"{node_str:<7} {pack['standard']}"
        )
    print(f"\nTotal validated packs: {len(packs)}")
    return 0


def _print_question_detail(q_diag: Any, q_raw: Dict[str, Any]) -> None:
    print(f"[{q_diag.question_id}] pack={q_diag.pack_id} polarity={q_diag.stem_polarity} "
          f"(inferred={q_diag.inferred_polarity}, cues={q_diag.polarity_cues})")
    print(f"  stem: {q_diag.stem}")
    print(
        f"  answer: predicted={q_diag.predicted_answers} expected={[q_diag.expected_answer]} "
        f"| match={'PASS' if q_diag.answer_exact_match else 'FAIL'}"
    )
    for o_diag, o_raw in zip(q_diag.options, q_raw["options"]):
        trap_match = (
            o_diag.is_trap == bool(o_raw["expected_is_trap"])
            and o_diag.trap_type == str(o_raw["expected_trap_type"])
            and o_diag.actual_status == str(o_raw["expected_status"])
        )
        print(
            f"    ({o_diag.label}) status={o_diag.actual_status:<17} "
            f"reasons=0x{o_diag.reasons:02x} {o_diag.reason_names} "
            f"-> {o_diag.trap_type} [trap={o_diag.is_trap}, expected={trap_match}]"
        )
        if any(o_diag.tripwires.values()) or o_diag.universal_counterexample_applied:
            print(
                f"        tripwires={o_diag.tripwires} "
                f"universal_ce={o_diag.universal_counterexample_applied} "
                f"counterpart_est={o_diag.counterpart_established}"
            )


def _cmd_benchmark(
    modules_root: Path,
    sound_bin: Path,
    schema_hash: int,
    domain: str,
    verbose: bool,
) -> int:
    packs = discover_packs(modules_root)
    bench_paths = discover_benchmarks(modules_root, domain)
    if not bench_paths:
        print(f"ERROR: no benchmark files found for domain={domain!r}", file=sys.stderr)
        return 1

    total_q = 0
    passed_q = 0
    total_opt = 0
    passed_opt = 0

    for bpath in bench_paths:
        bench = json.loads(bpath.read_text(encoding="utf-8"))
        for q in bench["questions"]:
            total_q += 1
            pid = str(q["pack_id"])
            if pid not in packs:
                print(f"ERROR: unknown pack_id {pid!r} in {bpath}", file=sys.stderr)
                return 1
            _, pack = packs[pid]
            q_diag = solve_question(pack, q, sound_bin, schema_hash)

            q_opt_ok = True
            for o_diag, o_raw in zip(q_diag.options, q["options"]):
                total_opt += 1
                ok = (
                    o_diag.is_trap == bool(o_raw["expected_is_trap"])
                    and o_diag.trap_type == str(o_raw["expected_trap_type"])
                    and o_diag.actual_status == str(o_raw["expected_status"])
                )
                if ok:
                    passed_opt += 1
                else:
                    q_opt_ok = False

            q_ok = q_diag.answer_exact_match and q_diag.polarity_consistent and q_opt_ok
            if q_ok:
                passed_q += 1

            if verbose or not q_ok:
                _print_question_detail(q_diag, q)

    print(
        f"[benchmark domain={domain}] {passed_q}/{total_q} questions PASS "
        f"({passed_opt}/{total_opt} options exact trap match)"
    )
    return 0 if (passed_q == total_q and passed_opt == total_opt) else 1


def _cmd_solve(
    modules_root: Path,
    sound_bin: Path,
    schema_hash: int,
    benchmark_path: Path,
    question_id: str,
) -> int:
    packs = discover_packs(modules_root)
    bench = json.loads(benchmark_path.read_text(encoding="utf-8"))
    target_q: Optional[Dict[str, Any]] = None
    for q in bench["questions"]:
        if q["id"] == question_id:
            target_q = q
            break
    if target_q is None:
        print(f"ERROR: question {question_id!r} not found in {benchmark_path}", file=sys.stderr)
        return 1

    pid = str(target_q["pack_id"])
    _, pack = packs[pid]
    q_diag = solve_question(pack, target_q, sound_bin, schema_hash)
    _print_question_detail(q_diag, target_q)
    return 0 if q_diag.answer_exact_match else 1


def main(argv: Optional[List[str]] = None) -> int:
    repo_root = _repo_root()
    modules_root = _modules_root()
    sound_bin = _default_sound_bin(repo_root)
    schema_hash = load_schema_hash(repo_root)

    parser = argparse.ArgumentParser(
        prog="python3 -m modules.common.cli",
        description="SOUND Taxation & Accounting Non-Computation MCQ Trap Detector",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("packs", help="Validate and list declarative Domain Topic Packs")

    p_bench = sub.add_parser("benchmark", help="Run MCQ trap benchmarks against build/sound verify")
    p_bench.add_argument(
        "--domain",
        choices=("all", "taxation", "accounting"),
        default="all",
        help="Domain subset to run",
    )
    p_bench.add_argument(
        "--all",
        action="store_true",
        help="Run all domains (alias for --domain all)",
    )
    p_bench.add_argument("--verbose", action="store_true", help="Print every question diagnosis")

    p_solve = sub.add_parser("solve", help="Inspect detailed diagnosis for one benchmark question")
    p_solve.add_argument("benchmark_path", type=Path, help="Path to benchmark JSON file")
    p_solve.add_argument("question_id", help="Question ID inside the benchmark JSON")

    args = parser.parse_args(argv)
    if args.command == "packs":
        return _cmd_packs(modules_root)
    if args.command == "benchmark":
        domain = "all" if args.all else args.domain
        return _cmd_benchmark(modules_root, sound_bin, schema_hash, domain, args.verbose)
    if args.command == "solve":
        return _cmd_solve(modules_root, sound_bin, schema_hash, args.benchmark_path, args.question_id)
    return 1


if __name__ == "__main__":
    sys.exit(main())
