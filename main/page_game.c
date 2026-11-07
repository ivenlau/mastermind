// main/page_game.c —— 对局页。状态机:编辑 →(暂停/道具)→ 结算 → 复盘。
//
// 棋盘设计(需求):画布风背景(纵横虚线网格),按对局类型预置全部占位行
// (序号 + 大空心圆 + 小空心圆),用户选色后原地填充;整块水平居中、尺寸自适应。
//
// 内存纪律(踩过的坑):LVGL 内存池耗尽会让对象构造半初始化并把
// update_obj_state 拖进死循环(看门狗复位现场)。因此棋盘对象只在进入页面时
// 创建一次,之后一律原地改样式,禁止按键级的整树重建。
#include "page_game.h"

#include <stdio.h>
#include <string.h>

#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "achieve.h"
#include "mastermind_core.h"
#include "solver_core.h"
#include "sound_player.h"
#include "store.h"
#include "ui_font.h"
#include "ui_theme.h"

static const char *TAG = "game";

#define MAX_TOAST_QUEUE 4
#define OV_ROWS_MAX 8
#define REVIEW_ROWS 6

typedef enum {
    ST_EDIT = 0,
    ST_PAUSE,
    ST_TOOL_PICK,
    ST_TOOL_COLOR,
    ST_SETTLE,
    ST_REVIEW,
} gstate_t;

// 预置占位行:序号 + 大圆(猜测)+ 小圆(反馈)+ 显影小点。
typedef struct {
    lv_obj_t *num;
    lv_obj_t *big[MM_MAX_SLOTS];
    lv_obj_t *digit[MM_MAX_SLOTS];   // 数字模式的大圆数字标签
    lv_obj_t *small[MM_MAX_SLOTS];   // 反馈小圆(空心→绿/黄填充)
} board_row_t;

static struct {
    mm_rules_t rules;
    game_mode_t mode;
    bool is_daily;
    uint32_t daily_day, daily_rule;
    uint8_t code[MM_MAX_SLOTS];
    mm_trial_t trials[SOLVER_MAX_GUESSES];
    uint8_t n_trials;
    uint8_t cur[MM_MAX_SLOTS];
    uint8_t cursor;
    uint8_t steps_used;
    bool won, forced, fused;
    bool revealed[MM_MAX_SLOTS];
    uint8_t tools_used;
    int64_t start_us;
    int elapsed_s;
    uint32_t count_initial;
    uint32_t count_before[SOLVER_MAX_GUESSES];
    uint32_t count_after[SOLVER_MAX_GUESSES];
    int score, stars, xp_gain;
    bool recorded;
    bool daily_reward;                   // 本局领取了每日密令道具奖励
    gstate_t state;
    int pause_sel, tool_sel, settle_sel, review_off;
    int tool_color_sel;
    bool tool_showing_result;
    ach_id_t toasts[MAX_TOAST_QUEUE];
    int toast_n;
} g;

// ---- 棋盘对象(创建一次,原地更新) ----
static lv_obj_t *s_canvas;
static board_row_t s_rows[SOLVER_MAX_GUESSES];
static lv_obj_t *s_info_lb;                  // 底部居中:第x步 · n种可能
static int s_big;                            // 大圆直径(自适应)
static int s_small;                          // 小圆直径
static int s_row_y[SOLVER_MAX_GUESSES];      // 各行大圆 y(自适应布局)
static int s_big_x[MM_MAX_SLOTS];            // 大圆列 x(自适应布局)
static lv_obj_t *s_mini[MM_MAX_SLOTS];       // 显影小点(懒创建,整盘一组)

static lv_obj_t *s_overlay;
static lv_obj_t *s_ov_rows[OV_ROWS_MAX];
static int s_ov_rows_n;
static lv_obj_t *s_ov_result;
static lv_obj_t *s_rev_rows[REVIEW_ROWS];
static lv_obj_t *s_rev_labels[REVIEW_ROWS];
static lv_obj_t *s_rev_bars[REVIEW_ROWS];
static lv_obj_t *s_tc_pegs[MM_MAX_COLORS];
static lv_timer_t *s_flash_timer;
static lv_timer_t *s_toast_timer;
static int s_toast_shown;

static void board_refresh(void);
static void hints_for_state(void);
static void end_game(void);
static void settle_make(void);
static void settle_refresh(void);
static void pause_make(void);
static void pause_refresh(void);
static void tool_pick_make(void);
static void tool_pick_refresh(void);
static void tool_color_make(void);
static void tool_color_refresh(void);
static void review_make(void);
static void review_refresh(void);

static void flash_timer_cb(lv_timer_t *tmr) {
    (void)tmr;
    board_refresh();
    hints_for_state();
    if (s_flash_timer) {
        lv_timer_del(s_flash_timer);
        s_flash_timer = NULL;
    }
}

// 彩带坠落完即自删,避免残留在屏幕底部。
static void anim_ready_delete(lv_anim_t *a) {
    if (a && a->var) lv_obj_delete((lv_obj_t *)a->var);
}

// 数字模式用专用 10 色板(0-9 各一色,互不重复、拉开区分度,需求);
// 数字墨色按底色明度自动黑/白,保证任何主题都可读。
static uint32_t peg_color_of(int color) {
    const app_theme_t *t = theme_cur();
    if (g.rules.colors > UI_PEG_COLORS) {
        return t->digit_peg[color % UI_DIGIT_COLORS];
    }
    return t->peg[color];
}

static uint32_t ink_of(uint32_t rgb) {
    int l = (int)(((rgb >> 16) & 0xFF) * 299 + ((rgb >> 8) & 0xFF) * 587 + (rgb & 0xFF) * 114);
    return l > 128000 ? 0x1A1A1A : 0xFFFFFF;
}

// ============================== 画布与圆点 ==============================

