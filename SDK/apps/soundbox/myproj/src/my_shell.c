/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_shell.c
 * Brief      : 设备平台APP AT CMD处理模块
 * Version    : V1.0.0
 * Author     : 黄俊凯(huangjunkai@jimiiot.com)
 * Date       : 2025-12-12
************************************************************************************************/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".my_shell.data.bss")
#pragma data_seg(".my_shell.data")
#pragma const_seg(".my_shell.text.const")
#pragma code_seg(".my_shell.text")
#endif

#include "my_common.h"

// 产测指令
const char FACTORY_CMD_HEADER[] = "AT^GT_CM=";
char FACTORY_CMD_RETURN[] = "RETURN_";

// 替换蓝牙名称中IMEI后4位
static void my_sync_bt_name_with_imei_last4(const char *imei);
extern int bt_modify_name(u8 *new_name);

static int sh_test_led_impl(int argc, char *argv[]);
static int sh_test_gpio_impl(int argc, char *argv[]);

/* { visible, command, help, function } */
CMD_STRUC AT_CMD_INNER[] = {

    {1, "TEST",      "AT CMD TEST",             sh_at_test},
#if 0
    {0, "GET",       "GET A PARAM",             sh_atget},
    {0, "SET",       "SET A PARAM",             sh_atset},
    {1, "IPC",       "IPC CMD TEST",            sh_ipc_test},
    {1, "AMS",       "AMS CMD TEST",            sh_ams_test},
    {0, JMLOG_STR,   "SHOW SYSTEM LOG",         sh_log},
    {1, "GPIO",      "GPIO TEST",               sh_at_gpio},
    {1, "IIC",       "IIC TEST",                sh_at_iic},
    {1, "ATCMD",     "AT CMD TEST",             sh_atcmd},
    {1, "SLEEP",     "SLEEP TEST",              sh_at_sleep},
    {0, "LOGFILE",   "LOG TO FILE",             sh_log_file},
    {0, "SMSSEND",   "SEND A SMS MSG",          sh_sendsms},
    {0, "FORMAT",    "FORMAT FAT SYSTEM",       sh_format_fs},
    {0, "RESET",     "RESET SYSTEM",            sh_reset},
#endif
    {0, NULL,        NULL,                      NULL}
};

