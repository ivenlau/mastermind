// main/game_rules.c —— 见 game_rules.h。
#include "game_rules.h"

mm_rules_t gr_mode_rules(game_mode_t mode) {
    switch (mode) {
    case MODE_STANDARD:
        return (mm_rules_t){ .slots = 4, .colors = 6, .code_no_dup = true, .guess_no_dup = true,
                             .max_guesses = 10 };
    case MODE_CLASSIC:
        return (mm_rules_t){ .slots = 4, .colors = 7, .code_no_dup = true, .guess_no_dup = true,
                             .max_guesses = 10 };
    case MODE_EXPERT:
        return (mm_rules_t){ .slots = 5, .colors = 7, .code_no_dup = false, .guess_no_dup = false,
                             .max_guesses = 12 };
    case MODE_MASTER:
        return (mm_rules_t){ .slots = 5, .colors = 8, .code_no_dup = false, .guess_no_dup = false,
                             .max_guesses = 10 };
    case MODE_DIGITS:
    default:
        return (mm_rules_t){ .slots = 4, .colors = 10, .code_no_dup = true, .guess_no_dup = true,
                             .max_guesses = 10 };
    }
}

const char *gr_mode_name(game_mode_t mode) {
    static const char *NAMES[MODE_COUNT] = {
        "标准模式", "经典模式", "专家模式", "大师模式", "数字密码",
    };
    return (mode < MODE_COUNT) ? NAMES[mode] : "?";
}

const char *gr_mode_params(game_mode_t mode) {
    static const char *PARAMS[MODE_COUNT] = {
        "6色·4位·10步",
        "7色·4位·10步",
        "7色·5位·12步",
        "8色·5位·10步",
        "数字·4位·10步",
    };
    return (mode < MODE_COUNT) ? PARAMS[mode] : "?";
}

// 每日日程:5 个模式日 + 暗牌日 + 彩虹日。改动这里必须同步改成就的 7 日程位图。
gr_daily_t gr_daily_rules(uint32_t day_index) {
    switch (day_index % 7) {
    case 0: {
        gr_daily_t d = { .rules = gr_mode_rules(MODE_STANDARD), .mode = MODE_STANDARD,
                         .name = "标准日" };
        return d;
    }
    case 1: {
        gr_daily_t d = { .rules = gr_mode_rules(MODE_CLASSIC), .mode = MODE_CLASSIC,
                         .name = "经典日" };
        return d;
    }
    case 2: {
        gr_daily_t d = { .rules = gr_mode_rules(MODE_EXPERT), .mode = MODE_EXPERT,
                         .name = "专家日" };
        return d;
    }
    case 3: {
        gr_daily_t d = { .rules = gr_mode_rules(MODE_MASTER), .mode = MODE_MASTER,
                         .name = "大师日" };
        return d;
    }
    case 4: {
        gr_daily_t d = { .rules = gr_mode_rules(MODE_DIGITS), .mode = MODE_DIGITS,
                         .name = "数字日" };
        return d;
    }
    case 5: {
        // 暗牌日:经典 4 位,只报绿钉,多给 2 步。
        gr_daily_t d = { .rules = gr_mode_rules(MODE_CLASSIC), .mode = MODE_CLASSIC,
                         .dark = true, .name = "暗牌日" };
        d.rules.dark = true;
        d.rules.max_guesses = 12;
        return d;
    }
    default: {
        // 彩虹日:标准参数 + 密码含 1 颗万能彩虹珠。
        gr_daily_t d = { .rules = gr_mode_rules(MODE_STANDARD), .mode = MODE_STANDARD,
                         .rainbow = true, .name = "彩虹日" };
        d.rules.rainbow = true;
        d.rules.max_guesses = 11;
        return d;
    }
    }
}

uint32_t gr_daily_seed(uint32_t day_index) {
    // 固定散列:同一天任何设备出题相同。0 会被 mm_rng_seed 换成常量,仍确定。
    return day_index * 2654435761u + 0x5A17u;
}

int gr_score(const gr_score_in_t *in) {
    int score = 1000;
    score += (in->elapsed_s < 90) ? (90 - in->elapsed_s) * 10 : 0;
    score += in->steps_left * 150;
    if (in->forced) score += 500;
    // 连胜倍率 1.0x..2.0x(每连胜 +10%,上限 10 连)。
    int streak = in->streak > 10 ? 10 : in->streak;
    score = score * (100 + streak * 10) / 100;
    // 每件道具 ×0.8。
    for (uint8_t i = 0; i < in->tools_used; i++) score = score * 8 / 10;
    return score;
}

int gr_stars(bool won, bool tools_used, uint8_t steps_left, uint8_t max_guesses) {
    if (!won) return 0;
    if (tools_used) return 1;
    return (steps_left * 2 >= max_guesses) ? 3 : 2;
}

int gr_xp_gain(int score) {
    return score / 10;
}

int gr_rank(int xp) {
    static const int FLOORS[6] = { 0, 500, 1500, 3500, 7000, 12000 };
    int rank = 0;
    for (int i = 5; i >= 0; i--) {
        if (xp >= FLOORS[i]) {
            rank = i;
            break;
        }
    }
    return rank;
}

const char *gr_rank_name(int rank) {
    static const char *NAMES[6] = {
        "见习生", "侦探", "高级侦探", "神探", "密码大师", "传奇密码机",
    };
    return (rank >= 0 && rank < 6) ? NAMES[rank] : "?";
}

int gr_rank_floor(int rank) {
    static const int FLOORS[6] = { 0, 500, 1500, 3500, 7000, 12000 };
    return (rank >= 0 && rank < 6) ? FLOORS[rank] : 0;
}
