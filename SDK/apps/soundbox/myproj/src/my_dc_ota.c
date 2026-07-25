/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_dc_ota.c
 * Brief      : DC板OTA模块实现
 * Version    : V1.0.0
 * Author     : songshiqing(songshiqing@jimiiot.com)
 * Date       : 2026-07-15
 ************************************************************************************************/

#include "my_common.h"
#include "utils/fs/resfile.h"

/*
 * DC OTA 流程：
 * 1) 从系统资源分区打开 DC 固件并校验 STM32 向量表；
 * 2) 写 DC 0x0001 Bit13 触发复位，暂停普通 Modbus 轮询；
 * 3) 识别 IAP 菜单并发送选项 1，通过 YMODEM 下发单个固件文件；
 * 4) 等待 DC 明确报告烧写成功，发送选项 2 运行应用并恢复 Modbus。
 *
 * 所有状态机操作均在 my_dc_uart_task 中执行，定时器只投递 tick 消息。
 */
#define DC_OTA_FILE_NAME       "VoltanaGo3EMS.bin" /* YMODEM Block0 上报给 DC 的文件名 */
#define DC_OTA_TICK_MS         100                 /* OTA 状态机调度周期 */
#define DC_OTA_BOOT_TIMEOUT    8000                /* 等待 IAP 主菜单超时 */
#define DC_OTA_C_TIMEOUT       10000               /* 发送选项 1 后等待字符 C 超时 */
#define DC_OTA_PROGRAM_TIMEOUT 20000               /* 等待 DC 烧写结果超时 */
#define DC_OTA_RUN_DELAY       3000                /* 发送选项 2 后等待应用启动时间 */
#define DC_OTA_MENU_RETRY_MS   500                 /* 菜单选项 1 重发间隔 */
#define DC_OTA_MENU_RETRY_MAX  5                   /* 菜单选项 1 最大发送次数 */

/* DC OTA 业务状态；YMODEM 协议内部状态由 ymodem_sender_t 独立维护。 */
typedef enum
{
    DC_OTA_IDLE = 0,       /* 空闲，可接受新的升级请求 */
    DC_OTA_WAIT_BOOT_MENU, /* 已复位 DC，等待 IAP 主菜单 */
    DC_OTA_WAIT_C,         /* 已发送菜单选项 1，等待接收端字符 C */
    DC_OTA_YMODEM,         /* 正在执行 YMODEM 文件传输 */
    DC_OTA_WAIT_PROGRAM,   /* 传输结束，等待烧写成功文本 */
    DC_OTA_WAIT_RUN_MENU,  /* 烧写成功，等待菜单可执行选项 2 */
    DC_OTA_WAIT_APP,       /* 已发送选项 2，等待应用启动 */
} my_dc_ota_state_t;

/* DC OTA 全局上下文；静态分配避免 1K YMODEM 帧占用任务栈。 */
typedef struct
{
    my_dc_ota_state_t state; /* 当前业务状态 */
    RESFILE *file;           /* 内置资源中的 DC 固件句柄 */
    uint32 file_size;        /* 固件有效字节数 */
    uint32 state_ms;         /* 当前状态已等待时间 */
    uint32 rx_total;         /* 本次升级累计收到的 DC 串口字节数 */
    uint8 progress;          /* YMODEM已确认的发送进度，范围0~100 */
    uint8 break_sent;        /* 兼容旧 IAP：是否已发送中断自动启动字符 */
    uint8 menu_send_count;   /* 菜单选项 1 已发送次数 */
    char res_path[64];       /* resfile_open 使用的完整资源路径 */
    /* 当前 DC 的 STM32 IAP 启动画面约 815 字节，缓存需完整保留菜单关键字。 */
    char text[1024];
    uint16 text_len;        /* text 中有效 ASCII 字节数 */
    ymodem_sender_t ymodem; /* 单文件 YMODEM 发送上下文 */
} my_dc_ota_ctx_t;

static my_dc_ota_ctx_t g_dc_ota;            /* DC OTA单实例上下文 */
static uint8 g_my_dc_ota_start_pending; /* 启动消息已投递但尚未执行 */

