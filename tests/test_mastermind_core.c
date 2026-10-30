// mastermind_core 的 host 测试:反馈算法(重复色/彩虹珠边界)、出题合法性、RNG 确定性。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mastermind_core.h"

static mm_feedback_t fb(const uint8_t *code, const uint8_t *guess, uint8_t slots) {
    mm_rules_t r = { .slots = slots, .colors = 10, .code_no_dup = false, .guess_no_dup = false,
                     .max_guesses = 10 };
    return mm_feedback(&r, code, guess);
}

int main(void) {
    // ---- RNG:同种子同序列;range 边界 ----
    mm_rng_t a, b;
    mm_rng_seed(&a, 42);
    mm_rng_seed(&b, 42);
    for (int i = 0; i < 100; i++) assert(mm_rng_next(&a) == mm_rng_next(&b));
    mm_rng_seed(&a, 0);                       // 0 种子也应产出非零序列
    assert(mm_rng_next(&a) != 0);
    mm_rng_seed(&a, 7);
    for (int i = 0; i < 1000; i++) assert(mm_rng_range(&a, 6) < 6);

    // ---- 反馈基础 ----
    {
        uint8_t code[4] = { 0, 1, 2, 3 }, guess[4] = { 0, 1, 2, 3 };
        mm_feedback_t f = fb(code, guess, 4);
        assert(f.exact == 4 && f.partial == 0);
    }
    {
        uint8_t code[4] = { 0, 1, 2, 3 }, guess[4] = { 4, 5, 4, 5 };
        mm_feedback_t f = fb(code, guess, 4);
        assert(f.exact == 0 && f.partial == 0);
    }
    // 经典陷阱:重复色交叉,4 颗全对色但全错位
    {
        uint8_t code[4] = { 0, 0, 1, 1 }, guess[4] = { 1, 1, 0, 0 };
        mm_feedback_t f = fb(code, guess, 4);
        assert(f.exact == 0 && f.partial == 4);
    }
    // 重复猜测不重复计:猜四颗 2,只有码里那颗 2 对位得 1 绿钉,其余不给黄钉
    {
        uint8_t code[4] = { 1, 2, 3, 4 }, guess[4] = { 2, 2, 2, 2 };
        mm_feedback_t f = fb(code, guess, 4);
        assert(f.exact == 1 && f.partial == 0);
    }
    // 码里没有 2:四颗 2 全部落空
    {
        uint8_t code[4] = { 1, 3, 4, 5 }, guess[4] = { 2, 2, 2, 2 };
        mm_feedback_t f = fb(code, guess, 4);
        assert(f.exact == 0 && f.partial == 0);
    }
    // 位对优先占坑:两颗 2 对位后,剩余不再给黄钉
    {
        uint8_t code[4] = { 2, 2, 3, 4 }, guess[4] = { 2, 2, 2, 2 };
        mm_feedback_t f = fb(code, guess, 4);
        assert(f.exact == 2 && f.partial == 0);
    }
    {
        uint8_t code[4] = { 2, 2, 3, 4 }, guess[4] = { 3, 2, 2, 2 };
        mm_feedback_t f = fb(code, guess, 4);
        assert(f.exact == 1 && f.partial == 2);
    }
    {
        uint8_t code[4] = { 0, 0, 1, 1 }, guess[4] = { 0, 0, 0, 0 };
        mm_feedback_t f = fb(code, guess, 4);
        assert(f.exact == 2 && f.partial == 0);
    }

    // ---- 彩虹珠:确定性"最有利"口径 ----
    {
        mm_rules_t r = { .slots = 4, .colors = 6, .rainbow = true, .max_guesses = 11 };
        uint8_t code[4] = { MM_RAINBOW, 5, 3, 4 };
        uint8_t guess[4] = { 5, 5, 3, 4 };
        mm_feedback_t f = mm_feedback(&r, code, guess);
        assert(f.exact == 4 && f.partial == 0);           // 彩虹代入 5 即全对
        assert(mm_is_win(&r, code, guess));
    }
    {
        mm_rules_t r = { .slots = 4, .colors = 6, .rainbow = true, .max_guesses = 11 };
        uint8_t code[4] = { MM_RAINBOW, 1, 2, 3 };
        uint8_t guess[4] = { 4, 4, 4, 4 };
        mm_feedback_t f = mm_feedback(&r, code, guess);
        assert(f.exact == 1 && f.partial == 0);           // 彩虹代入 4 只得 1 绿钉
    }
    {
        // 多解取字典序最大:exact 优先于 partial
        mm_rules_t r = { .slots = 4, .colors = 6, .rainbow = true, .max_guesses = 11 };
        uint8_t code[4] = { MM_RAINBOW, 0, 0, 5 };        // 注:彩虹日码非彩虹部分互不重复,
        uint8_t guess[4] = { 0, 0, 5, 0 };                // 此处仅测反馈口径,不要求码合法
        mm_feedback_t f1 = mm_feedback(&r, code, guess);
        mm_feedback_t f2 = mm_feedback(&r, code, guess);
        assert(f1.exact == f2.exact && f1.partial == f2.partial);   // 确定性
    }

    // ---- 出题合法性:各模式 × 大量种子 ----
    {
        mm_rules_t modes[5] = {
            { .slots = 4, .colors = 6, .code_no_dup = true, .guess_no_dup = true, .max_guesses = 10 },
            { .slots = 4, .colors = 7, .code_no_dup = true, .guess_no_dup = true, .max_guesses = 10 },
            { .slots = 5, .colors = 7, .code_no_dup = false, .guess_no_dup = false, .max_guesses = 12 },
            { .slots = 5, .colors = 8, .code_no_dup = false, .guess_no_dup = false, .max_guesses = 10 },
            { .slots = 4, .colors = 10, .code_no_dup = true, .guess_no_dup = true, .max_guesses = 10 },
        };
        for (int m = 0; m < 5; m++) {
            mm_rng_t rng;
            mm_rng_seed(&rng, 1000 + (uint32_t)m);
            for (int n = 0; n < 200; n++) {
                uint8_t code[MM_MAX_SLOTS];
                mm_generate_code(&modes[m], &rng, code);
                assert(mm_code_valid(&modes[m], code));
            }
        }
    }
    {
        // 彩虹日:恰一颗彩虹珠,其余互不重复
        mm_rules_t r = { .slots = 4, .colors = 6, .rainbow = true, .code_no_dup = true,
                         .max_guesses = 11 };
        mm_rng_t rng;
        mm_rng_seed(&rng, 9);
        for (int n = 0; n < 200; n++) {
            uint8_t code[MM_MAX_SLOTS];
            mm_generate_code(&r, &rng, code);
            assert(mm_code_valid(&r, code));
            int rainbows = 0;
            bool seen[MM_MAX_COLORS] = { false };
            for (int i = 0; i < 4; i++) {
                if (code[i] == MM_RAINBOW) rainbows++;
                else { assert(!seen[code[i]]); seen[code[i]] = true; }
            }
            assert(rainbows == 1);
        }
    }

    // ---- 合法性判定 ----
    {
        mm_rules_t r = { .slots = 4, .colors = 6, .code_no_dup = true, .guess_no_dup = true,
                         .max_guesses = 10 };
        uint8_t dup_guess[4] = { 1, 1, 2, 3 };
        uint8_t ok_guess[4] = { 1, 2, 3, 4 };
        uint8_t bad_guess[4] = { 1, 2, 3, 6 };
        uint8_t rainbow_guess[4] = { MM_RAINBOW, 2, 3, 4 };
        assert(!mm_guess_valid(&r, dup_guess));
        assert(mm_guess_valid(&r, ok_guess));
        assert(!mm_guess_valid(&r, bad_guess));
        assert(!mm_guess_valid(&r, rainbow_guess));       // 猜测禁用彩虹珠
    }
    {
        // 可重复模式允许重复猜测
        mm_rules_t r = { .slots = 5, .colors = 8, .code_no_dup = false, .guess_no_dup = false,
                         .max_guesses = 10 };
        uint8_t dup_guess[5] = { 1, 1, 1, 1, 1 };
        assert(mm_guess_valid(&r, dup_guess));
    }

    puts("mastermind core tests: PASS");
    return 0;
}
