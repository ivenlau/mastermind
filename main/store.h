// main/store.h —— NVS 持久化:设置、成就统计、道具库存、每日状态、日期锚点。
// 全部在一个版本化 blob 里;修改结构必须加版本迁移,不得默默重置用户进度。
#pragma once

#include "esp_err.h"

#include "achieve.h"

#define STORE_TOOL_COUNT 3     // 显影剂 / 排除仪 / 保险丝

typedef enum {
    TOOL_REVEAL = 0,     // 显影剂:揭示一格的正确颜色与位置
    TOOL_SCANNER,        // 排除仪:查询某颜色在密码中的数量
    TOOL_FUSE,           // 保险丝:本局失败不破连胜
    TOOL_COUNT
} tool_id_t;

typedef struct {
    // 设置
    uint8_t theme;               // 0..UI_THEME_COUNT-1
    uint8_t sound_on;            // 0/1
    uint8_t volume;              // 0..3
    uint8_t brightness;          // 0..3 → 20/50/80/100%
    uint8_t tools[STORE_TOOL_COUNT];   // 库存
    // 时间锚点:设备离线无 RTC,日期由用户在设置里设定一次;此后按
    // esp_timer 单调推进。date_day==0 表示未设置,每日密令拒绝开局。
    uint32_t date_day;           // 设定时的 day_index(距 epoch 天数)
    uint32_t date_sec;           // 设定时当天已过秒数 0..86399
    int64_t date_us;             // 设定时刻的 esp_timer 时间戳
    uint32_t best_score;         // 任意模式最高分
    uint8_t daily_done;          // 今日已完成标记(0/1;日期跨天自动清)
    uint32_t daily_done_day;     // 上次标记完成的 day_index
} app_settings_t;

typedef struct {
    app_settings_t settings;
    ach_stats_t stats;
    uint8_t ach_seen[ACH_COUNT];  // 已授予(解锁 toast 只弹一次)
    // —— v3 新增字段必须追加在末尾:旧存档是本结构的前缀,可无损迁移 ——
    uint32_t tool_supply_day;     // 上次道具每日补给的 day_index(0=未补过)
    uint32_t tool_reward_day;     // 上次领取每日密令奖励的 day_index
} app_save_t;

esp_err_t store_init(void);
app_save_t *store_get(void);
esp_err_t store_flush(void);              // 有改动才写;局末/设置变更时调用
void store_reset_progress(void);          // 清成就与统计,保留设置

// 道具经济(需求):每个新的 day_index(须已设置日期)把各道具补给到至少 1;
// 未设置日期不补给。启动/进主页/局末调用。
void store_daily_tick(void);

// 时间(日期未设置时 time_is_set()==false,day/hour 无意义返回 0)
bool time_is_set(void);
uint32_t time_day_index(void);
int time_hour(void);                      // 0..23
// 反推当前公历(显示用);未设置时输出全 0。
void time_get_date(int *year, int *month, int *day, int *hour);
esp_err_t time_set_date(int year, int month, int day, int hour);
