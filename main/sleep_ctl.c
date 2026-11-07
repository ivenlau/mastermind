// main/sleep_ctl.c —— 见 sleep_ctl.h。浅睡序列:持 LVGL 锁冻结渲染 → 挂起音频 →
// 熄背光(不动帧缓存,恢复即原画面)→ 停按键轮询并交回按键脚 → GPIO 低电平 +
// 1s 哨兵双唤醒源循环;按键唤醒等松手后恢复轮询,原地续局;静默到期不恢复外设,
// 直接转入下述深睡。深睡序列照搬基线 demo_low_power 的契约:拒绝按键被按住 →
// 交回按键脚 → 低电平唤醒 → 依次停电量计/音频/I2C → 持锁关屏。
// 深睡唤醒 = 芯片重启,key_guard 处理唤醒键仍按住的情形。
#include "sleep_ctl.h"

#include <stdio.h>

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "sleep_policy.h"
#include "sound_player.h"
#include "store.h"
#include "ui_font.h"
#include "ui_shell.h"
#include "ui_theme.h"

static const char *TAG = "sleepctl";

#define SLEEP_IDLE_US   (120LL * 1000 * 1000)   // 全局默认深睡时限:静默 2 分钟
#define LIGHT_SLEEP_POLL_US   (1LL * 1000 * 1000)   // 浅睡哨兵:每秒醒一次核对静默时限
#define WAKE_RELEASE_TIMEOUT_MS 3000            // 唤醒键松手等待上限(与深睡入睡等待一致)

static const int BRIGHTNESS_PCT[4] = { 20, 50, 80, 100 };   // 设置档位 → 背光百分比

// 锁住 sleep_policy.h 里硬编码的唤醒位与 IDF 枚举一致,漂移时这里编译失败。
_Static_assert((1u << ESP_SLEEP_WAKEUP_GPIO) == SLEEP_POLICY_WAKE_GPIO, "wake mask drift");
_Static_assert((1u << ESP_SLEEP_WAKEUP_TIMER) == SLEEP_POLICY_WAKE_TIMER, "wake mask drift");

static int64_t s_last_input_us;

int sleep_brightness_percent(void) {
    app_save_t *sv = store_get();
    return BRIGHTNESS_PCT[sv->settings.brightness % 4];
}

void sleep_ctl_kick(void) {
    s_last_input_us = esp_timer_get_time();
}

// 撤掉本轮浅睡武装的两个唤醒源(下轮重新武装;demo_low_power 同样用完即撤)。
static void light_sleep_disarm(void) {
    gpio_wakeup_disable((gpio_num_t)BSP_BTN_GPIO);
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
}

