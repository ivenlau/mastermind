// main/achieve.c —— 见 achieve.h。
#include "achieve.h"

#include <string.h>

static uint32_t min_steps_any(const ach_stats_t *s) {
    uint32_t m = 0;
    for (int i = 0; i < MODE_COUNT; i++) {
        if (s->best_steps[i] && (m == 0 || s->best_steps[i] < m)) m = s->best_steps[i];
    }
    return m;
}

static uint32_t popcount32(uint32_t v) {
    uint32_t n = 0;
    while (v) {
        n += v & 1u;
        v >>= 1;
    }
    return n;
}

void ach_apply_game(ach_stats_t *s, const ach_game_result_t *r) {
    s->total_games++;
    if (r->won) {
        s->total_wins++;
        s->wins_per_mode[r->mode]++;
        if (s->best_steps[r->mode] == 0 || r->steps_used < s->best_steps[r->mode]) {
            s->best_steps[r->mode] = r->steps_used;
        }
        if (r->forced) s->forced_wins++;
        if (r->night_win) s->night_wins++;
        if (r->steps_used >= r->max_guesses) s->last_step_wins++;
        if (r->mode == MODE_MASTER && !r->tools_used) s->no_tool_master = 1;

        s->cur_streak++;
        if (s->cur_streak > s->best_streak) s->best_streak = s->cur_streak;
        if (r->tools_used) {
            s->no_tool_streak = 0;
        } else {
            s->no_tool_streak++;
            if (s->no_tool_streak > s->best_no_tool_streak) {
                s->best_no_tool_streak = s->no_tool_streak;
            }
        }
    } else {
        if (!r->fuse_protect) s->cur_streak = 0;
        s->no_tool_streak = 0;
    }

    if (r->is_daily && r->won) {
        // 连签:同日重复完成不叠加,断档从 1 重新起算。
        if (r->daily_day == s->daily_last_day) {
            // 当天已计,不动
        } else if (s->daily_last_day && r->daily_day == s->daily_last_day + 1) {
            s->daily_streak++;
        } else {
            s->daily_streak = 1;
        }
        if (r->daily_day > s->daily_last_day) s->daily_last_day = r->daily_day;
        if (s->daily_streak > s->daily_best_streak) s->daily_best_streak = s->daily_streak;
        if (r->daily_rule < ACH_DAILY_RULES) s->daily_rules_mask |= 1u << r->daily_rule;
    }

    s->xp += gr_xp_gain(r->score);
}

void ach_apply_tool(ach_stats_t *s) {
    s->tool_uses++;
}

void ach_apply_theme(ach_stats_t *s, int theme) {
    if (theme >= 0 && theme < ACH_THEME_COUNT) s->themes_mask |= 1u << theme;
}

// 用宏把"条件 → 成就"写成一张表,判定与进度共用同一份阈值,不会漂移。
#define ACH_CASE(id, cond) case id: return (cond)

static bool unlocked_one(const ach_stats_t *s, ach_id_t id) {
    switch (id) {
        ACH_CASE(ACH_FIRST_WIN,        s->total_wins >= 1);
        ACH_CASE(ACH_WIN_10,           s->total_wins >= 10);
        ACH_CASE(ACH_WIN_50,           s->total_wins >= 50);
        ACH_CASE(ACH_FAST_STANDARD,    s->best_steps[MODE_STANDARD] >= 1
                                       && s->best_steps[MODE_STANDARD] <= 5);
        ACH_CASE(ACH_FAST_CLASSIC,     s->best_steps[MODE_CLASSIC] >= 1
                                       && s->best_steps[MODE_CLASSIC] <= 5);
        ACH_CASE(ACH_FIRST_MASTER,     s->wins_per_mode[MODE_MASTER] >= 1);
        ACH_CASE(ACH_NO_TOOL_MASTER,   s->no_tool_master >= 1);
        ACH_CASE(ACH_FORCED_5,         s->forced_wins >= 5);
        ACH_CASE(ACH_LUCKY_FIRST,      min_steps_any(s) == 1);
        ACH_CASE(ACH_LAST_GUESS,       s->last_step_wins >= 1);
        ACH_CASE(ACH_STREAK_3,         s->best_streak >= 3);
        ACH_CASE(ACH_STREAK_10,        s->best_streak >= 10);
        ACH_CASE(ACH_DIGITS_FIRST,     s->wins_per_mode[MODE_DIGITS] >= 1);
        ACH_CASE(ACH_DIGITS_FAST,      s->best_steps[MODE_DIGITS] >= 1
                                       && s->best_steps[MODE_DIGITS] <= 5);
        ACH_CASE(ACH_DAILY_3,          s->daily_best_streak >= 3);
        ACH_CASE(ACH_DAILY_7,          s->daily_best_streak >= 7);
        ACH_CASE(ACH_DAILY_ALL_RULES,  popcount32(s->daily_rules_mask) >= ACH_DAILY_RULES);
        ACH_CASE(ACH_FIRST_TOOL,       s->tool_uses >= 1);
        ACH_CASE(ACH_NO_TOOL_STREAK_5, s->best_no_tool_streak >= 5);
        ACH_CASE(ACH_ALL_THEMES,       popcount32(s->themes_mask) >= ACH_THEME_COUNT);
        ACH_CASE(ACH_100_GAMES,        s->total_games >= 100);
        ACH_CASE(ACH_NIGHT_OWL,        s->night_wins >= 1);
        ACH_CASE(ACH_RANK_MASTER,      gr_rank(s->xp) >= 3);
    case ACH_ALL_MODES:
        for (int i = 0; i < MODE_COUNT; i++) {
            if (s->wins_per_mode[i] == 0) return false;
        }
        return true;
    default:
        return false;
    }
}

