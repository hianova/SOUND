"""Domain-free exact quantitative compute bridge for SOUND, FLOW FIR1, and Lean 4.

Evaluates compact JSON expression DAGs using exact rational arithmetic (`Fraction`),
bounded iteration (`recur_fold`), mass-conserving allocation (`pro_rata`), linear
equation solving (`solve_linear`), and logical predicates without domain-specific logic.

Supports bidirectional integration with 16,384B `"SND2"` SoundArena containers:
  1. Normative -> Quantitative: SoundArena node statuses (`established`, `defeated`,
     `penumbraOpen`, `conflictUndecided`) gate guarded numeric branches.
  2. Quantitative -> Normative: Predicates populate `fact_slot` nodes (true -> `direct`,
     false -> empty clause `defeated`, undetermined/div-zero -> `penumbra=True`) and
     seal scaled `int64` audit values into `flow_aux[38][8]` under FNV-1a Merkle lock.
  3. Integer-scalar subsets lower directly to linear `FLOW` `"FIR1"` circuits and
     zero-import Lean 4 rational `decide` proof certificates.
"""

from __future__ import annotations

import hashlib
import json
import math
import struct
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from fractions import Fraction
from pathlib import Path
from typing import Any, Dict, List, Mapping, Optional, Sequence, Set, Tuple, Union

from .arena_codec import (
    FALLBACK_SCHEMA_HASH,
    SOUND_FLOW_AUX_WORDS,
    ArenaEvalResult,
    ArenaNodeSpec,
    encode_arena,
    fnv1a_64,
    read_flow_aux_words,
    verify_arena_bytes,
)

COMPUTE_AUX_MAGIC = 0x31305855415F4243  # "CB_AUX01" little-endian
COMPUTE_AUX_HDR_WORDS = 8
COMPUTE_AUX_ENTRY_WORDS = 3
COMPUTE_AUX_MAX_ENTRIES = (SOUND_FLOW_AUX_WORDS - COMPUTE_AUX_HDR_WORDS) // COMPUTE_AUX_ENTRY_WORDS

_STATUS_CODE_TO_STR: Dict[int, str] = {
    0: "ok",
    1: "gated_inactive",
    2: "undetermined",
}
_STATUS_STR_TO_CODE: Dict[str, int] = {v: k for k, v in _STATUS_CODE_TO_STR.items()}

_FLOW_IR_MAGIC = 0x31524946  # "FIR1"
_FLOW_IR_VERSION = 1
_FLOW_IR_MULT_LINEAR = 1
_FLOW_IR_FLAG_CLEAN = 1

_OP_CONST_U64 = 1
_OP_SLICE = 2
_OP_XOR_VEC = 4
_OP_AND_VEC = 5
_OP_ADD = 8
_OP_SUB = 9
_OP_MUL_FXP = 10
_OP_CTRL_SEL = 15
_OP_EQ_ZERO = 18

_MAX_RECURRENCE_STEPS = 512
_MAX_EXPONENT = 256
_I64_MIN = -(1 << 63)
_I64_MAX = (1 << 63) - 1

ValueOrVec = Union[Fraction, List[Fraction]]


@dataclass(frozen=True)
class ComputeValue:
    """Evaluated scalar or vector result of one named expression."""

    name: str
    status: str  # "ok" | "gated_inactive" | "undetermined"
    value: Optional[Fraction]
    vector: Optional[List[Fraction]]
    decimal_str: Optional[str]
    reason: str = ""


@dataclass(frozen=True)
class PredicateEval:
    """Three-valued outcome of a predicate (`established` / `defeated` / `penumbra`)."""

    name: str
    status: str  # "established" | "defeated" | "penumbra"
    truth: Optional[bool]
    reason: str = ""


@dataclass(frozen=True)
class ComputeReport:
    """Complete quantitative evaluation report for a compute specification."""

    values: Dict[str, ComputeValue]
    predicates: Dict[str, PredicateEval]
    aux_scale: int
    spec_digest: int


@dataclass(frozen=True)
class BridgedArenaResult:
    """Combined result of bidirectional compute <-> SoundArena verification."""

    compute: ComputeReport
    arena_bytes: bytes
    eval_result: ArenaEvalResult
    pre_eval_result: Optional[ArenaEvalResult]
    aux_words: List[int]


def _to_fraction(raw: Any) -> Fraction:
    """Parse exact scalar number (int, Fraction, Decimal, decimal/rational str, finite float)."""
    if isinstance(raw, bool):
        raise ValueError(f"Boolean value cannot be coerced to rational number: {raw!r}")
    if isinstance(raw, Fraction):
        return raw
    if isinstance(raw, int):
        return Fraction(raw, 1)
    if isinstance(raw, Decimal):
        if not raw.is_finite():
            raise ValueError(f"Non-finite Decimal: {raw!r}")
        return Fraction(raw)
    if isinstance(raw, float):
        if not math.isfinite(raw):
            raise ValueError(f"Non-finite float: {raw!r}")
        return Fraction(str(raw))
    if isinstance(raw, str):
        text = raw.strip()
        if not text:
            raise ValueError("Empty numeric string")
        if "/" in text:
            return Fraction(text)
        try:
            dec = Decimal(text)
        except InvalidOperation as exc:
            raise ValueError(f"Invalid numeric literal {raw!r}") from exc
        if not dec.is_finite():
            raise ValueError(f"Non-finite numeric literal {raw!r}")
        return Fraction(dec)
    raise ValueError(f"Unsupported scalar numeric value {raw!r}")


def _terminating_decimal_str(val: Fraction, max_fractional_digits: int = 12) -> str:
    """Format exact rational; use plain decimal when denominator only has factors 2 and 5."""
    num, den = val.numerator, val.denominator
    if den == 1:
        return str(num)
    rem = den
    p2 = 0
    while rem % 2 == 0:
        rem //= 2
        p2 += 1
    p5 = 0
    while rem % 5 == 0:
        rem //= 5
        p5 += 1
    if rem == 1:
        digits = max(p2, p5)
        if digits <= max_fractional_digits:
            sign = "-" if num < 0 else ""
            scaled = abs(num) * (10 ** digits) // den
            s = str(scaled).rjust(digits + 1, "0")
            whole = s[:-digits]
            frac = s[-digits:].rstrip("0")
            if not frac:
                return f"{sign}{whole}"
            return f"{sign}{whole}.{frac}"
    return f"{num}/{den}"


def _format_output_str(val: Optional[Fraction], vec: Optional[List[Fraction]]) -> Optional[str]:
    if val is not None:
        return _terminating_decimal_str(val)
    if vec is not None:
        return "[" + ", ".join(_terminating_decimal_str(item) for item in vec) + "]"
    return None


def round_half_up(x: Fraction, decimals: int = 0) -> Fraction:
    """Exact rational half-away-from-zero rounding to `decimals` digits."""
    factor = Fraction(10, 1) ** int(decimals)
    y = x * factor
    sign = -1 if y < 0 else 1
    ay = abs(y)
    q, r = divmod(ay.numerator, ay.denominator)
    if 2 * r >= ay.denominator:
        q += 1
    return Fraction(sign * q, 1) / factor


