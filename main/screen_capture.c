// main/screen_capture.c —— 见 screen_capture.h。
//
// 实现方式:控制台 RX 任务匹配命令;收到后先发协议头,再借 LVGL 全屏重绘,
// 在 FLUSH_START 钩子里把每个整行带(draw_buf,宽 240)按行序直接写串口。
// 240×320 恰为 3:4,截屏可直接用作竖版封面素材。
#include "screen_capture.h"

#include <stdio.h>
#include <string.h>

#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "screencap";

static volatile bool s_cap_active;
static volatile int s_cap_rows;

// FLUSH_START 钩子:此刻 draw_buf 尚未做 SPI 大端交换,内容即 RGB565LE
// (与 bsp 的 rounded_flush_event 同相位,且本钩子后注册,圆角遮罩已生效)。
static void flush_hook(lv_event_t *e) {
    if (!s_cap_active) return;
    lv_display_t *disp = lv_event_get_target(e);
    const lv_area_t *area = lv_event_get_param(e);
    lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
    if (!buf || !area || !buf->data) return;
    // 只接受"整行宽、且恰好接在已发像素之后"的带,保证流的行序连续。
    if (area->x1 != 0 || area->x2 != BSP_LCD_W - 1 || area->y1 != s_cap_rows) return;
    for (int32_t y = area->y1; y <= area->y2; y++) {
        const uint8_t *row = (const uint8_t *)buf->data +
                             (size_t)(y - area->y1) * buf->header.stride;
        fwrite(row, 1, (size_t)BSP_LCD_W * 2, stdout);
    }
    s_cap_rows = area->y2 + 1;
}

static void do_capture(void) {
    // 日志也走控制台:二进制流期间全局静默,防止插字节破坏帧。
    esp_log_level_set("*", ESP_LOG_NONE);
    printf("FAP_SCREENSHOT_V1 %d %d RGB565LE %d\n",
           BSP_LCD_W, BSP_LCD_H, BSP_LCD_W * BSP_LCD_H * 2);
    fflush(stdout);

    if (bsp_lvgl_lock(2000)) {
        lv_display_t *disp = lv_display_get_default();
        // 捕获开关只在持锁时打开:否则渲染任务的"在途"刷新带会混进帧流,
        // 造成局部乱纹(实测过)。
        s_cap_rows = 0;
        s_cap_active = true;
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(disp);
        s_cap_active = false;
        bsp_lvgl_unlock();
    }
    fflush(stdout);

    esp_log_level_set("*", ESP_LOG_INFO);
    if (s_cap_rows == BSP_LCD_H) {
        ESP_LOGI(TAG, "截屏完成 %dx%d", BSP_LCD_W, BSP_LCD_H);
    } else {
        // 带序保护触发:行数不足,主机侧会读短并报失败,如实日志。
        ESP_LOGW(TAG, "截屏中止:仅导出 %d/%d 行", s_cap_rows, BSP_LCD_H);
    }
}

static void capture_task(void *arg) {
    (void)arg;
    char line[32];
    int pos = 0;
    for (;;) {
        int c = fgetc(stdin);
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (c == '\n' || c == '\r') {
            line[pos] = '\0';
            if (strcmp(line, "FAP_SCREENSHOT_V1") == 0) do_capture();
            pos = 0;
        } else if (pos < (int)sizeof(line) - 1) {
            line[pos++] = (char)c;
        } else {
            pos = 0;   // 超长行丢弃重来
        }
    }
}

void screen_capture_init(void) {
    lv_display_t *disp = lv_display_get_default();
    if (disp) {
        lv_display_add_event_cb(disp, flush_hook, LV_EVENT_FLUSH_START, NULL);
    }
    // 栈给 4KB:printf/fwrite 链路较深。
    if (xTaskCreate(capture_task, "screencap", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "截屏任务创建失败");
        return;
    }
    ESP_LOGI(TAG, "截屏协议就绪 FAP_SCREENSHOT_V1");
}
