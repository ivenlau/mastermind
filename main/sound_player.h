// main(sound_player.h —— 音效:程序合成(正弦表 + 包络),无音频资产。
// 回调/按键上下文只做入队;PCM 写入在 worker 任务里(bsp_audio_write 阻塞)。
#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    SND_TICK = 0,    // 光标移动/换色
    SND_CONFIRM,     // 确认/提交起步
    SND_BACK,        // 返回
    SND_ERROR,       // 非法猜测/无效操作
    SND_PEG_IN,      // 珠子落位
    SND_FEEDBACK,    // 反馈钉点亮(短滴,可连放)
    SND_WIN,         // 破译号角
    SND_LOSE,        // 失败下滑音
    SND_ACHIEVE,     // 成就解锁
    SND_TOOL,        // 道具生效
    SND_ID_COUNT
} snd_id_t;

esp_err_t sound_init(void);
void sound_play(snd_id_t id);          // 任意上下文可调;队列满静默丢弃
void sound_set_enabled(bool on);
bool sound_enabled(void);
void sound_set_volume(int level);      // 0..3
