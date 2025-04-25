#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".IT7259E.data.bss")
#pragma data_seg(".IT7259E.data")
#pragma const_seg(".IT7259E.text.const")
#pragma code_seg(".IT7259E.text")
#endif
#include "app_config.h"
#include "system/includes.h"
#include "gpio.h"
#include "asm/mcpwm.h"

#if 1 //0:软件iic,  1:硬件iic
#define _IIC_USE_HW
#endif
#include "iic_api.h"

#if TCFG_TOUCH_PANEL_ENABLE

#if TCFG_TP_IT7259E_ENABLE

#include "ui/ui.h"
#include "ui/ui_api.h"

typedef struct {
    u8 init;
    hw_iic_dev iic_hdl;
} ft_param;

static ft_param module_param = {0};
#define __this   (&module_param)

#define FT6336G_IIC_DELAY   1
#define FT6336G_IIC_ADDR  0x46


#define TP_RESET_IO			IO_PORTC_02
#define TP_INT_IO			IO_PORTC_01

#define FT6336G_RESET_H() gpio_set_mode(IO_PORT_SPILT(TP_RESET_IO), PORT_OUTPUT_HIGH)
#define FT6336G_RESET_L() gpio_set_mode(IO_PORT_SPILT(TP_RESET_IO), PORT_OUTPUT_HIGH)
#define FT6336G_INT_IO    (TP_INT_IO)

#define FT6336G_INT_IN()  \
	do { \
        gpio_set_mode(IO_PORT_SPILT(FT6336G_INT_IO), PORT_INPUT_PULLUP_10K); \
	} while(0);
#define FT6336G_INT_R()   gpio_read(FT6336G_INT_IO)




static int tpd_i2c_write(u8 *buf, int len)
{
    int ret = 0;
    iic_start(__this->iic_hdl);
    if (0 == iic_tx_byte(__this->iic_hdl, FT6336G_IIC_ADDR << 1)) {
        iic_stop(__this->iic_hdl);
        return 0;
    }
    delay_nops(FT6336G_IIC_DELAY);
    ret = iic_write_buf(__this->iic_hdl, buf, len);
    return ret;
}


static u8 tpd_i2c_read(u8 *buf, u8 len, u8 address)
{
    int i;
    iic_start(__this->iic_hdl);
    if (0 == iic_tx_byte(__this->iic_hdl, FT6336G_IIC_ADDR << 1)) {
        iic_stop(__this->iic_hdl);
        return 0;
    }
    delay_nops(FT6336G_IIC_DELAY);
    if (0 == iic_tx_byte(__this->iic_hdl, address)) {
        iic_stop(__this->iic_hdl);
        return 0;
    }
    delay_nops(FT6336G_IIC_DELAY);

    iic_start(__this->iic_hdl);
    if (0 == iic_tx_byte(__this->iic_hdl, (FT6336G_IIC_ADDR << 1) | 0x01)) {
        iic_stop(__this->iic_hdl);
        return 0;
    }

    for (i = 0; i < len; i++) {
        delay_nops(FT6336G_IIC_DELAY);
        if (i == (len - 1)) {
            *buf++ = iic_rx_byte(__this->iic_hdl, 0);
        } else {
            *buf++ = iic_rx_byte(__this->iic_hdl, 1);
        }

    }
    delay_nops(FT6336G_IIC_DELAY);
    iic_stop(__this->iic_hdl);
    delay_nops(FT6336G_IIC_DELAY);

    return 1;
}

extern void delay_2ms(int cnt);