/** @brief OTA 周期定时器回调：仅向 DC UART 任务投递状态机 tick。 */
static void my_dc_ota_timer_cb(void *param)
{
    (void)param;
    my_send_msg(MOD_DC_UART, MOD_DC_UART, MY_MSG_DC_OTA_TICK);
}

/** @brief 清空 IAP 文本识别缓存。 */
static void my_dc_ota_text_clear(void)
{
    g_dc_ota.text_len = 0;
    g_dc_ota.text[0] = '\0';
}

/**
 * @brief 从串口数据中提取可打印 ASCII，供 IAP 菜单和烧写结果匹配。
 * @param data 串口接收数据。
 * @param len 数据长度。
 */
static void my_dc_ota_text_append(const uint8 *data, uint32 len)
{
    uint32 i;
    char ch;

    for (i = 0; i < len; i++)
    {
        ch = (char)data[i];
        if ((ch < 0x20 && ch != '\r' && ch != '\n' && ch != '\t') || ch > 0x7E)
        {
            continue;
        }

        if (g_dc_ota.text_len >= sizeof(g_dc_ota.text) - 1)
        {
            memmove(g_dc_ota.text, g_dc_ota.text + 64,
                    sizeof(g_dc_ota.text) - 1 - 64);
            g_dc_ota.text_len = sizeof(g_dc_ota.text) - 1 - 64;
        }
        g_dc_ota.text[g_dc_ota.text_len++] = ch;
        g_dc_ota.text[g_dc_ota.text_len] = '\0';
    }
}

/**
 * @brief 判断当前 IAP 文本缓存是否包含指定关键字。
 * @return 1=包含，0=不包含。
 */
static int my_dc_ota_text_has(const char *needle)
{
    return strstr(g_dc_ota.text, needle) != NULL;
}

/** @brief YMODEM 发送回调：通过 DC UART DMA 分片发送原始字节流。 */
static int my_dc_ota_send_cb(void *priv, const uint8 *data, uint32 len)
{
    (void)priv;
    return (int)my_dc_uart_send_raw(data, len);
}

/**
 * @brief YMODEM 固件读取回调：按偏移从资源文件读取数据。
 * @return 实际读取长度，负数表示失败。
 */
static int my_dc_ota_read_cb(void *priv, uint32 offset, uint8 *data, uint32 len)
{
    my_dc_ota_ctx_t *ctx = (my_dc_ota_ctx_t *)priv;

    if (ctx->file == NULL || resfile_seek(ctx->file, offset, RESFILE_SEEK_SET) < 0)
    {
        return -1;
    }
    return resfile_read(ctx->file, data, len);
}

/** @brief YMODEM 进度回调：保存进度并按约 10% 间隔打印。 */
static void my_dc_ota_progress_cb(void *priv, uint32 sent, uint32 total)
{
    my_dc_ota_ctx_t *ctx = (my_dc_ota_ctx_t *)priv;
    uint8 progress = total ? (uint8)((sent * 100u) / total) : 100;

    if (progress != ctx->progress && (progress == 100 || progress / 10 != ctx->progress / 10))
    {
        my_log_printf(1, "[DC_OTA] ymodem %u%% (%u/%u)",
                      progress, (unsigned)sent, (unsigned)total);
    }
    ctx->progress = progress;
}

/** @brief 关闭 DC 固件资源句柄；允许重复调用。 */
static void my_dc_ota_close_file(void)
{
    if (g_dc_ota.file)
    {
        resfile_close(g_dc_ota.file);
        g_dc_ota.file = NULL;
    }
}

/**
 * @brief 按系统当前提示音资源表打开 DC 固件，调试阶段兼容 tone_zh 回退路径。
 * @return 成功返回资源句柄，失败返回 NULL。
 */
static RESFILE *my_dc_ota_open_resource(void)
{
    const struct tone_files *tones = get_tone_files();
    const char *relative_path = tones ? tones->dc_ota : NULL;
    RESFILE *file = NULL;

    /* 路径拼接方式与系统 tone_player.c 保持一致。 */
    if (relative_path != NULL)
    {
        snprintf(g_dc_ota.res_path, sizeof(g_dc_ota.res_path),
                 "%s%s", FLASH_RES_PATH, relative_path);
        file = resfile_open(g_dc_ota.res_path);
    }

    /* 预研阶段固件仅放在 tone_zh，保留固定回退路径便于调试。 */
    if (file == NULL &&
        (relative_path == NULL || strcmp(relative_path, "tone_zh/dc_ota.*")))
    {
        snprintf(g_dc_ota.res_path, sizeof(g_dc_ota.res_path),
                 "%s%s", FLASH_RES_PATH, "tone_zh/dc_ota.*");
        file = resfile_open(g_dc_ota.res_path);
    }
    return file;
}

