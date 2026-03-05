/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_gpio.h
**文件描述:        设备平台GPIO初始化模块头文件
**当前版本:        V1.0
**作    者:       songshiqin(songshiqin@jimiiot.com)  
**完成日期:        2026.03.04
*********************************************************************/

#ifndef MY_GPIO_H
#define MY_GPIO_H

typedef void (*GpioIntCallback)(void);

typedef enum
{
    GPIO_DIR_INPUT = 0,
    GPIO_DIR_OUTPUT,
} GPIO_DIR_E;

typedef enum
{
    GPIO_PULL_NONE = 0,
    GPIO_PULL_UP,
    GPIO_PULL_DOWN,
} GPIO_PULL_MODE;

typedef enum GPIO_INT_tag
{
    GPIO_INT_DISABLE = 0,  ///< Disable the relative gpio interrupt.
    GPIO_INT_EDGE_RISING,  ///< detect the rising edges.
    GPIO_INT_EDGE_FALLING, ///< detect the falling edges.
    GPIO_INT_EDGE_BOTH,    ///< detect the rising edges and falling edges.
    // GPIO_INT_LEVEL_HIGH,   ///< detect high level.
    // GPIO_INT_LEVEL_LOW,    ///< detect low level.
} GPIO_INT_TYPE;

typedef struct
{
    GPIO_DIR_E dir;
    GPIO_PULL_MODE pull_mode;
    GPIO_INT_TYPE int_type;
    GpioIntCallback int_callback;
    uint8 init_val;
} GPIO_CFG_S;

int my_gpio_init(void);

int my_gpio_config(uint32 id, const GPIO_CFG_S *cfg);
int my_gpio_set_level(uint32 gpioId, uint8 level);
int my_gpio_get_level(uint32 gpioId);

#endif