// 对局页浅睡会话:熄屏、停按键轮询,在 GPIO 低电平 + 1s 哨兵双唤醒源下循环浅睡。
// 按键唤醒 → 等松手(唤醒键按下事件已被停轮询吞掉,防按住即触发)→ 恢复外设与计时。
// 静默到期 → 不恢复外设(屏保持黑、音频保持挂起),调用方直接转 sleep_enter_deep。
// 返回 true = 已回到正常运行(含入口条件不满足本轮放弃的情形);false = 到期待深睡。
// 全程持 bsp_lvgl_lock:冻结 LVGL 渲染与定时器(无在途 SPI/I2C),恢复后原画面不变。
static bool light_sleep_session(int64_t deep_after_us) {
    // 按键还按着就不进浅睡:低电平唤醒条件立即成立,等于白睡一拍。
    int mv = bsp_button_read_mv();
    if (mv >= 0 && mv < BSP_BTN_MV_RELEASED_MIN) return true;

    if (!bsp_lvgl_lock(1000)) return true;   // 取不到锁,本轮放弃,下个轮询再试
    esp_err_t audio_err = bsp_audio_sleep(); // 尽力挂起编解码器;失败不阻止浅睡
    if (audio_err != ESP_OK) {
        ESP_LOGW(TAG, "音频挂起失败(继续浅睡): %s", esp_err_to_name(audio_err));
    }
    bsp_display_backlight(0);                // 不画任何提示:帧缓存原样保留,亮屏即原画面

    int level = 0;
    if (bsp_button_prepare_light_sleep(&level) != ESP_OK || level != 1) {
        bsp_display_backlight((uint8_t)sleep_brightness_percent());
        if (audio_err == ESP_OK) (void)bsp_audio_wake();
        bsp_lvgl_unlock();
        return true;
    }
    // 按 IDF 契约先配引脚再启用唤醒源;失败只降级为哨兵定时唤醒(慢但能醒)。
    if (gpio_wakeup_enable((gpio_num_t)BSP_BTN_GPIO, GPIO_INTR_LOW_LEVEL) != ESP_OK ||
        esp_sleep_enable_gpio_wakeup() != ESP_OK) {
        ESP_LOGW(TAG, "按键唤醒源武装失败,浅睡仅剩 %ds 哨兵唤醒",
                 (int)(LIGHT_SLEEP_POLL_US / 1000000));
    }
    ESP_LOGI(TAG, "静默 %d 分钟,进入浅睡(按任意键恢复)",
             (int)((esp_timer_get_time() - s_last_input_us) / 60000000));

    bool resumed = false;
    for (;;) {
        const int64_t remain_us = s_last_input_us + deep_after_us - esp_timer_get_time();
        if (remain_us <= 0) break;
        esp_sleep_enable_timer_wakeup(remain_us < LIGHT_SLEEP_POLL_US ? remain_us
                                                                      : LIGHT_SLEEP_POLL_US);
        const esp_err_t serr = esp_light_sleep_start();
        if (serr != ESP_OK) {
            ESP_LOGW(TAG, "浅睡被拒: %s,1s 后重试", esp_err_to_name(serr));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        const sleep_wake_action_t next = sleep_policy_wake_next(
            esp_sleep_get_wakeup_causes(), esp_timer_get_time() - s_last_input_us,
            deep_after_us);
        if (next == SLEEP_POLICY_RESUME) {
            resumed = true;
            break;
        }
        if (next == SLEEP_POLICY_DEEP_NOW) break;
    }
    light_sleep_disarm();

    if (!resumed) {
        bsp_lvgl_unlock();
        return false;
    }

    int waited = 0;
    while (waited < WAKE_RELEASE_TIMEOUT_MS) {
        mv = bsp_button_read_mv();
        if (mv < 0 || mv >= BSP_BTN_MV_RELEASED_MIN) break;
        vTaskDelay(pdMS_TO_TICKS(50));
        waited += 50;
    }
    if (bsp_button_restore_after_light_sleep() != ESP_OK) {
        ESP_LOGE(TAG, "按键恢复失败,重启恢复外设");
        esp_restart();
        return false;   // 不可达,安抚编译器
    }
    if (audio_err == ESP_OK) {
        const esp_err_t wake_err = bsp_audio_wake();
        if (wake_err != ESP_OK) {
            ESP_LOGE(TAG, "音频恢复失败(音效停用至重启): %s", esp_err_to_name(wake_err));
        }
    }
    bsp_display_backlight((uint8_t)sleep_brightness_percent());
    s_last_input_us = esp_timer_get_time();   // 唤醒即活跃,浅睡计时清零重开
    ESP_LOGI(TAG, "浅睡唤醒,恢复对局");
    bsp_lvgl_unlock();
    return true;
}

void sleep_ctl_poll(void) {
    if (s_last_input_us == 0) {
        s_last_input_us = esp_timer_get_time();
        return;
    }
    // 深睡时限:页面覆盖优先(对局页 5 分钟),未覆盖用全局默认。
    int64_t deep_us = shell_idle_timeout_us();
    if (deep_us <= 0) deep_us = SLEEP_IDLE_US;
    const int64_t idle_us = esp_timer_get_time() - s_last_input_us;
    switch (sleep_policy_next(idle_us, shell_light_after_us(), deep_us)) {
    case SLEEP_POLICY_LIGHT:
        if (light_sleep_session(deep_us)) return;   // 按键恢复对局,静默重新计时
        ESP_LOGI(TAG, "浅睡静默到期,转深睡");
        sleep_enter_deep();
        return;
    case SLEEP_POLICY_DEEP:
        ESP_LOGI(TAG, "静默超时,自动休眠");
        sleep_enter_deep();
        return;
    default:
        return;
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