/**
 * @brief 统一结束 OTA：停止定时器、关闭文件并恢复 Modbus 轮询。
 * @param success 1=升级流程成功，0=失败。
 * @param reason 失败原因；成功时可为 NULL。
 */
static void my_dc_ota_finish(uint8 success, const char *reason)
{
    my_stop_timer(MY_TIMER_DC_OTA);
    my_dc_ota_close_file();
    my_dc_uart_exit_ota_mode();

    if (success)
    {
        my_log_printf(1, "[DC_OTA] completed, Modbus polling restored");
    }

    else
    {
        my_log_printf(1, "[DC_OTA] failed: %s", reason ? reason : "unknown");
    }
    memset(&g_dc_ota, 0, sizeof(g_dc_ota));
}

/**
 * @brief 校验固件基本合法性：长度、STM32 SRAM 初始栈和 Thumb 复位向量。
 * @return 0=合法，-1=非法或读取失败。
 */
static int my_dc_ota_validate_image(void)
{
    uint8 head[8];
    uint32 initial_sp;
    uint32 reset_vector;

    if (g_dc_ota.file_size < 1024 ||
        resfile_seek(g_dc_ota.file, 0, RESFILE_SEEK_SET) < 0 ||
        resfile_read(g_dc_ota.file, head, sizeof(head)) != sizeof(head))
    {
        return -1;
    }

    initial_sp = (uint32)head[0] | ((uint32)head[1] << 8) | ((uint32)head[2] << 16) | ((uint32)head[3] << 24);
    reset_vector = (uint32)head[4] | ((uint32)head[5] << 8) | ((uint32)head[6] << 16) | ((uint32)head[7] << 24);

    /* STM32 镜像要求：初始栈位于 SRAM，复位向量位于应用 Flash 且 Thumb 位为 1。 */
    if ((initial_sp & 0x2FFE0000u) != 0x20000000u ||
        reset_vector < 0x08004001u || (reset_vector & 1u) == 0)
    {
        my_log_printf(1, "[DC_OTA] invalid vector sp=0x%x reset=0x%x",
                      initial_sp, reset_vector);
        return -1;
    }
    return 0;
}

/**
 * @brief 从 BLE 等其他任务请求启动 DC OTA，实际操作投递到 DC UART 任务。
 * @return 0=请求已投递，-1=已有升级正在运行或等待启动。
 */
int my_dc_ota_request_start(void)
{
    if (g_dc_ota.state != DC_OTA_IDLE || g_my_dc_ota_start_pending)
    {
        return -1;
    }

    g_my_dc_ota_start_pending = 1;
    my_send_msg(MOD_BLE, MOD_DC_UART, MY_MSG_DC_OTA_START);
    return 0;
}

/**
 * @brief 在DC UART任务中打开并校验指定固件，启动状态机并复位DC。
 * @return 0=启动成功，-1=资源、校验、定时器或复位失败。
 */
int my_dc_ota_start(void)
{
    int file_len;

    g_my_dc_ota_start_pending = 0;
    if (g_dc_ota.state != DC_OTA_IDLE)
    {
        return -1;
    }

    memset(&g_dc_ota, 0, sizeof(g_dc_ota));
    g_dc_ota.file = my_dc_ota_open_resource();
    file_len = g_dc_ota.file ? resfile_get_len(g_dc_ota.file) : -1;

    if (g_dc_ota.file == NULL)
    {
        my_log_printf(1, "[DC_OTA] resource open failed: %s", g_dc_ota.res_path);
        return -1;
    }

    my_log_printf(1, "[DC_OTA] resource opened: %s", g_dc_ota.res_path);
    if (file_len <= 0)
    {
        my_dc_ota_close_file();
        my_log_printf(1, "[DC_OTA] resource length invalid: %d", file_len);
        return -1;
    }

    g_dc_ota.file_size = (uint32)file_len;

    if (my_dc_ota_validate_image() != 0)
    {
        my_dc_ota_close_file();
        return -1;
    }

    if (!my_start_timer(MY_TIMER_DC_OTA, DC_OTA_TICK_MS, true, my_dc_ota_timer_cb))
    {
        my_dc_ota_close_file();
        return -1;
    }

    g_dc_ota.state = DC_OTA_WAIT_BOOT_MENU;

    if (my_dc_uart_enter_ota_mode() != 0)
    {
        my_dc_ota_finish(0, "DC register 0x0001 baseline is not ready or reset send failed");
        return -1;
    }

    my_log_printf(1, "[DC_OTA] start, image=%s size=%u", DC_OTA_FILE_NAME,
                  (unsigned)g_dc_ota.file_size);
    return 0;
}

