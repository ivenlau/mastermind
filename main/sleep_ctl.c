// main/sleep_ctl.c —— 见 sleep_ctl.h。入睡序列照搬基线 demo_low_power 的深睡契约:
// 拒绝按键被按住 → 交回按键脚 → 低电平唤醒 → 依次停电量计/音频/I2C → 持锁关屏。
// 深睡唤醒 = 芯片重启,key_guard 处理唤醒键仍按住的情形。
#include "sleep_ctl.h"

#include <stdio.h>

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "sound_player.h"
#include "store.h"
#include "ui_font.h"
#include "ui_shell.h"
#include "ui_theme.h"

static const char *TAG = "sleepctl";

#define SLEEP_IDLE_US   (120LL * 1000 * 1000)   // 静默 2 分钟自动休眠

static int64_t s_last_input_us;

void sleep_ctl_kick(void) {
    s_last_input_us = esp_timer_get_time();
}

void sleep_ctl_poll(void) {
    if (s_last_input_us == 0) {
        s_last_input_us = esp_timer_get_time();
        return;
    }
    if (shell_no_auto_sleep()) return;            // 对局中不自动休眠(避免丢局)
    if (esp_timer_get_time() - s_last_input_us >= SLEEP_IDLE_US) {
        ESP_LOGI(TAG, "静默超时,自动休眠");
        sleep_enter_deep();
    }
}

void sleep_enter_deep(void) {
    // 挂钟与存档先落盘:深睡唤醒等同重启,esp_timer 归零。
    time_persist_now();
    sound_set_enabled(false);

    // 长按触发时按键必然还按着:被按住时低电平唤醒条件立即成立会秒醒,
    // 所以等松手(最多 3 秒)再入睡;一直不松则取消,恢复音效(需求时序)。
    int mv = bsp_button_read_mv();
    if (mv >= 0 && mv < BSP_BTN_MV_RELEASED_MIN) {
        shell_set_hints("松手后进入休眠…");
        int waited = 0;
        while (waited < 3000) {
            vTaskDelay(pdMS_TO_TICKS(50));
            waited += 50;
            mv = bsp_button_read_mv();
            if (mv < 0 || mv >= BSP_BTN_MV_RELEASED_MIN) break;
        }
        if (mv >= 0 && mv < BSP_BTN_MV_RELEASED_MIN) {
            ESP_LOGW(TAG, "持续按住 %d mV,取消休眠", mv);
            shell_set_hints("已取消休眠");
            sound_set_enabled(true);
            return;
        }
    }

    // 入睡提示(终端界面)
    if (bsp_lvgl_lock(500)) {
        const app_theme_t *t = theme_cur();
        lv_obj_t *content = shell_content();
        lv_obj_t *mask = lv_obj_create(content);
        if (mask) {
            lv_obj_set_size(mask, BSP_LCD_W, BSP_LCD_H - 28);
            lv_obj_set_pos(mask, 0, 0);
            lv_obj_set_style_bg_color(mask, lv_color_hex(t->bg), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(mask, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(mask, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(mask, 0, LV_PART_MAIN);
            lv_obj_remove_flag(mask, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t *lb = shell_label(mask, "休眠中\n按任意键唤醒", font_ui_20(), t->dim);
            if (lb) {
                lv_obj_set_width(lb, BSP_LCD_W);
                lv_obj_set_style_text_align(lb, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_center(lb);
            }
        }
        bsp_lvgl_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    // 交回按键脚(终端操作,失败只能重启)
    int level = 0;
    if (bsp_button_prepare_deep_sleep(&level) != ESP_OK) {
        ESP_LOGE(TAG, "按键交回失败,重启恢复外设");
        esp_restart();
        return;
    }
    if (level != 1) {
        ESP_LOGW(TAG, "入睡前按键仍被按住,可能立即醒来");
    }
    // 任一键把该脚拉低,按低电平武装;参数是位掩码不是引脚号。
    esp_err_t err = esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO,
                                                      ESP_GPIO_WAKEUP_GPIO_LOW);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "唤醒源武装失败: %s,重启恢复", esp_err_to_name(err));
        esp_restart();
        return;
    }

    (void)bsp_battery_sleep();
    (void)bsp_audio_sleep();
    (void)bsp_audio_prepare_deep_sleep();
    (void)bsp_i2c_prepare_deep_sleep();

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "无法停止 LVGL 刷屏,重启恢复外设");
        esp_restart();
        return;
    }
    (void)bsp_display_prepare_deep_sleep();
    ESP_LOGI(TAG, "进入深睡,按任意键唤醒");
    esp_deep_sleep_start();
    // 不应返回
    esp_restart();
}
