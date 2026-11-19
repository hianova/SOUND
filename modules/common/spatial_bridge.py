"""Domain-free spatial, physical-invariant, kinematic, and candidate-margin bridge.

Executes zero-dependency pure-Python rational and 2D topological checks:
  - Planar surface depth continuity and foreground near-occlusion detection
  - Fast 2D slot-grid ink density, border intrusion, and cross-slot blob detection
  - Multi-frame rigid-attachment vs drifting-anomaly offset verification
  - Exact rational kinematic stopping-distance envelope check
  - Multi-candidate score margin guard (rejects blind argmax when ambiguous)

Bridges predicates and scalar metrics seamlessly into `Compute Bridge` and
16,384B `"SND2"` `SoundArena` verification (`evaluate_world_governed_arena`).
"""

from __future__ import annotations

import json
from collections import deque
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path
from typing import Any, Deque, Dict, List, Mapping, Optional, Sequence, Set, Tuple

from .arena_codec import (
    FALLBACK_SCHEMA_HASH,
    ArenaEvalResult,
    ArenaNodeSpec,
    encode_arena,
    fnv1a_64,
    verify_arena_bytes,
)
from .compute_bridge import (
    ComputeReport,
    ComputeValue,
    PredicateEval,
    _terminating_decimal_str,
    _to_fraction,
    evaluate_bridged_arena,
    pack_compute_flow_aux,
)


@dataclass(frozen=True)
class PlanarDepthReport:
    """Result of exact rational plane fitting and near-occlusion detection."""

    valid_samples: int
    total_samples: int
    coverage: Fraction
    plane_coeffs: Tuple[Fraction, Fraction, Fraction]  # (a, b, c) for z = a*x + b*y + c
    near_occluder_count: int
    max_abs_residual: Fraction
    plane_consistent: bool
    near_occluded: bool
    undetermined: bool
    reason: str = ""


@dataclass(frozen=True)
class GridOcclusionReport:
    """Result of 2D rectilinear slot-grid intensity and connected-component topology analysis."""

    height: int
    width: int
    num_slots: int
    slot_ink_ratios: List[Fraction]
    occluded_slots: List[int]
    border_breached: bool
    cross_slot_blob: bool
    is_occluded: bool
    is_clean: bool
    undetermined: bool
    reason: str = ""


@dataclass(frozen=True)
class RigidAttachmentReport:
    """Result of multi-frame target motion vs relative anomaly offset jitter analysis."""

    frame_count: int
    present_frames: int
    target_motion: Fraction
    rel_jitter_x: Fraction
    rel_jitter_y: Fraction
    rigidly_attached: bool
    drifting: bool
    undetermined: bool
    reason: str = ""


@dataclass(frozen=True)
class KinematicEnvelopeReport:
    """Result of exact rational stopping distance vs available clearance check."""

    speed: Fraction
    max_decel: Fraction
    distance: Fraction
    reaction_time: Fraction
    stopping_distance: Fraction
    margin: Fraction
    can_stop: bool
    undetermined: bool
    reason: str = ""


@dataclass(frozen=True)
class CandidateMarginReport:
    """Result of format filtering and top-2 candidate confidence margin check."""

    winner_id: Optional[str]
    top_score: Optional[Fraction]
    margin: Optional[Fraction]
    rejected_format_count: int
    status: str  # "established" | "defeated" | "penumbra"
    reason: str = ""


@dataclass(frozen=True)
class SpatialReport:
    """Aggregated physical/spatial findings, numeric metrics, and SOUND predicates."""

    planar_depth: Optional[PlanarDepthReport]
    grid_occlusion: Optional[GridOcclusionReport]
    rigid_attachment: Optional[RigidAttachmentReport]
    kinematic_envelope: Optional[KinematicEnvelopeReport]
    candidate_margin: Optional[CandidateMarginReport]
    flags: Dict[str, Optional[bool]]
    metrics: Dict[str, Fraction]
    predicates: Dict[str, PredicateEval]


@dataclass(frozen=True)
class WorldGovernedResult:
    """Unified 3-layer Spatial -> Compute -> SoundArena verification output."""

    spatial: Optional[SpatialReport]
    compute: Optional[ComputeReport]
    arena_bytes: bytes
    eval_result: ArenaEvalResult
    pre_eval_result: Optional[ArenaEvalResult]
    aux_words: List[int]


def _median_fraction(sorted_vals: Sequence[Fraction]) -> Fraction:
    n = len(sorted_vals)
    if n == 0:
        return Fraction(0, 1)
    mid = n // 2
    if n % 2 == 1:
        return sorted_vals[mid]
    return (sorted_vals[mid - 1] + sorted_vals[mid]) / 2


