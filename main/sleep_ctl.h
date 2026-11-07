// main/sleep_ctl.h —— 休眠策略:静默先浅睡(对局页,可原地恢复)后深睡 + 手动按键休眠。
#pragma once

#include <stdbool.h>

void sleep_ctl_kick(void);      // 有用户输入时刷新活跃时刻(输入任务调用)
void sleep_ctl_poll(void);      // 输入空闲时轮询;按页面策略先浅睡、到期深睡
void sleep_enter_deep(void);    // 立即深睡(终端操作,调用后本运行不再恢复外设)
int sleep_brightness_percent(void);   // 亮度档位→背光百分比;app_main 与浅睡恢复共用的唯一来源