def round_half_even(x: Fraction, decimals: int = 0) -> Fraction:
    """Exact rational round-half-to-even (banker's rounding) to `decimals` digits."""
    factor = Fraction(10, 1) ** int(decimals)
    y = x * factor
    sign = -1 if y < 0 else 1
    ay = abs(y)
    q, r = divmod(ay.numerator, ay.denominator)
    two_r = 2 * r
    if two_r > ay.denominator or (two_r == ay.denominator and (q & 1) == 1):
        q += 1
    return Fraction(sign * q, 1) / factor


def _round_with_mode(x: Fraction, decimals: int, mode: str) -> Fraction:
    if mode == "half_up":
        return round_half_up(x, decimals)
    if mode == "half_even":
        return round_half_even(x, decimals)
    raise ValueError(f"Unsupported rounding mode {mode!r}; expected 'half_up' or 'half_even'")


class _UndeterminedError(Exception):
    """Internal control signal for missing variables, div-by-zero, or uncertain guards."""

    def __init__(self, reason: str) -> None:
        super().__init__(reason)
        self.reason = reason


def _is_scalar(val: ValueOrVec) -> bool:
    return isinstance(val, Fraction)


def _as_scalar(val: ValueOrVec, ctx: str) -> Fraction:
    if not isinstance(val, Fraction):
        raise ValueError(f"{ctx} expected scalar rational, got vector")
    return val


def _as_vector(val: ValueOrVec, ctx: str) -> List[Fraction]:
    if not isinstance(val, list):
        raise ValueError(f"{ctx} expected vector, got scalar")
    return val


def _as_int(val: ValueOrVec, ctx: str) -> int:
    sc = _as_scalar(val, ctx)
    if sc.denominator != 1:
        raise ValueError(f"{ctx} expected exact integer, got {sc}")
    return int(sc.numerator)


def _eval_bool_node(
    node: Any,
    env: Mapping[str, Optional[ValueOrVec]],
) -> bool:
    """Evaluate a boolean condition or predicate AST node; raises `_UndeterminedError` when uncertain."""
    if isinstance(node, bool):
        return node
    if not isinstance(node, list) or not node:
        raise ValueError(f"Invalid predicate/boolean AST node: {node!r}")
    op = node[0]
    args = node[1:]

    if op == "not":
        if len(args) != 1:
            raise ValueError("'not' expects 1 argument")
        return not _eval_bool_node(args[0], env)
    if op == "and":
        if not args:
            raise ValueError("'and' expects at least 1 argument")
        # Three-valued weak Kleene: undetermined anywhere propagates if result is unknown,
        # short-circuiting False matches strong Kleene.
        seen_undetermined: Optional[_UndeterminedError] = None
        for item in args:
            try:
                if not _eval_bool_node(item, env):
                    return False
            except _UndeterminedError as exc:
                if seen_undetermined is None:
                    seen_undetermined = exc
        if seen_undetermined is not None:
            raise seen_undetermined
        return True
    if op == "or":
        if not args:
            raise ValueError("'or' expects at least 1 argument")
        seen_undetermined = None
        for item in args:
            try:
                if _eval_bool_node(item, env):
                    return True
            except _UndeterminedError as exc:
                if seen_undetermined is None:
                    seen_undetermined = exc
        if seen_undetermined is not None:
            raise seen_undetermined
        return False

    if op in ("eq", "ne", "lt", "le", "gt", "ge"):
        if len(args) != 2:
            raise ValueError(f"{op!r} expects 2 arguments")
        lhs = _eval_expr_node(args[0], env)
        rhs = _eval_expr_node(args[1], env)
        if op in ("eq", "ne"):
            same = lhs == rhs
            return same if op == "eq" else not same
        a = _as_scalar(lhs, op)
        b = _as_scalar(rhs, op)
        if op == "lt":
            return a < b
        if op == "le":
            return a <= b
        if op == "gt":
            return a > b
        return a >= b

    if op == "approx_eq":
        if len(args) != 3:
            raise ValueError("'approx_eq' expects 3 arguments: a, b, tol")
        a = _as_scalar(_eval_expr_node(args[0], env), op)
        b = _as_scalar(_eval_expr_node(args[1], env), op)
        tol = _as_scalar(_eval_expr_node(args[2], env), op)
        if tol < 0:
            raise ValueError("approx_eq tolerance must be non-negative")
        return abs(a - b) <= tol

    if op == "in_range":
        if len(args) != 3:
            raise ValueError("'in_range' expects 3 arguments: x, lo, hi")
        x = _as_scalar(_eval_expr_node(args[0], env), op)
        lo = _as_scalar(_eval_expr_node(args[1], env), op)
        hi = _as_scalar(_eval_expr_node(args[2], env), op)
        return lo <= x <= hi

    if op == "sum_eq":
        if len(args) != 2:
            raise ValueError("'sum_eq' expects 2 arguments")
        va = _as_vector(_eval_expr_node(args[0], env), op)
        vb = _as_vector(_eval_expr_node(args[1], env), op)
        return sum(va, Fraction(0, 1)) == sum(vb, Fraction(0, 1))

    raise ValueError(f"Unknown boolean/predicate operator: {op!r}")