//AT^GT_CM=
char *my_handle_at_factory_cmd(char **pParam, int nParam)
{
    static char resp[256];
    char *p = resp;
    const lic_ff_struct *lic_ff;
    const lic_gg_struct *lic_gg;
    uint16 ECDH_GValue;
    uint8 data_buff[64] = {0};
    const GsmImei_t *gsmImei;
    int ret;
    uint8 audio_state; // 当前音频状态 (APP_AUDIO_STATE_xxx)
    int16 max_vol;     // 最大音量
    int vol;           // 音量值，查询/设置复用

    memset(resp, 0, sizeof(resp));

    if (CMD_MATCHED2(pParam[0], "FF"))
    {
        // AT^GT_CM=FF
        if (nParam < 2)
        {
            lic_ff = my_param_get_ff();
            if (lic_ff->flag == FLAG_VALID)
            {
                hex2hexstr(lic_ff->hex, LICENSE_FF_STR_LEN / 2, data_buff, sizeof(data_buff));
                sprintf(resp, "RETURN_FF:%s", data_buff);
            }
            else
            {
                sprintf(resp, "RETURN_FF");
            }
        }
        // AT^GT_CM=FF,xxxx
        else
        {
            my_param_set_ff(pParam[1], strlen(pParam[1]));
            sprintf(resp, "RETURN_FF_SET_OK");
        }
    }
    else if (CMD_MATCHED2(pParam[0], "GG"))
    {
        // AT^GT_CM=GG
        if (nParam < 2)
        {
            lic_gg = my_param_get_gg();
            if (lic_gg->flag == FLAG_VALID)
            {
                hex2hexstr(lic_gg->hex, LICENSE_GG_STR_LEN / 2, data_buff, sizeof(data_buff));
                sprintf(resp, "RETURN_GG:%s", data_buff);
            }
            else
            {
                sprintf(resp, "RETURN_GG");
            }
        }
        // AT^GT_CM=GG,xxxx
        else
        {
            my_param_set_gg(pParam[1], strlen(pParam[1]));
            sprintf(resp, "RETURN_GG_SET_OK");
        }
    }
    else if (CMD_MATCHED2(pParam[0], "JGTAG"))
    {
        // AT^GT_CM=JGTAG,ON/OFF
        if (nParam == 2)
        {
            ret = my_param_set_jgtag_or_jatag(pParam[0], pParam[1]);
            if (ret == 0) {
                sprintf(resp, "RETURN_JGTAG_%s_OK", pParam[1]);
            }else {
                sprintf(resp, "RETURN_JGTAG_%s_FAIL", pParam[1]);
            }
        }
        else
        {
            sprintf(resp, "RETURN_JGTAG_SET_FAIL");
        }
    }
    else if (CMD_MATCHED2(pParam[0], "JATAG"))
    {
        // AT^GT_CM=JATAG,ON/OFF
        if (nParam == 2)
        {
            ret = my_param_set_jgtag_or_jatag(pParam[0], pParam[1]);
            if (ret == 0) {
                sprintf(resp, "RETURN_JATAG_%s_OK", pParam[1]);
            }else {
                sprintf(resp, "RETURN_JATAG_%s_FAIL", pParam[1]);
            }
        }
        else
        {
            sprintf(resp, "RETURN_JATAG_SET_FAIL");
        }
    }
    else if (CMD_MATCHED2(pParam[0], "MODIFYGV"))
    {
        // AT^GT_CM=MODIFYGV
        if (nParam < 2)
        {
            ECDH_GValue = my_param_get_Gvalue();
            sprintf(resp, "RETURN_GV:%d (%04X)", ECDH_GValue, ECDH_GValue);
        }
        // AT^GT_CM=MODIFYGV,xxxx
        else
        {
            ret = my_param_set_Gvalue(pParam[1]);
            if (ret == 0){
                sprintf(resp, "RETURN_MODIFYGV_SET_OK");
            } else {
                sprintf(resp, "RETURN_MODIFYGV_SET_FAIL");
            }
        }
    }
    else if (CMD_MATCHED2(pParam[0], "IMEI"))
    {
        // AT^GT_CM=IMEI
        if (nParam < 2)
        {
            gsmImei = my_param_get_imei();
            if (gsmImei->flag == FLAG_VALID)
            {
                memcpy(data_buff, gsmImei->hex, sizeof(gsmImei->hex));
                sprintf(resp, "RETURN_IMEI:%s", data_buff);
            }
            else
            {
                sprintf(resp, "RETURN_IMEI");
            }
        }
        // AT^GT_CM=IMEI,xxxx
        else
        {
            ret = my_param_set_imei(pParam[1], strlen(pParam[1]));
            if (ret == 0){
                my_sync_bt_name_with_imei_last4(pParam[1]);
                sprintf(resp, "RETURN_IMEI_SET_OK");
            } else {
                sprintf(resp, "RETURN_IMEI_SET_FAIL");
            }
        }
    }
    else if (CMD_MATCHED2(pParam[0], "SEND_STATUS")) //测试用
    {
        // AT^GT_CM=SEND_STATUS
        if (nParam < 2)
        {
            // TODO 后续定时更新数据的话，调用这个接口
            my_send_all_status_handle();
            sprintf(resp, "RETURN_SEND_STATUS_OK");
        }
        else
        {
            sprintf(resp, "RETURN_SEND_STATUS_FAIL");
        }
    }
    else if (CMD_MATCHED2(pParam[0], "VOLUME"))
    {
        // AT^GT_CM=VOLUME 查询
        if (nParam < 2)
        {
            audio_state = app_audio_get_state();
            vol = app_audio_get_volume(audio_state);
            max_vol = app_audio_get_max_volume();
            sprintf(resp, "RETURN_VOLUME:%d (max:%d)", (int)vol, (int)max_vol);
        }
        // AT^GT_CM=VOLUME,volume 设置
        else
        {
            vol = atoi(pParam[1]);
            max_vol = app_audio_get_max_volume();

            if (vol < 0 || vol > (int)max_vol)
            {
                sprintf(resp, "RETURN_VOLUME_SET_FAIL");
            }
            else
            {
                audio_state = app_audio_get_state();
                app_audio_set_volume(audio_state, (int16)vol, 0);
                sprintf(resp, "RETURN_VOLUME_SET_OK");
            }
        }
    }

    return resp;
}