/**
 * @brief 处理 DC UART 输入，识别 IAP 文本并驱动 YMODEM 接收响应。
 * @param data DC 串口数据。
 * @param len 数据长度。
 */
void my_dc_ota_input(const uint8 *data, uint32 len)
{
    uint8 one = '1';
    uint8 run = '2';

    if (!my_dc_ota_is_active() || data == NULL || len == 0)
    {
        return;
    }

    g_dc_ota.rx_total += len;
    my_dc_ota_text_append(data, len);

    if (g_dc_ota.state == DC_OTA_WAIT_BOOT_MENU)
    {
        if (!g_dc_ota.break_sent && my_dc_ota_text_has("Break AutoBoot"))
        {
            uint8 c = 'C';
            my_dc_uart_send_raw(&c, 1);
            g_dc_ota.break_sent = 1;
            my_dc_ota_text_clear();
        }

        if (my_dc_ota_text_has("Main Menu") || my_dc_ota_text_has("Download image"))
        {
            my_dc_uart_send_raw(&one, 1);
            g_dc_ota.menu_send_count = 1;
            g_dc_ota.state = DC_OTA_WAIT_C;
            g_dc_ota.state_ms = 0;
            my_dc_ota_text_clear();
            my_log_printf(1, "[DC_OTA] boot menu found, send option 1");
        }
        return;
    }

    if (g_dc_ota.state == DC_OTA_WAIT_C)
    {
        uint32 i;
        for (i = 0; i < len; i++)
        {
            if (data[i] == 0x43)
            {
                ymodem_sender_init(&g_dc_ota.ymodem, DC_OTA_FILE_NAME,
                                   g_dc_ota.file_size, my_dc_ota_send_cb,
                                   my_dc_ota_read_cb, my_dc_ota_progress_cb, &g_dc_ota);
                g_dc_ota.state = DC_OTA_YMODEM;
                g_dc_ota.state_ms = 0;
                my_dc_ota_text_clear();
                ymodem_sender_input(&g_dc_ota.ymodem, &data[i], len - i);
                my_log_printf(1, "[DC_OTA] receiver C found, YMODEM started");
                break;
            }
        }
        return;
    }

    if (g_dc_ota.state == DC_OTA_YMODEM)
    {
        ymodem_sender_input(&g_dc_ota.ymodem, data, len);
        if (ymodem_sender_result(&g_dc_ota.ymodem) == YMODEM_RESULT_ERROR)
        {
            my_dc_ota_finish(0, ymodem_sender_error(&g_dc_ota.ymodem));
        }
        else if (ymodem_sender_result(&g_dc_ota.ymodem) == YMODEM_RESULT_DONE)
        {
            g_dc_ota.state = DC_OTA_WAIT_PROGRAM;
            g_dc_ota.state_ms = 0;
            my_log_printf(1, "[DC_OTA] YMODEM protocol completed");
            if (my_dc_ota_text_has("Programming Completed Successfully"))
            {
                g_dc_ota.state = DC_OTA_WAIT_RUN_MENU;
                my_dc_ota_text_clear();
                my_log_printf(1, "[DC_OTA] DC reports programming success");
            }
        }
        return;
    }

    if (g_dc_ota.state == DC_OTA_WAIT_PROGRAM &&
        my_dc_ota_text_has("Programming Completed Successfully"))
    {
        g_dc_ota.state = DC_OTA_WAIT_RUN_MENU;
        g_dc_ota.state_ms = 0;
        my_dc_ota_text_clear();
        my_log_printf(1, "[DC_OTA] DC reports programming success");
        return;
    }

    if (g_dc_ota.state == DC_OTA_WAIT_RUN_MENU &&
        (my_dc_ota_text_has("Main Menu") || my_dc_ota_text_has("Execute the loaded application")))
    {
        my_dc_uart_send_raw(&run, 1);
        g_dc_ota.state = DC_OTA_WAIT_APP;
        g_dc_ota.state_ms = 0;
        my_log_printf(1, "[DC_OTA] send option 2 to run application");
    }
}