// 虚线网格:整块画布一个对象,绘制期画线,零对象churn。
static void canvas_draw_cb(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *obj = lv_event_get_target(e);
    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    const app_theme_t *t = theme_cur();

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(t->stroke);
    dsc.width = 1;
    dsc.dash_width = 3;
    dsc.dash_gap = 4;
    dsc.opa = LV_OPA_50;

    int pitch = g.rules.max_guesses ? (c.y2 - c.y1 - 4) / g.rules.max_guesses : 22;
    // 竖线
    for (int x = c.x1 + 26; x < c.x2 - 8; x += 26) {
        dsc.p1.x = (lv_value_precise_t)x;
        dsc.p1.y = (lv_value_precise_t)(c.y1 + 2);
        dsc.p2.x = (lv_value_precise_t)x;
        dsc.p2.y = (lv_value_precise_t)(c.y2 - 2);
        lv_draw_line(layer, &dsc);
    }
    // 横线(与行距对齐)
    for (int y = c.y1 + pitch; y < c.y2 - 8; y += pitch) {
        dsc.p1.x = (lv_value_precise_t)(c.x1 + 2);
        dsc.p1.y = (lv_value_precise_t)y;
        dsc.p2.x = (lv_value_precise_t)(c.x2 - 2);
        dsc.p2.y = (lv_value_precise_t)y;
        lv_draw_line(layer, &dsc);
    }
}

static void circle_fill(lv_obj_t *o, uint32_t color) {
    if (!o) return;
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
}

static void circle_hollow(lv_obj_t *o) {
    if (!o) return;
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
}

// 选中环:粗描边用独立 token,和填充色永远区分(需求)。
// 不用盒阴影发光——LVGL box_shadow 软渲染路径触发过看门狗死循环,全应用禁用。
static void circle_select(lv_obj_t *o, bool on) {
    if (!o) return;
    const app_theme_t *t = theme_cur();
    if (on) {
        lv_obj_set_style_border_width(o, 3, 0);
        lv_obj_set_style_border_color(o, lv_color_hex(t->sel_ring), 0);
    } else {
        lv_obj_set_style_border_width(o, 1, 0);
        lv_obj_set_style_border_color(o, lv_color_hex(t->stroke), 0);
    }
}

static lv_obj_t *make_circle(lv_obj_t *parent, int x, int y, int size) {
    lv_obj_t *o = lv_obj_create(parent);
    if (!o) return NULL;
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(theme_cur()->stroke), 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

// ============================== 看板刷新(全部原地) ==============================

static void board_refresh(void) {
    const app_theme_t *t = theme_cur();
    bool show_answer = g.recorded || g.won;

    for (uint8_t i = 0; i < MM_MAX_SLOTS; i++) {
        if (s_mini[i]) lv_obj_add_flag(s_mini[i], LV_OBJ_FLAG_HIDDEN);
    }

    for (int r = 0; r < g.rules.max_guesses && r < SOLVER_MAX_GUESSES; r++) {
        board_row_t *row = &s_rows[r];
        if (!row->num) continue;

        bool submitted = r < g.n_trials;
        bool current = (!show_answer && g.state == ST_EDIT && r == g.steps_used);

        // 序号:当前行高亮
        lv_obj_set_style_text_color(row->num,
            lv_color_hex(current ? t->accent : t->dim), 0);

        for (uint8_t i = 0; i < g.rules.slots; i++) {
            // 大圆:猜测色 / 当前行实时预览 / 空心占位
            if (submitted || current) {
                int color = submitted ? g.trials[r].guess[i] : g.cur[i];
                uint32_t fill = peg_color_of(color);
                circle_fill(row->big[i], fill);
                if (g.rules.colors > UI_PEG_COLORS && row->digit[i]) {
                    lv_label_set_text_fmt(row->digit[i], "%d", color);
                    lv_obj_set_style_text_color(row->digit[i], lv_color_hex(ink_of(fill)), 0);
                }
                circle_select(row->big[i], !submitted && i == g.cursor);
            } else {
                circle_hollow(row->big[i]);
                circle_select(row->big[i], false);
                if (row->digit[i]) lv_label_set_text(row->digit[i], "");
            }

            // 显影小点:整盘一组懒创建,随当前行移动(省 60+ 常驻对象)
            if (current && g.revealed[i]) {
                uint32_t mc = (g.code[i] == MM_RAINBOW) ? t->accent2 : peg_color_of(g.code[i]);
                if (s_mini[i]) {
                    lv_obj_delete(s_mini[i]);
                    s_mini[i] = NULL;
                }
                s_mini[i] = make_circle(shell_content(), s_big_x[i] + s_big - 4, s_row_y[r] - 2, 6);
                if (s_mini[i]) circle_fill(s_mini[i], mc);
            }

            // 反馈小圆:绿=位对,黄=色对;空心=尚无反馈(暗牌只填绿)
            if (submitted) {
                mm_feedback_t fb = g.trials[r].fb;
                uint8_t exact = fb.exact;
                uint8_t partial = g.rules.dark ? 0 : fb.partial;
                if (i < exact) {
                    circle_fill(row->small[i], t->exact);
                } else if (i < exact + partial) {
                    circle_fill(row->small[i], t->partial);
                } else {
                    circle_hollow(row->small[i]);
                }
            } else {
                circle_hollow(row->small[i]);
            }
        }
    }

    // 底部居中信息:重复模式 · n种可能(需求;数字模式说"数字不重复")
    uint32_t cnt = g.n_trials ? g.count_after[g.n_trials - 1] : g.count_initial;
    const char *dup_txt = g.rules.guess_no_dup
        ? (g.rules.colors > UI_PEG_COLORS ? "数字不重复" : "颜色不重复")
        : "颜色可重复";
    if (s_info_lb) {
        lv_label_set_text_fmt(s_info_lb, "%s · %u种可能", dup_txt, (unsigned)cnt);
    }
}

static void hints_for_state(void) {
    switch (g.state) {
    case ST_EDIT:
        // 对局主页面不显示操作提示,把空间留给游戏(需求)。
        shell_set_hints("");
        break;
    case ST_PAUSE:
        shell_set_hints("↑↓ 选择 · OK 确认 · 双击继续");
        break;
    case ST_TOOL_PICK:
        shell_set_hints("↑↓ 选择道具 · OK 使用 · 双击返回");
        break;
    case ST_TOOL_COLOR:
        shell_set_hints(g.tool_showing_result ? "查询完成 · OK 返回"
                                              : "↑↓ 选颜色 · OK 查询(仅一次) · 双击返回");
        break;
    case ST_SETTLE:
        shell_set_hints("↑↓ 选择 · OK 确认 · 双击回主页");
        break;
    case ST_REVIEW:
        shell_set_hints("↑↓ 滚动 · 双击返回结算");
        break;
    }
}

// ============================== 覆盖层 ==============================

static void overlay_close(void) {
    if (s_overlay) {
        lv_obj_delete(s_overlay);
        s_overlay = NULL;
    }
    s_ov_rows_n = 0;
    s_ov_result = NULL;
    for (int i = 0; i < OV_ROWS_MAX; i++) s_ov_rows[i] = NULL;
    for (int i = 0; i < MM_MAX_COLORS; i++) s_tc_pegs[i] = NULL;
    for (int i = 0; i < REVIEW_ROWS; i++) {
        s_rev_rows[i] = NULL;
        s_rev_labels[i] = NULL;
        s_rev_bars[i] = NULL;
    }
}

static lv_obj_t *overlay_open_h(const char *title, int h, uint32_t title_color) {
    const app_theme_t *t = theme_cur();
    overlay_close();
    s_overlay = lv_obj_create(shell_content());
    if (!s_overlay) return NULL;
    lv_obj_set_size(s_overlay, 216, h);
    lv_obj_center(s_overlay);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(t->surface), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_overlay, lv_color_hex(t->stroke), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_overlay, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_overlay, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_overlay, 10, LV_PART_MAIN);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lb = shell_label(s_overlay, title, font_ui_20(), title_color);
    if (lb) lv_obj_align(lb, LV_ALIGN_TOP_MID, 0, 2);
    return s_overlay;
}

