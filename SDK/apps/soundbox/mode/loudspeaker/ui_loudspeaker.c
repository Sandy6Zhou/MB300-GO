#include "ui/ui_api.h"
#include "system/includes.h"

#if TCFG_APP_LOUDSPEAKER_EN
#if (TCFG_UI_ENABLE&&(CONFIG_UI_STYLE == STYLE_JL_LED7))
void ui_iis_temp_finsh(u8 menu)//子菜单被打断或者显示超时
{
    switch (menu) {
    default:
        break;
    }
}


static void led7_show_loudspeaker(void *hd)
{
    LCD_API *dis = (LCD_API *)hd;
    dis->lock(1);
    dis->clear();
    dis->setXY(0, 0);
    dis->show_string((u8 *)" SPK");
    dis->lock(0);
}

extern u32 recoder_mic_time;
static void led7_show_recoder_time(void *hd)
{
    LCD_API *dis = (LCD_API *)hd;

    u32 msec = jiffies_msec2offset(recoder_mic_time, jiffies_msec());

    u32 min = (u32)(msec / 1000 / 60);
    u32 sec = (u32)(msec / 1000 % 60);
    u8 tmp_buf[5] = {0};
    // u8 hours = (u8)(min/60);
    if (min >= 60) {
        min = 59;
    }
    itoa2(min, (u8 *)&tmp_buf[0]);
    itoa2(sec, (u8 *)&tmp_buf[2]);

    dis->lock(1);
    //dis->clear();
    dis->setXY(0, 0);
    dis->clear_icon(0xffff);

    dis->show_string(tmp_buf);
    dis->show_icon(LED7_2POINT);

    dis->lock(0);
}


static void *ui_open_loudspeaker(void *hd)
{
    ui_set_auto_reflash(1000);//设置主页500ms自动刷新

    return NULL;
}

static void ui_close_loudspeaker(void *hd, void *private)
{
    LCD_API *dis = (LCD_API *)hd;
    if (!dis) {
        return ;
    }

    if (private) {
        free(private);
    }
}

static void ui_loudspeaker_main(void *hd, void *private) //主界面显示
{
    if (!hd) {
        return;
    }
#if USER_OPEN_RECODER_UI
    if (get_mix_recorder_status()) {
        led7_show_recoder_time(hd);
    } else
#endif
    {
        led7_show_loudspeaker(hd);
    }
}


static int ui_loudspeaker_user(void *hd, void *private, int menu, int arg)//子界面显示 //返回true不继续传递 ，返回false由common统一处理
{
    int ret = true;
    LCD_API *dis = (LCD_API *)hd;
    if (!dis) {
        return false;
    }
    switch (menu) {
    case MENU_LOUDSPEAKER:
        led7_show_loudspeaker(hd);
        break;
    default:
        ret = false;
        break;
    }

    return ret;

}



const struct ui_dis_api loudspeaker_main = {
    .ui      = UI_LOUDSPEAKER_MENU_MAIN,
    .open    = ui_open_loudspeaker,
    .ui_main = ui_loudspeaker_main,
    .ui_user = ui_loudspeaker_user,
    .close   = ui_close_loudspeaker,
};


#endif
#endif