void ach_eval(const ach_stats_t *s, bool unlocked[ACH_COUNT]) {
    for (int i = 0; i < ACH_COUNT; i++) unlocked[i] = unlocked_one(s, (ach_id_t)i);
}

bool ach_is_unlocked(const ach_stats_t *s, ach_id_t id) {
    return unlocked_one(s, id);
}

// 文案表。中文字符必须全在字体字符清单内(tools/gen_font_inventory.py 核对)。
const char *ach_name(ach_id_t id) {
    static const char *NAMES[ACH_COUNT] = {
        "初出茅庐", "破译学徒", "破译专家", "逻辑大师", "步步为营",
        "神探驾到", "滴水不漏", "完美推理", "天选之人", "极限翻盘",
        "连胜三局", "连胜十局", "数字先锋", "心算高手", "每日坚持",
        "密令常客", "规则大师", "初次用药", "赤手空拳", "主题收藏家",
        "阅珠无数", "凌晨行动", "大器晚成", "五项全能",
    };
    return (id < ACH_COUNT) ? NAMES[id] : "?";
}

const char *ach_desc(ach_id_t id) {
    static const char *DESCS[ACH_COUNT] = {
        "首次破译任意密码",
        "累计破译 10 次",
        "累计破译 50 次",
        "标准模式 5 步内破译",
        "经典模式 5 步内破译",
        "大师模式首次破译",
        "不用道具通关大师模式",
        "逻辑必然获胜 5 次",
        "第 1 步就猜中",
        "用满步数后一击破译",
        "任意模式连胜 3 局",
        "任意模式连胜 10 局",
        "数字密码首次破译",
        "数字密码 5 步内破译",
        "连续 3 天完成每日密令",
        "连续 7 天完成每日密令",
        "完成全部 7 种日程",
        "首次使用道具",
        "不用道具连胜 5 局",
        "试遍全部 5 套主题",
        "累计对局 100 局",
        "凌晨 0 点到 5 点破译",
        "军衔升至神探",
        "五种模式各破译一次",
    };
    return (id < ACH_COUNT) ? DESCS[id] : "?";
}

bool ach_hidden(ach_id_t id) {
    switch (id) {
    case ACH_LUCKY_FIRST:
    case ACH_LAST_GUESS:
    case ACH_100_GAMES:
    case ACH_NIGHT_OWL:
        return true;
    default:
        return false;
    }
}

bool ach_progress(const ach_stats_t *s, ach_id_t id, uint32_t *cur, uint32_t *target) {
    uint32_t c = 0, t = 0;
    switch (id) {
    case ACH_WIN_10:           c = s->total_wins; t = 10; break;
    case ACH_WIN_50:           c = s->total_wins; t = 50; break;
    case ACH_FORCED_5:         c = s->forced_wins; t = 5; break;
    case ACH_STREAK_3:         c = s->best_streak; t = 3; break;
    case ACH_STREAK_10:        c = s->best_streak; t = 10; break;
    case ACH_DAILY_3:          c = s->daily_best_streak; t = 3; break;
    case ACH_DAILY_7:          c = s->daily_best_streak; t = 7; break;
    case ACH_DAILY_ALL_RULES:  c = popcount32(s->daily_rules_mask); t = ACH_DAILY_RULES; break;
    case ACH_NO_TOOL_STREAK_5: c = s->best_no_tool_streak; t = 5; break;
    case ACH_ALL_THEMES:       c = popcount32(s->themes_mask); t = ACH_THEME_COUNT; break;
    case ACH_100_GAMES:        c = s->total_games; t = 100; break;
    case ACH_RANK_MASTER:      c = (uint32_t)(s->xp < 0 ? 0 : s->xp); t = 3500; break;
    case ACH_ALL_MODES: {
        for (int i = 0; i < MODE_COUNT; i++) {
            if (s->wins_per_mode[i]) c++;
        }
        t = MODE_COUNT;
        break;
    }
    default:
        return false;
    }
    if (c > t) c = t;
    if (cur) *cur = c;
    if (target) *target = t;
    return true;
}
