#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ui_record.data.bss")
#pragma data_seg(".ui_record.data")
#pragma const_seg(".ui_record.text.const")
#pragma code_seg(".ui_record.text")
#endif
#include "ui/ui_api.h"


#if TCFG_APP_RECORD_EN
#if (TCFG_UI_ENABLE&&(CONFIG_UI_STYLE == STYLE_JL_LED7))

#include "record.h"

void ui_record_temp_finsh(u8 menu)//子菜单被打断或者显示超时
{
    switch (menu) {
    default:
        break;
    }
}

static void led7_show_record(void *hd)
{
    LCD_API *dis = (LCD_API *)hd;

    dis->lock(1);
    dis->clear();
    dis->setXY(0, 0);
    dis->show_string((u8 *)" REC");
    dis->lock(0);
}

static void led_show_record_time(void *hd, int time)
{
    LCD_API *dis = (LCD_API *)hd;
    u8 tmp_buf[5] = {0};

    u8 Min = (u8)(time / 60 % 60);
    u8 Sec = (u8)(time % 60);
    /* printf("rec Min = %d, Sec = %d\n", Min, Sec); */

    itoa2(Min, (u8 *)&tmp_buf[0]);
    itoa2(Sec, (u8 *)&tmp_buf[2]);

    dis->lock(1);
    dis->clear();
    dis->setXY(0, 0);
    dis->show_string(tmp_buf);
    dis->show_icon(LED7_2POINT);
    dis->lock(0);
}


static void *ui_open_record(void *hd)
{
    ui_set_auto_reflash(500);
    return NULL;
}

static void ui_close_record(void *hd, void *private)
{
    LCD_API *dis = (LCD_API *)hd;
    if (!dis) {
        return ;
    }
    if (private) {
        free(private);
    }
    record_ui_del_mutex();
}

static void ui_record_main(void *hd, void *private) //主界面显示
{
    if (!hd) {
        return;
    }

    int time = 0;
    if (is_recorder_runing()) {
        time = app_recorder_get_enc_time();
        /* printf("-->%d\n", time); */
        led_show_record_time(hd, time);
    } else {
        time = app_recorder_get_play_file_time();
        if (time == -1) {
            led7_show_record(hd);
        } else {
            led_show_record_time(hd, time);
        }
    }
}


static int ui_record_user(void *hd, void *private, int menu, int arg)//子界面显示 //返回true不继续传递 ，返回false由common统一处理
{
    int ret = true;
    LCD_API *dis = (LCD_API *)hd;
    if (!dis) {
        return false;
    }
    switch (menu) {
    case MENU_RECORD:
        led7_show_record(hd);
        break;
    default:
        ret = false;
        break;
    }

    return ret;

}



const struct ui_dis_api record_main = {
    .ui      = UI_RECORD_MENU_MAIN,
    .open    = ui_open_record,
    .ui_main = ui_record_main,
    .ui_user = ui_record_user,
    .close   = ui_close_record,
};

#endif
#endif//(TCFG_UI_ENABLE && TCFG_APP_RECORD_EN)
