// main/sleep_ctl.h —— 休眠策略:静默自动深睡 + 手动按键休眠;任意键唤醒(重启)。
#pragma once

#include <stdbool.h>

void sleep_ctl_kick(void);      // 有用户输入时刷新活跃时刻(输入任务调用)
void sleep_ctl_poll(void);      // 输入空闲时轮询;到静默时限自动入睡
void sleep_enter_deep(void);    // 立即深睡(终端操作,调用后本运行不再恢复外设)
