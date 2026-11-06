// main/ui_shell.h —— 自研 UI 外壳:页面栈、状态栏(含电量)、提示条、成就浮层。
// 与基线 demo 测试菜单/ui_pixel 视觉壳无关,是本应用自己的界面框架。
//
// 线程约定:输入任务的分发器在持 bsp_lvgl_lock() 期间调用页面回调与本文件
// 全部函数;页面回调内如需慢操作(求解器枚举等)先 bsp_lvgl_unlock() 再重锁。
// LVGL 任务的 lv_timer 回调可直接触 UI,不得调用 shell_push/pop。
#pragma once

#include "bsp_button.h"
#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct app_page {
    const char *title;
    void (*enter)(void);                 // 创建页面内容(shell_content() 为父)
    void (*exit)(void);                  // 停掉页面自己的定时器/动画,再由 shell 删内容
    bool (*key)(bsp_btn_t btn, bsp_btn_ev_t ev);   // true=已消费;false 时外壳接管
    int64_t idle_timeout_us;             // 页面自定静默时限;0=用全局默认(对局页放宽到 5 分钟)
} app_page_t;

void shell_init(void);
void shell_push(const app_page_t *page);   // 进入子页(栈上限 8)
void shell_pop(void);                      // 返回上一页;栈底无操作
void shell_dispatch(bsp_btn_t btn, bsp_btn_ev_t ev);   // 输入分发(持锁调用)
void shell_reload(void);                   // 主题切换后重建当前页与外壳配色

void shell_set_hints(const char *text);    // 底部按键提示
void shell_set_title(const char *text);    // 顶栏标题
void shell_toast(const char *title, const char *desc);   // 成就解锁等浮层
int64_t shell_idle_timeout_us(void);       // 当前页静默时限覆盖;0=无覆盖用全局默认

lv_obj_t *shell_content(void);             // 当前页内容容器(持锁访问)

// 样式助手:统一从主题 token 取色
lv_obj_t *shell_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color);
lv_obj_t *shell_card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);
void shell_peg(lv_obj_t *obj, uint32_t color, int radius);      // 珠子/色块外观
void shell_glow(lv_obj_t *obj, uint32_t color, bool on);        // 选中光晕
