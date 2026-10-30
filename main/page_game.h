// main/page_game.h —— 对局页:经典/大师/数字/每日共用一套界面,规则参数注入。
#pragma once

#include "game_rules.h"
#include "ui_shell.h"

// 开局并进入对局页(由 home/daily 调用:先 start 再 shell_push(game_page()))。
void game_start_mode(game_mode_t mode);
void game_start_daily(const gr_daily_t *daily, uint32_t day);
const app_page_t *game_page(void);
