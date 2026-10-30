// main/mastermind_core.h —— 猜色解密游戏核心规则(纯逻辑,不依赖 ESP-IDF/LVGL,host 可测)。
//
// 规则参数(需求定版):
//   标准模式 6色4位10步不可重复 / 经典模式 7色4位10步不可重复 /
//   专家模式 7色5位12步可重复   / 大师模式 8色5位10步可重复   /
//   数字密码 0-9 四位不可重复 10 步。
// 反馈:绿钉=色对位对(exact),黄钉=色对位错(partial);另有暗牌日(只报绿钉)
// 与彩虹日(密码含 1 颗万能彩虹珠)两种每日变体。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MM_MAX_SLOTS   5
#define MM_MAX_COLORS  10        // 数字密码用 0..9
#define MM_RAINBOW     0xFF      // 彩虹珠(万能色)哨兵值,不与 0..9 冲突

// 一局的完整规则。由 game_rules.c 的模式表/每日轮换填好,游戏层不自己拼参数。
typedef struct {
    uint8_t slots;          // 位数 4..5
    uint8_t colors;         // 色数 6..10,合法颜色值 0..colors-1
    bool code_no_dup;       // 密码不允许重复色
    bool guess_no_dup;      // 猜测也不允许重复(数字密码的惯例)
    uint8_t max_guesses;    // 步数上限
    bool rainbow;           // 密码恰含 1 颗 MM_RAINBOW,其余色互不重复
    bool dark;              // 暗牌:只报 exact,partial 不外显(求解按 exact 约束)
} mm_rules_t;

// 一次反馈。显示层把 exact 画绿钉、partial 画黄钉(顺序:绿在前黄在后)。
typedef struct {
    uint8_t exact;
    uint8_t partial;
} mm_feedback_t;

// xorshift32 确定性随机源:每日密令/出题都用它播种,host 测试可复现。
typedef struct {
    uint32_t state;
} mm_rng_t;

void mm_rng_seed(mm_rng_t *rng, uint32_t seed);
uint32_t mm_rng_next(mm_rng_t *rng);
// [0, n) 均匀取值;用取模偏置在本游戏规模下可忽略,保持实现简单。
uint32_t mm_rng_range(mm_rng_t *rng, uint32_t n);

// 按规则生成密钥到 out[slots]。
void mm_generate_code(const mm_rules_t *rules, mm_rng_t *rng, uint8_t *out);

// 校验候选是否属于规则定义的密钥空间(求解器枚举也用它过滤,保证口径一致)。
bool mm_code_valid(const mm_rules_t *rules, const uint8_t *code);
// 校验猜测合法:颜色在范围内;guess_no_dup 时不得重复;猜测中不允许彩虹珠。
bool mm_guess_valid(const mm_rules_t *rules, const uint8_t *guess);

// 计算反馈。密码含彩虹珠时按"对玩家最有利且确定性"的口径:代入 0..colors-1
// 逐一试算,取 (exact, partial) 字典序最大者——可推理、可复现,host 测试固定。
mm_feedback_t mm_feedback(const mm_rules_t *rules, const uint8_t *code, const uint8_t *guess);

// exact == slots 即破译。
static inline bool mm_is_win(const mm_rules_t *rules, const uint8_t *code, const uint8_t *guess) {
    return mm_feedback(rules, code, guess).exact == rules->slots;
}
