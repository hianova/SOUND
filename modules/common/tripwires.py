"""Thin, deterministic high-precision multilingual MCQ tripwires (no heavy NLP)."""

from __future__ import annotations

import re
from typing import Any, Dict, List, Sequence

# Checked before positive cues so substrings like "不正確" / "非屬" are never inverted.
STEM_NEGATIVE_CUES: Sequence[str] = (
    "何者敘述錯誤",
    "何者不正確",
    "下列何者非",
    "何者錯誤",
    "何者有誤",
    "何者為非",
    "何者非屬",
    "何者不符",
    "何者不屬",
    "錯誤者為",
    "不正確",
    "非屬",
    "錯誤",
    "除外",
    "not true",
    "incorrect",
    "false",
)

STEM_POSITIVE_CUES: Sequence[str] = (
    "何者敘述正確",
    "何者正確",
    "何者為是",
    "何者符合",
    "何者屬之",
    "正確者為",
    "correct",
    "true",
)

ABSOLUTIZER_CUES: Sequence[str] = (
    "無任何例外",
    "without exception",
    "無例外",
    "絕不得",
    "均不得",
    "一律",
    "均應",
    "必定",
    "必然",
    "絕對",
    "所有",
    "全面",
    "永遠",
    "一概",
    "皆",
    "always",
    "never",
)

IRREALIS_PREMATURE_CUES: Sequence[str] = (
    "僅簽訂",
    "僅口頭",
    "意向書",
    "僅為可能",
    "預計",
    "尚未",
    "假設",
    "若僅",
    "極微",
    "projected",
    "remote",
)

EXCEPTION_SCOPED_PATTERNS: Sequence[re.Pattern[str]] = tuple(
    re.compile(pat, re.IGNORECASE)
    for pat in (
        r"原則上",
        r"但書",
        r"除法律另有規定",
        r"除非",
        r"除.{1,20}外",
        r"依實質",
        r"個案判斷",
        r"視情況",
        r"綜合判斷",
        r"重大性原則",
    )
)


def _match_cues(text: str, cues: Sequence[str]) -> List[str]:
    lowered = text.lower()
    hits: List[str] = []
    for cue in cues:
        if cue.isascii():
            if re.search(r"\b" + re.escape(cue.lower()) + r"\b", lowered):
                hits.append(cue)
        elif cue in text:
            hits.append(cue)
    return hits


def detect_stem_polarity(stem: str) -> Dict[str, Any]:
    """Detect whether an MCQ stem asks for the correct option or an incorrect/trap option."""
    neg_hits = _match_cues(stem, STEM_NEGATIVE_CUES)
    if neg_hits:
        return {"inferred_polarity": "select_incorrect", "matched_cues": neg_hits}

    pos_hits = _match_cues(stem, STEM_POSITIVE_CUES)
    if pos_hits:
        return {"inferred_polarity": "select_correct", "matched_cues": pos_hits}

    return {"inferred_polarity": "unknown", "matched_cues": []}


def scan_option_tripwires(text: str) -> Dict[str, List[str]]:
    """Scan an option string for absolutizer, irrealis/premature, and exception-scoped cues."""
    absolutizers = _match_cues(text, ABSOLUTIZER_CUES)
    irrealis = _match_cues(text, IRREALIS_PREMATURE_CUES)

    exception_scoped: List[str] = []
    for pat in EXCEPTION_SCOPED_PATTERNS:
        m = pat.search(text)
        if m:
            exception_scoped.append(m.group(0))

    return {
        "absolutizers": absolutizers,
        "irrealis_premature": irrealis,
        "exception_scoped": exception_scoped,
    }