def _solve_3x3_fraction(
    a_mat: List[List[Fraction]], b_vec: List[Fraction]
) -> Optional[Tuple[Fraction, Fraction, Fraction]]:
    """Solve 3x3 exact rational linear system via Gaussian elimination with partial pivoting."""
    m = [row[:] + [b_vec[r_idx]] for r_idx, row in enumerate(a_mat)]
    for col in range(3):
        piv = max(range(col, 3), key=lambda r: abs(m[r][col]))
        if m[piv][col] == 0:
            return None
        if piv != col:
            m[col], m[piv] = m[piv], m[col]
        piv_val = m[col][col]
        for c in range(col, 4):
            m[col][c] /= piv_val
        for r in range(3):
            if r != col and m[r][col] != 0:
                factor = m[r][col]
                for c in range(col, 4):
                    m[r][c] -= factor * m[col][c]
    return (m[0][3], m[1][3], m[2][3])


def _fit_plane_ols(
    pts: Sequence[Tuple[Fraction, Fraction, Fraction]],
) -> Optional[Tuple[Fraction, Fraction, Fraction]]:
    """Fit exact plane z = a*x + b*y + c on non-collinear (x, y, z) points."""
    if len(pts) < 3:
        return None
    sxx = sxy = sx = syy = sy = szx = szy = sz = Fraction(0, 1)
    n = Fraction(len(pts), 1)
    for x, y, z in pts:
        sxx += x * x
        sxy += x * y
        sx += x
        syy += y * y
        sy += y
        szx += z * x
        szy += z * y
        sz += z
    mat = [
        [sxx, sxy, sx],
        [sxy, syy, sy],
        [sx, sy, n],
    ]
    vec = [szx, szy, sz]
    return _solve_3x3_fraction(mat, vec)


def _eval_plane_fit(
    coeffs: Tuple[Fraction, Fraction, Fraction],
    pts: Sequence[Tuple[Fraction, Fraction, Fraction]],
    near_delta: Fraction,
) -> Tuple[int, Fraction, Tuple[Fraction, Fraction, Fraction]]:
    a, b, c = coeffs
    cur = coeffs
    for _ in range(2):
        bg = [(x, y, z) for x, y, z in pts if z >= (cur[0] * x + cur[1] * y + cur[2] - near_delta)]
        if len(bg) >= 3:
            refit = _fit_plane_ols(bg)
            if refit is not None:
                cur = refit
    a, b, c = cur
    near_cnt = 0
    max_res = Fraction(0, 1)
    for x, y, z in pts:
        z_hat = a * x + b * y + c
        res = abs(z - z_hat)
        if res > max_res:
            max_res = res
        if z < z_hat - near_delta:
            near_cnt += 1
    return near_cnt, max_res, cur