def _eval_expr_node(
    node: Any,
    env: Mapping[str, Optional[ValueOrVec]],
) -> ValueOrVec:
    """Evaluate a pure mathematical expression node to exact `Fraction` or `List[Fraction]`."""
    if isinstance(node, bool):
        raise ValueError(f"Boolean literal {node!r} is invalid inside numeric expression")
    if isinstance(node, (int, float, Decimal, Fraction)):
        return _to_fraction(node)
    if isinstance(node, str):
        if node in env:
            bound = env[node]
            if bound is None:
                raise _UndeterminedError(f"undetermined:{node}")
            return bound
        try:
            return _to_fraction(node)
        except ValueError as exc:
            raise _UndeterminedError(f"missing_var:{node}") from exc
    if not isinstance(node, list):
        raise ValueError(f"Unsupported expression node {node!r}")
    if not node:
        return []

    head = node[0]
    if not isinstance(head, str) or _looks_numeric_str(head):
        # Literal vector of numeric literals (when list does not start with an op name)
        return [_as_scalar(_eval_expr_node(el, env), "vector_literal") for el in node]

    op = head
    args = node[1:]

    if op == "vec":
        return [_as_scalar(_eval_expr_node(el, env), "vec") for el in args]
    if op == "var":
        if len(args) != 1 or not isinstance(args[0], str):
            raise ValueError("'var' expects single variable name string")
        name = args[0]
        if name not in env or env[name] is None:
            raise _UndeterminedError(f"missing_or_undetermined:{name}")
        return env[name]  # type: ignore[return-value]
    if op == "add":
        if not args:
            raise ValueError("'add' expects >= 1 argument")
        total = Fraction(0, 1)
        for arg in args:
            total += _as_scalar(_eval_expr_node(arg, env), "add")
        return total
    if op == "sub":
        if len(args) != 2:
            raise ValueError("'sub' expects 2 arguments")
        return _as_scalar(_eval_expr_node(args[0], env), "sub") - _as_scalar(
            _eval_expr_node(args[1], env), "sub"
        )
    if op == "mul":
        if not args:
            raise ValueError("'mul' expects >= 1 argument")
        prod = Fraction(1, 1)
        for arg in args:
            prod *= _as_scalar(_eval_expr_node(arg, env), "mul")
        return prod
    if op == "div":
        if len(args) != 2:
            raise ValueError("'div' expects 2 arguments")
        num = _as_scalar(_eval_expr_node(args[0], env), "div")
        den = _as_scalar(_eval_expr_node(args[1], env), "div")
        if den == 0:
            raise _UndeterminedError("division_by_zero")
        return num / den
    if op == "neg":
        if len(args) != 1:
            raise ValueError("'neg' expects 1 argument")
        return -_as_scalar(_eval_expr_node(args[0], env), "neg")
    if op == "abs":
        if len(args) != 1:
            raise ValueError("'abs' expects 1 argument")
        return abs(_as_scalar(_eval_expr_node(args[0], env), "abs"))
    if op == "min":
        if not args:
            raise ValueError("'min' expects >= 1 argument")
        if len(args) == 1:
            v = _eval_expr_node(args[0], env)
            return min(_as_vector(v, "min")) if isinstance(v, list) else v
        return min(_as_scalar(_eval_expr_node(arg, env), "min") for arg in args)
    if op == "max":
        if not args:
            raise ValueError("'max' expects >= 1 argument")
        if len(args) == 1:
            v = _eval_expr_node(args[0], env)
            return max(_as_vector(v, "max")) if isinstance(v, list) else v
        return max(_as_scalar(_eval_expr_node(arg, env), "max") for arg in args)
    if op == "pow_int":
        if len(args) != 2:
            raise ValueError("'pow_int' expects 2 arguments: base, exp")
        base = _as_scalar(_eval_expr_node(args[0], env), "pow_int")
        exp = _as_int(_eval_expr_node(args[1], env), "pow_int")
        if abs(exp) > _MAX_EXPONENT:
            raise ValueError(f"pow_int exponent out of bounds ({exp})")
        if base == 0 and exp < 0:
            raise _UndeterminedError("zero_negative_exponent")
        return base ** exp
    if op == "if_else":
        if len(args) != 3:
            raise ValueError("'if_else' expects 3 arguments: cond, then_e, else_e")
        cond = _eval_bool_node(args[0], env)
        return _eval_expr_node(args[1] if cond else args[2], env)
    if op == "sum":
        if len(args) != 1:
            raise ValueError("'sum' expects 1 vector argument")
        vec = _as_vector(_eval_expr_node(args[0], env), "sum")
        return sum(vec, Fraction(0, 1))
    if op == "dot":
        if len(args) != 2:
            raise ValueError("'dot' expects 2 vector arguments")
        va = _as_vector(_eval_expr_node(args[0], env), "dot")
        vb = _as_vector(_eval_expr_node(args[1], env), "dot")
        if len(va) != len(vb):
            raise ValueError(f"dot length mismatch: {len(va)} vs {len(vb)}")
        return sum((a * b for a, b in zip(va, vb)), Fraction(0, 1))
    if op == "get":
        if len(args) != 2:
            raise ValueError("'get' expects 2 arguments: vec, index")
        vec = _as_vector(_eval_expr_node(args[0], env), "get")
        idx = _as_int(_eval_expr_node(args[1], env), "get")
        if idx < 0 or idx >= len(vec):
            raise _UndeterminedError(f"index_out_of_range:{idx}")
        return vec[idx]
    if op in ("round_half_up", "round_half_even"):
        if not (1 <= len(args) <= 2):
            raise ValueError(f"{op!r} expects (x, decimals=0)")
        val = _eval_expr_node(args[0], env)
        dec = _as_int(_eval_expr_node(args[1], env), op) if len(args) == 2 else 0
        fn = round_half_up if op == "round_half_up" else round_half_even
        if isinstance(val, list):
            return [fn(item, dec) for item in val]
        return fn(val, dec)
    if op == "floor":
        if len(args) != 1:
            raise ValueError("'floor' expects 1 scalar argument")
        return Fraction(math.floor(_as_scalar(_eval_expr_node(args[0], env), "floor")), 1)
    if op == "ceil":
        if len(args) != 1:
            raise ValueError("'ceil' expects 1 scalar argument")
        return Fraction(math.ceil(_as_scalar(_eval_expr_node(args[0], env), "ceil")), 1)
    if op == "pro_rata":
        if not (2 <= len(args) <= 4):
            raise ValueError("'pro_rata' expects (total, weights, decimals=0, mode='half_up')")
        total = _as_scalar(_eval_expr_node(args[0], env), "pro_rata")
        weights = _as_vector(_eval_expr_node(args[1], env), "pro_rata")
        decimals = _as_int(_eval_expr_node(args[2], env), "pro_rata") if len(args) >= 3 else 0
        mode = str(args[3]) if len(args) == 4 else "half_up"
        if not weights or any(w < 0 for w in weights):
            raise _UndeterminedError("invalid_pro_rata_weights")
        total_w = sum(weights, Fraction(0, 1))
        if total_w <= 0:
            raise _UndeterminedError("zero_pro_rata_weight_sum")
        target_total = _round_with_mode(total, decimals, mode)
        shares = [_round_with_mode(total * (w / total_w), decimals, mode) for w in weights]
        residual = target_total - sum(shares, Fraction(0, 1))
        if residual != 0:
            last_pos = max(i for i, w in enumerate(weights) if w > 0)
            shares[last_pos] += residual
        return shares
    if op == "recur_fold":
        if not (3 <= len(args) <= 5):
            raise ValueError("'recur_fold' expects (init, steps, step_expr, var_name='s', idx_name='k')")
        cur = _eval_expr_node(args[0], env)
        steps = _as_int(_eval_expr_node(args[1], env), "recur_fold")
        if steps < 0 or steps > _MAX_RECURRENCE_STEPS:
            raise ValueError(f"recur_fold steps out of range (0..{_MAX_RECURRENCE_STEPS}): {steps}")
        step_expr = args[2]
        var_name = str(args[3]) if len(args) >= 4 else "s"
        idx_name = str(args[4]) if len(args) == 5 else "k"
        local_env = dict(env)
        for k in range(steps):
            local_env[var_name] = cur
            local_env[idx_name] = Fraction(k, 1)
            cur = _eval_expr_node(step_expr, local_env)
        return cur
    if op == "solve_linear":
        if len(args) != 3:
            raise ValueError("'solve_linear' expects (a, b, c) for a*x + b = c")
        a = _as_scalar(_eval_expr_node(args[0], env), "solve_linear")
        b = _as_scalar(_eval_expr_node(args[1], env), "solve_linear")
        c = _as_scalar(_eval_expr_node(args[2], env), "solve_linear")
        if a == 0:
            raise _UndeterminedError("singular_linear_equation")
        return (c - b) / a

    # Fallback: treat list of expressions as literal vector only if head isn't an unknown op
    raise ValueError(f"Unknown compute operator: {op!r}")


def _looks_numeric_str(s: str) -> bool:
    try:
        _to_fraction(s)
        return True
    except ValueError:
        return False


def _parse_var_binding(raw: Any) -> Optional[ValueOrVec]:
    if raw is None:
        return None
    if isinstance(raw, list):
        return [_to_fraction(item) for item in raw]
    return _to_fraction(raw)