static lv_obj_t *ov_row_make(int idx, int y0, int spacing, int h,
                             const char *text, uint32_t color) {
    if (!s_overlay || idx >= OV_ROWS_MAX) return NULL;
    lv_obj_t *row = lv_obj_create(s_overlay);
    if (!row) return NULL;
    lv_obj_set_size(row, 190, h);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y0 + idx * spacing);   // 行整体居中
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 2, LV_PART_MAIN);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lb = shell_label(row, text, font_ui_16(), color);
    if (lb) lv_obj_align(lb, LV_ALIGN_CENTER, 0, 0);   // 选项文字居中(需求)
    s_ov_rows[idx] = row;
    if (idx + 1 > s_ov_rows_n) s_ov_rows_n = idx + 1;
    return row;
}

static void ov_row_sel(int sel) {
    const app_theme_t *t = theme_cur();
    for (int i = 0; i < s_ov_rows_n; i++) {
        if (!s_ov_rows[i]) continue;
        bool on = (i == sel);
        lv_obj_set_style_bg_color(s_ov_rows[i],
            lv_color_hex(on ? t->stroke : t->surface), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_ov_rows[i], on ? 2 : 0, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_ov_rows[i], lv_color_hex(t->accent), LV_PART_MAIN);
    }
}

// ---- 成就 toast 链 ----
static void toast_chain_cb(lv_timer_t *tmr) {
    (void)tmr;
    if (s_toast_shown >= g.toast_n) {
        if (s_toast_timer) {
            lv_timer_del(s_toast_timer);
            s_toast_timer = NULL;
        }
        return;
    }
    ach_id_t id = g.toasts[s_toast_shown++];
    shell_toast(ach_name(id), ach_desc(id));
    sound_play(SND_ACHIEVE);
}

static void toasts_start(void) {
    if (g.toast_n <= 0 || s_toast_timer) return;
    shell_toast(ach_name(g.toasts[0]), ach_desc(g.toasts[0]));
    sound_play(SND_ACHIEVE);
    s_toast_shown = 1;
    if (g.toast_n > 1) {
        s_toast_timer = lv_timer_create(toast_chain_cb, 3000, NULL);
    }
}

// ---- 暂停 ----
// 道具只在标准/经典模式开放(需求);其余模式暂停菜单不含道具项。
static bool tools_allowed(void) {
    return g.mode == MODE_STANDARD || g.mode == MODE_CLASSIC;
}

static int s_pause_rows_n;

static void pause_make(void) {
    static const char *ROWS4[4] = { "继续", "使用道具", "放弃本局", "返回主页" };
    static const char *ROWS3[3] = { "继续", "放弃本局", "返回主页" };
    if (!overlay_open_h("暂停", 216, theme_cur()->accent)) return;
    s_pause_rows_n = tools_allowed() ? 4 : 3;
    for (int i = 0; i < s_pause_rows_n; i++) {
        ov_row_make(i, 36, 32, 28, tools_allowed() ? ROWS4[i] : ROWS3[i],
                    theme_cur()->text);
    }
    pause_refresh();
}

static void pause_refresh(void) {
    ov_row_sel(g.pause_sel);
}

// 暂停菜单选择 → 行为(两套菜单共用)
static void pause_activate(void) {
    int act = g.pause_sel;
    if (tools_allowed()) {
        // 0 继续 1 道具 2 放弃 3 主页
        if (act == 1) {
            g.state = ST_TOOL_PICK;
            g.tool_sel = 0;
            tool_pick_make();
            if (!s_overlay) {
                g.state = ST_EDIT;
                overlay_close();
                board_refresh();
            }
            hints_for_state();
            return;
        }
        if (act == 2) act = 2; else if (act == 3) act = 3;
    } else {
        // 0 继续 1 放弃 2 主页 → 映射到统一编号
        if (act == 1) act = 2;
        else if (act == 2) act = 3;
    }
    if (act == 0) {
        g.state = ST_EDIT;
        overlay_close();
        board_refresh();
    } else if (act == 2) {
        g.won = false;
        end_game();
    } else {
        shell_pop();
        return;
    }
    hints_for_state();
}

