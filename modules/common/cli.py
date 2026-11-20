"""Domain-free CLI runner for SOUND Compute Bridge and Spatial Bridge."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

from .compute_bridge import (
    compile_compute_to_fir1,
    emit_lean4_certificate,
    evaluate_compute_spec,
    pack_compute_flow_aux,
    report_to_json_dict,
)
from .spatial_bridge import (
    evaluate_spatial_spec,
    spatial_report_to_json_dict,
)


def _cmd_compute(
    spec_path: str,
    emit_fir: Optional[Path],
    emit_lean: Optional[Path],
) -> int:
    raw_text = sys.stdin.read() if spec_path == "-" else Path(spec_path).read_text(encoding="utf-8")
    spec = json.loads(raw_text)
    report = evaluate_compute_spec(spec)
    aux_words = pack_compute_flow_aux(report)
    out: Dict[str, Any] = report_to_json_dict(report)
    out["aux_words_used"] = sum(1 for w in aux_words if w != 0)

    if emit_fir is not None:
        fir_bytes, in_names, _ = compile_compute_to_fir1(spec)
        emit_fir.parent.mkdir(parents=True, exist_ok=True)
        emit_fir.write_bytes(fir_bytes)
        out["fir_path"] = str(emit_fir)
        out["fir_bytes"] = len(fir_bytes)
        out["fir_inputs"] = in_names

    if emit_lean is not None:
        lean_src = emit_lean4_certificate(spec, report)
        emit_lean.parent.mkdir(parents=True, exist_ok=True)
        emit_lean.write_text(lean_src, encoding="utf-8")
        out["lean_path"] = str(emit_lean)

    print(json.dumps(out, indent=2, ensure_ascii=False))
    return 0


def _cmd_spatial(spec_path: str) -> int:
    raw_text = sys.stdin.read() if spec_path == "-" else Path(spec_path).read_text(encoding="utf-8")
    spec = json.loads(raw_text)
    report = evaluate_spatial_spec(spec)
    print(json.dumps(spatial_report_to_json_dict(report), indent=2, ensure_ascii=False))
    return 0


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python3 -m modules.common.cli",
        description="Domain-free SOUND Quantitative Compute & Spatial Bridge CLI",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p_comp = sub.add_parser("compute", help="Evaluate domain-free quantitative Compute Bridge JSON spec")
    p_comp.add_argument("spec_path", help="Path to JSON spec file or '-' for stdin")
    p_comp.add_argument("--emit-fir", type=Path, default=None, help="Optional output path for lowered FIR1 circuit")
    p_comp.add_argument("--emit-lean", type=Path, default=None, help="Optional output path for Lean 4 proof certificate")

    p_spat = sub.add_parser("spatial", help="Evaluate domain-free Spatial Bridge JSON spec")
    p_spat.add_argument("spec_path", help="Path to JSON spec file or '-' for stdin")

    args = parser.parse_args(argv)
    if args.command == "compute":
        return _cmd_compute(args.spec_path, args.emit_fir, args.emit_lean)
    if args.command == "spatial":
        return _cmd_spatial(args.spec_path)
    return 1


if __name__ == "__main__":
    sys.exit(main())
