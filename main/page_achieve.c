// main/page_achieve.c —— 成就档案:军衔进度 + 24 枚成就卡片墙(滚动)。
#include "page_achieve.h"

#include <stdio.h>
#include <string.h>

#include "achieve.h"
#include "game_rules.h"
#include "sound_player.h"
#include "store.h"
#include "ui_font.h"
#include "ui_theme.h"

static lv_obj_t *s_list;
static lv_obj_t *s_rank_bar;
static lv_obj_t *s_rank_label;

static void achieve_refresh(void) {
    const app_theme_t *t = theme_cur();
    const app_save_t *sv = store_get();

    int rank = gr_rank(sv->stats.xp);
    int lo = gr_rank_floor(rank);
    int hi = (rank < 5) ? gr_rank_floor(rank + 1) : lo + 1;
    int pct = hi > lo ? (sv->stats.xp - lo) * 100 / (hi - lo) : 100;
    lv_obj_set_width(s_rank_bar, 8 + 150 * pct / 100);
    lv_label_set_text_fmt(s_rank_label, "%s · %d XP", gr_rank_name(rank), (int)sv->stats.xp);

    lv_obj_clean(s_list);
    for (int i = 0; i < ACH_COUNT; i++) {
        ach_id_t id = (ach_id_t)i;
        bool unlocked = sv->ach_seen[i];
        bool hidden = ach_hidden(id) && !unlocked;
        uint32_t cur = 0, target = 0;
        bool has_prog = ach_progress(&sv->stats, id, &cur, &target);

        // 精简行:容器 + 单个双行文本(+进度条/勾),控制对象数防 LVGL 内存池耗尽。
        lv_obj_t *row = lv_obj_create(s_list);
        if (!row) break;
        lv_obj_set_size(row, 202, 46);
        lv_obj_set_style_bg_color(row,
            lv_color_hex(unlocked ? t->stroke : t->surface), LV_PART_MAIN);
        lv_obj_set_style_border_width(row, unlocked ? 1 : 0, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, lv_color_hex(t->accent), LV_PART_MAIN);
        lv_obj_set_style_radius(row, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 4, LV_PART_MAIN);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        char buf[64];
        if (hidden) {
            snprintf(buf, sizeof(buf), "???\n隐藏成就");
        } else if (has_prog) {
            snprintf(buf, sizeof(buf), "%s\n%s  %u/%u", ach_name(id), ach_desc(id),
                     (unsigned)cur, (unsigned)target);
        } else {
            snprintf(buf, sizeof(buf), "%s\n%s", ach_name(id), ach_desc(id));
        }
        lv_obj_t *tx = shell_label(row, buf, font_ui_14(),
                                   unlocked ? t->accent : t->text);
        if (!tx) continue;
        lv_obj_set_pos(tx, 6, 4);

        if (unlocked) {
            lv_obj_t *mark = shell_label(row, LV_SYMBOL_OK, font_ui_16(), t->ok);
            if (mark) lv_obj_align(mark, LV_ALIGN_TOP_RIGHT, -6, 3);
        } else if (has_prog) {
            lv_obj_t *bar = lv_obj_create(row);
            if (!bar) continue;
            lv_obj_set_pos(bar, 6, 38);
            lv_obj_set_size(bar, cur ? (int)(180 * cur / target) : 2, 3);
            lv_obj_set_style_bg_color(bar, lv_color_hex(t->accent), LV_PART_MAIN);
            lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
            lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        }
    }
}

static void achieve_enter(void) {
    const app_theme_t *t = theme_cur();
    lv_obj_t *content = shell_content();

    s_rank_bar = lv_obj_create(content);
    lv_obj_set_pos(s_rank_bar, 16, 12);
    lv_obj_set_size(s_rank_bar, 8, 5);
    lv_obj_set_style_bg_color(s_rank_bar, lv_color_hex(t->accent), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_rank_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_rank_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_rank_bar, 3, LV_PART_MAIN);
    lv_obj_remove_flag(s_rank_bar, LV_OBJ_FLAG_SCROLLABLE);
    s_rank_label = shell_label(content, "", font_ui_16(), t->text);
    lv_obj_set_pos(s_rank_label, 16, 22);

    s_list = lv_obj_create(content);
    lv_obj_set_pos(s_list, 12, 46);
    lv_obj_set_size(s_list, 216, 210);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_list, 4, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);

    achieve_refresh();
    shell_set_hints("↑↓ 滚动 · 双击返回");
}

static void achieve_exit(void) {
    s_list = NULL;
    s_rank_bar = NULL;
    s_rank_label = NULL;
}

static bool achieve_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if ((btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) && ev == BSP_BTN_PRESS && s_list) {
        int dir = (btn == BSP_BTN_UP) ? -1 : 1;
        lv_obj_scroll_by(s_list, 0, dir * -100, LV_ANIM_ON);
        sound_play(SND_TICK);
        return true;
    }
    return false;
}

static const app_page_t PAGE_ACHIEVE = {
    .title = "成就档案",
    .enter = achieve_enter,
    .exit = achieve_exit,
    .key = achieve_key,
};

const app_page_t *achieve_page(void) {
    return &PAGE_ACHIEVE;
}
