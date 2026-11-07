// main/sleep_policy.h —— 静默休眠策略的纯判定逻辑,不依赖 ESP-IDF/LVGL,可宿主测试。
// 时限单位与 sleep_ctl 一致,用微秒。约定:
//   deep_after_us  深睡时限(到达即深睡,唤醒=重启)
//   light_after_us 浅睡时限(0 = 该页禁用浅睡,到 deep 直接深睡)
// 浅睡时限配置得比深睡还晚没有意义:深睡判定优先,自然把这种配置压成"只深睡"。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SLEEP_POLICY_NONE = 0,   // 继续正常待机
    SLEEP_POLICY_LIGHT,      // 进入浅睡(可按键原地恢复)
    SLEEP_POLICY_DEEP,       // 进入深睡
} sleep_action_t;

typedef enum {
    SLEEP_POLICY_CONTINUE = 0,   // 哨兵定时唤醒,无事发生,继续浅睡
    SLEEP_POLICY_RESUME,         // 按键唤醒,恢复外设与对局
    SLEEP_POLICY_DEEP_NOW,       // 静默已到深睡时限,结束浅睡转深睡
} sleep_wake_action_t;

// 唤醒原因位掩码里与按键/哨兵相关的位,取值与 IDF esp_sleep.h 的枚举一致
// (TIMER=4, GPIO=7)。sleep_ctl.c 用 _Static_assert 锁住,枚举漂移时固件编译即失败。
#define SLEEP_POLICY_WAKE_GPIO  (1u << 7)    // ESP_SLEEP_WAKEUP_GPIO
#define SLEEP_POLICY_WAKE_TIMER (1u << 4)    // ESP_SLEEP_WAKEUP_TIMER

static inline sleep_action_t sleep_policy_next(int64_t idle_us, int64_t light_after_us,
                                               int64_t deep_after_us) {
    if (idle_us >= deep_after_us) return SLEEP_POLICY_DEEP;
    if (light_after_us > 0 && idle_us >= light_after_us) return SLEEP_POLICY_LIGHT;
    return SLEEP_POLICY_NONE;
}

// 浅睡中一次 esp_light_sleep_start() 返回后的下一步。按键唤醒优先于一切:
// 即便同时到期,也先恢复对局(下次静默重新计时)。
static inline sleep_wake_action_t sleep_policy_wake_next(uint32_t wake_causes, int64_t idle_us,
                                                         int64_t deep_after_us) {
    if (wake_causes & SLEEP_POLICY_WAKE_GPIO) return SLEEP_POLICY_RESUME;
    if (idle_us >= deep_after_us) return SLEEP_POLICY_DEEP_NOW;
    return SLEEP_POLICY_CONTINUE;
}