def evaluate_compute_spec(
    spec: Mapping[str, Any],
    *,
    guard_statuses: Optional[Mapping[str, str]] = None,
) -> ComputeReport:
    """Evaluate a pure mathematical DAG spec with optional SOUND node status guards."""
    raw_vars = spec.get("vars", {})
    raw_exprs = spec.get("exprs", {})
    raw_guards = spec.get("guards", {})
    raw_preds = spec.get("predicates", {})
    aux_scale = int(spec.get("aux_scale", 4))
    if not (0 <= aux_scale <= 12):
        raise ValueError(f"aux_scale must be in 0..12, got {aux_scale}")

    env: Dict[str, Optional[ValueOrVec]] = {}
    for k, v in raw_vars.items():
        env[str(k)] = _parse_var_binding(v)

    values: Dict[str, ComputeValue] = {}
    visiting: Set[str] = set()

    def _resolve_expr(name: str) -> Optional[ValueOrVec]:
        if name in values:
            cv = values[name]
            if cv.status == "undetermined":
                return None
            return cv.vector if cv.vector is not None else cv.value
        if name not in raw_exprs:
            return env.get(name)
        if name in visiting:
            raise ValueError(f"Cyclic expression reference involving {name!r}")
        visiting.add(name)
        try:
            if guard_statuses is not None and name in raw_guards:
                g_node = str(raw_guards[name])
                g_status = guard_statuses.get(g_node)
                if g_status == "defeated":
                    z = Fraction(0, 1)
                    cv = ComputeValue(
                        name=name,
                        status="gated_inactive",
                        value=z,
                        vector=None,
                        decimal_str="0",
                        reason=f"guard:{g_node}=defeated",
                    )
                    values[name] = cv
                    env[name] = z
                    return z
                if g_status != "established":
                    cv = ComputeValue(
                        name=name,
                        status="undetermined",
                        value=None,
                        vector=None,
                        decimal_str=None,
                        reason=f"guard:{g_node}={g_status or 'missing'}",
                    )
                    values[name] = cv
                    env[name] = None
                    return None

            try:
                res = _eval_expr_node(raw_exprs[name], env)
                if isinstance(res, list):
                    cv = ComputeValue(
                        name=name,
                        status="ok",
                        value=None,
                        vector=res,
                        decimal_str=_format_output_str(None, res),
                    )
                else:
                    cv = ComputeValue(
                        name=name,
                        status="ok",
                        value=res,
                        vector=None,
                        decimal_str=_format_output_str(res, None),
                    )
                values[name] = cv
                env[name] = res
                return res
            except _UndeterminedError as exc:
                cv = ComputeValue(
                    name=name,
                    status="undetermined",
                    value=None,
                    vector=None,
                    decimal_str=None,
                    reason=exc.reason,
                )
                values[name] = cv
                env[name] = None
                return None
        finally:
            visiting.remove(name)

    # Pre-resolve dependency DAG for any referenced exprs before declaration order loop
    def _collect_refs(node: Any) -> List[str]:
        if isinstance(node, str) and node in raw_exprs:
            return [node]
        if isinstance(node, list):
            out: List[str] = []
            for item in node[1:] if (node and isinstance(node[0], str) and node[0] not in raw_exprs and not _looks_numeric_str(node[0])) else node:
                out.extend(_collect_refs(item))
            return out
        return []

    def _ensure_order(name: str) -> None:
        if name in values:
            return
        if name in visiting:
            raise ValueError(f"Cyclic expression reference involving {name!r}")
        visiting.add(name)
        for dep in _collect_refs(raw_exprs[name]):
            if dep != name and dep not in values:
                visiting.remove(name)
                _ensure_order(dep)
                visiting.add(name)
        visiting.remove(name)
        _resolve_expr(name)

    for expr_name in raw_exprs:
        _ensure_order(str(expr_name))

    predicates: Dict[str, PredicateEval] = {}
    for pred_name, pred_ast in raw_preds.items():
        pname = str(pred_name)
        try:
            truth = _eval_bool_node(pred_ast, env)
            predicates[pname] = PredicateEval(
                name=pname,
                status="established" if truth else "defeated",
                truth=truth,
            )
        except _UndeterminedError as exc:
            predicates[pname] = PredicateEval(
                name=pname,
                status="penumbra",
                truth=None,
                reason=exc.reason,
            )

    canonical_json = json.dumps(spec, sort_keys=True, separators=(",", ":"), default=str).encode("utf-8")
    spec_digest = fnv1a_64(canonical_json)
    return ComputeReport(
        values=values,
        predicates=predicates,
        aux_scale=aux_scale,
        spec_digest=spec_digest,
    )


def pack_compute_flow_aux(report: ComputeReport) -> List[int]:
    """Encode `ComputeReport` values & predicate bitmasks into <= 304 `flow_aux` uint64 words."""
    scale_factor = Fraction(10, 1) ** report.aux_scale
    pred_true = 0
    pred_false = 0
    pred_pen = 0
    for idx, pred in enumerate(report.predicates.values()):
        if idx >= 64:
            break
        bit = 1 << idx
        if pred.truth is True:
            pred_true |= bit
        elif pred.truth is False:
            pred_false |= bit
        else:
            pred_pen |= bit

    entries: List[Tuple[int, int, int]] = []
    for name, cv in report.values.items():
        if len(entries) >= COMPUTE_AUX_MAX_ENTRIES:
            break
        name_hash = fnv1a_64(name.encode("utf-8"))
        st_code = _STATUS_STR_TO_CODE.get(cv.status, 2)
        if cv.value is not None:
            scaled = int(round_half_up(cv.value * scale_factor, 0).numerator)
        else:
            scaled = 0
        if scaled < _I64_MIN or scaled > _I64_MAX:
            scaled = 0
            st_code = 2
        entries.append((name_hash, scaled & 0xFFFFFFFFFFFFFFFF, st_code))

    words = [0] * SOUND_FLOW_AUX_WORDS
    words[0] = COMPUTE_AUX_MAGIC
    words[1] = report.aux_scale
    words[2] = len(entries)
    words[3] = pred_true
    words[4] = pred_false
    words[5] = pred_pen
    words[6] = report.spec_digest & 0xFFFFFFFFFFFFFFFF
    words[7] = 0

    for k, (nh, s_u64, st_code) in enumerate(entries):
        base = COMPUTE_AUX_HDR_WORDS + k * COMPUTE_AUX_ENTRY_WORDS
        words[base + 0] = nh
        words[base + 1] = s_u64
        words[base + 2] = st_code
    return words


def _u64_to_i64(u: int) -> int:
    u64 = u & 0xFFFFFFFFFFFFFFFF
    return u64 - (1 << 64) if (u64 & (1 << 63)) else u64


def unpack_compute_flow_aux(words: Sequence[int]) -> Dict[str, Any]:
    """Decode `flow_aux` uint64 words packed by `pack_compute_flow_aux`."""
    if len(words) < COMPUTE_AUX_HDR_WORDS:
        raise ValueError(f"Expected >= {COMPUTE_AUX_HDR_WORDS} flow_aux words, got {len(words)}")
    if words[0] != COMPUTE_AUX_MAGIC:
        raise ValueError(f"Invalid flow_aux compute magic: 0x{words[0]:016x}")
    aux_scale = int(words[1])
    num_entries = int(words[2])
    if num_entries > COMPUTE_AUX_MAX_ENTRIES:
        raise ValueError(f"Corrupt flow_aux num_entries={num_entries}")
    entries: List[Dict[str, Any]] = []
    for k in range(num_entries):
        base = COMPUTE_AUX_HDR_WORDS + k * COMPUTE_AUX_ENTRY_WORDS
        nh = int(words[base + 0])
        s_i64 = _u64_to_i64(int(words[base + 1]))
        st_code = int(words[base + 2])
        entries.append(
            {
                "name_hash": nh,
                "scaled_i64": s_u64_div_decimal(s_i64, aux_scale)[0],
                "scaled_str": s_u64_div_decimal(s_i64, aux_scale)[1],
                "status": _STATUS_CODE_TO_STR.get(st_code, "unknown"),
            }
        )
    return {
        "magic": words[0],
        "aux_scale": aux_scale,
        "num_entries": num_entries,
        "pred_true_mask": int(words[3]),
        "pred_false_mask": int(words[4]),
        "pred_pen_mask": int(words[5]),
        "spec_digest": int(words[6]),
        "entries": entries,
    }