static void my_sync_bt_name_with_imei_last4(const char *imei)
{
    uint8 new_bt_name[] = "Go 3 (0000)"; // 产品名+括号+IMEI后4位

    if ((imei == NULL) || (strlen(imei) != GSM_IMEI_LENGTH))
    {
        return;
    }

    // 替换括号后的4位
    memcpy(&new_bt_name[sizeof("Go 3 (") - 1], &imei[DEV_NAME_USE_IMEI_POS], 4);

    // 修改蓝牙名称
    bt_modify_name((u8 *)new_bt_name);
}

int sh_at_factory_cmd(char *pfactorycmd)
{
    int argc = 0; // 输入输出参数
    char *argv[MAX_ARGS] = { 0 };

    my_parse_cmd_line(pfactorycmd + strlen(FACTORY_CMD_HEADER), ',' , &argc, argv);

    my_log_printf_internal(0, "%s", my_handle_at_factory_cmd(argv, argc));

    return 0;
}

int sh_at_test(int argc, char *argv[])
{
    char szValue[30] = {0};

    if (argc < 2) return -1;

    strncpy(szValue, argv[1], 30);

    if (strcmp(szValue, "CPUINFO") == 0)
    {
        my_get_os_cpu_usage();
    }
    else if (strcmp(szValue, "LED") == 0)
    {
        if (argc < 3)
        {
            my_log_printf(1, "TEST LED: invalid");
            return -1;
        }
        return sh_test_led_impl(argc - 2, argv + 2);
    }
    else if (strcmp(szValue, "GPIO") == 0)
    {
        if (argc >= 3)
        {
            return sh_test_gpio_impl(argc - 2, argv + 2);
        }
    }
    else
    {
        my_log_printf(1, "Unrecognized Testing.");
    }

    return 0;
}

/**
 * @brief TEST GPIO 子命令实现
 * @note argv[0]=GET|SET, argv[1]=IO号, argv[2]=0|1(仅SET)
 *       例: TEST GPIO GET 104 / TEST GPIO SET 96 1
 */
static int sh_test_gpio_impl(int argc, char *argv[])
{
    uint32_t io;
    uint8_t level;
    int ret;

    if (argc < 2)
    {
        my_log_printf(1, "TEST GPIO: usage GET <io> | SET <io> <0|1>");
        return -1;
    }

    io = (uint32_t)atoi(argv[1]);

    if (CMD_EQUAL2(argv[0], "GET"))
    {
        ret = my_gpio_get_level(io);
        if (ret < 0)
        {
            my_log_printf(1, "GPIO %d read fail", io);
            return -1;
        }
        my_log_printf(1, "GPIO %d READ=%d", io, ret);
        return 0;
    }

    if (CMD_EQUAL2(argv[0], "SET"))
    {
        if (argc < 3)
        {
            my_log_printf(1, "TEST GPIO SET: need level 0|1");
            return -1;
        }
        level = (uint8_t)atoi(argv[2]);
        if (level != 0 && level != 1)
        {
            my_log_printf(1, "level must be 0 or 1");
            return -1;
        }
        ret = my_gpio_set_level(io, level);
        if (ret < 0)
        {
            my_log_printf(1, "GPIO %d set fail", io);
            return -1;
        }
        my_log_printf(1, "GPIO %d SET=%d %s", io, level, level ? "HIGH" : "LOW");
        return 0;
    }

    my_log_printf(1, "TEST GPIO: usage GET <io> | SET <io> <0|1>");
    return -1;
}

/**
 * @brief TEST LED 子命令实现
 * @note argv[0]=MODE|RGB|MODE7 或 R G B NUM(兼容简写)
 */
