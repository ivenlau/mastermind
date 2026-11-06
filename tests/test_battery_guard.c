// battery_guard 的 host 测试:阈值、两连判空、充电电压保护、警告重新武装。
#include <assert.h>
#include <stdio.h>

#include "battery_guard.h"

int main(void) {
    battery_guard_t g = { 0 };

    // 健康电量:无动作
    assert(battery_guard_step(&g, 80, 4000) == BATTERY_OK);
    assert(battery_guard_step(&g, 50, 3900) == BATTERY_OK);

    // ≤10% 触发一次警告;同区不再重复
    assert(battery_guard_step(&g, 10, 3800) == BATTERY_WARN);
    assert(battery_guard_step(&g, 9, 3800) == BATTERY_OK);
    // 回到 ≥15% 重新武装
    assert(battery_guard_step(&g, 15, 3900) == BATTERY_OK);
    assert(battery_guard_step(&g, 10, 3800) == BATTERY_WARN);

    // 单次低电读数:先警告不关机(瞬时压降保护);两连才关机保电
    battery_guard_t g1 = { 0 };
    assert(battery_guard_step(&g1, 2, 3500) == BATTERY_WARN);
    assert(battery_guard_step(&g1, 2, 3500) == BATTERY_SHUTDOWN);

    // 低电压两连直接关机,与 SOC 无关
    battery_guard_t g2 = { 0 };
    assert(battery_guard_step(&g2, 50, 3400) == BATTERY_OK);
    assert(battery_guard_step(&g2, 50, 3400) == BATTERY_SHUTDOWN);

    // 充电中(电压 ≥3600):低 SOC 仍提示一次,但绝不误关机
    battery_guard_t g3 = { 0 };
    assert(battery_guard_step(&g3, 2, 3700) == BATTERY_WARN);
    assert(battery_guard_step(&g3, 2, 3700) == BATTERY_OK);
    assert(battery_guard_step(&g3, 2, 3700) == BATTERY_OK);

    // 一次坏读数后恢复:计数清零;再低电重新警告,恢复后永不误关机
    battery_guard_t g4 = { 0 };
    assert(battery_guard_step(&g4, 2, 3500) == BATTERY_WARN);
    assert(battery_guard_step(&g4, 60, 3900) == BATTERY_OK);
    assert(battery_guard_step(&g4, 2, 3500) == BATTERY_WARN);
    assert(battery_guard_step(&g4, 60, 3900) == BATTERY_OK);
    assert(battery_guard_step(&g4, 80, 4000) == BATTERY_OK);

    puts("battery guard tests: PASS");
    return 0;
}
