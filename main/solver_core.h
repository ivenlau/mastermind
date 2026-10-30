// main/solver_core.h —— 推理引擎(纯逻辑):枚举合法密钥空间、统计"剩余可能数"、
// 判定"逻辑必然"(终局猜测是当时唯一剩余可能)。
//
// 这是"剩余可能计数器"创新玩法的算法核心:每次猜测后重算一致性空间大小,
// 供 UI 显示 `剩余可能 N 种` 与复盘页的收敛曲线。规模:最大 8^5=32768 候选
// × 12 次猜测 × 反馈试算,C3 上毫秒级,host 测试同样直接跑。
#pragma once

#include "mastermind_core.h"

#define SOLVER_MAX_GUESSES 12

// 一次已提交的猜测及其"当时对玩家公布"的反馈。暗牌日存 partial=0(只报绿钉),
// 求解一致性在暗牌下也只按 exact 约束——不作弊使用玩家看不到的信息。
typedef struct {
    uint8_t guess[MM_MAX_SLOTS];
    mm_feedback_t fb;
} mm_trial_t;

// 与全部历史猜测一致的合法密钥个数。
uint32_t mm_count_possibilities(const mm_rules_t *rules, const mm_trial_t *trials, uint8_t n);

// 列出一致密钥(每个 MM_MAX_SLOTS 字节)至多 cap 个,返回写入个数。
uint32_t mm_list_possibilities(const mm_rules_t *rules, const mm_trial_t *trials, uint8_t n,
                               uint8_t *out, uint32_t cap);

// 终局是否"逻辑必然":前 n-1 次猜测后仅剩唯一可能,且就是最后一次猜的内容。
bool mm_forced_win(const mm_rules_t *rules, const mm_trial_t *trials, uint8_t n);
