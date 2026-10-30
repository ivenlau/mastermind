// solver_core 的 host 测试:空间规模、重复色小空间手工核对、逻辑必然判定。
#include <assert.h>
#include <stdio.h>

#include "game_rules.h"
#include "solver_core.h"

int main(void) {
    // ---- 各模式密钥空间规模(与设计文档一致) ----
    assert(mm_count_possibilities(&(mm_rules_t){ .slots = 4, .colors = 6, .code_no_dup = true },
                                  NULL, 0) == 360);          // P(6,4)
    assert(mm_count_possibilities(&(mm_rules_t){ .slots = 4, .colors = 7, .code_no_dup = true },
                                  NULL, 0) == 840);          // P(7,4)
    assert(mm_count_possibilities(&(mm_rules_t){ .slots = 5, .colors = 7 },
                                  NULL, 0) == 16807);         // 7^5
    assert(mm_count_possibilities(&(mm_rules_t){ .slots = 5, .colors = 8 },
                                  NULL, 0) == 32768);         // 8^5
    assert(mm_count_possibilities(&(mm_rules_t){ .slots = 4, .colors = 10, .code_no_dup = true },
                                  NULL, 0) == 5040);          // P(10,4)
    // 彩虹日:4 个彩虹位置 × P(6,3)=120
    assert(mm_count_possibilities(&(mm_rules_t){ .slots = 4, .colors = 6, .rainbow = true },
                                  NULL, 0) == 480);

    // ---- 2 色 2 位可重复小空间:逐反馈手工核对(共 4 个码:00/01/10/11) ----
    {
        mm_rules_t r = { .slots = 2, .colors = 2, .max_guesses = 10 };
        mm_trial_t t = { .guess = { 0, 0 }, .fb = { 2, 0 } };
        assert(mm_count_possibilities(&r, &t, 1) == 1);       // 仅 00
        t.fb = (mm_feedback_t){ 1, 0 };
        assert(mm_count_possibilities(&r, &t, 1) == 2);       // 01、10
        t.fb = (mm_feedback_t){ 0, 0 };
        assert(mm_count_possibilities(&r, &t, 1) == 1);       // 仅 11
        t.fb = (mm_feedback_t){ 1, 1 };
        assert(mm_count_possibilities(&r, &t, 1) == 0);       // 空间内不可达
        t.fb = (mm_feedback_t){ 0, 2 };
        assert(mm_count_possibilities(&r, &t, 1) == 0);       // 猜 00 得不到 2 黄钉
    }

    // ---- 3 色 3 位不可重复空间(6 个码)核对过滤互斥性 ----
    {
        mm_rules_t r = { .slots = 3, .colors = 3, .code_no_dup = true, .guess_no_dup = true,
                         .max_guesses = 10 };
        mm_trial_t t = { .guess = { 0, 1, 2 }, .fb = { 3, 0 } };
        assert(mm_count_possibilities(&r, &t, 1) == 1);       // 仅 012
        t.fb = (mm_feedback_t){ 0, 3 };
        assert(mm_count_possibilities(&r, &t, 1) == 2);       // 201、210
    }

    // ---- 逻辑必然 ----
    {
        mm_rules_t r = { .slots = 2, .colors = 2, .max_guesses = 10 };
        mm_trial_t trials[3];
        // 码=10:第一步猜 00 得 {1,0} 剩 {01,10};第二步猜 01 得 {0,2} 剩 {10};
        // 第三步猜 10 即"唯一剩余可能"→ 逻辑必然。
        trials[0] = (mm_trial_t){ .guess = { 0, 0 }, .fb = { 1, 0 } };
        trials[1] = (mm_trial_t){ .guess = { 0, 1 }, .fb = { 0, 2 } };
        trials[2] = (mm_trial_t){ .guess = { 1, 0 }, .fb = { 2, 0 } };
        assert(mm_count_possibilities(&r, trials, 2) == 1);
        assert(mm_forced_win(&r, trials, 3));
        // 只猜一次就中(空间剩 3 个)→ 非必然
        {
            mm_trial_t one = { .guess = { 1, 0 }, .fb = { 2, 0 } };
            assert(!mm_forced_win(&r, &one, 1));
        }
    }

    // ---- 暗牌约束只用 exact:同样的历史,暗牌剩余数 ≥ 正常剩余数 ----
    {
        mm_rules_t normal = { .slots = 3, .colors = 3, .code_no_dup = true, .guess_no_dup = true,
                              .max_guesses = 12 };
        mm_rules_t dark = normal;
        dark.dark = true;
        mm_trial_t t = { .guess = { 0, 1, 2 }, .fb = { 1, 0 } };
        // 正常模式里猜 012 出现过的反馈只有 {3,0}/{1,2}/{0,3},{1,0} 不可达 → 0;
        // 暗牌只按 exact=1 约束 → 021、102、210(它的 1 恰在位)三个码都算可能。
        assert(mm_count_possibilities(&normal, &t, 1) == 0);
        assert(mm_count_possibilities(&dark, &t, 1) == 3);
    }

    // ---- list:cap 截断但总量正确 ----
    {
        mm_rules_t r = { .slots = 2, .colors = 2, .max_guesses = 10 };
        mm_trial_t t = { .guess = { 0, 0 }, .fb = { 1, 0 } };
        uint8_t out[2 * 2];
        uint32_t written = mm_list_possibilities(&r, &t, 1, out, 1);
        assert(written == 1);                                  // 总量 2,只写 1
        assert(mm_count_possibilities(&r, &t, 1) == 2);
    }

    puts("solver core tests: PASS");
    return 0;
}
