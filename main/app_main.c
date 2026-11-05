// main/app_main.c —— 超级密码机入口:初始化、输入分发、生命周期。
//
// 按键语义(应用自定,非基线 demo 约定):
//   UP/DOWN 短按=移动/调整,OK 短按=确认,OK 长按=页面主动作(提交),
//   OK 双击=返回/菜单。页面可消费事件,未消费时外壳接管 OK 双击返回。
#include <string.h>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "key_guard.h"
#include "page_home.h"
#include "screen_capture.h"
#include "sleep_ctl.h"
#include "sound_player.h"
#include "store.h"
#include "ui_font.h"
#include "ui_shell.h"
#include "ui_theme.h"

static const char *TAG = "app";

#define INPUT_QUEUE_DEPTH 8

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue;
static TaskHandle_t s_input_task;
static volatile bool s_input_ready;

// 深睡按键唤醒守卫(逻辑在 key_guard.h,基线已测):唤醒键未松手前吞掉动作。
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;

// button 回调跑在共享 esp_timer 任务:只入队,立即返回。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键已松手,按键恢复响应");
        }
        return;
    }
    if (!s_input_ready || !s_input_queue) return;
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

static void input_task(void *arg) {
    (void)arg;
    input_event_t input;
    uint32_t persist_ticks = 0;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, pdMS_TO_TICKS(2000)) == pdTRUE) {
            sleep_ctl_kick();
            if (bsp_lvgl_lock(500)) {
                shell_dispatch(input.btn, input.event);
                bsp_lvgl_unlock();
            }
        } else {
            // 空闲:自动休眠轮询 + 挂钟每 5 分钟落盘(断电兜底)
            sleep_ctl_poll();
            if (++persist_ticks >= 150) {
                persist_ticks = 0;
                time_persist_now();
            }
        }
    }
}

static esp_err_t input_dispatch_init(void) {
    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue) return ESP_ERR_NO_MEM;
    // 页面构建(尤其成就档案整列表)在本任务内跑 LVGL,栈给到 6KB 防溢出。
    if (xTaskCreate(input_task, "app_input", 6144, NULL, 5, &s_input_task) != pdPASS) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void app_main(void) {
    ESP_LOGI(TAG, "超级密码机启动");
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "按键唤醒:先屏蔽按键动作,直到那颗键松手");
    }

    if (store_init() != ESP_OK) {
        ESP_LOGE(TAG, "存档初始化失败,以默认设置继续");
    }
    store_daily_tick();                  // 道具每日补给(需已设置日期)
    app_save_t *sv = store_get();
    theme_set(sv->settings.theme);

    bsp_i2c_init();

    // 屏幕是本应用唯一载体:失败无降级,打清日志退出。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败。检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    ui_fonts_init();
    static const int BRIGHTNESS[4] = { 20, 50, 80, 100 };
    bsp_display_backlight((uint8_t)BRIGHTNESS[sv->settings.brightness % 4]);

    if (sound_init() == ESP_OK) {
        sound_set_enabled(sv->settings.sound_on != 0);
        sound_set_volume(sv->settings.volume);
    }
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "电量计不可用,UI 电量区优雅降级");
    }

    if (bsp_lvgl_lock(1000)) {
        shell_init();
        shell_push(home_page());
        bsp_lvgl_unlock();
    } else {
        ESP_LOGE(TAG, "LVGL 加锁失败,无法建立界面");
        return;
    }

    esp_err_t input_err = input_dispatch_init();
    screen_capture_init();               // FAP_SCREENSHOT_V1 串口截屏(只读)
    esp_err_t button_err = input_err == ESP_OK
                         ? bsp_button_init(on_key, NULL)
                         : ESP_ERR_INVALID_STATE;
    if (input_err != ESP_OK) {
        ESP_LOGE(TAG, "按键事件任务创建失败: %s", esp_err_to_name(input_err));
        return;
    }
    if (button_err != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(button_err));
        return;
    }
    if (s_key_guard.armed) {
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_key_guard, released);
        if (!s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键在按键驱动启动前已松手,提前撤防");
        }
    }
    s_input_ready = true;
    ESP_LOGI(TAG, "就绪:主题=%s 音效=%d 电量=%d",
             theme_name(sv->settings.theme), sv->settings.sound_on, bsp_battery_soc());
}
