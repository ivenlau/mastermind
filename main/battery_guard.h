// main/battery_guard.h —— 低电量策略(纯逻辑,不碰硬件,host 测试覆盖)。
// 逻辑移植自 passport-os 的 battery_guard.h(同源项目,保留其阈值语义):
// 每次轮询喂入 soc(%) 与 mv(毫伏),返回处置动作。两次连续判空才关机,
// 防止扬声器/射频瞬时压降导致误判;充电器在位时电压高,3% 读数不可信。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BATTERY_WARN_SOC        10    // ≤10% 低电警告
#define BATTERY_REARM_SOC       15    // 回到 ≥15% 重新武装警告
#define BATTERY_EMPTY_SOC       3     // ≤3% 视为耗尽
#define BATTERY_EMPTY_MV        3450  // 低于此电压直接视为耗尽
#define BATTERY_CHARGING_MV     3600  // 充电中电压高,低 SOC 读数不可信

typedef struct {
    bool warned;
    uint8_t critical_polls;
} battery_guard_t;

typedef enum {
    BATTERY_OK = 0,
    BATTERY_WARN,       // 一次性警告(重新武装前不再重复)
    BATTERY_SHUTDOWN,   // 耗尽:保存后休眠保电
} battery_action_t;

static inline battery_action_t battery_guard_step(battery_guard_t *g, int soc, int mv) {
    const bool low_voltage = mv > 0 && mv < BATTERY_EMPTY_MV;
    const bool empty_soc = soc >= 0 && soc <= BATTERY_EMPTY_SOC && (mv <= 0 || mv < BATTERY_CHARGING_MV);
    if (low_voltage || empty_soc) {
        if (g->critical_polls < 2) g->critical_polls++;
    } else {
        g->critical_polls = 0;
    }
    if (g->critical_polls >= 2) return BATTERY_SHUTDOWN;
    if (soc >= BATTERY_REARM_SOC) g->warned = false;
    if (!g->warned && soc >= 0 && soc <= BATTERY_WARN_SOC) {
        g->warned = true;
        return BATTERY_WARN;
    }
    return BATTERY_OK;
}