// ---- 道具选择 ----
static void tool_pick_make(void) {
    static const char *NAMES[TOOL_COUNT] = { "显影剂", "排除仪", "保险丝" };
    static const char *DESCS[TOOL_COUNT] = {
        "揭示一格正确颜色", "查某颜色出现次数", "失败不破连胜",
    };
    const app_save_t *sv = store_get();
    if (!overlay_open_h("使用道具", 216, theme_cur()->accent)) return;
    for (int i = 0; i < TOOL_COUNT; i++) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%s ×%u", NAMES[i], (unsigned)sv->settings.tools[i]);
        ov_row_make(i, 36, 32, 28, buf,
                    sv->settings.tools[i] ? theme_cur()->text : theme_cur()->dim);
    }
    ov_row_make(TOOL_COUNT, 36, 32, 28, "返回", theme_cur()->dim);
    lv_obj_t *d = shell_label(s_overlay,
                              DESCS[g.tool_sel < TOOL_COUNT ? g.tool_sel : 0],
                              font_ui_14(), theme_cur()->dim);
    if (d) lv_obj_align(d, LV_ALIGN_BOTTOM_MID, 0, -2);
    tool_pick_refresh();
}

static void tool_pick_refresh(void) {
    ov_row_sel(g.tool_sel);
}

// ---- 排除仪选色 ----
static void tool_color_make(void) {
    const app_theme_t *t = theme_cur();
    if (!overlay_open_h("排除仪 · 选颜色", 216, t->accent)) return;
    int size = 26, gap = 6;
    int total = g.rules.colors * size + (g.rules.colors - 1) * gap;
    int x = (216 - total) / 2 - 10;
    for (int c = 0; c < g.rules.colors && c < MM_MAX_COLORS; c++) {
        lv_obj_t *p;
        if (g.rules.colors > UI_PEG_COLORS) {
            p = make_circle(s_overlay, x + c * (size + gap), 60, size);
            if (p) {
                uint32_t fill = peg_color_of(c);
                circle_fill(p, fill);
                lv_obj_t *lb = shell_label(p, "", font_ui_20(), ink_of(fill));
                if (lb) {
                    lv_label_set_text_fmt(lb, "%d", c);
                    lv_obj_center(lb);
                }
            }
        } else {
            p = make_circle(s_overlay, x + c * (size + gap), 60, size);
            if (p) circle_fill(p, t->peg[c]);
        }
        s_tc_pegs[c] = p;
    }
    s_ov_result = shell_label(s_overlay, "OK 查询", font_ui_14(), t->dim);
    if (s_ov_result) lv_obj_align(s_ov_result, LV_ALIGN_BOTTOM_MID, 0, -20);
    tool_color_refresh();
}

static void tool_color_refresh(void) {
    for (int c = 0; c < g.rules.colors && c < MM_MAX_COLORS; c++) {
        circle_select(s_tc_pegs[c], c == g.tool_color_sel);
    }
    if (s_ov_result && g.tool_showing_result) {
        int cnt = 0;
        for (uint8_t i = 0; i < g.rules.slots; i++) {
            if (g.code[i] == g.tool_color_sel) cnt++;
            else if (g.code[i] == MM_RAINBOW) cnt++;   // 彩虹珠按任意色计
        }
        lv_label_set_text_fmt(s_ov_result, "出现 %d 次", cnt);
    }
}

// ---- 结算 ----
static void settle_make(void) {
    const app_theme_t *t = theme_cur();
    if (!overlay_open_h(g.won ? "破译成功!" : "破译失败!", 236, g.won ? t->accent : t->bad)) {
        return;
    }
    // 答案珠:整行水平居中
    for (uint8_t i = 0; i < g.rules.slots; i++) {
        int color = g.code[i];
        lv_obj_t *p = make_circle(s_overlay, 0, 34, 22);
        if (!p) continue;
        lv_obj_align(p, LV_ALIGN_TOP_MID,
                     (int)i * 28 - ((int)g.rules.slots - 1) * 28 / 2, 34);
        if (color == MM_RAINBOW) {
            circle_fill(p, t->accent2);
            lv_obj_t *star = shell_label(p, "★", font_ui_14(), 0xFFFFFF);
            if (star) lv_obj_center(star);
        } else if (g.rules.colors > UI_PEG_COLORS) {
            uint32_t fill = peg_color_of(color);
            circle_fill(p, fill);
            lv_obj_t *lb = shell_label(p, "", font_ui_20(), ink_of(fill));
            if (lb) {
                lv_label_set_text_fmt(lb, "%d", color);
                lv_obj_center(lb);
            }
        } else {
            circle_fill(p, t->peg[color]);
        }
    }
    // 得分与数字同一行(需求),整行居中
    lv_obj_t *sc = shell_label(s_overlay, "", font_ui_28(), g.won ? t->accent : t->dim);
    if (sc) {
        lv_label_set_text_fmt(sc, "得分 %d", g.score);
        lv_obj_align(sc, LV_ALIGN_TOP_MID, 0, 62);
    }
    const app_save_t *sv = store_get();
    lv_obj_t *ln = shell_label(s_overlay, "", font_ui_14(), t->dim);
    if (ln) {
        lv_label_set_text_fmt(ln, "连胜 %u · %s%s",
                              (unsigned)sv->stats.cur_streak,
                              gr_rank_name(gr_rank(sv->stats.xp)),
                              g.daily_reward ? " · 道具+1" : "");
        lv_obj_align(ln, LV_ALIGN_TOP_MID, 0, 104);
    }
    static const char *ROWS[3] = { "再来一局", "复盘", "返回主页" };
    for (int i = 0; i < 3; i++) {
        ov_row_make(i, 132, 28, 24, ROWS[i], t->text);
    }
    settle_refresh();
}

