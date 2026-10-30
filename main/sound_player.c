// main/sound_player.c —— 见 sound_player.h。16kHz/16bit/单声道,格式一次设定。
#include "sound_player.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "bsp_audio.h"

static const char *TAG = "sound";

#define SND_HZ        16000
#define SND_CHUNK     320          // 每次 bsp_audio_write 的采样数(20ms)
#define SND_QUEUE_LEN 8

// 1/4 周期 64 点正弦表(int16),相位累加器驱动,整数运算无浮点。
static int16_t s_sine[65];
static QueueHandle_t s_queue;
static volatile bool s_enabled = true;
static volatile int s_level = 2;

// 一段音调:频率、时长(ms)、振幅比例(0..256)。
typedef struct {
    uint16_t hz;
    uint16_t ms;
    uint16_t amp;
} tone_t;

#define MAX_TONES 6

// 每个音效 = 最多 MAX_TONES 段音调序列。
static const tone_t PATTERNS[SND_ID_COUNT][MAX_TONES] = {
    [SND_TICK]     = { { 1200, 25, 120 }, { 0, 0, 0 } },
    [SND_CONFIRM]  = { { 800, 45, 150 }, { 1200, 60, 150 }, { 0, 0, 0 } },
    [SND_BACK]     = { { 700, 40, 120 }, { 500, 60, 110 }, { 0, 0, 0 } },
    [SND_ERROR]    = { { 220, 130, 170 }, { 180, 130, 150 }, { 0, 0, 0 } },
    [SND_PEG_IN]   = { { 1500, 18, 110 }, { 0, 0, 0 } },
    [SND_FEEDBACK] = { { 1000, 35, 130 }, { 0, 0, 0 } },
    [SND_WIN]      = { { 523, 95, 170 }, { 659, 95, 170 }, { 784, 95, 170 },
                       { 1046, 105, 180 }, { 1318, 180, 180 }, { 0, 0, 0 } },
    [SND_LOSE]     = { { 392, 120, 150 }, { 311, 130, 145 }, { 233, 150, 140 },
                       { 175, 260, 135 }, { 0, 0, 0 } },
    [SND_ACHIEVE]  = { { 659, 85, 160 }, { 880, 85, 165 }, { 1108, 85, 170 },
                       { 1318, 120, 175 }, { 1760, 220, 180 }, { 0, 0, 0 } },
    [SND_TOOL]     = { { 900, 60, 130 }, { 1800, 60, 130 }, { 2700, 90, 130 },
                       { 0, 0, 0 } },
};

static const int VOLUME_AMP[4] = { 60, 110, 160, 220 };   // 0..3 档振幅上限

// 合成一段音调的 [offset, offset+count) 采样。相位与包络按绝对位置连续计算,
// 因此可分块合成整段而不破坏淡入淡出。返回实际写入采样数。
static int synth_tone(const tone_t *t, int16_t *buf, int count, int offset) {
    int total = (int)t->ms * SND_HZ / 1000;
    if (offset >= total) return 0;
    if (offset + count > total) count = total - offset;
    int amp = (int)t->amp * VOLUME_AMP[s_level] / 256;
    int fade_in = 4 * SND_HZ / 1000;
    int fade_out = 8 * SND_HZ / 1000;
    uint32_t step = ((uint32_t)t->hz * 64u * 256u) / SND_HZ;   // 相位步长(1/256 采样)
    for (int i = 0; i < count; i++) {
        int pos = offset + i;
        int env = 256;
        if (pos < fade_in) env = 256 * pos / fade_in;
        if (pos > total - fade_out) env = 256 * (total - pos) / fade_out;
        uint32_t phase = (uint32_t)pos * step;
        int idx = (int)((phase >> 8) & 63);
        buf[i] = (int16_t)((int)s_sine[idx] * amp * env / (256 * 256));
    }
    return count;
}

static void sound_task(void *arg) {
    (void)arg;
    static int16_t chunk[SND_CHUNK];
    snd_id_t id;
    for (;;) {
        if (xQueueReceive(s_queue, &id, portMAX_DELAY) != pdTRUE) continue;
        if (!s_enabled) continue;
        for (int tn = 0; tn < MAX_TONES; tn++) {
            const tone_t *t = &PATTERNS[id][tn];
            if (t->ms == 0) break;
            int total = (int)t->ms * SND_HZ / 1000;
            int offset = 0;
            while (offset < total) {
                int n = synth_tone(t, chunk, SND_CHUNK, offset);
                if (n <= 0) break;
                if (bsp_audio_write(chunk, (size_t)n * sizeof(int16_t)) != ESP_OK) break;
                offset += n;
            }
        }
    }
}

esp_err_t sound_init(void) {
    for (int i = 0; i <= 64; i++) {
        double ang = 3.14159265358979 * 0.5 * i / 64.0;
        s_sine[i] = (int16_t)(32767.0 * sin(ang));
    }
    esp_err_t err = bsp_audio_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "音频初始化失败:%s,音效静音", esp_err_to_name(err));
        return err;
    }
    err = bsp_audio_set_format(SND_HZ, 16, 1);
    if (err != ESP_OK) return err;
    bsp_audio_set_volume(80);

    s_queue = xQueueCreate(SND_QUEUE_LEN, sizeof(snd_id_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(sound_task, "sound", 3072, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void sound_play(snd_id_t id) {
    if (!s_queue || !s_enabled || id >= SND_ID_COUNT) return;
    (void)xQueueSend(s_queue, &id, 0);
}

void sound_set_enabled(bool on) {
    s_enabled = on;
}

bool sound_enabled(void) {
    return s_enabled;
}

void sound_set_volume(int level) {
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    s_level = level;
}
