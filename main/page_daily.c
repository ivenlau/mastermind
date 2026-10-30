// main/page_daily.c —— 每日密令:今日日程卡 + 连签统计 + 开局入口。
#include "page_daily.h"

#include <stdio.h>

#include "page_game.h"
#include "sound_player.h"
#include "store.h"
#include "ui_font.h"
#include "ui_theme.h"

static lv_obj_t *s_card;
static lv_obj_t *s_note;

static const char *daily_desc(int rule) {
    static const char *D[7] = {
        "标准规则,轻松开局",
        "7 色 4 位,经典推理",
        "5 位 12 步,可重复色",
        "8 色 5 位,硬核挑战",
        "数字 0-9,不重复",
        "只报绿钉,不报黄钉",
        "密码含万能彩虹珠",
    };
    return D[rule % 7];
}

static void daily_refresh(void) {
    const app_theme_t *t = theme_cur();
    const app_save_t *sv = store_get();
    lv_obj_clean(s_card);

    if (!time_is_set()) {
        lv_obj_t *w = shell_label(s_card, "日期未设置", font_ui_20(), t->warn);
        lv_obj_align(w, LV_ALIGN_TOP_MID, 0, 20);
        lv_obj_t *d = shell_label(s_card, "每日密令按日期出题\n请先到「设置」设置日期",
                                  font_ui_16(), t->dim);
        lv_obj_set_width(d, 190);
        lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(d, LV_ALIGN_TOP_MID, 0, 56);
        return;
    }

    uint32_t day = time_day_index();
    gr_daily_t dr = gr_daily_rules(day);
    int rule = (int)(day % 7);
    bool done = sv->settings.daily_done && sv->settings.daily_done_day == day;

    lv_obj_t *nm = shell_label(s_card, dr.name, font_ui_28(), t->accent);
    lv_obj_set_width(nm, 200);
    lv_obj_set_style_text_align(nm, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(nm, LV_ALIGN_TOP_MID, 0, 12);

    char buf[48];
    snprintf(buf, sizeof(buf), "%d色·%d位·%d步%s", dr.rules.colors, dr.rules.slots,
             dr.rules.max_guesses,
             dr.rules.rainbow ? "·彩虹珠" : "");
    lv_obj_t *pm = shell_label(s_card, buf, font_ui_14(), t->dim);
    lv_obj_set_width(pm, 200);
    lv_obj_set_style_text_align(pm, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(pm, LV_ALIGN_TOP_MID, 0, 52);

    lv_obj_t *ds = shell_label(s_card, daily_desc(rule), font_ui_16(), t->text);
    lv_obj_set_width(ds, 200);
    lv_obj_set_style_text_align(ds, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ds, LV_ALIGN_TOP_MID, 0, 74);

    snprintf(buf, sizeof(buf), "连签 %u 天 · 最佳 %u 天",
             (unsigned)sv->stats.daily_streak, (unsigned)sv->stats.daily_best_streak);
    lv_obj_t *st = shell_label(s_card, buf, font_ui_16(), t->text);
    lv_obj_set_width(st, 200);
    lv_obj_set_style_text_align(st, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(st, LV_ALIGN_TOP_MID, 0, 104);

    lv_obj_t *dd = shell_label(s_card, done ? "今日已完成 " LV_SYMBOL_OK : "今日未完成",
                               font_ui_16(), done ? t->ok : t->warn);
    lv_obj_set_width(dd, 200);
    lv_obj_set_style_text_align(dd, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(dd, LV_ALIGN_TOP_MID, 0, 130);
}

static void daily_enter(void) {
    const app_theme_t *t = theme_cur();
    s_card = shell_card(shell_content(), 12, 8, 216, 180);
    s_note = shell_label(shell_content(),
                         "同一日期人人同题\n双击返回主页",
                         font_ui_14(), t->dim);
    lv_obj_set_width(s_note, 216);
    lv_obj_set_style_text_align(s_note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_note, 12, 198);
    daily_refresh();
    shell_set_hints(time_is_set() ? "OK 开始今日密令 · 双击返回"
                                  : "双击返回 · 去设置日期");
}

static void daily_exit(void) {
    s_card = NULL;
    s_note = NULL;
}

static bool daily_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK && time_is_set()) {
        sound_play(SND_CONFIRM);
        uint32_t day = time_day_index();
        gr_daily_t dr = gr_daily_rules(day);
        game_start_daily(&dr, day);
        shell_push(game_page());
        return true;
    }
    return false;
}

static const app_page_t PAGE_DAILY = {
    .title = "每日密令",
    .enter = daily_enter,
    .exit = daily_exit,
    .key = daily_key,
};

const app_page_t *daily_page(void) {
    return &PAGE_DAILY;
}