static int tpd_print_version(void)
{
    char buffer[9];
    int ret = -1;
    int retry = 0;

    extern void wdt_clear();
    wdt_clear();
    do {
        buffer[0] = 0xFF;
        tpd_i2c_read((u8 *)&buffer[0], 1, 0x80);
        os_time_dly(1);
    } while ((buffer[0] & 0x01) && (retry++ < 10));
    if (buffer[0] & 0x01) {
        return -1;
    }

    buffer[0] = 0x20;
    buffer[1] = 0x1;
    buffer[2] = 0x0;
    ret = tpd_i2c_write((u8 *)buffer, 3);
    if (ret != 3) {
        printf("[mtk-tpd] i2c write communcate error in getting FW version : 0x%x\n", ret);
    }

    os_time_dly(10);

    retry = 0;
    do {
        buffer[0] = 0xFF;
        tpd_i2c_read((u8 *)&buffer[0], 1, 0x80);
    } while ((buffer[0] & 0x01) && (retry++ < 10));
    if (buffer[0] & 0x01) {
        return -1;
    }

    ret = tpd_i2c_read((u8 *)&buffer[0], 9, 0xA0);

    if (ret == 0) {
        printf("[mtk-tpd] i2c read communcate error in getting FW version : 0x%x\n", ret);
    } else {
        printf("[mtk-tpd] ITE7260 Touch Panel Firmware Version %x %x %x %x %x %x %x %x %x\n",
               buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7], buffer[8]);
    }
    return 0;
}



#define Y_MIRROR  1
#define X_MIRROR  1

#define VK_Y      		240
#define VK_X      		240
#define VK_Y_MOVE   	120
#define VK_X_MOVE      	120
#define VK_X_Y_DIFF    	25

static u8 tp_last_staus = ELM_EVENT_TOUCH_UP;
static int tp_down_cnt = 0;

#define abs(x)  ((x)>0?(x):-(x) )
u8 touch_press_status = 0;

static void tpd_down(int raw_x, int raw_y, int x, int y, int p)
{
    struct touch_event t;
    static int first_x = 0;
    static int first_y = 0;
    static u8 move_flag = 0;

    if (x < 0) {
        x = 0;
    }
    if (x > (VK_X - 1)) {
        x = VK_X - 1;
    }
    if (y < 0) {
        y = 0;
    }
    if (y > (VK_Y - 1)) {
        y = VK_Y - 1;
    }
#if Y_MIRROR
    x = VK_X - x - 1;
#endif

#if X_MIRROR
    y = VK_Y - y - 1;
#endif

    if ((tp_last_staus == ELM_EVENT_TOUCH_DOWN) && (x == first_x) && (y == first_y)) {
        tp_down_cnt++;
        if (tp_down_cnt < 30) {
            return;
        }
        tp_last_staus = ELM_EVENT_TOUCH_HOLD;
        tp_down_cnt = 0;

        t.event = tp_last_staus;
        t.x = x;
        t.y = y;
        ui_touch_msg_post(&t);
        return;
    }

    /* printf("D[%4d %4d %4d]\n", x, y, p); */
    if (tp_last_staus != ELM_EVENT_TOUCH_UP) {
        int x_move = abs(x - first_x);
        int y_move = abs(y - first_y);

        if (!move_flag && (x_move >= VK_X_MOVE || y_move >= VK_Y_MOVE) && (abs(x_move - y_move) >= VK_X_Y_DIFF)) {
            if (x_move > y_move) {
                if (x > first_x) {
                    tp_last_staus = ELM_EVENT_TOUCH_R_MOVE;
                } else {
                    tp_last_staus = ELM_EVENT_TOUCH_L_MOVE;
                }

            } else {

                if (y > first_y) {
                    tp_last_staus = ELM_EVENT_TOUCH_D_MOVE;
                } else {
                    tp_last_staus = ELM_EVENT_TOUCH_U_MOVE;
                }
            }
            move_flag = 1;
        } else {
            if ((x == first_x) || (y == first_y)) {
                return;
            }
            tp_last_staus = ELM_EVENT_TOUCH_MOVE;
        }
        /* tp_last_staus = ELM_EVENT_TOUCH_HOLD; */
    } else {
        tp_last_staus = ELM_EVENT_TOUCH_DOWN;
        first_x = x;
        first_y = y;
        move_flag = 0;
    }

    t.event = tp_last_staus;
    t.x = x;
    t.y = y;
    ui_touch_msg_post(&t);
}

