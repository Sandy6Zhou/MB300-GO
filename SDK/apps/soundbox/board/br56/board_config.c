#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".board_config.data.bss")
#pragma data_seg(".board_config.data")
#pragma const_seg(".board_config.text.const")
#pragma code_seg(".board_config.text")
#endif
// V300挪去sdk_board_config.c
/* #include "asm/power_interface.h" */
/* #include "gpadc.h" */
/* #include "app_config.h" */
/* #include "app_power_config.h" */
/*  */
/* extern void bt_wl_sync_clk_en(void); */
/* #if TCFG_APP_FM_EN */
/* #include "fm_manage.h" */
/*  */
/* FM_DEV_PLATFORM_DATA_BEGIN(fm_dev_data) */
/* .iic_hdl = 0, */
/*  .iic_delay = 50, */
/*   FM_DEV_PLATFORM_DATA_END(); */
/* #endif */
/*  */
/* void board_init() */
/* { */
/*     board_power_init(); */
/*  */
/* #if TCFG_UPDATE_UART_IO_EN */
/*     { */
/* #include "uart_update.h" */
/*         uart_update_cfg  update_cfg = { */
/*             .rx = IO_PORTA_02, */
/*             .tx = IO_PORTA_03, */
/*         }; */
/*         uart_update_init(&update_cfg); */
/*     } */
/* #endif */
/*  */
/*     adc_init(); */
/*  */
/*     bt_wl_sync_clk_en(); */
/*  */
/* #if TCFG_APP_FM_EN */
/*     y_printf(">> Func:%s, Line:%d, call: fm_dev_init Func!\n", __func__, __LINE__); */
/*     fm_dev_init((void *)(&fm_dev_data)); */
/* #endif */
/* } */
