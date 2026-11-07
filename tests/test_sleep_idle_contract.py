#!/usr/bin/env python3
"""Static contracts for the per-page idle sleep wiring (light then deep)."""

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
        cls.bsp_button_header = read("components/bsp/include/bsp_button.h")

    def test_game_page_overrides_idle_timeout_to_five_minutes(self) -> None:
        expression = page_field(self.page_game, "PAGE_GAME", "idle_timeout_us")
        self.assertEqual(c_int_expression_us(expression), 5 * 60 * 1_000_000)

    def test_game_page_enables_light_sleep_at_two_minutes(self) -> None:
        expression = page_field(self.page_game, "PAGE_GAME", "light_after_us")
        self.assertEqual(c_int_expression_us(expression), 2 * 60 * 1_000_000)

    def test_poll_routes_through_the_policy_and_falls_back_to_default(self) -> None:
        body = function_body(self.sleep_ctl, "sleep_ctl_poll")
        self.assertIn("sleep_policy_next(", body)
        self.assertIn("shell_light_after_us()", body)
        self.assertIn("shell_idle_timeout_us()", body)
        # 0 表示页面未覆盖;此时必须回到全局默认,而不是把 0 当“立即休眠”。
        self.assertIn("deep_us <= 0", body)
        self.assertIn("deep_us = SLEEP_IDLE_US", body)
        # 浅睡会话返回 false(静默到期)时必须转深睡,不允许静默停在黑屏浅睡。
        self.assertIn("light_sleep_session(deep_us)", body)
        self.assertIn("sleep_enter_deep()", body)

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

    def test_light_sleep_session_arms_and_disarms_both_wake_sources(self) -> None:
        body = function_body(self.sleep_ctl, "light_sleep_session")
        # 双唤醒源:GPIO0 低电平(按键)+ 1s 哨兵定时器。
        self.assertIn("gpio_wakeup_enable((gpio_num_t)BSP_BTN_GPIO, GPIO_INTR_LOW_LEVEL)", body)
        self.assertIn("esp_sleep_enable_gpio_wakeup()", body)
        self.assertIn("esp_sleep_enable_timer_wakeup(", body)
        self.assertIn("esp_light_sleep_start()", body)
        # 每次退出会话都要撤防,不留武装的唤醒源给后续路径。
        self.assertIn("light_sleep_disarm()", body)
        # 唤醒键按下事件被停轮询吞掉:恢复轮询前必须等松手,防按住的键立即触发动作。
        self.assertIn("BSP_BTN_MV_RELEASED_MIN", body)
        self.assertIn("bsp_button_restore_after_light_sleep()", body)
        # 帧缓存原样保留(不画任何提示),恢复背光走设置档位。
        self.assertIn("bsp_display_backlight(0)", body)
        self.assertIn("bsp_display_backlight((uint8_t)sleep_brightness_percent())", body)

    def test_light_sleep_button_handback_is_paired_nonterminal(self) -> None:
        # 浅睡走停轮询+交回引脚(可恢复);终态的 button_cleanup 只属于深睡准备。
        prepare = function_body(read("components/bsp/src/bsp_button.c"),
                                "bsp_button_prepare_light_sleep")
        restore = function_body(read("components/bsp/src/bsp_button.c"),
                                "bsp_button_restore_after_light_sleep")
        self.assertIn("iot_button_stop()", prepare)
        self.assertIn("gpio_set_direction(pin, GPIO_MODE_INPUT)", prepare)
        self.assertNotIn("button_cleanup()", prepare)
        self.assertIn("iot_button_resume()", restore)
        # 契约文档:调用方必须知道两者成对、prepare 非终态。
        self.assertIn("非终态", self.bsp_button_header)

    def test_brightness_table_has_a_single_source(self) -> None:
        # 亮度档位表只在 sleep_ctl.c 出现;app_main 改调 sleep_brightness_percent()。
        app_main = read("main/app_main.c")
        self.assertNotIn("BRIGHTNESS[4]", app_main)
        self.assertIn("sleep_brightness_percent()", app_main)
        body = function_body(self.sleep_ctl, "sleep_brightness_percent")
        self.assertIn("BRIGHTNESS_PCT", body)
        self.assertIn("sv->settings.brightness % 4", body)


if __name__ == "__main__":
    unittest.main()