static int sh_test_led_impl(int argc, char *argv[])
{
    if (argc < 1)
    {
        my_log_printf(1, "TEST LED: invalid");
        return -1;
    }

    if (CMD_EQUAL2(argv[0], "MODE"))
    {
        if (argc < 2)
        {
            my_log_printf(1, "TEST LED: invalid");
            return -1;
        }

        if (CMD_EQUAL2(argv[1], "LIST"))
        {
            my_log_printf(1, "mode list: 0 1 2 3 4 5 6 7");
            return 0;
        }

        if (CMD_EQUAL2(argv[1], "START"))
        {
            uint8 pixel_cnt = LED_MAX_PIXELS;
            uint8 mode_idx = 0;
            if (argc < 3)
            {
                my_log_printf(1, "TEST LED: invalid");
                return -1;
            }
            mode_idx = (uint8)atoi(argv[2]);
            if (argc >= 4)
            {
                pixel_cnt = (uint8)atoi(argv[3]);
            }
            if ((mode_idx == 7) && (argc >= 5))
            {
                uint8 base_idx = (uint8)atoi(argv[4]);
                my_led_mode7_set_base(base_idx);
            }
            my_led_mode_start(mode_idx, pixel_cnt);
            return 0;
        }

        if (CMD_EQUAL2(argv[1], "STOP"))
        {
            my_led_mode_stop();
            return 0;
        }

        my_log_printf(1, "TEST LED: invalid");
        return -1;
    }

    if (CMD_EQUAL2(argv[0], "RGB"))
    {
        if (argc < 5)
        {
            my_log_printf(1, "TEST LED: invalid");
            return -1;
        }
        my_led_mode_stop();
        my_led_set_rgb(
            (uint8)atoi(argv[1]),
            (uint8)atoi(argv[2]),
            (uint8)atoi(argv[3]),
            (uint8)atoi(argv[4])
        );
        return 0;
    }

    if (CMD_EQUAL2(argv[0], "MODE7"))
    {
        if ((argc >= 3) && CMD_EQUAL2(argv[1], "STYLE"))
        {
            uint8 style = (uint8)atoi(argv[2]);
            my_led_mode7_set_fill_style(style);
            return 0;
        }
        my_log_printf(1, "TEST LED: invalid");
        return -1;
    }

    /* 兼容: TEST LED R G B NUM */
    if (argc >= 4)
    {
        my_led_mode_stop();
        my_led_set_rgb(
            (uint8)atoi(argv[0]),
            (uint8)atoi(argv[1]),
            (uint8)atoi(argv[2]),
            (uint8)atoi(argv[3])
        );
        return 0;
    }

    my_log_printf(1, "TEST LED: invalid");
    return -1;
}

/************************************************************************
**@brief: SHELL命令执行体
**@param[in] cmdline: 命令
*************************************************************************/
static int GetCmdMatche(char *cmdline)
{
    int i;

    for (i = 0; AT_CMD_INNER[i].cmd != NULL; i++)
    {
        if (strlen(cmdline) != strlen(AT_CMD_INNER[i].cmd))
            continue;
        if (strncmp(AT_CMD_INNER[i].cmd, cmdline, strlen(AT_CMD_INNER[i].cmd))==0)
            return i;
    }

    return -1;
}

/************************************************************************
**@brief: 对命令行的参数进行解析
**@param[in] cmdline: 命令行原始内容
**@param[in] argc: 解析之后参数的个数
**@param[in] argv: 参数的内容
*************************************************************************/
static void ParseArgs(char *cmdline, int *argc, char **argv)
{
#define STATE_WHITESPACE    0
#define STATE_WORD          1

    char *c;
    int state = STATE_WHITESPACE;
    int i;

    *argc = 0;

    if (strlen(cmdline) == 0)
        return;

    /* convert all tabs into single spaces */
    c = cmdline;
    while (*c != '\0')
    {
        if (*c == '\t')
            *c = ' ';
        c++;
    }

    c = cmdline;
    i = 0;

    /* now find all words on the command line */
    while (*c != '\0')
    {
        if (state == STATE_WHITESPACE)
        {
            if (*c != ' ')
            {
                argv[i] = c;        //将argv[i]指向c
                i++;
                state = STATE_WORD;
            }
        }
        else
        { /* state == STATE_WORD */
            if (*c == ' ')
            {
                *c = '\0';
                state = STATE_WHITESPACE;
            }
        }
        c++;
    }

    *argc = i;

#undef STATE_WHITESPACE
#undef STATE_WORD
}

void my_parse_cmd_line(char *cmdline, char flag, int *argc, char **argv)
{
    char *c = cmdline;
    int state = 0;
    int i = 0;
    int max_args = *argc;

    *argc = 0;

    if (strlen(cmdline) == 0)
        return;

    /* now find all words on the command line */
    while (*c != '\0')
    {
        if (state == 0)
        {
            if (*c != flag)
            {
                argv[i] = c;        //将argv[i]指向c
                state = 1;
                i++;

                if (i == max_args) break;
            }
        }
        else
        { /* state == 1*/
            if (*c == flag)
            {
                *c = '\0';
                state = 0;
            }
        }
        c++;
    }

    *argc = i;
}

