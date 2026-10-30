// main/solver_core.c —— 见 solver_core.h。枚举 + 一致性过滤,无外部依赖。
#include "solver_core.h"

#include <stddef.h>

// 反馈是否与历史一致:正常模式比对 exact+partial;暗牌只比对 exact
// (玩家只见过绿钉数,约束只能用它,否则计数会小于玩家可推理出的下限)。
static bool consistent(const mm_rules_t *rules, const mm_feedback_t *fb, const mm_trial_t *t) {
    if (fb->exact != t->fb.exact) return false;
    if (!rules->dark && fb->partial != t->fb.partial) return false;
    return true;
}

static bool matches_all(const mm_rules_t *rules, const uint8_t *code,
                        const mm_trial_t *trials, uint8_t n) {
    for (uint8_t i = 0; i < n; i++) {
        mm_feedback_t fb = mm_feedback(rules, code, trials[i].guess);
        if (!consistent(rules, &fb, &trials[i])) return false;
    }
    return true;
}

// 里程表式枚举整空间,mm_code_valid 过滤出规则定义的密钥空间。彩虹日的里程
// 数字 colors 映射为 MM_RAINBOW 哨兵值(合法判定只认哨兵)。
// 返回一致密钥总数;out 非空时写入前 min(total, cap) 个。
static uint32_t enumerate(const mm_rules_t *rules, const mm_trial_t *trials, uint8_t n,
                          uint8_t *out, uint32_t cap) {
    uint8_t code[MM_MAX_SLOTS] = { 0 };
    uint8_t digit[MM_MAX_SLOTS] = { 0 };
    uint8_t base = rules->rainbow ? (uint8_t)(rules->colors + 1) : rules->colors;
    uint32_t total = 0;
    for (;;) {
        for (uint8_t i = 0; i < rules->slots; i++) {
            code[i] = (rules->rainbow && digit[i] == rules->colors) ? MM_RAINBOW : digit[i];
        }
        if (mm_code_valid(rules, code) && matches_all(rules, code, trials, n)) {
            if (out && total < cap) {
                for (uint8_t i = 0; i < rules->slots; i++) out[total * rules->slots + i] = code[i];
            }
            total++;
        }
        // +1 进位
        uint8_t i = 0;
        while (i < rules->slots) {
            if (++digit[i] < base) break;
            digit[i] = 0;
            i++;
        }
        if (i == rules->slots) break;
    }
    return total;
}

uint32_t mm_count_possibilities(const mm_rules_t *rules, const mm_trial_t *trials, uint8_t n) {
    return enumerate(rules, trials, n, NULL, 0);
}

uint32_t mm_list_possibilities(const mm_rules_t *rules, const mm_trial_t *trials, uint8_t n,
                               uint8_t *out, uint32_t cap) {
    uint32_t total = enumerate(rules, trials, n, out, cap);
    return total < cap ? total : cap;
}

bool mm_forced_win(const mm_rules_t *rules, const mm_trial_t *trials, uint8_t n) {
    if (n == 0) return false;
    uint8_t only[MM_MAX_SLOTS];
    // 前 n-1 次猜测只应剩一个可能,且就是最后一次提交的组合。
    if (enumerate(rules, trials, (uint8_t)(n - 1), only, 1) != 1) return false;
    for (uint8_t i = 0; i < rules->slots; i++) {
        if (only[i] != trials[n - 1].guess[i]) return false;
    }
    return true;
}