def s_u64_div_decimal(s_i64: int, aux_scale: int) -> Tuple[int, str]:
    frac = Fraction(s_i64, 10 ** aux_scale) if aux_scale > 0 else Fraction(s_i64, 1)
    return s_i64, _terminating_decimal_str(frac)


def evaluate_bridged_arena(
    base_nodes: Sequence[ArenaNodeSpec],
    compute_spec: Mapping[str, Any],
    sound_bin: Path,
    *,
    schema_hash: int = FALLBACK_SCHEMA_HASH,
    backend: str = "auto",
    power_mask: int = 0,
    immunity_mask: int = 0,
    duty_pos_mask: int = 0,
    duty_neg_mask: int = 0,
    voidable_mask: int = 0,
    rescission_power_mask: int = 0,
) -> BridgedArenaResult:
    """Execute 2-stage bidirectional SoundArena <-> Compute evaluation and seal `flow_aux`."""
    pre_eval: Optional[ArenaEvalResult] = None
    guard_statuses: Optional[Dict[str, str]] = None

    if compute_spec.get("guards"):
        pre_arena = encode_arena(
            base_nodes,
            schema_hash=schema_hash,
            power_mask=power_mask,
            immunity_mask=immunity_mask,
            duty_pos_mask=duty_pos_mask,
            duty_neg_mask=duty_neg_mask,
            voidable_mask=voidable_mask,
            rescission_power_mask=rescission_power_mask,
        )
        pre_eval = verify_arena_bytes(pre_arena, sound_bin, backend=backend)
        guard_statuses = {nd.label: nd.status for nd in pre_eval.nodes}

    report = evaluate_compute_spec(compute_spec, guard_statuses=guard_statuses)

    final_nodes: List[ArenaNodeSpec] = []
    for nd in base_nodes:
        pred = report.predicates.get(nd.label)
        if pred is None:
            final_nodes.append(nd)
            continue
        if pred.truth is True:
            final_nodes.append(
                ArenaNodeSpec(
                    label=nd.label,
                    sort=nd.sort,
                    evid="direct",
                    aspect=nd.aspect,
                    constitutive=nd.constitutive,
                    revoked=nd.revoked,
                    penumbra=False,
                    presup_mask=nd.presup_mask,
                    attack_mask=nd.attack_mask,
                    clauses=[0],
                    span_start=nd.span_start,
                    span_end=nd.span_end,
                )
            )
        elif pred.truth is False:
            final_nodes.append(
                ArenaNodeSpec(
                    label=nd.label,
                    sort=nd.sort,
                    evid="direct",
                    aspect=nd.aspect,
                    constitutive=nd.constitutive,
                    revoked=nd.revoked,
                    penumbra=False,
                    presup_mask=nd.presup_mask,
                    attack_mask=nd.attack_mask,
                    clauses=[],
                    span_start=nd.span_start,
                    span_end=nd.span_end,
                )
            )
        else:
            final_nodes.append(
                ArenaNodeSpec(
                    label=nd.label,
                    sort=nd.sort,
                    evid="direct",
                    aspect=nd.aspect,
                    constitutive=nd.constitutive,
                    revoked=nd.revoked,
                    penumbra=True,
                    presup_mask=nd.presup_mask,
                    attack_mask=nd.attack_mask,
                    clauses=[0],
                    span_start=nd.span_start,
                    span_end=nd.span_end,
                )
            )

    aux_words = pack_compute_flow_aux(report)
    arena_bytes = encode_arena(
        final_nodes,
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
    return BridgedArenaResult(
        compute=report,
        arena_bytes=arena_bytes,
        eval_result=eval_res,
        pre_eval_result=pre_eval,
        aux_words=aux_words,
    )


def _write_fixed_ascii24(text: str) -> bytes:
    raw = text.encode("ascii", errors="strict")[:23]
    return raw.ljust(24, b"\x00")


def compile_compute_to_fir1(
    spec: Mapping[str, Any],
    *,
    prog_name: str = "cb_gate",
    output_names: Optional[Sequence[str]] = None,
) -> Tuple[bytes, List[str], List[int]]:
    """Lower integer-scalar subset (`add`, `sub`, `mul`, `neg`, `abs`, `min`, `max`, `if_else`, comparisons) to `FIR1`.

    Returns `(fir_bytes, input_var_names, default_input_values)` matching `FLOW/sdk/flow_ir.h`.
    """
    if not prog_name or "/" in prog_name or "\\" in prog_name:
        raise ValueError(f"Invalid FIR1 program name: {prog_name!r}")

    raw_vars = spec.get("vars", {})
    raw_exprs = spec.get("exprs", {})
    raw_preds = spec.get("predicates", {})

    in_names: List[str] = [str(k) for k in raw_vars.keys()]
    in_defaults: List[int] = []
    for k in in_names:
        v = raw_vars[k]
        if isinstance(v, bool) or not isinstance(v, int):
            raise ValueError(f"FIR1 lowering requires integer variable {k!r}, got {v!r}")
        in_defaults.append(int(v))

    if not in_names:
        in_names = ["_unused"]
        in_defaults = [0]

    # Values: (width, name) starting cleanly with inputs at indices 0..num_inputs-1
    vals: List[Tuple[int, str]] = [(64, name) for name in in_names]
    ops: List[Tuple[int, int, int, Tuple[int, ...], int]] = []  # (kind, num_ins, out_val, ins, p0)
    sym_to_val: Dict[str, Tuple[int, int]] = {name: (idx, 64) for idx, name in enumerate(in_names)}

    def _emit_op(kind: int, width: int, name: str, ins: Sequence[int], p0: int = 0) -> Tuple[int, int]:
        out_id = len(vals)
        vals.append((width, name))
        ops.append((kind, len(ins), out_id, tuple(ins), p0 & 0xFFFFFFFFFFFFFFFF))
        return out_id, width

    def _emit_const(val: int, width: int = 64, tag: str = "c") -> Tuple[int, int]:
        return _emit_op(_OP_CONST_U64, width, f"{tag}_{len(vals)}", (), val & 0xFFFFFFFFFFFFFFFF)

    def _lower_bool(node: Any) -> int:
        if isinstance(node, bool):
            vid, _ = _emit_const(1 if node else 0, width=1, tag="bconst")
            return vid
        if not isinstance(node, list) or not node:
            raise ValueError(f"Unsupported FIR1 boolean AST: {node!r}")
        op = node[0]
        args = node[1:]
        if op == "not":
            if len(args) != 1:
                raise ValueError("'not' expects 1 arg")
            v = _lower_bool(args[0])
            one, _ = _emit_const(1, width=1, tag="one1")
            out, _ = _emit_op(_OP_XOR_VEC, 1, f"not_{len(vals)}", (v, one))
            return out
        if op == "and":
            cur = _lower_bool(args[0])
            for item in args[1:]:
                rhs = _lower_bool(item)
                cur, _ = _emit_op(_OP_AND_VEC, 1, f"and_{len(vals)}", (cur, rhs))
            return cur
        if op == "or":
            cur = _lower_bool(args[0])
            for item in args[1:]:
                rhs = _lower_bool(item)
                cur, _ = _emit_op(_OP_CTRL_SEL, 1, f"or_{len(vals)}", (cur, rhs, cur))
            return cur
        if op in ("eq", "ne", "lt", "le", "gt", "ge"):
            if len(args) != 2:
                raise ValueError(f"{op!r} expects 2 args")
            a = _lower_scalar(args[0])
            b = _lower_scalar(args[1])
            if op in ("eq", "ne"):
                diff, _ = _emit_op(_OP_SUB, 64, f"d_{len(vals)}", (a, b))
                eqz, _ = _emit_op(_OP_EQ_ZERO, 1, f"eqz_{len(vals)}", (diff,))
                if op == "eq":
                    return eqz
                one, _ = _emit_const(1, width=1, tag="one1")
                neq, _ = _emit_op(_OP_XOR_VEC, 1, f"ne_{len(vals)}", (eqz, one))
                return neq
            # Ordered signed comparisons via sign bit (bit 63) of (a - b) or (b - a)
            if op in ("lt", "ge"):
                diff, _ = _emit_op(_OP_SUB, 64, f"cmpd_{len(vals)}", (a, b))
            else:
                diff, _ = _emit_op(_OP_SUB, 64, f"cmpd_{len(vals)}", (b, a))
            sgn, _ = _emit_op(_OP_SLICE, 1, f"sgn_{len(vals)}", (diff,), p0=63)
            if op in ("lt", "gt"):
                return sgn
            one, _ = _emit_const(1, width=1, tag="one1")
            inv, _ = _emit_op(_OP_XOR_VEC, 1, f"inv_{len(vals)}", (sgn, one))
            return inv
        raise ValueError(f"Operator {op!r} is not supported in FIR1 pure-GPR integer lowering")

    def _lower_scalar(node: Any) -> int:
        if isinstance(node, int) and not isinstance(node, bool):
            vid, _ = _emit_const(int(node), width=64, tag="k")
            return vid
        if isinstance(node, str):
            if node in sym_to_val:
                return sym_to_val[node][0]
            val = _to_fraction(node)
            if val.denominator != 1:
                raise ValueError(f"FIR1 lowering requires integers, got {node!r}")
            vid, _ = _emit_const(int(val.numerator), width=64, tag="k")
            return vid
        if not isinstance(node, list) or not node:
            raise ValueError(f"Unsupported FIR1 expression AST: {node!r}")
        op = node[0]
        args = node[1:]
        if op == "add":
            cur = _lower_scalar(args[0])
            for arg in args[1:]:
                rhs = _lower_scalar(arg)
                cur, _ = _emit_op(_OP_ADD, 64, f"add_{len(vals)}", (cur, rhs))
            return cur
        if op == "sub":
            if len(args) != 2:
                raise ValueError("'sub' expects 2 args")
            a = _lower_scalar(args[0])
            b = _lower_scalar(args[1])
            out, _ = _emit_op(_OP_SUB, 64, f"sub_{len(vals)}", (a, b))
            return out
        if op == "mul":
            cur = _lower_scalar(args[0])
            for arg in args[1:]:
                rhs = _lower_scalar(arg)
                cur, _ = _emit_op(_OP_MUL_FXP, 64, f"mul_{len(vals)}", (cur, rhs), p0=0)
            return cur
        if op == "neg":
            if len(args) != 1:
                raise ValueError("'neg' expects 1 arg")
            zero, _ = _emit_const(0, width=64, tag="z")
            x = _lower_scalar(args[0])
            out, _ = _emit_op(_OP_SUB, 64, f"neg_{len(vals)}", (zero, x))
            return out
        if op == "abs":
            if len(args) != 1:
                raise ValueError("'abs' expects 1 arg")
            x = _lower_scalar(args[0])
            zero, _ = _emit_const(0, width=64, tag="z")
            nx, _ = _emit_op(_OP_SUB, 64, f"neg_{len(vals)}", (zero, x))
            is_neg, _ = _emit_op(_OP_SLICE, 1, f"sgn_{len(vals)}", (x,), p0=63)
            out, _ = _emit_op(_OP_CTRL_SEL, 64, f"abs_{len(vals)}", (is_neg, x, nx))
            return out
        if op in ("min", "max"):
            cur = _lower_scalar(args[0])
            for arg in args[1:]:
                rhs = _lower_scalar(arg)
                diff, _ = _emit_op(_OP_SUB, 64, f"mm_d_{len(vals)}", (cur, rhs))
                lt, _ = _emit_op(_OP_SLICE, 1, f"mm_lt_{len(vals)}", (diff,), p0=63)
                if op == "min":
                    cur, _ = _emit_op(_OP_CTRL_SEL, 64, f"min_{len(vals)}", (lt, rhs, cur))
                else:
                    cur, _ = _emit_op(_OP_CTRL_SEL, 64, f"max_{len(vals)}", (lt, cur, rhs))
            return cur
        if op == "if_else":
            if len(args) != 3:
                raise ValueError("'if_else' expects 3 args")
            cond = _lower_bool(args[0])
            t_val = _lower_scalar(args[1])
            f_val = _lower_scalar(args[2])
            out, _ = _emit_op(_OP_CTRL_SEL, 64, f"sel_{len(vals)}", (cond, f_val, t_val))
            return out
        raise ValueError(f"Operator {op!r} is not supported in FIR1 pure-GPR integer lowering")

    for ename, east in raw_exprs.items():
        vid = _lower_scalar(east)
        sym_to_val[str(ename)] = (vid, 64)

    for pname, past in raw_preds.items():
        vid = _lower_bool(past)
        sym_to_val[str(pname)] = (vid, 1)

    if output_names is None:
        selected = [str(k) for k in raw_exprs.keys()] + [str(k) for k in raw_preds.keys()]
    else:
        selected = [str(k) for k in output_names]

    out_indices = [sym_to_val[name][0] for name in selected]
    if not out_indices:
        raise ValueError("FIR1 requires at least 1 output value")

    num_vals = len(vals)
    num_inputs = len(in_names)
    num_outputs = len(out_indices)
    num_ops = len(ops)
    if num_vals > 512 or num_ops > 512 or num_inputs > 32 or num_outputs > 32:
        raise ValueError("Program exceeds FLOW_IR resource bounds")

    buf = bytearray()
    # 64B header
    buf += struct.pack(
        "<IIIIIIIIII",
        _FLOW_IR_MAGIC,
        _FLOW_IR_VERSION,
        _FLOW_IR_MULT_LINEAR,
        _FLOW_IR_FLAG_CLEAN,
        num_vals,
        num_inputs,
        num_outputs,
        num_ops,
        0,  # num_lut_u64
        0,  # reserved
    )
    buf += _write_fixed_ascii24(prog_name)
    assert len(buf) == 64

    # vals: 32B each
    for w, nm in vals:
        buf += struct.pack("<HBB", w, 0, 0) + b"\x00" * 4 + _write_fixed_ascii24(nm)

    # inputs padded to 8B multiple
    in_pad = ((num_inputs + 1) // 2) * 2
    for i in range(in_pad):
        buf += struct.pack("<I", i if i < num_inputs else 0)

    # outputs padded to 8B multiple
    out_pad = ((num_outputs + 1) // 2) * 2
    for i in range(out_pad):
        buf += struct.pack("<I", out_indices[i] if i < num_outputs else 0)

    # ops: 72B each
    for kind, nins, ov, ins, p0 in ops:
        padded_ins = [ins[j] if j < nins else 0 for j in range(6)]
        buf += struct.pack(
            "<HHI6III4Q",
            kind,
            nins,
            ov,
            *padded_ins,
            0,  # lut_off
            0,  # lut_len
            p0,
            0,
            0,
            0,
        )

    trailer = fnv1a_64(bytes(buf))
    buf += struct.pack("<Q", trailer)
    return bytes(buf), in_names, in_defaults


def eval_fir1_bytes_u64(fir_bytes: bytes, input_vals: Sequence[int]) -> List[int]:
    """Decode and evaluate a `"FIR1"` circuit matching `flow_ir_eval_u64` in `FLOW/sdk/flow_ir.h`."""
    if len(fir_bytes) < 72:
        raise ValueError("FIR1 artifact too short")
    body = fir_bytes[:-8]
    exp_mac = struct.unpack_from("<Q", fir_bytes, len(fir_bytes) - 8)[0]
    if fnv1a_64(body) != exp_mac:
        raise ValueError("FIR1 trailer FNV-1a checksum mismatch")

    (
        magic,
        ver,
        mult,
        _flags,
        num_vals,
        num_inputs,
        num_outputs,
        num_ops,
        num_lut,
        _res,
    ) = struct.unpack_from("<IIIIIIIIII", body, 0)
    if magic != _FLOW_IR_MAGIC or ver != _FLOW_IR_VERSION:
        raise ValueError(f"Invalid FIR1 magic/version: 0x{magic:08x} v{ver}")
    if mult != _FLOW_IR_MULT_LINEAR or num_inputs + num_ops != num_vals:
        raise ValueError("Malformed FIR1 header invariants")
    if len(input_vals) != num_inputs:
        raise ValueError(f"Expected {num_inputs} FIR1 inputs, got {len(input_vals)}")

    off = 64
    widths: List[int] = []
    for _ in range(num_vals):
        w = struct.unpack_from("<H", body, off)[0]
        widths.append(w)
        off += 32

    in_slots = ((num_inputs + 1) // 2) * 2
    inputs = list(struct.unpack_from(f"<{num_inputs}I", body, off))
    off += in_slots * 4

    out_slots = ((num_outputs + 1) // 2) * 2
    outputs = list(struct.unpack_from(f"<{num_outputs}I", body, off))
    off += out_slots * 4

    def _mask(w: int) -> int:
        if w <= 0:
            return 0
        if w >= 64:
            return 0xFFFFFFFFFFFFFFFF
        return (1 << w) - 1

    env: Dict[int, int] = {}
    for i, slot in enumerate(inputs):
        env[slot] = int(input_vals[i]) & _mask(widths[slot])

    for _ in range(num_ops):
        unpacked = struct.unpack_from("<HHI6III4Q", body, off)
        off += 72
        kind, nins, out_val = unpacked[0], unpacked[1], unpacked[2]
        ins = unpacked[3:9]
        p0, p1, p2 = unpacked[11], unpacked[12], unpacked[13]
        omask = _mask(widths[out_val])
        args = [env[ins[j]] for j in range(nins)]

        if kind == _OP_CONST_U64:
            r = p0 & omask
        elif kind == _OP_SLICE:
            r = (args[0] >> p0) & omask
        elif kind == _OP_XOR_VEC:
            r = (args[0] ^ args[1]) & omask
        elif kind == _OP_AND_VEC:
            r = (args[0] & args[1]) & omask
        elif kind == _OP_ADD:
            r = (args[0] + args[1]) & omask
        elif kind == _OP_SUB:
            r = (args[0] - args[1]) & omask
        elif kind == _OP_MUL_FXP:
            sh = (p0 + p1 - p2) if (p0 + p1 > p2) else 0
            r = ((args[0] * args[1]) >> sh) & omask
        elif kind == _OP_CTRL_SEL:
            r = (args[2] if (args[0] & 1) != 0 else args[1]) & omask
        elif kind == _OP_EQ_ZERO:
            r = 1 if args[0] == 0 else 0
        else:
            raise ValueError(f"Unsupported opcode in eval_fir1_bytes_u64: {kind}")
        env[out_val] = r

    if num_lut != 0 or off != len(body):
        raise ValueError("FIR1 body length mismatch")
    return [env[idx] for idx in outputs]


def _lean_q_lit(val: Fraction) -> str:
    return f"(Q.mk ({val.numerator}) {val.denominator})"


def _lean_expr_code(node: Any, env_names: Set[str]) -> str:
    if isinstance(node, (int, float, Decimal, Fraction)) and not isinstance(node, bool):
        return _lean_q_lit(_to_fraction(node))
    if isinstance(node, str):
        if node in env_names:
            return f"v_{node}"
        return _lean_q_lit(_to_fraction(node))
    if not isinstance(node, list) or not node:
        raise ValueError(f"Unsupported Lean certificate expr node: {node!r}")
    op = node[0]
    args = node[1:]
    if op == "add":
        acc = _lean_expr_code(args[0], env_names)
        for arg in args[1:]:
            acc = f"(qAdd {acc} {_lean_expr_code(arg, env_names)})"
        return acc
    if op == "sub":
        return f"(qSub {_lean_expr_code(args[0], env_names)} {_lean_expr_code(args[1], env_names)})"
    if op == "mul":
        acc = _lean_expr_code(args[0], env_names)
        for arg in args[1:]:
            acc = f"(qMul {acc} {_lean_expr_code(arg, env_names)})"
        return acc
    if op == "div":
        return f"(qDiv {_lean_expr_code(args[0], env_names)} {_lean_expr_code(args[1], env_names)})"
    if op == "neg":
        return f"(qNeg {_lean_expr_code(args[0], env_names)})"
    if op == "abs":
        return f"(qAbs {_lean_expr_code(args[0], env_names)})"
    if op == "min":
        acc = _lean_expr_code(args[0], env_names)
        for arg in args[1:]:
            acc = f"(qMin {acc} {_lean_expr_code(arg, env_names)})"
        return acc
    if op == "max":
        acc = _lean_expr_code(args[0], env_names)
        for arg in args[1:]:
            acc = f"(qMax {acc} {_lean_expr_code(arg, env_names)})"
        return acc
    if op == "round_half_up":
        dec = int(args[1]) if len(args) == 2 else 0
        return f"(qRoundHalfUp {_lean_expr_code(args[0], env_names)} ({dec}))"
    if op == "if_else":
        return (
            f"(if {_lean_bool_code(args[0], env_names)} "
            f"then {_lean_expr_code(args[1], env_names)} "
            f"else {_lean_expr_code(args[2], env_names)})"
        )
    if op == "solve_linear":
        a = _lean_expr_code(args[0], env_names)
        b = _lean_expr_code(args[1], env_names)
        c = _lean_expr_code(args[2], env_names)
        return f"(qDiv (qSub {c} {b}) {a})"
    raise ValueError(f"Operator {op!r} is not supported in zero-import Lean certificate emission")


def _lean_bool_code(node: Any, env_names: Set[str]) -> str:
    if isinstance(node, bool):
        return "true" if node else "false"
    if not isinstance(node, list) or not node:
        raise ValueError(f"Unsupported Lean certificate boolean node: {node!r}")
    op = node[0]
    args = node[1:]
    if op == "not":
        return f"(!({_lean_bool_code(args[0], env_names)}))"
    if op == "and":
        return "(" + " && ".join(_lean_bool_code(a, env_names) for a in args) + ")"
    if op == "or":
        return "(" + " || ".join(_lean_bool_code(a, env_names) for a in args) + ")"
    if op == "eq":
        return f"(qEq {_lean_expr_code(args[0], env_names)} {_lean_expr_code(args[1], env_names)})"
    if op == "ne":
        return f"(!(qEq {_lean_expr_code(args[0], env_names)} {_lean_expr_code(args[1], env_names)}))"
    if op == "lt":
        return f"(qLt {_lean_expr_code(args[0], env_names)} {_lean_expr_code(args[1], env_names)})"
    if op == "le":
        return f"(qLe {_lean_expr_code(args[0], env_names)} {_lean_expr_code(args[1], env_names)})"
    if op == "gt":
        return f"(qLt {_lean_expr_code(args[1], env_names)} {_lean_expr_code(args[0], env_names)})"
    if op == "ge":
        return f"(qLe {_lean_expr_code(args[1], env_names)} {_lean_expr_code(args[0], env_names)})"
    if op == "approx_eq":
        a = _lean_expr_code(args[0], env_names)
        b = _lean_expr_code(args[1], env_names)
        tol = _lean_expr_code(args[2], env_names)
        return f"(qLe (qAbs (qSub {a} {b})) {tol})"
    if op == "in_range":
        x = _lean_expr_code(args[0], env_names)
        lo = _lean_expr_code(args[1], env_names)
        hi = _lean_expr_code(args[2], env_names)
        return f"(qLe {lo} {x} && qLe {x} {hi})"
    raise ValueError(f"Boolean operator {op!r} is not supported in Lean certificate emission")


def emit_lean4_certificate(
    spec: Mapping[str, Any],
    report: ComputeReport,
    *,
    namespace: str = "ComputeBridgeCert",
) -> str:
    """Emit a self-contained zero-import Lean 4 theorem verifying computed rationals & predicates."""
    lines: List[str] = [
        f"namespace {namespace}",
        "",
        "structure Q where",
        "  num : Int",
        "  den : Nat",
        "  deriving DecidableEq, Repr",
        "",
        "def qNorm (n : Int) (d : Nat) : Q :=",
        "  if d == 0 then Q.mk 0 1",
        "  else",
        "    let g := Nat.gcd n.natAbs d",
        "    Q.mk (n / Int.ofNat g) (d / g)",
        "",
        "def qAdd (a b : Q) : Q :=",
        "  qNorm (a.num * Int.ofNat b.den + b.num * Int.ofNat a.den) (a.den * b.den)",
        "",
        "def qSub (a b : Q) : Q :=",
        "  qNorm (a.num * Int.ofNat b.den - b.num * Int.ofNat a.den) (a.den * b.den)",
        "",
        "def qMul (a b : Q) : Q :=",
        "  qNorm (a.num * b.num) (a.den * b.den)",
        "",
        "def qDiv (a b : Q) : Q :=",
        "  if b.num == 0 then Q.mk 0 1",
        "  else",
        "    let s : Int := if b.num < 0 then -1 else 1",
        "    qNorm (s * a.num * Int.ofNat b.den) (a.den * b.num.natAbs)",
        "",
        "def qNeg (a : Q) : Q := Q.mk (-a.num) a.den",
        "def qAbs (a : Q) : Q := Q.mk (Int.ofNat a.num.natAbs) a.den",
        "def qEq (a b : Q) : Bool := a.num * Int.ofNat b.den == b.num * Int.ofNat a.den",
        "def qLe (a b : Q) : Bool := a.num * Int.ofNat b.den <= b.num * Int.ofNat a.den",
        "def qLt (a b : Q) : Bool := a.num * Int.ofNat b.den < b.num * Int.ofNat a.den",
        "def qMin (a b : Q) : Q := if qLe a b then a else b",
        "def qMax (a b : Q) : Q := if qLe b a then a else b",
        "",
        "def pow10 : Nat → Nat",
        "  | 0 => 1",
        "  | n + 1 => 10 * pow10 n",
        "",
        "def qRoundHalfUp (x : Q) (dec : Int) : Q :=",
        "  let factor : Q := if dec >= 0 then Q.mk (Int.ofNat (pow10 dec.natAbs)) 1 else Q.mk 1 (pow10 dec.natAbs)",
        "  let y := qMul x factor",
        "  let ay := y.num.natAbs",
        "  let q := ay / y.den",
        "  let r := ay % y.den",
        "  let qOut := if 2 * r >= y.den then q + 1 else q",
        "  let signed := if y.num < 0 then -Int.ofNat qOut else Int.ofNat qOut",
        "  qDiv (Q.mk signed 1) factor",
        "",
    ]

    env_names: Set[str] = set()
    raw_vars = spec.get("vars", {})
    for k, v in raw_vars.items():
        if v is None or isinstance(v, list):
            continue
        k_str = str(k)
        lines.append(f"def v_{k_str} : Q := {_lean_q_lit(_to_fraction(v))}")
        env_names.add(k_str)

    raw_exprs = spec.get("exprs", {})
    checks: List[str] = []
    for ename, east in raw_exprs.items():
        e_str = str(ename)
        cv = report.values[e_str]
        if cv.status != "ok" or cv.value is None:
            continue
        lines.append(f"def v_{e_str} : Q := {_lean_expr_code(east, env_names)}")
        env_names.add(e_str)
        checks.append(f"qEq v_{e_str} ({_lean_q_lit(cv.value)})")

    raw_preds = spec.get("predicates", {})
    for pname, past in raw_preds.items():
        p_str = str(pname)
        pe = report.predicates[p_str]
        if pe.truth is None:
            continue
        b_code = _lean_bool_code(past, env_names)
        lines.append(f"def p_{p_str} : Bool := {b_code}")
        checks.append(f"(p_{p_str} == {'true' if pe.truth else 'false'})")

    conj = " && ".join(checks) if checks else "true"
    lines += [
        "",
        f"def certAll : Bool := {conj}",
        "",
        "theorem compute_bridge_cert : certAll = true := by decide",
        "",
        f"end {namespace}",
        "",
    ]
    return "\n".join(lines)


def report_to_json_dict(report: ComputeReport) -> Dict[str, Any]:
    """Serialize `ComputeReport` into compact deterministic JSON structure."""
    return {
        "aux_scale": report.aux_scale,
        "spec_digest": hex(report.spec_digest),
        "values": {
            k: {
                "status": cv.status,
                "value": cv.decimal_str,
                "reason": cv.reason,
            }
            for k, cv in report.values.items()
        },
        "predicates": {
            k: {
                "status": pv.status,
                "truth": pv.truth,
                "reason": pv.reason,
            }
            for k, pv in report.predicates.items()
        },
    }