static void settle_refresh(void) {
    ov_row_sel(g.settle_sel);
}

// ---- 复盘 ----
static void review_make(void) {
    const app_theme_t *t = theme_cur();
    if (!overlay_open_h("复盘 · 收敛曲线", 216, t->accent)) return;
    for (int i = 0; i < REVIEW_ROWS; i++) {
        lv_obj_t *row = lv_obj_create(s_overlay);
        if (!row) break;
        lv_obj_set_size(row, 190, 30);
        lv_obj_set_pos(row, 3, 34 + i * 30);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        s_rev_rows[i] = row;
        s_rev_labels[i] = shell_label(row, "", font_ui_14(), t->text);
        if (s_rev_labels[i]) lv_obj_set_pos(s_rev_labels[i], 2, 0);
        s_rev_bars[i] = lv_obj_create(row);
        if (s_rev_bars[i]) {
            lv_obj_set_pos(s_rev_bars[i], 2, 18);
            lv_obj_set_size(s_rev_bars[i], 6, 4);
            lv_obj_set_style_bg_color(s_rev_bars[i], lv_color_hex(t->accent), LV_PART_MAIN);
            lv_obj_set_style_radius(s_rev_bars[i], 2, LV_PART_MAIN);
            lv_obj_set_style_border_width(s_rev_bars[i], 0, LV_PART_MAIN);
            lv_obj_remove_flag(s_rev_bars[i], LV_OBJ_FLAG_SCROLLABLE);
        }
    }
    review_refresh();
}

