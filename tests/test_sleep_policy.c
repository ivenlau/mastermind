// 静默休眠策略判定的 host 测试:不依赖 SDK,直接验证 sleep_policy.h 的纯逻辑。
// 对应固件行为:对局页静默 2 分钟浅睡(可原地恢复),累计 5 分钟深睡;其他页面
// 禁用浅睡,静默 2 分钟直接深睡。
#include <assert.h>
#include <stdio.h>

#include "sleep_policy.h"

const int64_t S = 1000 * 1000;   // 1 秒(微秒)

int main(void) {
    // 未到任何时限:正常待机。
    assert(sleep_policy_next(0, 120 * S, 300 * S) == SLEEP_POLICY_NONE);
    assert(sleep_policy_next(119 * S, 120 * S, 300 * S) == SLEEP_POLICY_NONE);

    // 到浅睡时限未到深睡:进浅睡。
    assert(sleep_policy_next(120 * S, 120 * S, 300 * S) == SLEEP_POLICY_LIGHT);
    assert(sleep_policy_next(299 * S, 120 * S, 300 * S) == SLEEP_POLICY_LIGHT);

    // 到深睡时限:深睡优先,无论浅睡时限如何配置。
    assert(sleep_policy_next(300 * S, 120 * S, 300 * S) == SLEEP_POLICY_DEEP);
    assert(sleep_policy_next(600 * S, 120 * S, 300 * S) == SLEEP_POLICY_DEEP);

    // 禁用浅睡(0):静默 2 分钟直接深睡(非对局页行为)。
    assert(sleep_policy_next(119 * S, 0, 120 * S) == SLEEP_POLICY_NONE);
    assert(sleep_policy_next(120 * S, 0, 120 * S) == SLEEP_POLICY_DEEP);

    // 浅睡时限配得比深睡还晚:深睡判定优先,等效于只深睡,不会先浅睡。
    assert(sleep_policy_next(299 * S, 300 * S, 300 * S) == SLEEP_POLICY_NONE);
    assert(sleep_policy_next(300 * S, 300 * S, 300 * S) == SLEEP_POLICY_DEEP);

    // ---- 浅睡中的唤醒判定 ----

    // 按键唤醒优先于一切:即便同时到期,也先恢复对局。
    assert(sleep_policy_wake_next(SLEEP_POLICY_WAKE_GPIO, 300 * S, 300 * S)
           == SLEEP_POLICY_RESUME);
    // 按键 + 哨兵同醒(GPIO 位独立):仍是恢复。
    assert(sleep_policy_wake_next(SLEEP_POLICY_WAKE_GPIO | SLEEP_POLICY_WAKE_TIMER,
                                  100 * S, 300 * S) == SLEEP_POLICY_RESUME);

    // 哨兵唤醒未到期:继续浅睡。
    assert(sleep_policy_wake_next(SLEEP_POLICY_WAKE_TIMER, 120 * S, 300 * S)
           == SLEEP_POLICY_CONTINUE);
    // 异常唤醒原因(如浅睡被拒后的自旋):未到期继续浅睡,由时限兜底。
    assert(sleep_policy_wake_next(0, 150 * S, 300 * S) == SLEEP_POLICY_CONTINUE);

    // 哨兵唤醒但静默已到深睡时限:转深睡。
    assert(sleep_policy_wake_next(SLEEP_POLICY_WAKE_TIMER, 300 * S, 300 * S)
           == SLEEP_POLICY_DEEP_NOW);
    assert(sleep_policy_wake_next(0, 301 * S, 300 * S) == SLEEP_POLICY_DEEP_NOW);

    puts("sleep policy decision tests: PASS");
    return 0;
}