/** @brief 100ms 状态机时基：处理协议超时、菜单重试和应用启动等待。 */
void my_dc_ota_tick(void)
{
    uint8 run = '2';
    ymodem_result_t result;

    if (!my_dc_ota_is_active())
    {
        return;
    }

    g_dc_ota.state_ms += DC_OTA_TICK_MS;

    if (g_dc_ota.state == DC_OTA_YMODEM)
    {
        ymodem_sender_tick(&g_dc_ota.ymodem, DC_OTA_TICK_MS);
        result = ymodem_sender_result(&g_dc_ota.ymodem);
        if (result == YMODEM_RESULT_ERROR)
        {
            my_dc_ota_finish(0, ymodem_sender_error(&g_dc_ota.ymodem));
        }
    }
    else if (g_dc_ota.state == DC_OTA_WAIT_BOOT_MENU &&
             g_dc_ota.state_ms >= DC_OTA_BOOT_TIMEOUT)
    {
        const char *tail = g_dc_ota.text;
        if (g_dc_ota.text_len > 180)
        {
            tail += g_dc_ota.text_len - 180;
        }
        my_log_printf(1, "[DC_OTA] boot wait received %u bytes",
                      (unsigned)g_dc_ota.rx_total);
        my_log_printf(1, "[DC_OTA] boot text tail: %s", tail);
        my_dc_ota_finish(0, "boot menu timeout");
    }
    else if (g_dc_ota.state == DC_OTA_WAIT_C)
    {
        if (g_dc_ota.state_ms >= DC_OTA_C_TIMEOUT)
        {
            my_dc_ota_finish(0, "YMODEM C timeout");
        }
        else if ((g_dc_ota.state_ms % DC_OTA_MENU_RETRY_MS) == 0 &&
                 g_dc_ota.menu_send_count < DC_OTA_MENU_RETRY_MAX)
        {
            uint8 one = '1';
            my_dc_uart_send_raw(&one, 1);
            g_dc_ota.menu_send_count++;
            my_log_printf(1, "[DC_OTA] retry option 1 (%u/%u)",
                          g_dc_ota.menu_send_count, DC_OTA_MENU_RETRY_MAX);
        }
    }
    else if (g_dc_ota.state == DC_OTA_WAIT_PROGRAM &&
             g_dc_ota.state_ms >= DC_OTA_PROGRAM_TIMEOUT)
    {
        my_dc_ota_finish(0, "program result timeout");
    }
    else if (g_dc_ota.state == DC_OTA_WAIT_RUN_MENU &&
             g_dc_ota.state_ms >= 1500)
    {
        /* 部分 IAP 烧写后不重绘菜单，超时后主动发送选项 2。 */
        my_dc_uart_send_raw(&run, 1);
        g_dc_ota.state = DC_OTA_WAIT_APP;
        g_dc_ota.state_ms = 0;
        my_log_printf(1, "[DC_OTA] run menu not redrawn, send option 2");
    }
    else if (g_dc_ota.state == DC_OTA_WAIT_APP &&
             g_dc_ota.state_ms >= DC_OTA_RUN_DELAY)
    {
        my_dc_ota_finish(1, NULL);
    }
}

/** @brief 查询 DC OTA 是否占用 UART。@return 1=等待启动或升级中，0=空闲。 */
int my_dc_ota_is_active(void)
{
    return g_dc_ota.state != DC_OTA_IDLE || g_my_dc_ota_start_pending != 0;
}

/** @brief 获取最近一次 YMODEM 已确认进度，范围 0~100。 */
uint8 my_dc_ota_progress(void)
{
    return g_dc_ota.progress;
}