static void tpd_up(int raw_x, int raw_y, int x, int y, int p)
{
    struct touch_event t;

    if (x < 0) {
        x = 0;
    }
    if (x > (VK_X - 1)) {
        x = VK_X - 1;
    }
    if (y < 0) {
        y = 0;
    }
    if (y > (VK_Y - 1)) {
        y = VK_Y - 1;
    }

#if Y_MIRROR
    x = VK_X - x - 1;
#endif

#if X_MIRROR
    y = VK_Y - y - 1;
#endif

    /* printf("U[%4d %4d %4d]\n", x, y, 0); */
    tp_last_staus = ELM_EVENT_TOUCH_UP;
    tp_down_cnt = 0;
    t.event = tp_last_staus;
    t.x = x;
    t.y = y;
    ui_touch_msg_post(&t);
}

static int x[2] = {(int) - 1, (int) - 1 };
static int y[2] = {(int) - 1, (int) - 1 };
static bool finger[2] = { 0, 0 };
static bool flag = 0;


static int touch_event_handler()
{
    unsigned char pucPoint[14];
    //unsigned char cPoint[8];
    //unsigned char ePoint[6];
    unsigned char key_temp = 0;
    int ret;
    int xraw, yraw;
    int i = 0;

    do {
        ret = tpd_i2c_read((u8 *)&pucPoint[0], 1, 0x80);
        if (!(pucPoint[0] & 0x80 || pucPoint[0] & 0x01)) {
            /* printf("No point.\n"); */
            continue;
        }
        ret = tpd_i2c_read((u8 *)&pucPoint[0], 14, 0xE0);
        /* printf("puc[0] = %d,puc[1] = %d,puc[2] = %d\n",pucPoint[0],pucPoint[1],pucPoint[2]); */
        if (ret != 0) {
            if (pucPoint[0] & 0xF0) {          /* gesture */
                {
                    /* printf("[mtk-tpd] it's a button/gesture\n"); */
                    continue;
                }
            } else if (pucPoint[1] & 0x01) {        /* palm */
                /* printf("[mtk-tpd] it's a palm\n"); */
                continue;
            } else if (!(pucPoint[0] & 0x08)) {        /* no more data */
                if (finger[0]) {
                    finger[0] = 0;
                    tpd_up(x[0], y[0], x[0], y[0], 0);
                    flag = 1;
                }

                if (finger[1]) {
                    finger[1] = 0;
                    tpd_up(x[1], y[1], x[1], y[1], 0);
                    flag = 1;
                }

                if (flag) {
                    flag = 0;
                }

                /* printf("[mtk-tpd] no more data\n"); */

                continue;
            } else if (pucPoint[0] & 0x04) {        /* 3 fingers */
                /* printf("[mtk-tpd] we don't support three fingers\n"); */
                continue;
            } else {
                if (pucPoint[0] & 0x01) {              /* finger 1 */
                    //char pressure_point;

                    xraw = ((pucPoint[3] & 0x0F) << 8) + pucPoint[2];
                    yraw = ((pucPoint[3] & 0xF0) << 4) + pucPoint[4];
                    //pressure_point = pucPoint[5] & 0x0f;
                    //printf("[mtk-tpd] input Read_Point1 x=%d y=%d p=%d\n",xraw,yraw,pressure_point);
                    //tpd_calibrate(&xraw, &yraw);
                    x[0] = xraw;
                    y[0] = yraw;
                    finger[0] = 1;
                    tpd_down(x[0], y[0], x[0], y[0], 0);//1
                    //printf("tpd_down:x0=%d,y0=%d\n",x[0],y[0]);

                } else if (finger[0]) {
                    tpd_up(x[0], y[0], x[0], y[0], 0);
                    finger[0] = 0;
                }

                if (pucPoint[0] & 0x02) {              // finger 2
                    //char pressure_point;
                    xraw = ((pucPoint[7] & 0x0F) << 8) + pucPoint[6];
                    yraw = ((pucPoint[7] & 0xF0) << 4) + pucPoint[8];
                    //pressure_point = pucPoint[9] & 0x0f;
                    //printf("[mtk-tpd] input Read_Point2 x=%d y=%d p=%d\n",xraw,yraw,pressure_point);
                    // tpd_calibrate(&xraw, &yraw);
                    x[1] = xraw;
                    y[1] = yraw;
                    finger[1] = 1;
                    tpd_down(x[1], y[1], x[1], y[1], 1);
                    //printf("tpd_down:x1=%d,y1=%d\n",x[1],y[1]);

                } else if (finger[1]) {
                    tpd_up(x[1], y[1], x[1], y[1], 0);
                    finger[1] = 0;
                }
            }
        } else {
            /* printf("[mtk-tpd] i2c read communcate error in getting pixels : 0x%x\n", ret); */
        }
    } while (0);
    return 0;
}


