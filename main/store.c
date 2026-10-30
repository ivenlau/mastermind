// main/store.c —— 见 store.h。
#include "store.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ui_theme.h"

static const char *TAG = "store";
static const char *NVS_NAMESPACE = "cipher";
static const char *NVS_KEY_SAVE = "save";
#define SAVE_VERSION 3

static app_save_t s_save;

// 带版本头的 blob:版本不符时保留设置重置进度,并打日志说明,不静默吞掉。
typedef struct {
    uint32_t version;
    app_save_t save;
} save_blob_t;

static void defaults(app_save_t *s) {
    memset(s, 0, sizeof(*s));
    s->settings.theme = 1;               // 默认主题:黄铜机械(需求)
    s->settings.sound_on = 1;
    s->settings.volume = 2;
    s->settings.brightness = 3;
    s->settings.tools[TOOL_REVEAL] = 1;
    s->settings.tools[TOOL_SCANNER] = 1;
    s->settings.tools[TOOL_FUSE] = 1;
}

esp_err_t store_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 分区被写满/版本异常:这是既有数据不可读的场景,擦除后从默认值起步。
        ESP_LOGW(TAG, "NVS 分区不可用(%s),擦除重建", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;

    nvs_handle_t h;
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        defaults(&s_save);
        return store_flush();
    }
    if (err != ESP_OK) return err;

    save_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    size_t len = sizeof(blob);
    err = nvs_get_blob(h, NVS_KEY_SAVE, &blob, &len);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        defaults(&s_save);
        return store_flush();
    }
    if (err != ESP_OK) return err;
    // v1/v2 存档比当前结构短 8 字节(新字段追加在末尾),前缀兼容可无损迁移。
    if ((len != sizeof(blob) && len != sizeof(blob) - 8) || blob.version == 0) {
        ESP_LOGW(TAG, "存档不可读(版本 %u 长度 %u),重置", (unsigned)blob.version,
                 (unsigned)len);
        defaults(&s_save);
        return store_flush();
    }
    if (blob.version == 1) {
        // v1→v3:保留全部进度;按 v2 语义默认黄铜主题;v3 新字段已为 0。
        s_save = blob.save;
        s_save.settings.theme = 1;
        ESP_LOGI(TAG, "存档 v1→v3 迁移完成");
        return store_flush();
    }
    if (blob.version == 2) {
        // v2→v3:前缀兼容,新增的道具补给/领奖日期为 0(未补给/未领奖)。
        s_save = blob.save;
        ESP_LOGI(TAG, "存档 v2→v3 迁移完成");
        return store_flush();
    }
    if (blob.version != SAVE_VERSION) {
        ESP_LOGW(TAG, "存档版本不匹配(读到 %u),重置进度", (unsigned)blob.version);
        defaults(&s_save);
        return store_flush();
    }
    s_save = blob.save;
    if (s_save.settings.theme >= UI_THEME_COUNT) s_save.settings.theme = 0;
    return ESP_OK;
}

app_save_t *store_get(void) {
    return &s_save;
}

esp_err_t store_flush(void) {
    save_blob_t blob = { .version = SAVE_VERSION, .save = s_save };
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, NVS_KEY_SAVE, &blob, sizeof(blob));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

void store_reset_progress(void) {
    app_settings_t keep = s_save.settings;
    memset(&s_save.stats, 0, sizeof(s_save.stats));
    memset(s_save.ach_seen, 0, sizeof(s_save.ach_seen));
    s_save.settings = keep;
    s_save.settings.best_score = 0;
}

void store_daily_tick(void) {
    if (!time_is_set()) return;          // 未设置日期不补给(需求)
    uint32_t day = time_day_index();
    if (s_save.tool_supply_day == day) return;
    s_save.tool_supply_day = day;
    for (int i = 0; i < STORE_TOOL_COUNT; i++) {
        if (s_save.settings.tools[i] < 1) s_save.settings.tools[i] = 1;
    }
    store_flush();
}

bool time_is_set(void) {
    return s_save.settings.date_day != 0;
}

uint32_t time_day_index(void) {
    if (!time_is_set()) return 0;
    int64_t elapsed_s = (esp_timer_get_time() - s_save.settings.date_us) / 1000000;
    return s_save.settings.date_day + (uint32_t)((s_save.settings.date_sec + elapsed_s) / 86400);
}

int time_hour(void) {
    if (!time_is_set()) return 0;
    int64_t elapsed_s = (esp_timer_get_time() - s_save.settings.date_us) / 1000000;
    uint32_t sec_of_day = (uint32_t)((s_save.settings.date_sec + elapsed_s) % 86400);
    return (int)(sec_of_day / 3600);
}

void time_get_date(int *year, int *month, int *day, int *hour) {
    if (!time_is_set()) {
        if (year) *year = 0;
        if (month) *month = 0;
        if (day) *day = 0;
        if (hour) *hour = 0;
        return;
    }
    uint32_t days = time_day_index();
    int y = 1970;
    for (;;) {
        uint32_t ny = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
        if (days < ny) break;
        days -= ny;
        y++;
    }
    static const int MDAYS[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int m = 1;
    for (;;) {
        int md = MDAYS[m - 1];
        if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) md += 1;
        if ((uint32_t)md > days) break;
        days -= (uint32_t)md;
        m++;
    }
    if (year) *year = y;
    if (month) *month = m;
    if (day) *day = (int)days + 1;
    if (hour) *hour = time_hour();
}

esp_err_t time_set_date(int year, int month, int day, int hour) {
    // day_index 用简单日差算法(1970-01-01 起),足够播种与连签判定。
    if (year < 2024 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31) {
        return ESP_ERR_INVALID_ARG;
    }
    int64_t days = 0;
    for (int y = 1970; y < year; y++) {
        days += ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
    }
    static const int MDAYS[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    for (int m = 1; m < month; m++) {
        days += MDAYS[m - 1];
        if (m == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) days += 1;
    }
    days += day - 1;

    s_save.settings.date_day = (uint32_t)days;
    s_save.settings.date_sec = (uint32_t)(hour * 3600);
    s_save.settings.date_us = esp_timer_get_time();
    return store_flush();
}
