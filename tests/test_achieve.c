// achieve 的 host 测试:事件推进、解锁判定、每日连签、进度。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "achieve.h"

static ach_game_result_t win(game_mode_t mode, uint8_t steps, bool tools, bool forced) {
    ach_game_result_t r = { 0 };
    r.mode = mode;
    r.won = true;
    r.steps_used = steps;
    r.max_guesses = 10;
    r.tools_used = tools;
    r.forced = forced;
    r.score = 2000;
    return r;
}

static ach_game_result_t loss(game_mode_t mode) {
    ach_game_result_t r = { 0 };
    r.mode = mode;
    r.won = false;
    r.steps_used = 10;
    r.max_guesses = 10;
    r.score = 0;
    return r;
}

// 传值包装:测试里大量"一步结算"调用更顺手。
static void apply(ach_stats_t *s, ach_game_result_t r) {
    ach_apply_game(s, &r);
}

int main(void) {
    ach_stats_t s;
    bool unlocked[ACH_COUNT];
    ach_game_result_t r;

    // ---- 全新档案:一无所有 ----
    memset(&s, 0, sizeof(s));
    ach_eval(&s, unlocked);
    for (int i = 0; i < ACH_COUNT; i++) assert(!unlocked[i]);

    // ---- 首胜 + 步数纪录 ----
    memset(&s, 0, sizeof(s));
    apply(&s, win(MODE_STANDARD, 3, false, false));
    assert(ach_is_unlocked(&s, ACH_FIRST_WIN));
    assert(ach_is_unlocked(&s, ACH_FAST_STANDARD));          // 3 ≤ 5
    assert(s.best_steps[MODE_STANDARD] == 3 && s.cur_streak == 1);

    // ---- 天选之人 / 极限翻盘 ----
    memset(&s, 0, sizeof(s));
    apply(&s, win(MODE_CLASSIC, 1, false, false));
    assert(ach_is_unlocked(&s, ACH_LUCKY_FIRST));
    assert(!ach_is_unlocked(&s, ACH_LAST_GUESS));
    memset(&s, 0, sizeof(s));
    apply(&s, win(MODE_CLASSIC, 10, false, false));          // 用满 10 步才中
    assert(ach_is_unlocked(&s, ACH_LAST_GUESS));
    assert(!ach_is_unlocked(&s, ACH_LUCKY_FIRST));

    // ---- 逻辑必然 5 次 ----
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < 4; i++) apply(&s, win(MODE_EXPERT, 6, false, true));
    assert(!ach_is_unlocked(&s, ACH_FORCED_5));
    apply(&s, win(MODE_EXPERT, 6, false, true));
    assert(ach_is_unlocked(&s, ACH_FORCED_5));

    // ---- 连胜与失败清零 ----
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < 3; i++) apply(&s, win(MODE_STANDARD, 4, false, false));
    assert(ach_is_unlocked(&s, ACH_STREAK_3));
    assert(!ach_is_unlocked(&s, ACH_STREAK_10));
    apply(&s, loss(MODE_STANDARD));
    assert(s.cur_streak == 0 && s.best_streak == 3);         // 纪录保留

    // ---- 保险丝:失败不破连胜 ----
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < 2; i++) apply(&s, win(MODE_STANDARD, 4, false, false));
    r = loss(MODE_STANDARD);
    r.fuse_protect = true;
    apply(&s, r);
    assert(s.cur_streak == 2);                               // 保住了
    apply(&s, loss(MODE_STANDARD));
    assert(s.cur_streak == 0);                               // 没保险丝就清

    // ---- 大师模式(含无道具) ----
    memset(&s, 0, sizeof(s));
    apply(&s, win(MODE_MASTER, 6, true, false));             // 用道具不算滴水不漏
    assert(ach_is_unlocked(&s, ACH_FIRST_MASTER));
    assert(!ach_is_unlocked(&s, ACH_NO_TOOL_MASTER));
    memset(&s, 0, sizeof(s));
    apply(&s, win(MODE_MASTER, 6, false, false));
    assert(ach_is_unlocked(&s, ACH_NO_TOOL_MASTER));

    // ---- 数字密码 ----
    memset(&s, 0, sizeof(s));
    apply(&s, win(MODE_DIGITS, 5, false, false));
    assert(ach_is_unlocked(&s, ACH_DIGITS_FIRST));
    assert(ach_is_unlocked(&s, ACH_DIGITS_FAST));

    // ---- 每日连签:隔天 +1、同日不叠、断档重来 ----
    memset(&s, 0, sizeof(s));
    r = win(MODE_STANDARD, 4, false, false);
    r.is_daily = true; r.daily_rule = 0; r.daily_day = 100;
    apply(&s, r);
    apply(&s, r);                                            // 同日再胜不叠加
    assert(s.daily_streak == 1);
    r.daily_day = 101;
    apply(&s, r);
    r.daily_day = 102;
    apply(&s, r);
    assert(s.daily_streak == 3);
    assert(ach_is_unlocked(&s, ACH_DAILY_3));
    r.daily_day = 200;                                       // 断档,从 1 重来
    apply(&s, r);
    assert(s.daily_streak == 1 && s.daily_best_streak == 3);

    // ---- 7 种日程全完成 ----
    memset(&s, 0, sizeof(s));
    for (uint32_t rule = 0; rule < ACH_DAILY_RULES; rule++) {
        r = win(MODE_STANDARD, 4, false, false);
        r.is_daily = true;
        r.daily_rule = rule;
        r.daily_day = 10 + rule;
        apply(&s, r);
    }
    assert(ach_is_unlocked(&s, ACH_DAILY_ALL_RULES));

    // ---- 道具 / 无道具连胜 ----
    memset(&s, 0, sizeof(s));
    ach_apply_tool(&s);
    assert(ach_is_unlocked(&s, ACH_FIRST_TOOL));
    for (int i = 0; i < 5; i++) apply(&s, win(MODE_STANDARD, 4, false, false));
    assert(ach_is_unlocked(&s, ACH_NO_TOOL_STREAK_5));
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < 4; i++) apply(&s, win(MODE_STANDARD, 4, false, false));
    apply(&s, win(MODE_STANDARD, 4, true, false));           // 中间用了道具 → 断
    assert(!ach_is_unlocked(&s, ACH_NO_TOOL_STREAK_5));

    // ---- 主题收藏家 ----
    memset(&s, 0, sizeof(s));
    for (int t = 0; t < ACH_THEME_COUNT; t++) ach_apply_theme(&s, t);
    assert(ach_is_unlocked(&s, ACH_ALL_THEMES));
    assert(s.themes_mask == (1u << ACH_THEME_COUNT) - 1);

    // ---- 五项全能 ----
    memset(&s, 0, sizeof(s));
    for (int m = 0; m < MODE_COUNT; m++) apply(&s, win((game_mode_t)m, 4, false, false));
    assert(ach_is_unlocked(&s, ACH_ALL_MODES));

    // ---- 隐藏:100 局 / 凌晨 ----
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < 99; i++) apply(&s, loss(MODE_STANDARD));
    assert(!ach_is_unlocked(&s, ACH_100_GAMES));
    apply(&s, loss(MODE_STANDARD));
    assert(ach_is_unlocked(&s, ACH_100_GAMES));
    memset(&s, 0, sizeof(s));
    r = win(MODE_STANDARD, 4, false, false);
    r.night_win = true;
    apply(&s, r);
    assert(ach_is_unlocked(&s, ACH_NIGHT_OWL));

    // ---- 军衔 ----
    memset(&s, 0, sizeof(s));
    s.xp = 3500;
    assert(ach_is_unlocked(&s, ACH_RANK_MASTER));

    // ---- 文案与进度 ----
    for (int i = 0; i < ACH_COUNT; i++) {
        assert(ach_name((ach_id_t)i)[0] != '?');
        assert(ach_desc((ach_id_t)i)[0] != '?');
    }
    assert(ach_hidden(ACH_LUCKY_FIRST) && ach_hidden(ACH_NIGHT_OWL));
    assert(!ach_hidden(ACH_FIRST_WIN));
    memset(&s, 0, sizeof(s));
    s.total_wins = 4;
    uint32_t cur = 0, target = 0;
    assert(ach_progress(&s, ACH_WIN_10, &cur, &target));
    assert(cur == 4 && target == 10);
    assert(!ach_progress(&s, ACH_FIRST_WIN, NULL, NULL));    // 无进度概念

    puts("achieve tests: PASS");
    return 0;
}
