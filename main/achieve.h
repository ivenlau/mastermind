// main/achieve.h —— 成就系统(纯逻辑):统计汇总、事件推进、解锁判定、进度。
// 存储层(store.c)只负责把 ach_stats_t 落 NVS,判定逻辑全在这里,host 可测。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "game_rules.h"

#define ACH_THEME_COUNT 5     // 与 ui_theme 的主题数一致
#define ACH_DAILY_RULES 7     // 每日日程种数(标准/经典/专家/大师/数字/暗牌/彩虹)

typedef enum {
    ACH_FIRST_WIN = 0,     // 初出茅庐:首次破译
    ACH_WIN_10,            // 破译学徒:累计 10 胜
    ACH_WIN_50,            // 破译专家:累计 50 胜
    ACH_FAST_STANDARD,     // 逻辑大师:标准模式 5 步内
    ACH_FAST_CLASSIC,      // 步步为营:经典模式 5 步内
    ACH_FIRST_MASTER,      // 神探驾到:大师模式首胜
    ACH_NO_TOOL_MASTER,    // 滴水不漏:无道具通关大师模式
    ACH_FORCED_5,          // 完美推理:逻辑必然 5 次
    ACH_LUCKY_FIRST,       // 天选之人:1 步即中(隐藏)
    ACH_LAST_GUESS,        // 极限翻盘:最后一步命中(隐藏)
    ACH_STREAK_3,          // 连胜三局
    ACH_STREAK_10,         // 连胜十局
    ACH_DIGITS_FIRST,      // 数字先锋:数字密码首胜
    ACH_DIGITS_FAST,       // 心算高手:数字密码 5 步内
    ACH_DAILY_3,           // 每日坚持:连签 3 天
    ACH_DAILY_7,           // 密令常客:连签 7 天
    ACH_DAILY_ALL_RULES,   // 规则大师:7 种日程各完成一次
    ACH_FIRST_TOOL,        // 初次用药:首次使用道具
    ACH_NO_TOOL_STREAK_5,  // 赤手空拳:无道具连胜 5 局
    ACH_ALL_THEMES,        // 主题收藏家:试遍全部主题
    ACH_100_GAMES,         // 阅珠无数:累计 100 局(隐藏)
    ACH_NIGHT_OWL,         // 凌晨行动:0-5 点破译(隐藏)
    ACH_RANK_MASTER,       // 大器晚成:军衔升至神探
    ACH_ALL_MODES,         // 五项全能:五模式各胜一次
    ACH_COUNT
} ach_id_t;

// 全部持久统计。store.c 以二进制 blob 存 NVS,字段只增不改序。
typedef struct {
    uint32_t total_games;
    uint32_t total_wins;
    uint32_t wins_per_mode[MODE_COUNT];
    uint32_t forced_wins;
    uint32_t last_step_wins;          // 用满步数才破译(最后一步命中)
    uint32_t no_tool_master;          // 无道具通关大师(0/1)
    uint32_t cur_streak, best_streak; // 胜负连胜
    uint32_t best_steps[MODE_COUNT];  // 各模式最小破译步数,0=尚无
    uint32_t tool_uses;
    uint32_t no_tool_streak, best_no_tool_streak;
    uint32_t daily_last_day;          // 上次完成每日的 day_index,0=无
    uint32_t daily_streak, daily_best_streak;
    uint32_t daily_rules_mask;        // bit i=完成过 day%7==i 的日程
    uint32_t themes_mask;             // bit i=试用过主题 i
    uint32_t night_wins;              // 0-5 点破译次数
    int32_t xp;
} ach_stats_t;

// 一局结算输入(夜间/每日判定所需的时间信息由上层换算好传入)。
typedef struct {
    game_mode_t mode;
    bool won;
    uint8_t steps_used;
    uint8_t max_guesses;      // 本局规则步数上限(判定"最后一步命中"用)
    bool tools_used;
    bool forced;
    bool fuse_protect;       // 保险丝生效:失败不破连胜
    bool night_win;          // 破译时刻在 0-5 点
    bool is_daily;
    uint32_t daily_day;      // is_daily 时有效
    uint32_t daily_rule;     // is_daily 时有效,0..6
    int score;               // gr_score 结果,用于 XP
} ach_game_result_t;

// 推进统计(胜负都调用;一局只调一次)。
void ach_apply_game(ach_stats_t *s, const ach_game_result_t *r);
void ach_apply_tool(ach_stats_t *s);
void ach_apply_theme(ach_stats_t *s, int theme);   // 0..ACH_THEME_COUNT-1

// 解锁判定:unlocked[ACH_COUNT] 全量重算(幂等,状态无"已解锁"位,
// 由 store 侧与已授列表合并展示;此处只回答"条件是否达成")。
void ach_eval(const ach_stats_t *s, bool unlocked[ACH_COUNT]);
bool ach_is_unlocked(const ach_stats_t *s, ach_id_t id);

// 文案与展示属性(UI 表格化,避免散落字符串)。
const char *ach_name(ach_id_t id);
const char *ach_desc(ach_id_t id);
bool ach_hidden(ach_id_t id);

// 进度:cur/target(无进度概念时返回 false)。
bool ach_progress(const ach_stats_t *s, ach_id_t id, uint32_t *cur, uint32_t *target);
