#!/usr/bin/env python3
"""Static contracts for the per-page idle auto-sleep timeout wiring."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{re.escape(name)}\s*\([^;]*?\)\s*\{{", source)
    if not match:
        raise AssertionError(f"function not found: {name}")
    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1:index]
    raise AssertionError(f"function is unterminated: {name}")


def page_field(source: str, page_symbol: str, field: str) -> str:
    match = re.search(rf"app_page_t {re.escape(page_symbol)}\s*=\s*\{{", source)
    if not match:
        raise AssertionError(f"initializer not found: {page_symbol}")
    end = source.find("};", match.end())
    if end < 0:
        raise AssertionError("initializer is unterminated")
    field_match = re.search(
        rf"\.{re.escape(field)}\s*=\s*([^,;]+)", source[match.end():end]
    )
    if not field_match:
        raise AssertionError(f"field not set: {field}")
    return field_match.group(1).strip()


def c_int_expression_us(expression: str) -> int:
    """Evaluate an integer constant expression like ``300LL * 1000 * 1000``."""
    python_literal = re.sub(r"(?<=[0-9])[uUlL]+(?![A-Za-z0-9_])", "", expression)
    return int(eval(python_literal, {"__builtins__": {}}, {}))  # noqa: S307 (controlled literal)


class SleepIdleTimeoutContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.shell_header = read("main/ui_shell.h")
        cls.shell = read("main/ui_shell.c")
        cls.sleep_ctl = read("main/sleep_ctl.c")
        cls.page_game = read("main/page_game.c")

    def test_game_page_overrides_idle_timeout_to_five_minutes(self) -> None:
        expression = page_field(self.page_game, "PAGE_GAME", "idle_timeout_us")
        self.assertEqual(c_int_expression_us(expression), 5 * 60 * 1_000_000)

    def test_poll_prefers_page_override_and_falls_back_to_default(self) -> None:
        body = function_body(self.sleep_ctl, "sleep_ctl_poll")
        self.assertIn("shell_idle_timeout_us()", body)
        # 0 表示页面未覆盖;此时必须回到全局默认,而不是把 0 当“立即休眠”。
        self.assertIn("idle_us <= 0", body)
        self.assertIn("idle_us = SLEEP_IDLE_US", body)

    def test_global_default_idle_timeout_is_two_minutes(self) -> None:
        self.assertRegex(
            self.sleep_ctl, r"#define\s+SLEEP_IDLE_US\s+\(120LL \* 1000 \* 1000\)"
        )

    def test_shell_returns_zero_without_a_page_override(self) -> None:
        body = function_body(self.shell, "shell_idle_timeout_us")
        self.assertIn("cur->idle_timeout_us", body)
        self.assertIn(": 0", body)

    def test_no_page_disables_auto_sleep_outright(self) -> None:
        # 旧的 no_auto_sleep 通路(对局中永不休眠)已被页面级时限取代;
        # 若它重新出现,说明两条机制并行,时限契约将失去唯一解释。
        for path in (self.shell_header, self.shell, self.sleep_ctl, self.page_game):
            self.assertNotIn("no_auto_sleep", path)


if __name__ == "__main__":
    unittest.main()