/************************************************************************
**@brief: 对收到的指令进行解析
**@param[in] cmdline: 命令行原始内容
**@param[in] cmd_len: 命令的长度
*************************************************************************/
static int ParseCmd(char *cmdline, int cmd_len)
{
    int argc, num_commands;
    char *argv[MAX_ARGS];

    if (0 == strlen(cmdline))
    {
        return -1;
    }

    // 检查是否是产测指令
    if (strncmp(cmdline, FACTORY_CMD_HEADER, strlen(FACTORY_CMD_HEADER)) == 0)
    {
        sh_at_factory_cmd(cmdline);

        return 0;
    }
#if 0

#if AT_JMCMD_ENABLE
    if (strncmp(cmdline, "AT+JMCMD=", strlen("AT+JMCMD=")) == 0)
    {
        sh_at_jmcmd(cmdline);

        return 0;
    }
#endif

    // 检查是否短信兼容指令
    if (0 == my_handle_sms(UART_FAKE_PHONE, cmdline))
    {
        return 0;
    }
#endif
    ParseArgs(cmdline, &argc, argv);

    /* only whitespace */
    if (argc == 0) {
        return 0;
    }

    num_commands = GetCmdMatche(argv[0]);
    if (num_commands < 0) {
        my_log_printf(1, "No '%s' command\n", argv[0]);
        return -1;
    }

    if (AT_CMD_INNER[num_commands].proc != NULL) {
        AT_CMD_INNER[num_commands].proc(argc, argv);
    }

    return 0;
}

void my_shell_uart_init(void)
{
    MY_UART_ST_STRUCT param = {
        .mod = MOD_SHELL,
        .port = MY_SHELL_PORT,
        .tx_pin = MY_SHELL_UART_TX_PIN,
        .rx_pin = MY_SHELL_UART_RX_PIN,
#if CONFIG_DEBUG_ENABLE
        .baud = TCFG_DEBUG_UART_BAUDRATE,
#else
        .baud = MY_UART_BAUD_115200,
#endif
    };

    my_uart_init(&param);
}

// 处理所有收到的字符串
static void my_shell_handle_rx(uint8 *pData, uint32 iLen)
{
    static uint32 t_i = 0;
    static char shell_cmd[MAX_CMD_LEN] = {0};
    uint32 i;

    for ( i = 0 ; i < iLen ; i++ )
    {
        if ( pData[i] == '\r' || pData[i] == '\n' )   // 回车是\r 为了兼容同时处理 \n
        {
            //my_log_printf_internal(0, shell_cmd);
            ParseCmd(shell_cmd, t_i);

            shell_cmd[0] = 0;
            t_i = 0;

            // 如果下个字符是\n，跳过
            if( pData[i+1] == '\n' )
            {
                i++;
            }
        }
        else if ( t_i < (MAX_CMD_LEN - 1) )
        {
            shell_cmd[t_i++] = pData[i];
            shell_cmd[t_i] = '\0';
        }
    }
}

/************************************************************************
**@brief: 指令任务处理函数
**@param[in] p_arg: 参数内容
*************************************************************************/
void my_shell_task(void *p_arg)
{
    int ret = 0;
    int msg[8] = {0};
    int32 len = 0;
    uint8 *rx_buff = (uint8 *)dma_malloc(MY_UART_RX_BUF_LEN);

    my_log_printf(2, "############# my_shell_task is runing ##################");

    while (1)
    {
        ret = os_taskq_pend(NULL, msg, ARRAY_SIZE(msg));
        if (ret != OS_TASKQ) {
            continue;
        }

        switch (msg[0])
        {
            case MY_MSG_UART_RECV:
            {
                memset(rx_buff, 0, MY_UART_RX_BUF_LEN);

                len = my_shell_uart_read_data(rx_buff, MY_UART_RX_BUF_LEN - 1);
                if (len > 0)
                {
                    rx_buff[len] = 0x00;
                    my_shell_handle_rx(rx_buff, len);
                }
                break;
            }

            default:
                break;
        }
    }

    if (rx_buff != NULL)
    {
        dma_free(rx_buff);
        rx_buff = NULL;
    }
}