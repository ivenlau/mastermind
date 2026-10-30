// game_rules 的 host 测试:模式参数、每日轮换、评分/星级/军衔。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "game_rules.h"

int main(void) {
    // ---- 模式参数(需求定版) ----
    {
        mm_rules_t r = gr_mode_rules(MODE_STANDARD);
        assert(r.slots == 4 && r.colors == 6 && r.code_no_dup && r.guess_no_dup);
        assert(r.max_guesses == 10 && !r.rainbow && !r.dark);
        r = gr_mode_rules(MODE_CLASSIC);
        assert(r.slots == 4 && r.colors == 7 && r.code_no_dup && r.guess_no_dup);
        assert(r.max_guesses == 10);
        r = gr_mode_rules(MODE_EXPERT);
        assert(r.slots == 5 && r.colors == 7 && !r.code_no_dup && !r.guess_no_dup);
        assert(r.max_guesses == 12);
        r = gr_mode_rules(MODE_MASTER);
        assert(r.slots == 5 && r.colors == 8 && !r.code_no_dup && !r.guess_no_dup);
        assert(r.max_guesses == 10);
        r = gr_mode_rules(MODE_DIGITS);
        assert(r.slots == 4 && r.colors == 10 && r.code_no_dup && r.guess_no_dup);
        assert(r.max_guesses == 10);
    }
    for (int m = 0; m < MODE_COUNT; m++) {
        assert(gr_mode_name((game_mode_t)m)[0] != '?');
        assert(gr_mode_params((game_mode_t)m)[0] != '?');
    }

    // ---- 每日轮换:7 日程参数与命名 ----
    {
        gr_daily_t d = gr_daily_rules(0);
        assert(strcmp(d.name, "标准日") == 0 && d.mode == MODE_STANDARD && !d.dark && !d.rainbow);
        d = gr_daily_rules(1);
        assert(strcmp(d.name, "经典日") == 0 && d.mode == MODE_CLASSIC);
        d = gr_daily_rules(2);
        assert(strcmp(d.name, "专家日") == 0 && d.mode == MODE_EXPERT);
        d = gr_daily_rules(3);
        assert(strcmp(d.name, "大师日") == 0 && d.mode == MODE_MASTER);
        d = gr_daily_rules(4);
        assert(strcmp(d.name, "数字日") == 0 && d.mode == MODE_DIGITS);
        d = gr_daily_rules(5);
        assert(strcmp(d.name, "暗牌日") == 0 && d.dark && d.rules.dark);
        assert(d.rules.max_guesses == 12);
        d = gr_daily_rules(6);
        assert(strcmp(d.name, "彩虹日") == 0 && d.rainbow && d.rules.rainbow);
        assert(d.rules.max_guesses == 11);
        assert(strcmp(gr_daily_rules(7).name, "标准日") == 0);   // 以 7 为周期
    }
    {
        // 播种确定性:同天同种子,不同天种子不同
        assert(gr_daily_seed(123) == gr_daily_seed(123));
        assert(gr_daily_seed(123) != gr_daily_seed(124));
    }

    // ---- 评分 ----
    {
        gr_score_in_t in = { .elapsed_s = 90, .steps_left = 0, .tools_used = 0, .streak = 0 };
        assert(gr_score(&in) == 1000);                       // 无任何加成
        in.elapsed_s = 60;
        in.steps_left = 4;
        assert(gr_score(&in) == 1000 + 300 + 600);           // 速度 300 + 步数 600
        in.forced = true;
        assert(gr_score(&in) == 1000 + 300 + 600 + 500);     // 必然 +500
        in.forced = false;
        in.streak = 3;
        assert(gr_score(&in) == (1000 + 300 + 600) * 130 / 100);
        in.streak = 50;                                      // 倍率封顶 2.0x
        assert(gr_score(&in) == (1000 + 300 + 600) * 200 / 100);
        in.streak = 0;
        in.tools_used = 2;
        assert(gr_score(&in) == (1000 + 300 + 600) * 8 / 10 * 8 / 10);
    }

    // ---- 星级 ----
    assert(gr_stars(false, false, 10, 10) == 0);
    assert(gr_stars(true, false, 5, 10) == 3);               // 剩余过半
    assert(gr_stars(true, false, 4, 10) == 2);
    assert(gr_stars(true, true, 9, 10) == 1);                // 用过道具

    // ---- 军衔 ----
    assert(gr_rank(0) == 0 && gr_rank(499) == 0);
    assert(gr_rank(500) == 1 && gr_rank(1499) == 1);
    assert(gr_rank(1500) == 2);
    assert(gr_rank(3500) == 3);
    assert(gr_rank(7000) == 4);
    assert(gr_rank(12000) == 5 && gr_rank(999999) == 5);
    for (int r0 = 0; r0 < 6; r0++) {
        assert(gr_rank(gr_rank_floor(r0)) == r0);
        assert(gr_rank_name(r0)[0] != '?');
    }

    puts("game rules tests: PASS");
    return 0;
}
