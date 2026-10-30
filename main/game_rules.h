// main/game_rules.h —— 模式参数、每日密令轮换、评分/星级/军衔(纯逻辑,host 可测)。
#pragma once

#include "mastermind_core.h"

// 五种对局模式。参数即需求定版:标准 6-4-10 不可重复 / 经典 7-4-10 不可重复 /
// 专家 7-5-12 可重复 / 大师 8-5-10 可重复 / 数字密码 0-9 四位不可重复 10 步。
// "不可重复"同时约束密码与猜测(数字密码即经典 Bulls & Cows 惯例)。
typedef enum {
    MODE_STANDARD = 0,
    MODE_CLASSIC,
    MODE_EXPERT,
    MODE_MASTER,
    MODE_DIGITS,
    MODE_COUNT
} game_mode_t;

mm_rules_t gr_mode_rules(game_mode_t mode);
const char *gr_mode_name(game_mode_t mode);   // "标准模式" 等(UI 中文)
// "6色 · 4位 · 10步 · 不重复" 参数摘要(UI 用,静态字符串)。
const char *gr_mode_params(game_mode_t mode);

// ---------------------------------------------------------------------------
// 每日密令:按"距 epoch 天数"轮换 7 种日程(与需求的日程表一致)。
// ---------------------------------------------------------------------------
typedef struct {
    mm_rules_t rules;
    game_mode_t mode;      // 成就归类用的基础模式
    bool dark;             // 暗牌日(只报绿钉)
    bool rainbow;          // 彩虹日(密码含彩虹珠)
    const char *name;      // "标准日" 等
} gr_daily_t;

gr_daily_t gr_daily_rules(uint32_t day_index);
// 出题种子固定由 day_index 导出,同一天全设备同题。
uint32_t gr_daily_seed(uint32_t day_index);

// ---------------------------------------------------------------------------
// 评分:1000 基础 + 速度(90s 内每省 1s +10) + 剩余步数×150 + 逻辑必然 +500,
// 再乘连胜倍率(1 + 0.1×连胜,上限 2.0),每件道具 ×0.8。
// ---------------------------------------------------------------------------
typedef struct {
    uint16_t elapsed_s;    // 本局用时
    uint8_t steps_left;    // 破译时剩余步数
    uint8_t tools_used;    // 本局使用道具件数
    uint16_t streak;       // 破译前的连胜数(本局计入之前的连胜)
    bool forced;           // 逻辑必然胜
} gr_score_in_t;

int gr_score(const gr_score_in_t *in);

// 星级:0=未胜;3=无道具且剩余步数≥半;2=无道具;1=用了道具通关。
int gr_stars(bool won, bool tools_used, uint8_t steps_left, uint8_t max_guesses);

// XP 每局 = score/10;军衔 6 档。阈值即设计定版。
int gr_xp_gain(int score);
int gr_rank(int xp);                    // 0..5
const char *gr_rank_name(int rank);     // "见习生".."传奇密码机"
int gr_rank_floor(int rank);            // 本档 XP 下限(进度条用)