static void review_refresh(void) {
    for (int i = 0; i < REVIEW_ROWS; i++) {
        int idx = g.review_off + i;
        if (!s_rev_rows[i]) continue;
        bool has = idx < g.n_trials;
        if (has) {
            lv_obj_remove_flag(s_rev_rows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_rev_rows[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        uint32_t before = g.count_before[idx];
        uint32_t after = g.count_after[idx];
        char buf[48];
        snprintf(buf, sizeof(buf), "第%d步  %u→%u", idx + 1, (unsigned)before, (unsigned)after);
        if (s_rev_labels[i]) lv_label_set_text(s_rev_labels[i], buf);
        if (s_rev_bars[i]) {
            int w_after = after ? (int)(6 + 150.0 * (double)__builtin_log10((double)after + 1) / 3.2) : 6;
            lv_obj_set_width(s_rev_bars[i], w_after);
        }
    }
}

// ============================== 游戏流程 ==============================

static void game_start(const mm_rules_t *rules, game_mode_t mode, bool is_daily,
                       uint32_t daily_day, uint32_t daily_rule, uint32_t seed) {
    memset(&g, 0, sizeof(g));
    g.rules = *rules;
    g.mode = mode;
    g.is_daily = is_daily;
    g.daily_day = daily_day;
    g.daily_rule = daily_rule;
    mm_rng_t rng;
    mm_rng_seed(&rng, seed);
    mm_generate_code(&g.rules, &rng, g.code);
    // 开局编辑行随机摆放互不相同的颜色/数字,而不是全同色(需求)。
    {
        uint8_t pool[MM_MAX_COLORS];
        for (int i = 0; i < g.rules.colors; i++) pool[i] = (uint8_t)i;
        for (uint8_t i = 0; i < g.rules.slots; i++) {
            uint8_t j = (uint8_t)(i + mm_rng_range(&rng, (uint32_t)(g.rules.colors - i)));
            uint8_t tmp = pool[i];
            pool[i] = pool[j];
            pool[j] = tmp;
            g.cur[i] = pool[i];
        }
    }
    g.start_us = esp_timer_get_time();
    g.count_initial = mm_count_possibilities(&g.rules, NULL, 0);
    g.state = ST_EDIT;
    ESP_LOGI(TAG, "开局 %s %d位 %d色 seed=%08x", gr_mode_name(mode), rules->slots,
             rules->colors, (unsigned)seed);
}

void game_start_mode(game_mode_t mode) {
    mm_rules_t rules = gr_mode_rules(mode);
    game_start(&rules, mode, false, 0, 0, esp_random());
}

void game_start_daily(const gr_daily_t *daily, uint32_t day) {
    game_start(&daily->rules, daily->mode, true, day, day % 7, gr_daily_seed(day));
}

static void record_end(void) {
    if (g.recorded) return;
    g.recorded = true;
    g.elapsed_s = (int)((esp_timer_get_time() - g.start_us) / 1000000);

    app_save_t *sv = store_get();
    uint8_t steps_left = (uint8_t)(g.rules.max_guesses - g.steps_used);
    gr_score_in_t in = {
        .elapsed_s = (uint16_t)g.elapsed_s,
        .steps_left = g.won ? steps_left : 0,
        .tools_used = g.tools_used,
        .streak = (uint16_t)sv->stats.cur_streak,
        .forced = g.forced,
    };
    g.score = g.won ? gr_score(&in) : 0;
    g.stars = gr_stars(g.won, g.tools_used > 0, steps_left, g.rules.max_guesses);
    g.xp_gain = gr_xp_gain(g.score);

    ach_game_result_t r = { 0 };
    r.mode = g.mode;
    r.won = g.won;
    r.steps_used = g.steps_used;
    r.max_guesses = g.rules.max_guesses;
    r.tools_used = g.tools_used > 0;
    r.forced = g.forced;
    r.night_win = time_is_set() && g.won && time_hour() < 5;
    r.is_daily = g.is_daily;
    r.daily_day = g.daily_day;
    r.daily_rule = g.daily_rule;
    r.fuse_protect = g.fused;
    r.score = g.score;
    ach_apply_game(&sv->stats, &r);

    if (g.won && g.score > (int)sv->settings.best_score) {
        sv->settings.best_score = (uint32_t)g.score;
    }
    if (g.won && g.is_daily) {
        sv->settings.daily_done = 1;
        sv->settings.daily_done_day = g.daily_day;
        // 每日密令奖励:当天首次通关送随机道具 ×1(需求;未设日期不可达)。
        if (sv->tool_reward_day != g.daily_day) {
            sv->tool_reward_day = g.daily_day;
            int t = (int)(esp_random() % STORE_TOOL_COUNT);
            sv->settings.tools[t]++;
            g.daily_reward = true;
            ESP_LOGI(TAG, "每日密令奖励:道具 %d +1", t);
        }
    }
    store_daily_tick();   // 局末顺带补给(对局中跨天也生效)

    g.toast_n = 0;
    for (int i = 0; i < ACH_COUNT && g.toast_n < MAX_TOAST_QUEUE; i++) {
        if (!sv->ach_seen[i] && ach_is_unlocked(&sv->stats, (ach_id_t)i)) {
            sv->ach_seen[i] = 1;
            g.toasts[g.toast_n++] = (ach_id_t)i;
        }
    }
    store_flush();
}

static void end_game(void) {
    record_end();
    g.state = ST_SETTLE;
    g.settle_sel = 0;
    settle_make();           // 面板先显示,后面的音效/彩带/存档慢了也不挡结果
    if (!s_overlay) {
        // 兜底:结算面板建不出来(LVGL 池不足)时优雅回主页,不再半屏卡死。
        ESP_LOGE(TAG, "结算面板创建失败,返回主页");
        shell_pop();
        return;
    }
    board_refresh();
    if (g.won) {
        sound_play(SND_WIN);
        const app_theme_t *t = theme_cur();
        lv_obj_t *content = shell_content();
        for (int i = 0; i < 12; i++) {
            lv_obj_t *c = lv_obj_create(content);
            if (!c) break;
            int x = 8 + (int)(esp_random() % 220);
            lv_obj_set_pos(c, x, -12);
            lv_obj_set_size(c, 6, 6);
            circle_fill(c, t->peg[i % UI_PEG_COLORS]);
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, c);
            lv_anim_set_values(&a, -12, 270);
            lv_anim_set_duration(&a, 900 + (int)(esp_random() % 900));
            lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
            lv_anim_set_ready_cb(&a, anim_ready_delete);
            lv_anim_start(&a);
        }
    } else {
        sound_play(SND_LOSE);
    }
    hints_for_state();
    toasts_start();
    ESP_LOGI(TAG, "结算面板已显示:胜=%d 步数=%d 分数=%d", (int)g.won, g.steps_used, g.score);
}

// 不重复模式的自动解冲突(需求):确定当前格时,若其他格与当前格同色,
// 把那个格改成"当前所有点位已有颜色之外"的任意色。
static void resolve_dup(void) {
    if (!g.rules.guess_no_dup) return;
    for (uint8_t j = 0; j < g.rules.slots; j++) {
        if (j == g.cursor || g.cur[j] != g.cur[g.cursor]) continue;
        bool used[MM_MAX_COLORS] = { false };
        for (uint8_t i = 0; i < g.rules.slots; i++) used[g.cur[i]] = true;
        for (int nc = 0; nc < g.rules.colors; nc++) {
            if (!used[nc]) {
                g.cur[j] = (uint8_t)nc;
                break;
            }
        }
    }
}

static void do_submit(void) {
    if (g.steps_used >= g.rules.max_guesses) {
        // 防御兜底:步数已尽却仍停在编辑态(报告中的"卡住"),强制结算出面板。
        ESP_LOGW(TAG, "步数已尽仍在编辑态,强制结算");
        if (g.state == ST_EDIT) end_game();
        return;
    }
    resolve_dup();   // 提交也是一种"确定",同样自动解冲突
    if (!mm_guess_valid(&g.rules, g.cur)) {
        sound_play(SND_ERROR);
        shell_set_hints(g.rules.guess_no_dup ? "颜色不可重复!" : "猜测不合法!");
        const app_theme_t *t = theme_cur();
        board_row_t *row = &s_rows[g.steps_used];
        for (uint8_t i = 0; i < g.rules.slots; i++) {
            if (row->big[i]) {
                lv_obj_set_style_border_color(row->big[i], lv_color_hex(t->bad), 0);
                lv_obj_set_style_border_width(row->big[i], 2, 0);
            }
        }
        s_flash_timer = lv_timer_create(flash_timer_cb, 600, NULL);
        return;
    }

    memcpy(g.trials[g.n_trials].guess, g.cur, g.rules.slots);
    g.trials[g.n_trials].fb = mm_feedback(&g.rules, g.code, g.cur);
    mm_feedback_t fb = g.trials[g.n_trials].fb;
    g.count_before[g.n_trials] = g.n_trials ? g.count_after[g.n_trials - 1] : g.count_initial;
    g.n_trials++;
    g.steps_used++;

    // 求解器枚举在锁外跑(几十 ms),避免卡 LVGL 渲染
    bsp_lvgl_unlock();
    uint32_t cnt = mm_count_possibilities(&g.rules, g.trials, g.n_trials);
    bool forced = (fb.exact == g.rules.slots) &&
                  mm_forced_win(&g.rules, g.trials, g.n_trials);
    if (!bsp_lvgl_lock(1000)) return;
    g.count_after[g.n_trials - 1] = cnt;
    g.forced = forced;
    g.won = (fb.exact == g.rules.slots);

    sound_play(SND_PEG_IN);
    sound_play(SND_FEEDBACK);
    board_refresh();          // 占位行原地填充
    if (g.won || g.steps_used >= g.rules.max_guesses) {
        end_game();
        return;
    }
    hints_for_state();
}

static void use_tool(tool_id_t id) {
    if (!tools_allowed()) {
        sound_play(SND_ERROR);
        return;
    }
    app_save_t *sv = store_get();
    if (sv->settings.tools[id] == 0) {
        sound_play(SND_ERROR);
        shell_set_hints("库存不足!");
        return;
    }
    sv->settings.tools[id]--;
    g.tools_used++;
    ach_apply_tool(&sv->stats);
    sound_play(SND_TOOL);

    if (id == TOOL_REVEAL) {
        for (uint8_t i = 0; i < g.rules.slots; i++) {
            if (!g.revealed[i]) {
                g.revealed[i] = true;
                break;
            }
        }
    } else if (id == TOOL_FUSE) {
        g.fused = true;
    }
    store_flush();
    if (id == TOOL_SCANNER) {
        g.state = ST_TOOL_COLOR;
        g.tool_color_sel = 0;
        g.tool_showing_result = false;
        tool_color_make();
    } else {
        g.state = ST_EDIT;
        overlay_close();
        board_refresh();
    }
    hints_for_state();
}

static void new_game_same(void) {
    bool is_daily = g.is_daily;
    game_mode_t mode = g.mode;
    uint32_t day = g.daily_day, rule = g.daily_rule;
    if (is_daily) {
        gr_daily_t d = gr_daily_rules(day);
        game_start(&d.rules, mode, true, day, rule, gr_daily_seed(day));
    } else {
        mm_rules_t rules = gr_mode_rules(mode);
        game_start(&rules, mode, false, 0, 0, esp_random());
    }
    overlay_close();
    board_refresh();
    hints_for_state();
}

// ============================== 页面接口 ==============================

static void game_enter(void) {
    const app_theme_t *t = theme_cur();
    lv_obj_t *content = shell_content();
    for (int i = 0; i < SOLVER_MAX_GUESSES; i++) {
        memset(&s_rows[i], 0, sizeof(s_rows[i]));
    }
    for (int i = 0; i < MM_MAX_SLOTS; i++) s_mini[i] = NULL;

    // 对局页不吃底部提示条的空间:内容区拉到底,全屏留给游戏(需求)。
    lv_obj_set_size(content, BSP_LCD_W, BSP_LCD_H - 28);

    // 画布:虚线网格背景,绘制期画线
    s_canvas = lv_obj_create(content);
    if (s_canvas) {
        lv_obj_set_pos(s_canvas, 4, 0);
        lv_obj_set_size(s_canvas, BSP_LCD_W - 8, 272);
        lv_obj_set_style_bg_opa(s_canvas, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_canvas, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(s_canvas, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_canvas, 0, LV_PART_MAIN);
        lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_canvas, canvas_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    }

    // 自适应布局:占位行整体居中,行距/圆径随步数缩放
    int rows = g.rules.max_guesses;
    if (rows > SOLVER_MAX_GUESSES) rows = SOLVER_MAX_GUESSES;
    int pitch = (268 - 6) / rows;
    if (pitch > 26) pitch = 26;
    s_big = pitch - 5;
    s_small = s_big / 2;
    int row_w = 16 + g.rules.slots * (s_big + 4) + 8 + g.rules.slots * (s_small + 3);
    int x0 = (BSP_LCD_W - row_w) / 2;
    int top = 3 + (268 - 6 - rows * pitch) / 2;

    for (int r = 0; r < rows; r++) {
        board_row_t *row = &s_rows[r];
        int y = top + r * pitch + (pitch - s_big) / 2;
        s_row_y[r] = y;

        row->num = shell_label(content, "", font_ui_12(), t->dim);
        if (row->num) {
            lv_label_set_text_fmt(row->num, "%d", r + 1);
            lv_obj_set_pos(row->num, x0 - 2, y + (s_big - 14) / 2);
        }

        int x = x0 + 16;
        for (uint8_t i = 0; i < g.rules.slots; i++) {
            row->big[i] = make_circle(content, x, y, s_big);
            if (row->big[i] && g.rules.colors > UI_PEG_COLORS) {
                row->digit[i] = shell_label(row->big[i], "", font_ui_14(), 0xFFFFFF);
                if (row->digit[i]) lv_obj_center(row->digit[i]);
            }
            s_big_x[i] = x;
            x += s_big + 4;
        }
        x += 8;
        for (uint8_t i = 0; i < g.rules.slots; i++) {
            row->small[i] = make_circle(content, x, y + (s_big - s_small) / 2, s_small);
            x += s_small + 3;
        }
    }

    // 底部居中信息行
    s_info_lb = shell_label(content, "", font_ui_14(), t->dim);
    if (s_info_lb) {
        lv_obj_set_width(s_info_lb, BSP_LCD_W);
        lv_obj_set_style_text_align(s_info_lb, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_info_lb, 0, 274);
    }

    board_refresh();
    hints_for_state();

    // 进入时按状态恢复覆盖层(复盘返回等场景)
    switch (g.state) {
    case ST_PAUSE: pause_make(); break;
    case ST_TOOL_PICK: tool_pick_make(); break;
    case ST_TOOL_COLOR: tool_color_make(); break;
    case ST_SETTLE: settle_make(); break;
    case ST_REVIEW: review_make(); break;
    default: break;
    }
    if (g.recorded && g.toast_n > 0) toasts_start();
}

static void game_exit(void) {
    if (s_flash_timer) {
        lv_timer_del(s_flash_timer);
        s_flash_timer = NULL;
    }
    if (s_toast_timer) {
        lv_timer_del(s_toast_timer);
        s_toast_timer = NULL;
    }
    s_overlay = NULL;   // 挂在 content 下,随父删除
}

static bool game_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    switch (g.state) {
    case ST_EDIT:
        if (btn == BSP_BTN_OK && ev == BSP_BTN_DOUBLE) {
            g.pause_sel = 0;
            g.state = ST_PAUSE;
            pause_make();
            if (!s_overlay) {
                // 兜底:暂停面板建不出来就留在对局,不半屏卡死。
                g.state = ST_EDIT;
                ESP_LOGE(TAG, "暂停面板创建失败,留在对局");
            }
            hints_for_state();
            sound_play(SND_TICK);
            return true;
        }
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            do_submit();
            return true;
        }
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            resolve_dup();   // 确定当前格:自动解掉与其他格的颜色冲突(需求)
            g.cursor = (uint8_t)((g.cursor + 1) % g.rules.slots);
            board_refresh();
            sound_play(SND_TICK);
            return true;
        }
        // UP/DOWN 用 PRESS 即时响应;上键增大、下键减小(需求)。两种模式都
        // 全色循环,不重复模式靠 OK 确定时的 resolve_dup 解冲突。
        if ((btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) && ev == BSP_BTN_PRESS) {
            int dir = (btn == BSP_BTN_UP) ? 1 : -1;
            int c = g.cur[g.cursor];
            c = (c + dir + g.rules.colors) % g.rules.colors;
            g.cur[g.cursor] = (uint8_t)c;
            board_refresh();
            sound_play(SND_TICK);
            return true;
        }
        return true;   // 游戏内吞掉其余事件,避免全局返回误触

    case ST_PAUSE:
        if (btn == BSP_BTN_OK && ev == BSP_BTN_DOUBLE) {
            g.state = ST_EDIT;
            overlay_close();
            board_refresh();
            hints_for_state();
            return true;
        }
        if (btn == BSP_BTN_UP && ev == BSP_BTN_PRESS) {
            g.pause_sel = (g.pause_sel + s_pause_rows_n - 1) % s_pause_rows_n;
            pause_refresh();
            sound_play(SND_TICK);
            return true;
        }
        if (btn == BSP_BTN_DOWN && ev == BSP_BTN_PRESS) {
            g.pause_sel = (g.pause_sel + 1) % s_pause_rows_n;
            pause_refresh();
            sound_play(SND_TICK);
            return true;
        }
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            sound_play(SND_CONFIRM);
            pause_activate();
            return true;
        }
        return true;

    case ST_TOOL_PICK:
        if (btn == BSP_BTN_OK && ev == BSP_BTN_DOUBLE) {
            g.state = ST_PAUSE;
            pause_make();
            hints_for_state();
            return true;
        }
        if (btn == BSP_BTN_UP && ev == BSP_BTN_PRESS) {
            g.tool_sel = (g.tool_sel + TOOL_COUNT) % (TOOL_COUNT + 1);
            tool_pick_refresh();
            sound_play(SND_TICK);
            return true;
        }
        if (btn == BSP_BTN_DOWN && ev == BSP_BTN_PRESS) {
            g.tool_sel = (g.tool_sel + 1) % (TOOL_COUNT + 1);
            tool_pick_refresh();
            sound_play(SND_TICK);
            return true;
        }
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            if (g.tool_sel == TOOL_COUNT) {
                g.state = ST_PAUSE;
                pause_make();
                hints_for_state();
            } else {
                use_tool((tool_id_t)g.tool_sel);
            }
            return true;
        }
        return true;

    case ST_TOOL_COLOR:
        if (btn == BSP_BTN_OK && ev == BSP_BTN_DOUBLE) {
            g.state = ST_EDIT;
            overlay_close();
            board_refresh();
            hints_for_state();
            return true;
        }
        // 一处排除仪只准查一次(需求):出结果后任意键收工返回。
        if (g.tool_showing_result) {
            if (ev == BSP_BTN_PRESS || ev == BSP_BTN_CLICK) {
                g.state = ST_EDIT;
                overlay_close();
                board_refresh();
                hints_for_state();
                sound_play(SND_BACK);
            }
            return true;
        }
        if ((btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) && ev == BSP_BTN_PRESS) {
            int dir = (btn == BSP_BTN_UP) ? 1 : -1;
            g.tool_color_sel = (g.tool_color_sel + dir + g.rules.colors) % g.rules.colors;
            tool_color_refresh();
            sound_play(SND_TICK);
            return true;
        }
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            g.tool_showing_result = true;
            tool_color_refresh();
            hints_for_state();
            sound_play(SND_CONFIRM);
            return true;
        }
        return true;

    case ST_SETTLE:
        if (btn == BSP_BTN_OK && ev == BSP_BTN_DOUBLE) {
            shell_pop();
            return true;
        }
        if (btn == BSP_BTN_UP && ev == BSP_BTN_PRESS) {
            g.settle_sel = (g.settle_sel + 2) % 3;
            settle_refresh();
            sound_play(SND_TICK);
            return true;
        }
        if (btn == BSP_BTN_DOWN && ev == BSP_BTN_PRESS) {
            g.settle_sel = (g.settle_sel + 1) % 3;
            settle_refresh();
            sound_play(SND_TICK);
            return true;
        }
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            sound_play(SND_CONFIRM);
            if (g.settle_sel == 0) {
                new_game_same();
            } else if (g.settle_sel == 1) {
                g.state = ST_REVIEW;
                g.review_off = 0;
                review_make();
                if (!s_overlay) {
                    g.state = ST_SETTLE;
                    settle_make();
                }
                hints_for_state();
            } else {
                shell_pop();
                return true;
            }
            return true;
        }
        return true;

    case ST_REVIEW:
        if (btn == BSP_BTN_OK && ev == BSP_BTN_DOUBLE) {
            g.state = ST_SETTLE;
            settle_make();
            hints_for_state();
            return true;
        }
        if ((btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) && ev == BSP_BTN_PRESS) {
            int dir = (btn == BSP_BTN_UP) ? -1 : 1;
            int max_off = g.n_trials > REVIEW_ROWS ? g.n_trials - REVIEW_ROWS : 0;
            g.review_off += dir;
            if (g.review_off < 0) g.review_off = 0;
            if (g.review_off > max_off) g.review_off = max_off;
            review_refresh();
            sound_play(SND_TICK);
            return true;
        }
        return true;
    }
    return false;
}

static const app_page_t PAGE_GAME = {
    .title = "对局",
    .enter = game_enter,
    .exit = game_exit,
    .key = game_key,
    .light_after_us = 120LL * 1000 * 1000,    // 静默 2 分钟浅睡:熄屏省电,任意键原地恢复对局
    .idle_timeout_us = 300LL * 1000 * 1000,   // 静默累计 5 分钟转深睡(重启,当局作废)
};

const app_page_t *game_page(void) {
    return &PAGE_GAME;
}
