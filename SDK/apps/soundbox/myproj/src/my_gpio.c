/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_gpio.c
**文件描述:        设备平台GPIO初始化模块
**当前版本:        V1.0
**作    者:       songshiqin(songshiqin@jimiiot.com)  
**完成日期:        2026.03.04
*********************************************************************/
#include "my_common.h"

typedef struct
{
    int8 gpioId;
    GPIO_CFG_S gpioCfg;
} MY_GPIO_CONFIG_STRUCT;

MY_GPIO_CONFIG_STRUCT my_gpio_table[] = {
  // GPIO ID, OUT/IN, PULL UP/DOWN, INT ENABLE?, INT CALLBACK, INIT VALUE
    {IO_PORTG_08, {GPIO_DIR_INPUT, GPIO_PULL_UP, GPIO_INT_DISABLE, NULL, 0}   }, // PG8 蓝牙配对按键GPIO
    {IO_PORTA_08, {GPIO_DIR_INPUT, GPIO_PULL_UP, GPIO_INT_DISABLE, NULL, 0}   }, // PA8 DC_UART_RX 默认上拉
    {IO_PORTG_01, {GPIO_DIR_OUTPUT, GPIO_PULL_NONE, GPIO_INT_DISABLE, NULL, 1} }, // PG1 458/232接口5V供电使能（高有效，上电默认打开）

#if 0  //硬件预留GPIO
    {IO_PORTG_02,  {GPIO_DIR_OUTPUT, GPIO_PULL_NONE, GPIO_INT_DISABLE, NULL, 0}}, // PG2 外挂4G模块的电源控制
    {IO_PORTG_06, {GPIO_DIR_OUTPUT, GPIO_PULL_NONE, GPIO_INT_DISABLE, NULL, 0}}, // PG6 458通信数据使能口
    {IO_PORTC_03,  {GPIO_DIR_INPUT, GPIO_PULL_NONE, GPIO_INT_DISABLE, NULL, 0} }, // PC3 Gsensor中断GPIO
#endif

    {IO_PORTG_00,  {GPIO_DIR_OUTPUT, GPIO_PULL_NONE, GPIO_INT_DISABLE, NULL, 1}}, // PG0 音频PA使能控制 硬件罗工要求上电默认拉高使能
    {-1,  {GPIO_DIR_OUTPUT, GPIO_PULL_NONE, GPIO_INT_DISABLE, NULL, 0}},
};

/*
 * Init all GPIOs what we need.
 */
int my_gpio_init(void)
{
    int ret = -1;
    int level = 0;
    MY_GPIO_CONFIG_STRUCT *pCfg = my_gpio_table;

    while (pCfg->gpioId != -1)
    {
        ret = my_gpio_config(pCfg->gpioId, &(pCfg->gpioCfg));

        if (ret < 0)
        {
            my_log_printf(1, "[GPIO_Config] Config GPIO %d RET= %d", pCfg->gpioId, ret);
            my_log_printf(1, "DIR=%d,PULL=%d,INT=%d,VALUE=%d",
                          pCfg->gpioCfg.dir,
                          pCfg->gpioCfg.pull_mode,
                          pCfg->gpioCfg.int_type,
                          pCfg->gpioCfg.init_val);
        }
#if 0
        else
        {
            level = gpio_read(pCfg->gpioId);
            my_log_printf(1, "[GPIO_Init_OK] GPIO=%d DIR=%d INIT=%d READ=%d",
                          pCfg->gpioId,
                          pCfg->gpioCfg.dir,
                          pCfg->gpioCfg.init_val,
                          level);
        }
#endif
        pCfg++;
    }

    return ret;
}

int my_gpio_config(uint32 id, const GPIO_CFG_S *cfg)
{
    int ret = 0;
    int mode = PORT_INPUT_FLOATING;

    if (cfg == NULL)
    {
        return -1;
    }

    if (cfg->dir == GPIO_DIR_OUTPUT)
    {
        mode = cfg->init_val ? PORT_OUTPUT_HIGH : PORT_OUTPUT_LOW;
        ret = gpio_set_mode(IO_PORT_SPILT(id), mode);
        if (ret < 0)
        {
            return ret;
        }
        return gpio_write(id, cfg->init_val ? 1 : 0);
    }

    if (cfg->pull_mode == GPIO_PULL_UP)
    {
        mode = PORT_INPUT_PULLUP_10K;
    }
    else if (cfg->pull_mode == GPIO_PULL_DOWN)
    {
        mode = PORT_INPUT_PULLDOWN_10K;
    }
    else
    {
        mode = PORT_INPUT_FLOATING;
    }

    return gpio_set_mode(IO_PORT_SPILT(id), mode);
}

int my_gpio_set_level(uint32 gpioId, uint8 level)
{
    if (level > 1)
    {
        my_log_printf(1, "level param error! level=%d", level);
        return -1;
    }

    gpio_set_mode(IO_PORT_SPILT(gpioId), PORT_OUTPUT_LOW);
    return gpio_write(gpioId, level);
}

/*
 * 读取GPIO电平(当前仅支持输入模式IO口)
 * @param gpioId  GPIO引脚ID
 * @return       0/1 成功返回电平值, -1 失败
 *
 * 说明: 本SDK要求IO配置为输入模式才能正确读取外部电平
 */
int my_gpio_get_level(uint32 gpioId)
{
#if 0  // NOTE: 此接口会修改IO口的模式为输入模式
    gpio_set_mode(IO_PORT_SPILT(gpioId), PORT_INPUT_FLOATING);
#endif
    int level = gpio_read(gpioId);

    if (level < 0)
    {
        return -1;
    }

    return level;
}
