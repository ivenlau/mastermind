// main/mastermind_core.c —— 见 mastermind_core.h。纯逻辑实现,唯一依赖 stdint/stdbool。
#include "mastermind_core.h"

#include <string.h>

void mm_rng_seed(mm_rng_t *rng, uint32_t seed) {
    // xorshift32 的 0 是不动点,换一个与 seed 无关的启动常量再混合。
    rng->state = seed ? seed : 0x9E3779B9u;
}

uint32_t mm_rng_next(mm_rng_t *rng) {
    uint32_t x = rng->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

uint32_t mm_rng_range(mm_rng_t *rng, uint32_t n) {
    return n ? (mm_rng_next(rng) % n) : 0;
}

// 部分 Fisher-Yates:前 slots 个互不重复,天然满足 code_no_dup / rainbow 的散色要求。
static void fill_distinct(const mm_rules_t *rules, mm_rng_t *rng, uint8_t *out, uint8_t n) {
    uint8_t pool[MM_MAX_COLORS];
    for (uint8_t i = 0; i < rules->colors; i++) pool[i] = i;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t j = (uint8_t)(i + mm_rng_range(rng, rules->colors - i));
        uint8_t t = pool[i]; pool[i] = pool[j]; pool[j] = t;
        out[i] = pool[i];
    }
}

void mm_generate_code(const mm_rules_t *rules, mm_rng_t *rng, uint8_t *out) {
    if (rules->rainbow) {
        // 彩虹日:恰 1 颗彩虹珠,其余 slots-1 色互不重复(不会与彩虹冲突)。
        fill_distinct(rules, rng, out, rules->slots);
        uint8_t pos = (uint8_t)mm_rng_range(rng, rules->slots);
        // 把彩虹珠换到 pos:与末位交换即可,散色性不受影响。
        uint8_t t = out[pos]; out[pos] = out[rules->slots - 1]; out[rules->slots - 1] = t;
        out[rules->slots - 1] = MM_RAINBOW;
        return;
    }
    if (rules->code_no_dup) {
        fill_distinct(rules, rng, out, rules->slots);
        return;
    }
    for (uint8_t i = 0; i < rules->slots; i++) {
        out[i] = (uint8_t)mm_rng_range(rng, rules->colors);
    }
}

bool mm_code_valid(const mm_rules_t *rules, const uint8_t *code) {
    bool seen[MM_MAX_COLORS] = { false };
    int rainbows = 0;
    for (uint8_t i = 0; i < rules->slots; i++) {
        if (code[i] == MM_RAINBOW) {
            rainbows++;
            continue;
        }
        if (code[i] >= rules->colors) return false;
        if (rules->code_no_dup || rules->rainbow) {
            if (seen[code[i]]) return false;
            seen[code[i]] = true;
        }
    }
    if (rules->rainbow) return rainbows == 1;
    return rainbows == 0;
}

bool mm_guess_valid(const mm_rules_t *rules, const uint8_t *guess) {
    bool seen[MM_MAX_COLORS] = { false };
    for (uint8_t i = 0; i < rules->slots; i++) {
        if (guess[i] >= rules->colors) return false;   // 猜测禁用彩虹珠
        if (rules->guess_no_dup) {
            if (seen[guess[i]]) return false;
            seen[guess[i]] = true;
        }
    }
    return true;
}

// 标准黑白钉(绿黄钉)算法:先数位对,再对"剩余"色做多重集交,重复色不会重复计。
static mm_feedback_t feedback_plain(const uint8_t *code, const uint8_t *guess, uint8_t slots) {
    mm_feedback_t fb = { 0, 0 };
    int freq[MM_MAX_COLORS] = { 0 };
    for (uint8_t i = 0; i < slots; i++) {
        if (code[i] == guess[i]) {
            fb.exact++;
        } else {
            freq[code[i]]++;
        }
    }
    for (uint8_t i = 0; i < slots; i++) {
        if (code[i] == guess[i]) continue;
        if (freq[guess[i]] > 0) {
            freq[guess[i]]--;
            fb.partial++;
        }
    }
    return fb;
}

mm_feedback_t mm_feedback(const mm_rules_t *rules, const uint8_t *code, const uint8_t *guess) {
    bool has_rainbow = false;
    for (uint8_t i = 0; i < rules->slots; i++) {
        if (code[i] == MM_RAINBOW) has_rainbow = true;
    }
    if (!has_rainbow) return feedback_plain(code, guess, rules->slots);

    // 彩虹珠口径:代入每种颜色取 (exact, partial) 字典序最大,保证确定性与可推理。
    uint8_t sub[MM_MAX_SLOTS];
    memcpy(sub, code, rules->slots);
    mm_feedback_t best = { 0, 0 };
    for (uint8_t c = 0; c < rules->colors; c++) {
        for (uint8_t i = 0; i < rules->slots; i++) {
            if (sub[i] == MM_RAINBOW) sub[i] = c;
        }
        mm_feedback_t fb = feedback_plain(sub, guess, rules->slots);
        if (fb.exact > best.exact || (fb.exact == best.exact && fb.partial > best.partial)) {
            best = fb;
        }
        for (uint8_t i = 0; i < rules->slots; i++) {
            if (code[i] == MM_RAINBOW) sub[i] = MM_RAINBOW;   // 还原后下一轮再代入
        }
    }
    return best;
}
