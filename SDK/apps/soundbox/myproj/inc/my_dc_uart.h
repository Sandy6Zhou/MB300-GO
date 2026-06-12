/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_dc_uart.h
 * Brief      : DC板UART通信模块头文件
 * Version    : V1.0.0
 * Author     : songshiqing(songshiqing@jimiiot.com)
 * Date       : 2026-03-02
************************************************************************************************/

#ifndef MY_DC_UART_H
#define MY_DC_UART_H

#define MY_DC_UART_PORT              HAL_UART_2
#define MY_DC_UART_BAUD              MY_UART_BAUD_115200

#define MY_DC_UART_TX_PIN            IO_PORTA_07
#define MY_DC_UART_RX_PIN            IO_PORTA_08

#define MY_DC_UART_RX_TMP_BUF_LEN    512

/* DC关机后自动运输延迟（小时）；0=禁用，VM 持久化 */
#define DC_TRANSPORT_DELAY_HOURS_DEFAULT 6   /* 默认 6 小时 */
#define DC_TRANSPORT_DELAY_HOURS_MIN     1   /* 最小 1 小时 */
#define DC_TRANSPORT_DELAY_HOURS_MAX     720 /* 最大约 30 天，防 ms 换算溢出 */

/* 开关ID：与 MB300_SW_xx 指令一一对应，用于 my_dc_ctrl_switch(sw_id, onoff)
 * LED 时 onoff 传模式值 0~4（000/001/010/011/100） */
typedef enum {
    MY_DC_SW_AC = 0,
    MY_DC_SW_DC,
    MY_DC_SW_USB,
    MY_DC_SW_LED,
} my_dc_sw_id_t;

/* 开关控制结果（异步）：my_dc_ctrl_switch 提交后，由 dc_ctrl_ack_timer_cb 轮询此结果 */
typedef enum {
    MY_DC_CTRL_RET_IDLE = 0,        // 尚无控制结果（初始状态）
    MY_DC_CTRL_RET_PENDING,         // 命令已受理，等待从机回执（Modbus 写线圈已发送）
    MY_DC_CTRL_RET_OK,              // 从机正常应答
    MY_DC_CTRL_RET_EXCEPTION,       // 从机异常应答(功能码|0x80)，err_code 为异常码
    MY_DC_CTRL_RET_TIMEOUT,         // 超时(含重试后失败)
    MY_DC_CTRL_RET_SEND_FAIL,       // 本地发送失败/参数映射失败
} my_dc_ctrl_result_t;

typedef struct{
    uint8 bat_percent;            // 电量百分比
    uint16 remain_time;           // 剩余可用时间(单位min)
    uint16 bat_temp;              // 电池温度(单位0.1℃)
    uint16 output_total_power;    // 输出总功率(单位0.1W)
    uint16 input_total_power;     // 输入总功率(单位0.1W)
    uint16 ac_power;              // AC功率(单位0.1W)
    uint16 dc_power;              // DC功率(单位0.1W)
    uint16 usb_power;             // USB功率(单位0.1W)
    uint16 led_power;             // LED功率(单位0.1W)
    uint8 sw_status;              // FF01 0x800C: bit0 AC bit1 DC bit2 USB bit3 LED闪 bit4~5 LED亮度 bit6~7保留
    uint8 fault_status;           // 故障状态(bit0:逆变器过载,bit1:逆变器过温,bit2:电池过温)
}device_data;

void my_dc_uart_init(void);
void my_dc_uart_deinit(void);

unsigned int my_dc_uart_send(unsigned char *data, unsigned int size);

void my_dc_proto_feed(const unsigned char *data, unsigned int len);

/* 获取最新设备数据快照；返回0表示成功。 */
int my_dc_get_data(device_data *out);

/* 提交开关控制请求(异步)；返回0表示已受理。 */
int my_dc_ctrl_switch(my_dc_sw_id_t sw_id, uint8 onoff);

/* 提交开关控制并启动 ACK 定时器；结果通过 BLE 推送 RETURN_<cmd>_<state>_OK/FAIL；返回0表示已受理。 */
int my_dc_ctrl_switch_with_ack(my_dc_sw_id_t sw_id, uint8 onoff, const char *cmd, const char *state);

/* 查询最近一次控制结果与错误码；返回0表示参数有效。 */
int my_dc_get_last_ctrl_result(uint8 *result, uint8 *err_code);

/* 查询当前DC链路在线状态。 */
int my_dc_is_online(void);

/* 进入运输模式：写 0x0001 Bit10；返回0表示已受理。 */
int my_dc_enter_transport_mode(void);

void my_dc_uart_task(void *p_arg);

#endif