def check_planar_depth(
    samples: Sequence[Any],
    *,
    max_plane_dev: Any = "25",
    near_occlusion_delta: Any = "60",
    min_occluder_samples: int = 1,
    min_coverage: Any = "0.5",
) -> PlanarDepthReport:
    """Verify planar depth continuity vs foreground near-occlusion using exact rational fits."""
    max_dev_f = _to_fraction(max_plane_dev)
    near_delta_f = _to_fraction(near_occlusion_delta)
    min_cov_f = _to_fraction(min_coverage)

    total = len(samples)
    valid: List[Tuple[Fraction, Fraction, Fraction]] = []
    for item in samples:
        if isinstance(item, Mapping):
            rx, ry, rz = item.get("x"), item.get("y"), item.get("z")
        elif isinstance(item, (list, tuple)) and len(item) >= 3:
            rx, ry, rz = item[0], item[1], item[2]
        else:
            raise ValueError(f"Invalid depth sample entry: {item!r}")
        if rx is None or ry is None or rz is None:
            continue
        valid.append((_to_fraction(rx), _to_fraction(ry), _to_fraction(rz)))

    coverage = Fraction(len(valid), total) if total > 0 else Fraction(0, 1)
    if total == 0 or len(valid) < 3 or coverage < min_cov_f:
        return PlanarDepthReport(
            valid_samples=len(valid),
            total_samples=total,
            coverage=coverage,
            plane_coeffs=(Fraction(0), Fraction(0), Fraction(0)),
            near_occluder_count=0,
            max_abs_residual=Fraction(0),
            plane_consistent=False,
            near_occluded=False,
            undetermined=True,
            reason="insufficient_depth_coverage",
        )

    xs = [p[0] for p in valid]
    ys = [p[1] for p in valid]
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)

    by_z_desc = sorted(valid, key=lambda p: p[2], reverse=True)
    upper_half = by_z_desc[: max(3, (len(valid) * 3) // 5)]
    border_pts = [p for p in valid if p[0] in (min_x, max_x) or p[1] in (min_y, max_y)]

    candidate_subsets: List[Sequence[Tuple[Fraction, Fraction, Fraction]]] = [
        upper_half,
        border_pts,
        valid,
    ]
    # Also seed from a well-spread triple of highest-depth perimeter/corner points
    if len(border_pts) >= 3:
        border_desc = sorted(border_pts, key=lambda p: p[2], reverse=True)
        candidate_subsets.insert(2, border_desc[: max(3, (len(border_desc) + 1) // 2)])

    med_z = _median_fraction(sorted(p[2] for p in valid))
    candidates: List[Tuple[Fraction, Fraction, Fraction]] = []
    for sub in candidate_subsets:
        fit = _fit_plane_ols(sub)
        if fit is not None:
            candidates.append(fit)
    candidates.append((Fraction(0), Fraction(0), med_z))

    best_score: Optional[Tuple[int, Fraction, Fraction]] = None
    best_result: Optional[Tuple[int, Fraction, Tuple[Fraction, Fraction, Fraction]]] = None
    for cand in candidates:
        near_cnt, max_res, refined = _eval_plane_fit(cand, valid, near_delta_f)
        a, b, c = refined
        bg_inliers = sum(1 for x, y, z in valid if abs(z - (a * x + b * y + c)) <= max_dev_f)
        non_near = len(valid) - near_cnt
        bg_sum_res = sum(
            abs(z - (a * x + b * y + c))
            for x, y, z in valid
            if z >= (a * x + b * y + c - near_delta_f)
        )
        # Prefer maximum exact plane inliers, then non-near points, then lowest background residual
        key = (-bg_inliers, Fraction(-non_near), bg_sum_res)
        if best_score is None or key < best_score:
            best_score = key
            best_result = (near_cnt, max_res, refined)
        if bg_sum_res == 0 and bg_inliers * 3 >= len(valid) * 2:
            break

    assert best_result is not None
    near_cnt, max_res, coeffs = best_result
    near_occluded = near_cnt >= int(min_occluder_samples)
    plane_consistent = (not near_occluded) and (max_res <= max_dev_f)
    return PlanarDepthReport(
        valid_samples=len(valid),
        total_samples=total,
        coverage=coverage,
        plane_coeffs=coeffs,
        near_occluder_count=near_cnt,
        max_abs_residual=max_res,
        plane_consistent=plane_consistent,
        near_occluded=near_occluded,
        undetermined=False,
    )


def check_grid_occlusion(
    bitmap: Sequence[Sequence[Any]],
    *,
    num_slots: int = 4,
    fg_threshold: Any = 128,
    dark_is_fg: bool = True,
    expected_ink_min: Any = "0.05",
    expected_ink_max: Any = "0.55",
    border_band_px: int = 1,
) -> GridOcclusionReport:
    """Detect slot ink saturation/dropout, border intrusion, and cross-slot blobs on a 2D intensity grid."""
    thresh_f = _to_fraction(fg_threshold)
    ink_min_f = _to_fraction(expected_ink_min)
    ink_max_f = _to_fraction(expected_ink_max)

    h = len(bitmap)
    if h == 0 or num_slots < 1:
        return GridOcclusionReport(
            height=h,
            width=0,
            num_slots=num_slots,
            slot_ink_ratios=[],
            occluded_slots=[],
            border_breached=False,
            cross_slot_blob=False,
            is_occluded=False,
            is_clean=False,
            undetermined=True,
            reason="empty_or_invalid_grid",
        )
    w = len(bitmap[0])
    if w < num_slots or any(len(row) != w for row in bitmap):
        return GridOcclusionReport(
            height=h,
            width=w,
            num_slots=num_slots,
            slot_ink_ratios=[],
            occluded_slots=[],
            border_breached=False,
            cross_slot_blob=False,
            is_occluded=False,
            is_clean=False,
            undetermined=True,
            reason="non_rectangular_or_narrow_grid",
        )

    fg: List[List[bool]] = []
    slot_fg = [0] * num_slots
    slot_tot = [0] * num_slots
    col_slot = [min(num_slots - 1, (c * num_slots) // w) for c in range(w)]

    for r in range(h):
        row_fg: List[bool] = []
        row = bitmap[r]
        for c in range(w):
            raw_px = row[c]
            val = raw_px if (isinstance(raw_px, (int, float)) and not isinstance(raw_px, bool)) else _to_fraction(raw_px)
            is_on = (val < thresh_f) if dark_is_fg else (val > thresh_f)
            row_fg.append(is_on)
            s = col_slot[c]
            slot_tot[s] += 1
            if is_on:
                slot_fg[s] += 1
        fg.append(row_fg)

    ratios = [
        Fraction(slot_fg[s], slot_tot[s]) if slot_tot[s] > 0 else Fraction(0, 1)
        for s in range(num_slots)
    ]
    occluded_slots = [
        s for s, ratio in enumerate(ratios) if ratio > ink_max_f or ratio < ink_min_f
    ]

    visited = [[False] * w for _ in range(h)]
    border_breached = False
    cross_slot_blob = False

    for r0 in range(h):
        for c0 in range(w):
            if not fg[r0][c0] or visited[r0][c0]:
                continue
            comp_size = 0
            touches_border = False
            slots_touched: Set[int] = set()
            q: Deque[Tuple[int, int]] = deque([(r0, c0)])
            visited[r0][c0] = True
            while q:
                r, c = q.popleft()
                comp_size += 1
                slots_touched.add(col_slot[c])
                if border_band_px > 0 and (
                    r < border_band_px
                    or r >= h - border_band_px
                    or c < border_band_px
                    or c >= w - border_band_px
                ):
                    touches_border = True
                for dr, dc in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    nr, nc = r + dr, c + dc
                    if 0 <= nr < h and 0 <= nc < w and fg[nr][nc] and not visited[nr][nc]:
                        visited[nr][nc] = True
                        q.append((nr, nc))
            if comp_size >= 3 and touches_border:
                border_breached = True
            if len(slots_touched) >= 2:
                cross_slot_blob = True

    is_occluded = bool(occluded_slots) or border_breached or cross_slot_blob
    return GridOcclusionReport(
        height=h,
        width=w,
        num_slots=num_slots,
        slot_ink_ratios=ratios,
        occluded_slots=occluded_slots,
        border_breached=border_breached,
        cross_slot_blob=cross_slot_blob,
        is_occluded=is_occluded,
        is_clean=not is_occluded,
        undetermined=False,
    )


def _box_center_and_size(box: Sequence[Any]) -> Tuple[Fraction, Fraction, Fraction, Fraction]:
    if len(box) != 4:
        raise ValueError(f"Bounding box must have 4 elements [x0, y0, x1, y1], got {box!r}")
    x0, y0, x1, y1 = (_to_fraction(v) for v in box)
    return (x0 + x1) / 2, (y0 + y1) / 2, x1 - x0, y1 - y0


def check_rigid_attachment(
    frames: Sequence[Mapping[str, Any]],
    *,
    max_rel_jitter: Any = "0.05",
    min_frames: int = 3,
    min_target_motion: Any = "0",
) -> RigidAttachmentReport:
    """Verify whether an anomaly bounding box moves rigidly with a target box across frames."""
    max_jitter_f = _to_fraction(max_rel_jitter)
    min_motion_f = _to_fraction(min_target_motion)
    n = len(frames)
    if n < min_frames:
        return RigidAttachmentReport(
            frame_count=n,
            present_frames=0,
            target_motion=Fraction(0),
            rel_jitter_x=Fraction(0),
            rel_jitter_y=Fraction(0),
            rigidly_attached=False,
            drifting=False,
            undetermined=True,
            reason="insufficient_frames",
        )

    t_centers: List[Tuple[Fraction, Fraction]] = []
    rel_xs: List[Fraction] = []
    rel_ys: List[Fraction] = []

    for frame in frames:
        tbox = frame.get("target_box")
        if tbox is None:
            return RigidAttachmentReport(
                frame_count=n,
                present_frames=len(rel_xs),
                target_motion=Fraction(0),
                rel_jitter_x=Fraction(0),
                rel_jitter_y=Fraction(0),
                rigidly_attached=False,
                drifting=False,
                undetermined=True,
                reason="missing_target_box",
            )
        tx, ty, tw, th = _box_center_and_size(tbox)
        if tw <= 0 or th <= 0:
            return RigidAttachmentReport(
                frame_count=n,
                present_frames=len(rel_xs),
                target_motion=Fraction(0),
                rel_jitter_x=Fraction(0),
                rel_jitter_y=Fraction(0),
                rigidly_attached=False,
                drifting=False,
                undetermined=True,
                reason="degenerate_target_box",
            )
        t_centers.append((tx, ty))
        abox = frame.get("anomaly_box")
        if abox is not None:
            ax, ay, _, _ = _box_center_and_size(abox)
            rel_xs.append((ax - tx) / tw)
            rel_ys.append((ay - ty) / th)

    t0x, t0y = t_centers[0]
    target_motion = max(abs(cx - t0x) + abs(cy - t0y) for cx, cy in t_centers)
    present = len(rel_xs)

    if present < min_frames:
        return RigidAttachmentReport(
            frame_count=n,
            present_frames=present,
            target_motion=target_motion,
            rel_jitter_x=Fraction(0),
            rel_jitter_y=Fraction(0),
            rigidly_attached=False,
            drifting=(present > 0),
            undetermined=False,
        )

    jx = max(rel_xs) - min(rel_xs)
    jy = max(rel_ys) - min(rel_ys)
    rigidly_attached = (target_motion >= min_motion_f) and (jx <= max_jitter_f) and (jy <= max_jitter_f)
    drifting = (not rigidly_attached) and (present > 0)
    return RigidAttachmentReport(
        frame_count=n,
        present_frames=present,
        target_motion=target_motion,
        rel_jitter_x=jx,
        rel_jitter_y=jy,
        rigidly_attached=rigidly_attached,
        drifting=drifting,
        undetermined=False,
    )


def check_kinematic_envelope(
    speed: Any,
    max_decel: Any,
    distance: Any,
    *,
    reaction_time: Any = 0,
) -> KinematicEnvelopeReport:
    """Compute exact stopping distance v*t_r + v^2/(2*a) and clearance margin."""
    v = _to_fraction(speed)
    a = _to_fraction(max_decel)
    d = _to_fraction(distance)
    tr = _to_fraction(reaction_time)
    if v < 0 or a <= 0 or d < 0 or tr < 0:
        return KinematicEnvelopeReport(
            speed=v,
            max_decel=a,
            distance=d,
            reaction_time=tr,
            stopping_distance=Fraction(0),
            margin=Fraction(0),
            can_stop=False,
            undetermined=True,
            reason="invalid_kinematic_parameters",
        )
    d_stop = v * tr + (v * v) / (2 * a)
    margin = d - d_stop
    return KinematicEnvelopeReport(
        speed=v,
        max_decel=a,
        distance=d,
        reaction_time=tr,
        stopping_distance=d_stop,
        margin=margin,
        can_stop=(d_stop <= d),
        undetermined=False,
    )


def check_candidate_margin(
    candidates: Sequence[Mapping[str, Any]],
    *,
    min_top_score: Any = "0.6",
    min_margin: Any = "0.15",
) -> CandidateMarginReport:
    """Enforce format constraint and minimum top-2 margin to block blind argmax selection."""
    min_score_f = _to_fraction(min_top_score)
    min_margin_f = _to_fraction(min_margin)

    valid: List[Tuple[Fraction, str]] = []
    rejected = 0
    for item in candidates:
        cid = str(item.get("id", ""))
        fmt_ok = bool(item.get("format_valid", False))
        if not fmt_ok:
            rejected += 1
            continue
        score_raw = item.get("score")
        if score_raw is None:
            continue
        valid.append((_to_fraction(score_raw), cid))

    valid.sort(key=lambda pair: (-pair[0], pair[1]))
    if not valid:
        return CandidateMarginReport(
            winner_id=None,
            top_score=None,
            margin=None,
            rejected_format_count=rejected,
            status="defeated",
            reason="no_valid_candidate",
        )

    top_score, top_id = valid[0]
    margin = (top_score - valid[1][0]) if len(valid) >= 2 else top_score
    if top_score < min_score_f:
        return CandidateMarginReport(
            winner_id=None,
            top_score=top_score,
            margin=margin,
            rejected_format_count=rejected,
            status="defeated",
            reason="top_score_below_threshold",
        )
    if len(valid) >= 2 and margin < min_margin_f:
        return CandidateMarginReport(
            winner_id=None,
            top_score=top_score,
            margin=margin,
            rejected_format_count=rejected,
            status="penumbra",
            reason="ambiguous_top2_margin",
        )
    return CandidateMarginReport(
        winner_id=top_id,
        top_score=top_score,
        margin=margin,
        rejected_format_count=rejected,
        status="established",
    )


def evaluate_spatial_spec(spec: Mapping[str, Any]) -> SpatialReport:
    """Evaluate all spatial/physical sub-specs present in `spec` and bind predicates."""
    planar: Optional[PlanarDepthReport] = None
    grid: Optional[GridOcclusionReport] = None
    rigid: Optional[RigidAttachmentReport] = None
    kinematic: Optional[KinematicEnvelopeReport] = None
    cand: Optional[CandidateMarginReport] = None

    flags: Dict[str, Optional[bool]] = {}
    metrics: Dict[str, Fraction] = {}

    if "planar_depth" in spec and spec["planar_depth"] is not None:
        pd = spec["planar_depth"]
        planar = check_planar_depth(
            pd.get("samples", []),
            max_plane_dev=pd.get("max_plane_dev", "25"),
            near_occlusion_delta=pd.get("near_occlusion_delta", "60"),
            min_occluder_samples=int(pd.get("min_occluder_samples", 1)),
            min_coverage=pd.get("min_coverage", "0.5"),
        )
        if planar.undetermined:
            flags["plane_consistent"] = None
            flags["near_occluded"] = None
        else:
            flags["plane_consistent"] = planar.plane_consistent
            flags["near_occluded"] = planar.near_occluded
        metrics["near_occluder_count"] = Fraction(planar.near_occluder_count, 1)
        metrics["max_abs_residual"] = planar.max_abs_residual
        metrics["depth_coverage"] = planar.coverage

    if "grid_occlusion" in spec and spec["grid_occlusion"] is not None:
        go = spec["grid_occlusion"]
        grid = check_grid_occlusion(
            go.get("bitmap", []),
            num_slots=int(go.get("num_slots", 4)),
            fg_threshold=go.get("fg_threshold", 128),
            dark_is_fg=bool(go.get("dark_is_fg", True)),
            expected_ink_min=go.get("expected_ink_min", "0.05"),
            expected_ink_max=go.get("expected_ink_max", "0.55"),
            border_band_px=int(go.get("border_band_px", 1)),
        )
        if grid.undetermined:
            flags["grid_occluded"] = None
            flags["grid_clean"] = None
            flags["border_breached"] = None
            flags["cross_slot_blob"] = None
        else:
            flags["grid_occluded"] = grid.is_occluded
            flags["grid_clean"] = grid.is_clean
            flags["border_breached"] = grid.border_breached
            flags["cross_slot_blob"] = grid.cross_slot_blob
        metrics["occluded_slot_count"] = Fraction(len(grid.occluded_slots), 1)

    if "rigid_attachment" in spec and spec["rigid_attachment"] is not None:
        ra = spec["rigid_attachment"]
        rigid = check_rigid_attachment(
            ra.get("frames", []),
            max_rel_jitter=ra.get("max_rel_jitter", "0.05"),
            min_frames=int(ra.get("min_frames", 3)),
            min_target_motion=ra.get("min_target_motion", "0"),
        )
        if rigid.undetermined:
            flags["rigidly_attached"] = None
            flags["drifting"] = None
        else:
            flags["rigidly_attached"] = rigid.rigidly_attached
            flags["drifting"] = rigid.drifting
        metrics["target_motion"] = rigid.target_motion
        metrics["rel_jitter_x"] = rigid.rel_jitter_x
        metrics["rel_jitter_y"] = rigid.rel_jitter_y

    if "kinematic_envelope" in spec and spec["kinematic_envelope"] is not None:
        ke = spec["kinematic_envelope"]
        kinematic = check_kinematic_envelope(
            ke.get("speed", 0),
            ke.get("max_decel", 1),
            ke.get("distance", 0),
            reaction_time=ke.get("reaction_time", 0),
        )
        if kinematic.undetermined:
            flags["can_stop"] = None
            flags["cannot_stop"] = None
        else:
            flags["can_stop"] = kinematic.can_stop
            flags["cannot_stop"] = not kinematic.can_stop
        metrics["stopping_distance"] = kinematic.stopping_distance
        metrics["kinematic_margin"] = kinematic.margin

    if "candidate_margin" in spec and spec["candidate_margin"] is not None:
        cm = spec["candidate_margin"]
        cand = check_candidate_margin(
            cm.get("candidates", []),
            min_top_score=cm.get("min_top_score", "0.6"),
            min_margin=cm.get("min_margin", "0.15"),
        )
        if cand.status == "established":
            flags["candidate_clear"] = True
            flags["candidate_ambiguous"] = False
        elif cand.status == "defeated":
            flags["candidate_clear"] = False
            flags["candidate_ambiguous"] = False
        else:
            flags["candidate_clear"] = None
            flags["candidate_ambiguous"] = True
        if cand.top_score is not None:
            metrics["top_score"] = cand.top_score
        if cand.margin is not None:
            metrics["score_margin"] = cand.margin

    # Combined convenience flags if both planar and grid checks exist
    if "grid_occluded" in flags or "near_occluded" in flags:
        sub_vals = [flags[k] for k in ("grid_occluded", "near_occluded") if k in flags]
        if any(v is True for v in sub_vals):
            flags["any_occluded"] = True
            flags["all_clear"] = False
        elif any(v is None for v in sub_vals):
            flags["any_occluded"] = None
            flags["all_clear"] = None
        else:
            flags["any_occluded"] = False
            flags["all_clear"] = True

    predicates: Dict[str, PredicateEval] = {}
    bindings = spec.get("predicate_bindings", {})
    for node_label, flag_name in bindings.items():
        nlabel = str(node_label)
        fname = str(flag_name)
        if fname not in flags:
            predicates[nlabel] = PredicateEval(
                name=nlabel,
                status="penumbra",
                truth=None,
                reason=f"missing_flag:{fname}",
            )
            continue
        fval = flags[fname]
        if fval is True:
            predicates[nlabel] = PredicateEval(name=nlabel, status="established", truth=True)
        elif fval is False:
            predicates[nlabel] = PredicateEval(name=nlabel, status="defeated", truth=False)
        else:
            predicates[nlabel] = PredicateEval(
                name=nlabel, status="penumbra", truth=None, reason=f"undetermined:{fname}"
            )

    return SpatialReport(
        planar_depth=planar,
        grid_occlusion=grid,
        rigid_attachment=rigid,
        kinematic_envelope=kinematic,
        candidate_margin=cand,
        flags=flags,
        metrics=metrics,
        predicates=predicates,
    )


def _apply_predicates_to_nodes(
    base_nodes: Sequence[ArenaNodeSpec],
    predicates: Mapping[str, PredicateEval],
) -> List[ArenaNodeSpec]:
    out: List[ArenaNodeSpec] = []
    for nd in base_nodes:
        pred = predicates.get(nd.label)
        if pred is None:
            out.append(nd)
            continue
        if pred.truth is True:
            clauses = [0]
            pen = False
        elif pred.truth is False:
            clauses = []
            pen = False
        else:
            clauses = [0]
            pen = True
        out.append(
            ArenaNodeSpec(
                label=nd.label,
                sort=nd.sort,
                evid="direct",
                aspect=nd.aspect,
                constitutive=nd.constitutive,
                revoked=nd.revoked,
                penumbra=pen,
                presup_mask=nd.presup_mask,
                attack_mask=nd.attack_mask,
                clauses=clauses,
                span_start=nd.span_start,
                span_end=nd.span_end,
            )
        )
    return out


def evaluate_world_governed_arena(
    base_nodes: Sequence[ArenaNodeSpec],
    *,
    spatial_spec: Optional[Mapping[str, Any]] = None,
    compute_spec: Optional[Mapping[str, Any]] = None,
    sound_bin: Path,
    schema_hash: int = FALLBACK_SCHEMA_HASH,
    backend: str = "auto",
    power_mask: int = 0,
    immunity_mask: int = 0,
    duty_pos_mask: int = 0,
    duty_neg_mask: int = 0,
    voidable_mask: int = 0,
    rescission_power_mask: int = 0,
) -> WorldGovernedResult:
    """Compose Spatial invariants -> Compute Bridge -> 16KB SoundArena Ring-0 evaluation."""
    spatial_rep: Optional[SpatialReport] = None
    working_nodes = list(base_nodes)

    if spatial_spec is not None:
        spatial_rep = evaluate_spatial_spec(spatial_spec)
        working_nodes = _apply_predicates_to_nodes(working_nodes, spatial_rep.predicates)

    if compute_spec is not None:
        merged_vars = dict(compute_spec.get("vars", {}))
        if spatial_rep is not None:
            for k, val in spatial_rep.metrics.items():
                if k not in merged_vars:
                    merged_vars[k] = val
        merged_spec = dict(compute_spec)
        merged_spec["vars"] = merged_vars
        bridged = evaluate_bridged_arena(
            working_nodes,
            merged_spec,
            sound_bin,
            schema_hash=schema_hash,
            backend=backend,
            power_mask=power_mask,
            immunity_mask=immunity_mask,
            duty_pos_mask=duty_pos_mask,
            duty_neg_mask=duty_neg_mask,
            voidable_mask=voidable_mask,
            rescission_power_mask=rescission_power_mask,
        )
        return WorldGovernedResult(
            spatial=spatial_rep,
            compute=bridged.compute,
            arena_bytes=bridged.arena_bytes,
            eval_result=bridged.eval_result,
            pre_eval_result=bridged.pre_eval_result,
            aux_words=bridged.aux_words,
        )

    # Spatial-only path: pack spatial metrics into ComputeReport for uniform flow_aux sealing
    aux_values: Dict[str, ComputeValue] = {}
    aux_preds: Dict[str, PredicateEval] = {}
    aux_scale = int(spatial_spec.get("aux_scale", 4)) if spatial_spec is not None else 4
    if spatial_rep is not None:
        for k, v in spatial_rep.metrics.items():
            aux_values[k] = ComputeValue(
                name=k,
                status="ok",
                value=v,
                vector=None,
                decimal_str=_terminating_decimal_str(v),
            )
        aux_preds = dict(spatial_rep.predicates)

    digest_bytes = json.dumps(spatial_spec or {}, sort_keys=True, separators=(",", ":"), default=str).encode("utf-8")
    min_report = ComputeReport(
        values=aux_values,
        predicates=aux_preds,
        aux_scale=aux_scale,
        spec_digest=fnv1a_64(digest_bytes),
    )
    aux_words = pack_compute_flow_aux(min_report)
    arena_bytes = encode_arena(
        working_nodes,
        schema_hash=schema_hash,
        power_mask=power_mask,
        immunity_mask=immunity_mask,
        duty_pos_mask=duty_pos_mask,
        duty_neg_mask=duty_neg_mask,
        voidable_mask=voidable_mask,
        rescission_power_mask=rescission_power_mask,
        flow_aux_words=aux_words,
    )
    eval_res = verify_arena_bytes(arena_bytes, sound_bin, backend=backend)
    return WorldGovernedResult(
        spatial=spatial_rep,
        compute=None,
        arena_bytes=arena_bytes,
        eval_result=eval_res,
        pre_eval_result=None,
        aux_words=aux_words,
    )


def spatial_report_to_json_dict(report: SpatialReport) -> Dict[str, Any]:
    """Serialize `SpatialReport` to compact deterministic JSON."""
    out: Dict[str, Any] = {
        "flags": dict(report.flags),
        "metrics": {k: _terminating_decimal_str(v) for k, v in report.metrics.items()},
        "predicates": {
            k: {
                "status": pv.status,
                "truth": pv.truth,
                "reason": pv.reason,
            }
            for k, pv in report.predicates.items()
        },
    }
    if report.planar_depth is not None:
        out["planar_depth"] = {
            "valid_samples": report.planar_depth.valid_samples,
            "total_samples": report.planar_depth.total_samples,
            "coverage": _terminating_decimal_str(report.planar_depth.coverage),
            "plane_coeffs": [_terminating_decimal_str(c) for c in report.planar_depth.plane_coeffs],
            "near_occluder_count": report.planar_depth.near_occluder_count,
            "max_abs_residual": _terminating_decimal_str(report.planar_depth.max_abs_residual),
            "plane_consistent": report.planar_depth.plane_consistent,
            "near_occluded": report.planar_depth.near_occluded,
            "undetermined": report.planar_depth.undetermined,
        }
    if report.grid_occlusion is not None:
        out["grid_occlusion"] = {
            "height": report.grid_occlusion.height,
            "width": report.grid_occlusion.width,
            "num_slots": report.grid_occlusion.num_slots,
            "slot_ink_ratios": [
                _terminating_decimal_str(r) for r in report.grid_occlusion.slot_ink_ratios
            ],
            "occluded_slots": report.grid_occlusion.occluded_slots,
            "border_breached": report.grid_occlusion.border_breached,
            "cross_slot_blob": report.grid_occlusion.cross_slot_blob,
            "is_occluded": report.grid_occlusion.is_occluded,
            "is_clean": report.grid_occlusion.is_clean,
            "undetermined": report.grid_occlusion.undetermined,
        }
    if report.rigid_attachment is not None:
        out["rigid_attachment"] = {
            "frame_count": report.rigid_attachment.frame_count,
            "present_frames": report.rigid_attachment.present_frames,
            "target_motion": _terminating_decimal_str(report.rigid_attachment.target_motion),
            "rel_jitter_x": _terminating_decimal_str(report.rigid_attachment.rel_jitter_x),
            "rel_jitter_y": _terminating_decimal_str(report.rigid_attachment.rel_jitter_y),
            "rigidly_attached": report.rigid_attachment.rigidly_attached,
            "drifting": report.rigid_attachment.drifting,
            "undetermined": report.rigid_attachment.undetermined,
        }
    if report.kinematic_envelope is not None:
        out["kinematic_envelope"] = {
            "stopping_distance": _terminating_decimal_str(report.kinematic_envelope.stopping_distance),
            "margin": _terminating_decimal_str(report.kinematic_envelope.margin),
            "can_stop": report.kinematic_envelope.can_stop,
            "undetermined": report.kinematic_envelope.undetermined,
        }
    if report.candidate_margin is not None:
        out["candidate_margin"] = {
            "winner_id": report.candidate_margin.winner_id,
            "top_score": (
                _terminating_decimal_str(report.candidate_margin.top_score)
                if report.candidate_margin.top_score is not None
                else None
            ),
            "margin": (
                _terminating_decimal_str(report.candidate_margin.margin)
                if report.candidate_margin.margin is not None
                else None
            ),
            "rejected_format_count": report.candidate_margin.rejected_format_count,
            "status": report.candidate_margin.status,
        }
    return out