static void delay_ms(int cnt)
{
    int delay = (cnt + 9) / 10;
    os_time_dly(delay);
}

static OS_SEM touch_sem; // 触摸中断post

/*------------------
 * tp中断配置
 * -----------------*/
static void tp_irq_callback(enum gpio_port port, u32 pin, enum gpio_irq_edge edge)
{
    /* printf("port%d.%d:%d-cb1\n", (u32)port, pin, (u32)edge); */

    os_sem_post(&touch_sem);
}

struct gpio_irq_config_st tp_int_irq_config = {
    .pin = NO_CONFIG_PORT,
    .irq_edge = PORT_IRQ_EDGE_FALL,
    .callback = tp_irq_callback,
    .irq_priority = 3,
};

void tp_irq_init(void)
{
    u8 port_x = FT6336G_INT_IO / IO_GROUP_NUM;
    u16 port_pin_x = BIT(FT6336G_INT_IO % IO_GROUP_NUM);
    tp_int_irq_config.pin =  port_pin_x;
    printf("[%s] port_x:%x, port_pin_x:%x", __func__, port_x, port_pin_x);
    gpio_set_mode(IO_PORT_SPILT(FT6336G_INT_IO), PORT_INPUT_PULLUP_10K);
    gpio_irq_config(port_x, &tp_int_irq_config);
}

static void touch_interupt_task(void *p)
{
    int sem_timeout = 0;

    os_sem_create(&touch_sem, 0);
    tp_irq_init();

    while (1) {
        os_sem_pend(&touch_sem, sem_timeout);

        touch_event_handler();
    }
}

void it7259e_init()
{
    int ret = 0;
    FT6336G_INT_IN();

    FT6336G_RESET_H();
    delay_ms(10);
    FT6336G_RESET_L();
    delay_ms(10);
    FT6336G_RESET_H();

    struct iic_master_config ir_iic_cfg = {
        .role = IIC_MASTER,
        .scl_io = IO_PORTA_09,
        .sda_io = IO_PORTA_10,
        .io_mode = PORT_INPUT_PULLUP_10K,      //上拉或浮空，如果外部电路没有焊接上拉电阻需要置上拉
        .hdrive = PORT_DRIVE_STRENGT_2p4mA,    //IO口强驱
        .master_frequency = 5000,  //IIC通讯波特率 未使用
        .io_filter = 1,                        //软件iic无滤波器
        .ie_en = 1,//1:注册中断
        .irq_priority = 3,//优先级
    };

    enum iic_state_enum state = iic_init(__this->iic_hdl, &ir_iic_cfg);
    if (state == IIC_OK) {
        printf("iic(%d) master init ok", (u32)__this->iic_hdl);
    } else {
        printf("iic(%d) master init fail", (u32)__this->iic_hdl);
        return;
    }

    ret = tpd_print_version();        //read firmware version
    if (ret) {
        return;
    }
    unsigned char reset_buf[2] = { 0x20, 0x6F};
    u8 buffer[8];
    if (!tpd_i2c_read(buffer, 1, 0x80)) {
        printf("it7260 I2C transfer error, line: %d\n", __LINE__);
        return;
    }

    os_time_dly(1);
    tpd_i2c_write(reset_buf, 2);
    /* os_time_dly(10); */

    task_create(touch_interupt_task, NULL, "touch_task");
}

#endif

#endif
