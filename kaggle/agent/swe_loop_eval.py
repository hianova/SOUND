"""Bounded multi-turn tool-calling SWE agent A/B evaluator (Baseline vs SOUND Gate).

Runs real git-backed temporary workspaces (Container A agent sandbox + fresh Container B
hidden test verifier) across 4 self-contained bugfix tasks with `max_turns=8` and tools
(`read_file`, `edit_file`, `run_command`, `submit_patch`), comparing:
  - Arm A (Baseline Agent): `submit_patch` submits the working tree immediately.
  - Arm B (SOUND-Gated Agent): wires standalone `adk_pack/.../gate_cli.py` (`init`,
    `record-localize`, `record-repro`, `record-regression`, `check-submit --strict`),
    intercepting premature or broken submissions and returning actionable `blocked_reasons`.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

from .ab_eval_runner import chat_completion, extract_json_block
from .sound_gate import REPO_ROOT

GATE_CLI_PATH = REPO_ROOT / "kaggle" / "agent" / "adk_pack" / "skills" / "sound_gate" / "scripts" / "gate_cli.py"


@dataclass(frozen=True)
class SweLoopTask:
    task_id: str
    prompt: str
    files: Dict[str, str]
    hidden_test_file: Tuple[str, str]
    visible_cmd: str
    mock_baseline_turns: List[Dict[str, Any]]
    mock_sound_turns: List[Dict[str, Any]]


def _build_tasks() -> List[SweLoopTask]:
    """Four isolated Python bugfix tasks with visible reproduction + hidden edge-case grading."""
    return [
        SweLoopTask(
            task_id="swe_01_pagination_boundary",
            prompt=(
                "Fix `paginator.py`: `paginate(items, page, page_size)` returns wrong slice length "
                "(drops the last item on each page) and computes `has_next` wrongly when a page ends "
                "exactly at `len(items)`. Inspect code, run `python3 -m unittest test_visible.py`, "
                "fix `paginator.py`, verify, and call `submit_patch`."
            ),
            files={
                "paginator.py": (
                    "def paginate(items, page, page_size):\n"
                    "    if page < 1 or page_size < 1:\n"
                    "        raise ValueError('page and page_size must be >= 1')\n"
                    "    start = (page - 1) * page_size\n"
                    "    end = start + page_size - 1\n"
                    "    slice_items = list(items[start:end])\n"
                    "    has_next = end <= len(items)\n"
                    "    return {'items': slice_items, 'has_next': has_next}\n"
                ),
                "test_visible.py": (
                    "import unittest\n"
                    "from paginator import paginate\n\n"
                    "class TestVisible(unittest.TestCase):\n"
                    "    def test_first_page(self):\n"
                    "        self.assertEqual(paginate([1, 2, 3, 4], 1, 2), {'items': [1, 2], 'has_next': True})\n"
                ),
            },
            hidden_test_file=(
                "test_hidden_grading.py",
                (
                    "import unittest\n"
                    "from paginator import paginate\n\n"
                    "class TestHidden(unittest.TestCase):\n"
                    "    def test_exact_end(self):\n"
                    "        self.assertEqual(paginate([1, 2, 3, 4], 2, 2), {'items': [3, 4], 'has_next': False})\n"
                    "    def test_empty(self):\n"
                    "        self.assertEqual(paginate([], 1, 5), {'items': [], 'has_next': False})\n"
                    "    def test_past_end(self):\n"
                    "        self.assertEqual(paginate([10], 2, 1), {'items': [], 'has_next': False})\n"
                ),
            ),
            visible_cmd="python3 -m unittest test_visible.py",
            mock_baseline_turns=[
                {"tool": "edit_file", "args": {"path": "paginator.py", "old_text": "    end = start + page_size - 1\n    slice_items = list(items[start:end])\n    has_next = end <= len(items)", "new_text": "    end = start + page_size\n    slice_items = list(items[start:end])\n    has_next = end <= len(items)"}},
                {"tool": "submit_patch", "args": {}},
            ],
            mock_sound_turns=[
                {"tool": "read_file", "args": {"path": "paginator.py"}},
                {"tool": "run_command", "args": {"cmd": "python3 -m unittest test_visible.py"}},
                {"tool": "edit_file", "args": {"path": "paginator.py", "old_text": "    end = start + page_size - 1\n    slice_items = list(items[start:end])\n    has_next = end <= len(items)", "new_text": "    end = start + page_size\n    slice_items = list(items[start:end])\n    has_next = end <= len(items)"}},
                # Premature submit before running post-edit regression -> intercepted with DEFER_PENUMBRA
                {"tool": "submit_patch", "args": {}},
                {"tool": "edit_file", "args": {"path": "paginator.py", "old_text": "has_next = end <= len(items)", "new_text": "has_next = end < len(items)"}},
                {"tool": "run_command", "args": {"cmd": "python3 -m unittest test_visible.py"}},
                {"tool": "submit_patch", "args": {}},
            ],
        ),
        SweLoopTask(
            task_id="swe_02_header_case_trim",
            prompt=(
                "Fix `headers.py`: `get_header(headers, name, default=None)` must match header names "
                "case-insensitively and strip leading/trailing whitespace from string values before returning. "
                "Verify with `python3 -m unittest test_visible.py` and call `submit_patch`."
            ),
            files={
                "headers.py": (
                    "def get_header(headers, name, default=None):\n"
                    "    for k, v in headers.items():\n"
                    "        if k == name:\n"
                    "            return v\n"
                    "    return default\n"
                ),
                "test_visible.py": (
                    "import unittest\n"
                    "from headers import get_header\n\n"
                    "class TestVisible(unittest.TestCase):\n"
                    "    def test_case_lookup(self):\n"
                    "        self.assertEqual(get_header({'Content-Type': 'application/json'}, 'content-type'), 'application/json')\n"
                ),
            },
            hidden_test_file=(
                "test_hidden_grading.py",
                (
                    "import unittest\n"
                    "from headers import get_header\n\n"
                    "class TestHidden(unittest.TestCase):\n"
                    "    def test_trim_and_case(self):\n"
                    "        self.assertEqual(get_header({'X-Request-Id': '  abc-123  '}, 'x-request-id'), 'abc-123')\n"
                    "    def test_missing_default(self):\n"
                    "        self.assertEqual(get_header({'Host': 'x'}, 'Accept', 'none'), 'none')\n"
                ),
            ),
            visible_cmd="python3 -m unittest test_visible.py",
            mock_baseline_turns=[
                {"tool": "read_file", "args": {"path": "headers.py"}},
                {"tool": "edit_file", "args": {"path": "headers.py", "old_text": "        if k == name:\n            return v", "new_text": "        if k.lower() == name.lower():\n            return v.strip() if isinstance(v, str) else v"}},
                {"tool": "run_command", "args": {"cmd": "python3 -m unittest test_visible.py"}},
                {"tool": "submit_patch", "args": {}},
            ],
            mock_sound_turns=[
                {"tool": "read_file", "args": {"path": "headers.py"}},
                {"tool": "run_command", "args": {"cmd": "python3 -m unittest test_visible.py"}},
                {"tool": "edit_file", "args": {"path": "headers.py", "old_text": "        if k == name:\n            return v", "new_text": "        if k.lower() == name.lower():\n            return v.strip() if isinstance(v, str) else v"}},
                {"tool": "run_command", "args": {"cmd": "python3 -m unittest test_visible.py"}},
                {"tool": "submit_patch", "args": {}},
            ],
        ),
        SweLoopTask(
            task_id="swe_03_cache_ttl_zero",
            prompt=(
                "Fix `ttl_cache.py`: entries with `ttl=0` or queried exactly at `now == expires_at` must be "
                "treated as expired (removed and return `None`), not active. Run `python3 -m unittest test_visible.py`, "
                "fix `ttl_cache.py`, test, and call `submit_patch`."
            ),
            files={
                "ttl_cache.py": (
                    "class TTLCache:\n"
                    "    def __init__(self):\n"
                    "        self._store = {}\n\n"
                    "    def set(self, key, value, ttl, now=0.0):\n"
                    "        self._store[key] = (value, now + ttl)\n\n"
                    "    def get(self, key, now=0.0):\n"
                    "        if key not in self._store:\n"
                    "            return None\n"
                    "        val, exp = self._store[key]\n"
                    "        if now > exp:\n"
                    "            del self._store[key]\n"
                    "            return None\n"
                    "        return val\n"
                ),
                "test_visible.py": (
                    "import unittest\n"
                    "from ttl_cache import TTLCache\n\n"
                    "class TestVisible(unittest.TestCase):\n"
                    "    def test_zero_ttl_expired(self):\n"
                    "        c = TTLCache()\n"
                    "        c.set('k', 'v', ttl=0, now=10.0)\n"
                    "        self.assertIsNone(c.get('k', now=10.0))\n"
                ),
            },
            hidden_test_file=(
                "test_hidden_grading.py",
                (
                    "import unittest\n"
                    "from ttl_cache import TTLCache\n\n"
                    "class TestHidden(unittest.TestCase):\n"
                    "    def test_exact_boundary(self):\n"
                    "        c = TTLCache()\n"
                    "        c.set('a', 42, ttl=5.0, now=1.0)\n"
                    "        self.assertEqual(c.get('a', now=5.9), 42)\n"
                    "        self.assertIsNone(c.get('a', now=6.0))\n"
                ),
            ),
            visible_cmd="python3 -m unittest test_visible.py",
            mock_baseline_turns=[
                {"tool": "read_file", "args": {"path": "ttl_cache.py"}},
                # Accidentally introduces syntax typo and submits without running tests
                {"tool": "edit_file", "args": {"path": "ttl_cache.py", "old_text": "        if now > exp:", "new_text": "        if now >= exp"}},
                {"tool": "submit_patch", "args": {}},
            ],
            mock_sound_turns=[
                {"tool": "read_file", "args": {"path": "ttl_cache.py"}},
                {"tool": "run_command", "args": {"cmd": "python3 -m unittest test_visible.py"}},
                {"tool": "edit_file", "args": {"path": "ttl_cache.py", "old_text": "        if now > exp:", "new_text": "        if now >= exp"}},
                # Blocked with BLOCK_SUBMIT due to SyntaxError in AST check!
                {"tool": "submit_patch", "args": {}},
                {"tool": "edit_file", "args": {"path": "ttl_cache.py", "old_text": "        if now >= exp", "new_text": "        if now >= exp:"}},
                {"tool": "run_command", "args": {"cmd": "python3 -m unittest test_visible.py"}},
                {"tool": "submit_patch", "args": {}},
            ],
        ),
        SweLoopTask(
            task_id="swe_04_diff_hunk_counter",
            prompt=(
                "Fix `diff_stats.py`: `count_diff_lines(diff_text)` miscounts `+++ ` and `--- ` file headers "
                "as added/removed lines. Exclude `+++ ` and `--- ` file headers while counting real `+`/`-` content lines. "
                "Run `python3 -m unittest test_visible.py`, fix `diff_stats.py`, verify, and call `submit_patch`."
            ),
            files={
                "diff_stats.py": (
                    "def count_diff_lines(diff_text):\n"
                    "    added = 0\n"
                    "    removed = 0\n"
                    "    for line in diff_text.splitlines():\n"
                    "        if line.startswith('+'):\n"
                    "            added += 1\n"
                    "        elif line.startswith('-'):\n"
                    "            removed += 1\n"
                    "    return {'added': added, 'removed': removed}\n"
                ),
                "test_visible.py": (
                    "import unittest\n"
                    "from diff_stats import count_diff_lines\n\n"
                    "class TestVisible(unittest.TestCase):\n"
                    "    def test_skip_headers(self):\n"
                    "        d = '--- a/x.py\\n+++ b/x.py\\n@@ -1 +1 @@\\n-old\\n+new\\n'\n"
                    "        self.assertEqual(count_diff_lines(d), {'added': 1, 'removed': 1})\n"
                ),
            },
            hidden_test_file=(
                "test_hidden_grading.py",
                (
                    "import unittest\n"
                    "from diff_stats import count_diff_lines\n\n"
                    "class TestHidden(unittest.TestCase):\n"
                    "    def test_empty_and_multifile(self):\n"
                    "        d = '--- a/a.py\\n+++ b/a.py\\n@@ -1 +1,2 @@\\n+x=1\\n+y=2\\n--- a/b.py\\n+++ b/b.py\\n-z=0\\n'\n"
                    "        self.assertEqual(count_diff_lines(d), {'added': 2, 'removed': 1})\n"
                    "        self.assertEqual(count_diff_lines(''), {'added': 0, 'removed': 0})\n"
                ),
            ),
            visible_cmd="python3 -m unittest test_visible.py",
            mock_baseline_turns=[
                {"tool": "read_file", "args": {"path": "diff_stats.py"}},
                {"tool": "edit_file", "args": {"path": "diff_stats.py", "old_text": "        if line.startswith('+'):\n            added += 1\n        elif line.startswith('-'):", "new_text": "        if line.startswith('+++') or line.startswith('---'):\n            continue\n        if line.startswith('+'):\n            added += 1\n        elif line.startswith('-'):"}},
                {"tool": "run_command", "args": {"cmd": "python3 -m unittest test_visible.py"}},
                {"tool": "submit_patch", "args": {}},
            ],
            mock_sound_turns=[
                {"tool": "read_file", "args": {"path": "diff_stats.py"}},
                {"tool": "run_command", "args": {"cmd": "python3 -m unittest test_visible.py"}},
                {"tool": "edit_file", "args": {"path": "diff_stats.py", "old_text": "        if line.startswith('+'):\n            added += 1\n        elif line.startswith('-'):", "new_text": "        if line.startswith('+++') or line.startswith('---'):\n            continue\n        if line.startswith('+'):\n            added += 1\n        elif line.startswith('-'):"}},
                {"tool": "run_command", "args": {"cmd": "python3 -m unittest test_visible.py"}},
                {"tool": "submit_patch", "args": {}},
            ],
        ),
    ]


def _init_git_repo(repo_dir: Path, files: Dict[str, str]) -> str:
    subprocess.run(["git", "init", str(repo_dir)], check=True, capture_output=True)
    subprocess.run(["git", "-C", str(repo_dir), "config", "user.email", "eval@example.com"], check=True)
    subprocess.run(["git", "-C", str(repo_dir), "config", "user.name", "Eval"], check=True)
    for rel, txt in files.items():
        fpath = (repo_dir / rel).resolve()
        fpath.parent.mkdir(parents=True, exist_ok=True)
        fpath.write_text(txt, encoding="utf-8")
    subprocess.run(["git", "-C", str(repo_dir), "add", "."], check=True, capture_output=True)
    subprocess.run(["git", "-C", str(repo_dir), "commit", "-m", "baseline"], check=True, capture_output=True)
    return subprocess.run(
        ["git", "-C", str(repo_dir), "rev-parse", "HEAD"], check=True, capture_output=True, text=True
    ).stdout.strip()


def _grade_diff_in_container_b(task: SweLoopTask, diff_text: str) -> bool:
    """Apply `diff_text` onto a clean Container-B checkout and run visible + hidden tests."""
    if not diff_text.strip():
        return False
    with tempfile.TemporaryDirectory(prefix=f"sound_cb_{task.task_id}_") as tmp:
        c_b = Path(tmp) / "repo_b"
        _init_git_repo(c_b, task.files)
        patch = diff_text if diff_text.endswith("\n") else diff_text + "\n"
        app = subprocess.run(
            ["git", "-C", str(c_b), "apply", "-"],
            input=patch,
            capture_output=True,
            text=True,
            check=False,
        )
        if app.returncode != 0:
            return False
        h_name, h_code = task.hidden_test_file
        (c_b / h_name).write_text(h_code, encoding="utf-8")
        res = subprocess.run(
            [sys.executable, "-m", "unittest", "discover", "-s", ".", "-p", "test_*.py"],
            cwd=str(c_b),
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
        return res.returncode == 0


def _run_gate_cli(args_list: Sequence[str]) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(GATE_CLI_PATH), *args_list],
        capture_output=True,
        text=True,
        check=False,
    )


def _safe_rel_path(repo_dir: Path, raw_path: str) -> Path:
    target = (repo_dir / raw_path).resolve()
    root = repo_dir.resolve()
    if root != target and root not in target.parents:
        raise ValueError(f"Path escapes workspace: {raw_path!r}")
    return target


def _run_episode(
    task: SweLoopTask,
    *,
    arm: str,
    api_base: str,
    model: str,
    api_key: Optional[str],
    backend: str,
    mock: bool,
    max_turns: int = 8,
) -> Dict[str, Any]:
    with tempfile.TemporaryDirectory(prefix=f"sound_loop_{arm}_{task.task_id}_") as tmp:
        tmp_path = Path(tmp)
        repo_dir = tmp_path / "workspace"
        state_file = tmp_path / "sound_gate_state.json"
        _init_git_repo(repo_dir, task.files)

        if arm == "sound":
            init_p = _run_gate_cli(["init", "--workspace", str(repo_dir), "--state-file", str(state_file), "--task-id", task.task_id])
            if init_p.returncode != 0:
                raise RuntimeError(f"gate_cli init failed: {init_p.stderr}")

        sys_prompt = (
            "You are an autonomous SWE debugging agent in a clean git workspace.\n"
            "Available tools (respond with ONE JSON object ONLY per turn):\n"
            '1) {"tool": "read_file", "args": {"path": "<rel_path>"}}\n'
            '2) {"tool": "edit_file", "args": {"path": "<rel_path>", "old_text": "<exact>", "new_text": "<replacement>"}}\n'
            '3) {"tool": "run_command", "args": {"cmd": "python3 -m unittest test_visible.py"}}\n'
            '4) {"tool": "submit_patch", "args": {}}\n'
            "Workflow: inspect source file(s), run visible test/repro, edit source file(s), re-run tests after editing, then submit_patch."
        )
        history: List[Dict[str, str]] = [
            {"role": "system", "content": sys_prompt},
            {"role": "user", "content": f"Task: {task.prompt}\nFiles: {sorted(task.files.keys())}"},
        ]

        mock_seq = task.mock_sound_turns if arm == "sound" else task.mock_baseline_turns
        total_out_tokens = 0
        turns_used = 0
        submitted = False
        blocked_attempts = 0
        premature_submit = False
        edited_since_test = False
        repro_seen = False
        overblock_hit = False

        for turn_idx in range(max_turns):
            turns_used += 1
            if mock:
                act_obj = mock_seq[min(turn_idx, len(mock_seq) - 1)]
                mock_payload = {"content": json.dumps(act_obj), "completion_tokens": 42}
            else:
                mock_payload = None
            res = chat_completion(
                api_base,
                model,
                history,
                api_key=api_key,
                backend=backend,
                max_tokens=1024,
                mock=mock,
                mock_payload=mock_payload,
            )
            total_out_tokens += res.completion_tokens
            parsed = extract_json_block(res.content)
            history.append({"role": "assistant", "content": res.content})

            if not isinstance(parsed, dict) or "tool" not in parsed:
                history.append({"role": "user", "content": 'TOOL_ERROR: Reply ONLY valid JSON {"tool": ..., "args": {...}}.'})
                continue

            tool = str(parsed.get("tool", ""))
            targs = parsed.get("args") if isinstance(parsed.get("args"), dict) else {}

            if tool == "read_file":
                try:
                    p = _safe_rel_path(repo_dir, str(targs.get("path", "")))
                    txt = p.read_text(encoding="utf-8")
                    if arm == "sound":
                        _run_gate_cli(["record-localize", "--workspace", str(repo_dir), "--state-file", str(state_file), "--files", str(targs.get("path", ""))])
                    obs = f"FILE {targs.get('path')}:\n{txt}"
                except Exception as exc:
                    obs = f"READ_ERROR: {exc}"
                history.append({"role": "user", "content": obs})

            elif tool == "edit_file":
                try:
                    rel = str(targs.get("path", ""))
                    p = _safe_rel_path(repo_dir, rel)
                    old_s = str(targs.get("old_text", ""))
                    new_s = str(targs.get("new_text", ""))
                    cur = p.read_text(encoding="utf-8")
                    if old_s not in cur:
                        obs = f"EDIT_ERROR: old_text not found verbatim in {rel}"
                    else:
                        p.write_text(cur.replace(old_s, new_s, 1), encoding="utf-8")
                        edited_since_test = True
                        if arm == "sound" and state_file.is_file():
                            st = json.loads(state_file.read_text(encoding="utf-8"))
                            st["regression_run"] = False
                            st["regression_passed"] = None
                            state_file.write_text(json.dumps(st, indent=2), encoding="utf-8")
                        obs = f"EDIT_OK: updated {rel}"
                except Exception as exc:
                    obs = f"EDIT_ERROR: {exc}"
                history.append({"role": "user", "content": obs})

            elif tool == "run_command":
                cmd = str(targs.get("cmd", "")).strip()
                if not cmd.startswith("python3 ") and not cmd.startswith("python "):
                    obs = "CMD_REJECTED: only python3 test commands allowed"
                else:
                    cmd_parts = cmd.split()[1:]
                    pr = subprocess.run(
                        [sys.executable, *cmd_parts],
                        cwd=str(repo_dir),
                        capture_output=True,
                        text=True,
                        timeout=15,
                        check=False,
                    )
                    if pr.returncode != 0:
                        repro_seen = True
                        if arm == "sound":
                            _run_gate_cli(["record-repro", "--workspace", str(repo_dir), "--state-file", str(state_file), "--observed"])
                    if arm == "sound" and (edited_since_test or pr.returncode == 0):
                        if pr.returncode == 0:
                            edited_since_test = False
                            _run_gate_cli(["record-regression", "--workspace", str(repo_dir), "--state-file", str(state_file), "--passed"])
                        else:
                            _run_gate_cli(["record-regression", "--workspace", str(repo_dir), "--state-file", str(state_file), "--failed"])
                    elif pr.returncode == 0:
                        edited_since_test = False
                    obs = f"EXIT {pr.returncode}\n{(pr.stdout + pr.stderr)[-1200:]}"
                history.append({"role": "user", "content": obs})

            elif tool == "submit_patch":
                cur_diff = subprocess.run(
                    ["git", "-C", str(repo_dir), "diff", "--no-ext-diff", "--no-color"],
                    capture_output=True,
                    text=True,
                    check=True,
                ).stdout
                if edited_since_test or not cur_diff.strip():
                    premature_submit = True

                if arm == "baseline":
                    submitted = True
                    break

                # Ensure repro stays explicit if test passed cleanly after prior fix
                chk = _run_gate_cli(["check-submit", "--workspace", str(repo_dir), "--state-file", str(state_file), "--strict"])
                if chk.returncode == 0:
                    submitted = True
                    break

                blocked_attempts += 1
                if _grade_diff_in_container_b(task, cur_diff):
                    overblock_hit = True
                history.append(
                    {
                        "role": "user",
                        "content": f"SUBMIT_BLOCKED_BY_SOUND:\n{chk.stdout}\nFix the blocked reasons before re-submitting.",
                    }
                )
            else:
                history.append({"role": "user", "content": f"UNKNOWN_TOOL: {tool!r}"})

        final_diff = subprocess.run(
            ["git", "-C", str(repo_dir), "diff", "--no-ext-diff", "--no-color"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout
        passed_hidden = _grade_diff_in_container_b(task, final_diff)
        sound_overblock = (arm == "sound") and (not submitted) and (overblock_hit or passed_hidden)
        return {
            "task_id": task.task_id,
            "arm": arm,
            "submitted": submitted,
            "passed_hidden": passed_hidden,
            "turns_used": turns_used,
            "out_tokens": total_out_tokens,
            "premature_submit_attempted": premature_submit,
            "sound_blocked_attempts": blocked_attempts,
            "sound_overblock_failure": sound_overblock,
            "repro_seen": repro_seen,
        }


def run_track5_swe_loop_ab(
    *,
    api_base: str,
    model: str,
    api_key: Optional[str],
    backend: str = "openai",
    mock: bool,
    max_turns: int = 8,
) -> Dict[str, Any]:
    """Run multi-turn SWE tool-calling A/B evaluation across 4 self-contained bugfix tasks."""
    tasks = _build_tasks()
    base_eps: List[Dict[str, Any]] = []
    snd_eps: List[Dict[str, Any]] = []

    for t in tasks:
        if not mock:
            print(f"[Track 5 SWE Loop] {t.task_id} arm=baseline...", file=sys.stderr, flush=True)
        base_eps.append(
            _run_episode(
                t, arm="baseline", api_base=api_base, model=model, api_key=api_key, backend=backend, mock=mock, max_turns=max_turns
            )
        )
        if not mock:
            print(f"[Track 5 SWE Loop] {t.task_id} arm=sound...", file=sys.stderr, flush=True)
        snd_eps.append(
            _run_episode(
                t, arm="sound", api_base=api_base, model=model, api_key=api_key, backend=backend, mock=mock, max_turns=max_turns
            )
        )

    n = max(1, len(tasks))
    return {
        "total_tasks": len(tasks),
        "baseline_pass_at_1": round(sum(1 for e in base_eps if e["passed_hidden"]) / n, 4),
        "sound_pass_at_1": round(sum(1 for e in snd_eps if e["passed_hidden"]) / n, 4),
        "baseline_premature_submit_rate": round(sum(1 for e in base_eps if e["premature_submit_attempted"]) / n, 4),
        "sound_blocked_premature_submits": sum(e["sound_blocked_attempts"] for e in snd_eps),
        "sound_overblock_failures": sum(1 for e in snd_eps if e["sound_overblock_failure"]),
        "baseline_avg_turns": round(statistics.mean(e["turns_used"] for e in base_eps), 2),
        "sound_avg_turns": round(statistics.mean(e["turns_used"] for e in snd_eps), 2),
        "baseline_avg_out_tokens": round(statistics.mean(e["out_tokens"] for e in base_eps), 2),
        "sound_avg_out_tokens": round(statistics.mean(e["out_tokens"] for e in snd_eps), 2),
        "cases": [{"baseline": b, "sound": s} for b, s in zip(base_eps, snd_eps)],
    }


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Multi-turn SWE agent loop A/B evaluator for SOUND")
    parser.add_argument("--backend", choices=("openai", "hf_local", "colab_ai"), default="openai")
    parser.add_argument("--api-base", default="http://127.0.0.1:8000/v1")
    parser.add_argument("--model", default="gemma-4-31b-it-qat-w4a16-ct")
    parser.add_argument("--api-key-env", default="GEMINI_API_KEY")
    parser.add_argument("--max-turns", type=int, default=8)
    parser.add_argument("--out", type=Path, default=None)
    parser.add_argument("--mock", action="store_true")
    args = parser.parse_args(argv)

    api_key = os.environ.get(args.api_key_env) or None
    report = {
        "mode": "mock" if args.mock else "live_llm",
        "backend": args.backend,
        "model": args.model,
        "track5_swe_loop_ab": run_track5_swe_loop_ab(
            api_base=args.api_base,
            model=args.model,
            api_key=api_key,
            backend=args.backend,
            mock=args.mock,
            max_turns=args.max_turns,
        ),
    }
    rendered = json.dumps(report, indent=2, ensure_ascii=False)
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    sys.exit(main())
