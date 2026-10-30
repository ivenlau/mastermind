// main/page_about.c —— 关于页:版本信息 + 玩法介绍,UP/DOWN 滚动查看(需求)。
#include "page_about.h"

#include "bsp_pins.h"

#include "sound_player.h"
#include "ui_font.h"
#include "ui_theme.h"

static lv_obj_t *s_list;

static void section(lv_obj_t *parent, const char *title, const char *body) {
    const app_theme_t *t = theme_cur();
    lv_obj_t *tt = shell_label(parent, title, font_ui_16(), t->accent);
    if (tt) lv_obj_set_width(tt, 202);
    if (body && body[0]) {
        lv_obj_t *bb = shell_label(parent, body, font_ui_14(), t->text);
        if (bb) {
            lv_obj_set_width(bb, 202);
            lv_label_set_long_mode(bb, LV_LABEL_LONG_WRAP);
        }
    }
}

static void about_enter(void) {
    const app_theme_t *t = theme_cur();
    lv_obj_t *content = shell_content();

    lv_obj_t *head = shell_label(content, "超级密码机 v1.0", font_ui_20(), t->text);
    if (head) {
        lv_obj_set_width(head, BSP_LCD_W);
        lv_obj_set_style_text_align(head, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(head, 0, 2);
    }
    lv_obj_t *meta = shell_label(content, "FoloToy AI Passport", font_ui_12(), t->dim);
    if (meta) {
        lv_obj_set_width(meta, BSP_LCD_W);
        lv_obj_set_style_text_align(meta, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(meta, 0, 26);
    }

    s_list = lv_obj_create(content);
    if (s_list) {
        lv_obj_set_pos(s_list, 12, 44);
        lv_obj_set_size(s_list, 216, 218);
        lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(s_list, 6, LV_PART_MAIN);
        lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
        lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);

        section(s_list, "· 玩法介绍 ·", NULL);

        section(s_list, "【目标】",
                "系统秘密生成一组彩色密码珠,请在限定次数内猜出全部颜色和位置。");

        section(s_list, "【反馈规则】",
                "每次提交后获得两类提示:\n"
                "绿钉 = 颜色对、位置也对\n"
                "黄钉 = 颜色对、位置不对\n"
                "根据反馈推理出密码,即为破译成功。");

        section(s_list, "【五种模式】",
                "标准:6色·4位·10步·不重复\n"
                "经典:7色·4位·10步·不重复\n"
                "专家:7色·5位·12步·可重复\n"
                "大师:8色·5位·10步·可重复\n"
                "数字:4位·10步·不重复\n"
                "“不重复”指密码中同色或同数字最多出现一次;"
                "“可重复”模式密码可能含多颗同色珠。");

        section(s_list, "【按键操作】",
                "↑↓ 切换当前格颜色\n"
                "OK 短按:确认当前格并移到下一格\n"
                "OK 长按:提交本次猜测\n"
                "OK 双击:打开菜单(道具/放弃/主页)");

        section(s_list, "【智能辅助】",
                "底部实时显示「剩余可能数」——还剩几种密码符合已获得的全部反馈,"
                "数字越少越接近答案;破译后可进「复盘」看收敛曲线。");

        section(s_list, "【道具】",
                "对局菜单中使用,仅标准/经典模式可用:\n"
                "显影剂:揭示一格的正确颜色\n"
                "排除仪:查询某颜色出现几次(一处限查一次)\n"
                "保险丝:本局失败不破连胜\n"
                "使用道具会适当降低本局得分。"
                "每日补给各 1 个(需设置日期),每日密令首次通关送随机道具。");

        section(s_list, "【每日密令】",
                "按日期出题,天天不重样,含特殊规则:暗牌日(只报绿钉)、"
                "彩虹日(密码含万能彩虹珠);完成可累计连签天数。");

        section(s_list, "【评分与成就】",
                "越快破译、剩余次数越多、不用道具,得分越高,连胜有倍率加成;"
                "破译、连胜、速解、每日连签等可解锁 24 枚成就。");

        section(s_list, "【小技巧】",
                "开局用几颗不同颜色探路,一次收集更多反馈;\n"
                "绿钉优先锁定位置,黄钉用来排除;\n"
                "可重复模式注意:某颜色黄钉数有上限,别重复猜同色超过必要颗数。");

        section(s_list, "【主题】",
                "设置中可切换 5 套主题,珠子配色与界面同步换肤。");
    }

    shell_set_hints("↑↓ 滚动 · 双击返回");
}

static void about_exit(void) {
    s_list = NULL;
}

static bool about_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if ((btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) && ev == BSP_BTN_PRESS && s_list) {
        int dir = (btn == BSP_BTN_UP) ? -1 : 1;
        lv_obj_scroll_by(s_list, 0, dir * -100, LV_ANIM_ON);
        sound_play(SND_TICK);
        return true;
    }
    return false;
}

static const app_page_t PAGE_ABOUT = {
    .title = "关于",
    .enter = about_enter,
    .exit = about_exit,
    .key = about_key,
};

const app_page_t *about_page(void) {
    return &PAGE_ABOUT;
}
