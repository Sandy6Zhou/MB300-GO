#include "system/includes.h"
#include "app_le_auracast.h"
#include "wireless_trans.h"
#include "le/auracast_source_api.h"
#include "le/auracast_sink_api.h"
#include "app_config.h"
#include "btstack/avctp_user.h"
#include "app_tone.h"
#include "app_main.h"
#include "audio_config.h"
#include "le_audio_player.h"
#include "a2dp_player.h"
#include "vol_sync.h"
#include "spdif_player.h"
#include "fm_api.h"
#include "linein.h"
#include "spdif_file.h"
#include "spdif.h"

#if ((TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SINK_EN | LE_AUDIO_JL_AURACAST_SINK_EN)) || \
     (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SOURCE_EN | LE_AUDIO_JL_AURACAST_SOURCE_EN)))

/**************************************************************************************************
  Macros
**************************************************************************************************/
#define LOG_TAG             "[APP_LE_AURACAST]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

/**************************************************************************************************
  测试专用
**************************************************************************************************/
/****
| sampling_frequency| variant  | 采样率 | 帧间隔(us) | 包长(字节) | 码率(kbps) | 重发次数 |
| ----------------- | -------- | ------ | ---------- | -----------| -----------| -------- |
|        0          |    0     | 8000   | 7500       | 26         | 27.732     | 2        |
|        0          |    1     | 8000   | 10000      | 30         | 24         | 2        |
|        1          |    0     | 16000  | 7500       | 30         | 32         | 2        |
|        1          |    1     | 16000  | 10000      | 40         | 32         | 2        |
|        2          |    0     | 24000  | 7500       | 45         | 48         | 2        |
|        2          |    1     | 24000  | 10000      | 60         | 48         | 2        |
|        3          |    0     | 32000  | 7500       | 60         | 64         | 2        |
|        3          |    1     | 32000  | 10000      | 80         | 64         | 2        |
|        4          |    0     | 44100  | 8163       | 97         | 95.06      | 4        |
|        4          |    1     | 44100  | 10884      | 130        | 95.55      | 4        |
|        5          |    0     | 48000  | 7500       | 75         | 80         | 4        |
|        5          |    1     | 48000  | 10000      | 100        | 80         | 4        |
****/
#define AURACAST_BIS_NUM                    (1)
#define AURACAST_BIS_SAMPLING_RATE          (5)
#define AURACAST_BIS_VARIANT                (1)
#define AURACAST_BIS_ENCRYPTION_ENABLE      (0)

auracast_user_config_t user_config = {
    .config_num_bis = AURACAST_BIS_NUM,
    .config_sampling_frequency = AURACAST_BIS_SAMPLING_RATE,
    .config_variant = AURACAST_BIS_VARIANT,
    .encryption = AURACAST_BIS_ENCRYPTION_ENABLE,
    .broadcast_id = 0x123456,
    .broadcast_name = "JL_auracast",
};

/**************************************************************************************************
  Data Types
**************************************************************************************************/
enum {
    ///0x1000起始为了不要跟提示音的IDEX_TONE_重叠了
    TONE_INDEX_AURACAST_OPEN = 0x1000,
    TONE_INDEX_AURACAST_CLOSE,
};

struct app_auracast_info_t {
    bool init_ok;
    u16 bis_hdl;
    void *recorder;
    struct le_audio_player_hdl rx_player;
};

struct app_auracast_t {
    u8 status;
    u8 role;
    u8 bis_num;
    u8 big_hdl;
    u16 latch_bis_hdl;
    struct app_auracast_info_t bis_hdl_info[MAX_BIS_NUMS];
};

const struct _auracast_code_list_t {
    u32 sample_rate;
    u32 SDU_interval;     // (us)
    u16 max_SDU_octets;   // (bytes / ch)
    u32 bit_rate;         // (bps / ch)
} auracast_code_list[6][2] = {
    {
        { 8000, 7500, 26, 27732 },
        { 8000, 10000, 30, 24000 },
    },
    {
        { 16000, 7500, 30, 32000 },
        { 16000, 10000, 40, 32000 },
    },
    {
        { 24000, 7500, 45, 48000 },
        { 24000, 10000, 60, 48000 },
    },
    {
        { 32000, 7500, 60, 64000 },
        { 32000, 10000, 80, 64000 },
    },
    {
        { 44100, 8163, 97, 95060 },
        { 44100, 10884, 130, 95550 },
    },
    {
        { 48000, 7500, 75, 80000 },
        { 48000, 10000, 100, 80000 },
    },
};
/**************************************************************************************************
  Local Function Prototypes
**************************************************************************************************/
static bool is_auracast_as_source();
static int auracast_source_media_open(uint8_t index);
static int auracast_source_media_close(uint8_t index);
static int auracast_source_media_reset();
static void auracast_iso_rx_callback(uint8_t *packet, uint16_t size);
static int auracast_sink_media_open(uint8_t index, uint8_t *packet, uint16_t length);
static int auracast_sink_media_close();

/**************************************************************************************************
  Local Global Variables
**************************************************************************************************/
static u8 auracast_app_mode_exit = 0;  /*!< 音源模式退出标志 */
static u8 config_auracast_as_master = 0;   /*!< 配置广播强制做主机 */
static int cur_deal_scene = -1; /*< 当前系统处于的运行场景 */
static auracast_sink_source_info_t sink_info;
static auracast_user_config_t source_user_config;
static struct app_auracast_t app_auracast;
static struct le_audio_mode_ops *le_audio_switch_ops = NULL; /*!< 广播音频和本地音频切换回调接口指针 */
static uint8_t match_auracast_num = 0;
uint8_t match_aurcast_name[3][28] = {
    [0] = "JBL Clip 5",
    [1] = "LE-H_54B7E5C85311",
    [2] = "MoerDuo_BLE",
};
static unsigned char errpacket[2] = {
    0x02, 0x00
};

u8 lea_cfg_support_ll_hci_cmd_in_lea_lib = 1;
/**************************************************************************************************
  Function Declarations
**************************************************************************************************/

u8 get_auracast_role(void)
{
    return app_auracast.role;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief BIG开关提示音结束回调接口
 *
 * @param priv:传递的参数
 * @param event:提示音回调事件
 *
 * @return
 */
/* ----------------------------------------------------------------------------*/
static int auracast_tone_play_end_callback(void *priv, enum stream_event event)
{
    u32 index = (u32)priv;

    g_printf("%s, event:0x%x", __FUNCTION__, event);

    switch (event) {
    case STREAM_EVENT_NONE:
    case STREAM_EVENT_STOP:
        switch (index) {
        case TONE_INDEX_AURACAST_OPEN:
            g_printf("TONE_INDEX_AURACAST_OPEN");
            break;
        case TONE_INDEX_AURACAST_CLOSE:
            g_printf("TONE_INDEX_AURACAST_CLOSE");
            break;
        default:
            break;
        }
    default:
        break;
    }

    return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 获取接收方连接状态
 *
 * @return 接收方连接状态
 */
/* ----------------------------------------------------------------------------*/
u8 get_auracast_status(void)
{
    return app_auracast.status;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 获取当前是否在退出模式的状态
 *
 * @return 1；是，0：否
 */
/* ----------------------------------------------------------------------------*/
u8 get_auracast_app_mode_exit_flag(void)
{
    return auracast_app_mode_exit;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 判断当前设备作为广播发送设备还是广播接收设备
 *
 * @return true:发送设备，false:接收设备
 */
/* ----------------------------------------------------------------------------*/
static bool is_auracast_as_source()
{
#if (LEA_BIG_FIX_ROLE == 1)
    return true;
#elif (LEA_BIG_FIX_ROLE == 2)
    return false;
#endif

    struct app_mode *cur_mode = app_get_current_mode();

    //当前处于蓝牙模式并且已连接手机设备时，
    //(1)播歌作为广播发送设备；
    //(2)暂停作为广播接收设备。
    if ((cur_mode->name == APP_MODE_BT) &&
        (bt_get_connect_status() != BT_STATUS_WAITINT_CONN)) {
        if ((bt_a2dp_get_status() == BT_MUSIC_STATUS_STARTING) ||
            get_a2dp_decoder_status() ||
            a2dp_player_runing()) {
            return true;
        } else {
            return false;
        }
    }

#if TCFG_APP_LINEIN_EN
    if (cur_mode->name == APP_MODE_LINEIN)  {
        if (linein_get_status() || config_auracast_as_master) {
            return true;
        } else {
            return false;
        }
    }
#endif

#if TCFG_APP_IIS_EN
    if (cur_mode->name == APP_MODE_IIS)  {
        if (iis_get_status() || config_auracast_as_master) {
            return true;
        } else {
            return false;
        }
    }
#endif

#if TCFG_APP_MIC_EN
    if (cur_mode->name == APP_MODE_MIC)  {
        if (mic_get_status() || config_auracast_as_master) {
            return true;
        } else {
            return false;
        }
    }
#endif

#if TCFG_APP_MUSIC_EN
    if (cur_mode->name == APP_MODE_MUSIC) {
        if ((music_file_get_player_status(get_music_file_player()) == FILE_PLAYER_START) || config_auracast_as_master) {
            return true;
        } else {
            return false;
        }
    }
#endif

#if TCFG_APP_FM_EN
    //当处于下面几种模式时，作为广播发送设备
    if (cur_mode->name == APP_MODE_FM)  {
        if (fm_get_fm_dev_mute() == 0 || config_auracast_as_master) {
            return true;
        } else {
            return false;
        }
    }
#endif

#if TCFG_APP_SPDIF_EN
    //当处于下面几种模式时，作为广播发送设备
    if (cur_mode->name == APP_MODE_SPDIF) {
        //由于spdif 是先打开数据源然后再打开数据流的顺序，具有一定的滞后性，所以不能用 函数 spdif_player_runing 函数作为判断依据
        /* if (spdif_player_runing() ||  config_auracast_as_master) { */
        if (!get_spdif_mute_state()) {
            y_printf("spdif_player_runing?\n");
            return true;
        } else {
            return false;
        }
    }
#endif

#if TCFG_APP_PC_EN
    //当处于下面几种模式时，作为广播发送设备
    if (cur_mode->name == APP_MODE_PC) {
#if defined(TCFG_USB_SLAVE_AUDIO_SPK_ENABLE) && TCFG_USB_SLAVE_AUDIO_SPK_ENABLE
        return true;
#else
        return false;
#endif
    }
#endif

    return false;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 检测广播当前是否处于挂起状态
 *
 * @return true:处于挂起状态，false:处于非挂起状态
 */
/* ----------------------------------------------------------------------------*/
static bool is_need_resume_auracast()
{
    if (app_auracast.status == APP_AURACAST_STATUS_SUSPEND) {
        return true;
    } else {
        return false;
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 广播从挂起状态恢复
 */
/* ----------------------------------------------------------------------------*/
static void app_auracast_resume()
{
    if (!g_bt_hdl.init_ok) {
        return;
    }

    if (!is_need_resume_auracast()) {
        return;
    }

    if (is_auracast_as_source()) {
        app_auracast_source_open();
    } else {
        app_auracast_sink_open();
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 广播进入挂起状态
 */
/* ----------------------------------------------------------------------------*/
static void app_auracast_suspend()
{
    if (app_auracast.role == APP_AURACAST_AS_SOURCE) {
        app_auracast_source_close(APP_AURACAST_STATUS_SUSPEND);
    } else if (app_auracast.role == APP_AURACAST_AS_SINK) {
        app_auracast_sink_close(APP_AURACAST_STATUS_SUSPEND);
    }
}

static bool match_name(char *target_name, char *source_name, size_t target_len)
{
    if (strlen(target_name) == 0 || strlen(source_name) == 0) {
        return FALSE;
    }

    if (0 == memcmp(target_name, source_name, target_len)) {
        return TRUE;
    }

    return FALSE;
}

static void auracast_sync_info_report(uint8_t *packet, uint16_t length)
{
    auracast_sink_source_info_t *config = (auracast_sink_source_info_t *)packet;
    ASSERT(config, "config is NULL");
    printf("sync create\n");
    printf("Advertising_SID[%d]Address_Type[%d]ADDR:\n", config->Advertising_SID, config->Address_Type);
    put_buf(config->source_mac_addr, 6);
    printf("auracast name:%s\n", config->broadcast_name);
#if 1
    //不匹配设备名，搜到直接同步，如需匹配设备名，请#if 0
    auracast_sink_big_sync_create(config);
    return ;
#endif
    printf("match auracast name:%s[%d]\n", match_aurcast_name[match_auracast_num], (int)strlen((void *)match_aurcast_name[match_auracast_num]));
    if (match_name((void *)config->broadcast_name, (void *)match_aurcast_name[match_auracast_num], strlen((void *)match_aurcast_name[match_auracast_num]))) {
        printf("auracast name match\n");
        auracast_sink_big_sync_create(config);
    } else {
        printf("auracast name no match\n");
    }
}

static int auracast_sink_sync_create(uint8_t *packet, uint16_t length)
{
    u8 i;
    auracast_sink_source_info_t *config = (auracast_sink_source_info_t *)packet;

    if (config->Num_BIS > SINK_MAX_BIS_MUMS) {
        app_auracast.bis_num = SINK_MAX_BIS_MUMS;
    } else {
        app_auracast.bis_num = config->Num_BIS;
    }

    app_auracast.big_hdl = config->BIG_Handle;
    app_auracast.status = APP_AURACAST_STATUS_SYNC;

    for (i = 0; i < app_auracast.bis_num; i++) {
        app_auracast.bis_hdl_info[i].bis_hdl = config->Connection_Handle[i];
        if (!app_auracast.latch_bis_hdl) {
            app_auracast.latch_bis_hdl = app_auracast.bis_hdl_info[i].bis_hdl;
        }
        auracast_sink_media_open(i, packet, length);
        app_auracast.bis_hdl_info[i].init_ok = 1;
    }

    return 0;
}

static int auracast_sink_sync_terminate(uint8_t *packet, uint16_t length)
{
    u8 i;

    for (i = 0; i < app_auracast.bis_num; i++) {
        app_auracast.bis_hdl_info[i].init_ok = 0;
    }

    auracast_sink_media_close();

    app_auracast.bis_num = 0;
    app_auracast.big_hdl = 0;
    app_auracast.latch_bis_hdl = 0;
    app_auracast.status = APP_AURACAST_STATUS_SCAN;

    return 0;
}

static void auracast_sink_event_callback(uint16_t event, uint8_t *packet, uint16_t length)
{
    switch (event) {
    case BIG_SYNC_CREATE:
        //建立同步
        g_printf("sink BIG_SYNC_CREATE");
        auracast_sink_sync_create(packet, length);
        break;
    case BIG_SYNC_TERMINATE:
        //主动解除同步
        g_printf("sink BIG_SYNC_TERMINATE");
        auracast_sink_sync_terminate(packet, length);
        break;
    case ISO_RX_CALLBACK:
        //获取音频数据
        auracast_iso_rx_callback(packet, length);
        break;
    case SOURCE_INFO_REPORT:
        //获取远端设备信息
        auracast_sync_info_report(packet, length);
        break;
    case BIG_SYNC_LOST:
        //被动解除同步
        g_printf("sink BIG_SYNC_LOST");
        auracast_sink_sync_terminate(packet, length);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 开启广播
 *
 * @return >=0:success
 */
/* ----------------------------------------------------------------------------*/
int app_auracast_sink_open()
{
    if (!g_bt_hdl.init_ok || app_var.goto_poweroff_flag) {
        return -EPERM;
    }

    if (app_auracast.status != APP_AURACAST_STATUS_STOP && app_auracast.status != APP_AURACAST_STATUS_SUSPEND) {
        return -EPERM;
    }

    struct app_mode *mode = app_get_current_mode();
    if (mode && (mode->name == APP_MODE_BT) &&
        (bt_get_call_status() != BT_CALL_HANGUP)) {
        return -EPERM;
    }

    log_info("auracast_sink_open");

    auracast_sink_init();
    auracast_sink_event_callback_register(auracast_sink_event_callback);
    auracast_sink_scan_start();

    app_auracast.role = APP_AURACAST_AS_SINK;
    app_auracast.status = APP_AURACAST_STATUS_SCAN;

    return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 关闭广播
 *
 * @param status:挂起还是停止
 *
 * @return 0:success
 */
/* ----------------------------------------------------------------------------*/
int app_auracast_sink_close(u8 status)
{
    if (app_auracast.status == APP_AURACAST_STATUS_STOP || app_auracast.status == APP_AURACAST_STATUS_SUSPEND) {
        return -EPERM;
    }

    u8 i;

    log_info("auracast_sink_close");

    if (app_auracast.status == APP_AURACAST_STATUS_SYNC) {
        auracast_sink_big_sync_terminate(&sink_info);
    }
    auracast_sink_scan_stop();
    os_time_dly(10);
    auracast_sink_uninit();
    auracast_sink_media_close();

    app_auracast.status = status;
    app_auracast.bis_num = 0;
    app_auracast.role = 0;
    app_auracast.big_hdl = 0;
    app_auracast.latch_bis_hdl = 0;

    for (i = 0; i < MAX_BIS_NUMS; i++) {
        memset(&app_auracast.bis_hdl_info[i], 0, sizeof(struct app_auracast_info_t));
    }

    return 0;
}

static void auracast_source_app_send_callback(uint8_t bis_index, uint8_t *buff, uint16_t length)
{
    u8 i;
    int rlen = 0;
    u32 timestamp;

    timestamp = (auracast_source_read_iso_tx_sync(bis_index) + auracast_source_get_sync_delay()) & 0xfffffff;
    if (app_auracast.bis_hdl_info[bis_index].recorder) {
        rlen = le_audio_stream_tx_data_handler(app_auracast.bis_hdl_info[bis_index].recorder, buff, length, timestamp, TCFG_LE_AUDIO_PLAY_LATENCY);
        if (!rlen) {
            putchar('^');
        }
    }
}

static void auracast_source_create(uint8_t *packet, uint16_t length)
{
    app_auracast.role = APP_AURACAST_AS_SOURCE;
    app_auracast.bis_num = AURACAST_BIS_NUM;
    app_auracast.latch_bis_hdl = auracast_source_get_bis_hdl(0);

    for (u8 i = 0; i < app_auracast.bis_num; i++) {
        auracast_source_media_open(i);
        app_auracast.bis_hdl_info[i].init_ok = 1;
    }
}

static void auracast_source_terminated(uint8_t *packet, uint16_t length)
{
    auracast_source_media_close(0xff);

    app_auracast.bis_num = 0;
    app_auracast.role = 0;
    app_auracast.big_hdl = 0;
    app_auracast.latch_bis_hdl = 0;

    for (u8 i = 0; i < MAX_BIS_NUMS; i++) {
        memset(&app_auracast.bis_hdl_info[i], 0, sizeof(struct app_auracast_info_t));
    }
}

static void auracast_source_app_event_callback(uint16_t event, uint8_t *packet, uint16_t length)
{
    uint8_t bis_index;

    if (event >= AURACAST_SOURCE_BIG_CREATED) {
        switch (event) {
        case AURACAST_SOURCE_BIG_CREATED:
            g_printf("AURACAST_SOURCE_BIG_CREATED\n");
            auracast_source_create(packet, length);
            break;
        case AURACAST_SOURCE_BIG_TERMINATED:
            g_printf("AURACAST_SOURCE_BIG_TERMINATED\n");
            auracast_source_terminated(packet, length);
            break;
        }
        return;
    } else {
        bis_index = (uint8_t)event;
        auracast_source_app_send_callback(bis_index, packet, length);
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 开启广播
 *
 * @return >=0:success
 */
/* ----------------------------------------------------------------------------*/
int app_auracast_source_open()
{
    if (!g_bt_hdl.init_ok || app_var.goto_poweroff_flag) {
        return -EPERM;
    }

    if (app_auracast.status != APP_AURACAST_STATUS_STOP && app_auracast.status != APP_AURACAST_STATUS_SUSPEND) {
        return -EPERM;
    }

    struct app_mode *mode = app_get_current_mode();
    if (mode && (mode->name == APP_MODE_BT) &&
        (bt_get_call_status() != BT_CALL_HANGUP)) {
        return -EPERM;
    }

    log_info("auracast_source_open");

    auracast_source_init();
    auracast_source_config(&user_config);
    auracast_source_event_callback_register(auracast_source_app_event_callback);
    auracast_source_start();

    app_auracast.status = APP_AURACAST_STATUS_BROADCAST;

    return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 关闭广播
 *
 * @param status:挂起还是停止
 *
 * @return 0:success
 */
/* ----------------------------------------------------------------------------*/
int app_auracast_source_close(u8 status)
{
    if (app_auracast.status == APP_AURACAST_STATUS_STOP || app_auracast.status == APP_AURACAST_STATUS_SUSPEND) {
        return -EPERM;
    }

    log_info("auracast_source_close");

    auracast_source_stop();
    os_time_dly(10);
    auracast_source_uninit();

    app_auracast.status = status;

    return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 广播开关切换
 *
 * @return 0：操作成功
 */
/* ----------------------------------------------------------------------------*/
int app_auracast_switch(void)
{
    u8 i;
    u8 find = 0;
    struct app_mode *mode;

    if (!g_bt_hdl.init_ok) {
        return -EPERM;
    }

    mode = app_get_current_mode();
    if (mode && (mode->name == APP_MODE_BT) &&
        (bt_get_call_status() != BT_CALL_HANGUP)) {
        return -EPERM;
    }

    if (!tone_player_runing()) {
        if (app_auracast.status == APP_AURACAST_STATUS_STOP || app_auracast.status == APP_AURACAST_STATUS_SUSPEND) {
            if (is_auracast_as_source()) {
                app_auracast_source_open();
            } else {
                app_auracast_sink_open();
            }
            play_tone_file_alone_callback(get_tone_files()->le_broadcast_open,
                                          (void *)TONE_INDEX_AURACAST_OPEN,
                                          auracast_tone_play_end_callback);
        } else {
            if (app_auracast.role == APP_AURACAST_AS_SOURCE) {
                app_auracast_source_close(APP_AURACAST_STATUS_STOP);
            } else if (app_auracast.role == APP_AURACAST_AS_SINK) {
                app_auracast_sink_close(APP_AURACAST_STATUS_STOP);
            }
            play_tone_file_alone_callback(get_tone_files()->le_broadcast_close,
                                          (void *)TONE_INDEX_AURACAST_CLOSE,
                                          auracast_tone_play_end_callback);
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 更新系统当前处于的场景
 *
 * @param scene:当前系统状态
 *
 * @return 0:success
 */
/* ----------------------------------------------------------------------------*/
int update_app_auracast_deal_scene(int scene)
{
    cur_deal_scene = scene;
    return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 广播开启情况下，不同场景的处理流程
 *
 * @param scene:当前系统状态
 *
 * @return ret < 0:无需处理，ret == 0:处理事件但不拦截后续流程，ret > 0:处理事件并拦截后续流程
 */
/* ----------------------------------------------------------------------------*/
int app_auracast_deal(int scene)
{
    u32 rets_addr;
    __asm__ volatile("%0 = rets ;" : "=r"(rets_addr));

    u8 i;
    int ret = 0;
    static u8 phone_start_cnt = 0;
    struct app_mode *mode;

    if (!g_bt_hdl.init_ok) {
        return -EPERM;
    }

    if ((cur_deal_scene == scene) &&
        (scene != LE_AUDIO_PHONE_START) &&
        (scene != LE_AUDIO_PHONE_STOP)) {
        log_error("app_auracast_deal,scene not be modified:%d", scene);
        return -EPERM;
    }

    g_printf("app_auracast_deal, rets_addr:0x%x", rets_addr);

    cur_deal_scene = scene;

    switch (scene) {
    case LE_AUDIO_APP_MODE_ENTER:
        log_info("LE_AUDIO_APP_MODE_ENTER");
        //进入当前模式
        auracast_app_mode_exit = 0;
        config_auracast_as_master = 1;
        mode = app_get_current_mode();
        if (mode) {
            le_audio_ops_register(mode->name);
        }
        if (is_need_resume_auracast()) {
            if (mode->name == APP_MODE_BT) {
                //处于其他模式时，手机后台播歌使设备跳回蓝牙模式，此时获取蓝牙底层a2dp状态为正在播放，
                //但BT_STATUS_A2DP_MEDIA_START事件还没到来，无法获取设备信息，导致直接开关tx_le_audio_open使用了空设备地址引起死机
                app_auracast_sink_open();
            } else {
                app_auracast_resume();
            }
            ret = 1;
        }
        config_auracast_as_master = 0;
        break;

    case LE_AUDIO_APP_MODE_EXIT:
        log_info("auracast_app_mode_exit");
        //退出当前模式
        auracast_app_mode_exit = 1;
        app_auracast_suspend();
        le_audio_ops_unregister();
        break;

    case LE_AUDIO_MUSIC_START:
    case LE_AUDIO_A2DP_START:
        log_info("LE_AUDIO_MUSIC_START");
        //启动a2dp播放
        if (auracast_app_mode_exit) {
            //防止蓝牙非后台情况下退出蓝牙模式时，会先出现auracast_app_mode_exit，再出现LE_AUDIO_A2DP_START，导致广播状态发生改变
            break;
        }
#if (LEA_BIG_FIX_ROLE == 0)
        if (app_auracast.status != APP_AURACAST_STATUS_STOP && app_auracast.status != APP_AURACAST_STATUS_SUSPEND) {
            //(1)当处于广播开启并且作为接收设备时，挂起广播，播放当前手机音乐；
            //(2)当前广播处于挂起状态时，恢复广播并作为发送设备。
            if (app_auracast.role == APP_AURACAST_AS_SINK) {
                app_auracast_suspend();
            } else if (app_auracast.role == APP_AURACAST_AS_SOURCE) {
                ret = 1;
            }
        }
#else
        if (app_auracast.role == APP_AURACAST_AS_SOURCE) {
            //固定收发角色重启广播数据流
            auracast_source_media_reset();
            ret = 1;
            break;
        }
#endif

#if (LEA_BIG_CTRLER_TX_EN || LEA_BIG_CTRLER_RX_EN)
#if TCFG_BT_VOL_SYNC_ENABLE
        mode = app_get_current_mode();
        if (mode && (mode->name == APP_MODE_BT)) {
            set_music_device_volume(get_music_sync_volume());
        }
#endif
#endif

        if (is_need_resume_auracast()) {
            app_auracast_resume();
            ret = 1;
        }
        break;

    case LE_AUDIO_MUSIC_STOP:
    case LE_AUDIO_A2DP_STOP:
        log_info("LE_AUDIO_MUSIC_STOP");
        //停止a2dp播放
        if (auracast_app_mode_exit) {
            //防止蓝牙非后台情况下退出蓝牙模式时，会先出现auracast_app_mode_exit，再出现LE_AUDIO_A2DP_STOP，导致广播状态发生改变
            break;
        }
#if (LEA_BIG_FIX_ROLE == 0)
        //当前处于广播挂起状态时，停止手机播放，恢复广播并接收其他设备的音频数据
        app_auracast_suspend();
#else
        if (app_auracast.role == APP_AURACAST_AS_SOURCE) {
            //固定收发角色暂停播放时关闭广播数据流
            auracast_source_media_close(0xff);
            ret = 1;
            break;
        }
#endif
        if (is_need_resume_auracast()) {
            app_auracast_resume();
            ret = 1;
        }
        break;

    case LE_AUDIO_PHONE_START:
        log_info("LE_AUDIO_PHONE_START");
        //通话时，挂起广播
        phone_start_cnt++;
        printf("===phone_start_cnt:%d===\n", phone_start_cnt);
        app_auracast_suspend();
        break;

    case LE_AUDIO_PHONE_STOP:
        log_info("LE_AUDIO_PHONE_STOP");
        //通话结束恢复广播
        phone_start_cnt--;
        printf("===phone_start_cnt:%d===\n", phone_start_cnt);
        if (phone_start_cnt) {
            log_info("phone_start_cnt:%d", phone_start_cnt);
            break;
        }
        //当前处于蓝牙模式并且挂起前广播，恢复广播并作为接收设备
        if (is_need_resume_auracast()) {
            app_auracast_resume();
        }
        break;

    case LE_AUDIO_EDR_DISCONN:
        log_info("LE_AUDIO_EDR_DISCONN");
        if (auracast_app_mode_exit) {
            //防止蓝牙非后台情况下退出蓝牙模式时，会先出现auracast_app_mode_exit，再出现LE_AUDIO_EDR_DISCONN，导致广播状态发生改变
            break;
        }
        //当经典蓝牙断开后，作为发送端的广播设备挂起广播
        if (app_auracast.role == APP_AURACAST_AS_SOURCE) {
            app_auracast_suspend();
        }
        if (is_need_resume_auracast()) {
            app_auracast_resume();
        }
        break;

    default:
        log_error("%s invalid operation\n", __FUNCTION__);
        ret = -ESRCH;
        break;
    }

    return ret;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 非蓝牙后台情况下，在其他音源模式开启BIG，前提要先开蓝牙协议栈
 */
/* ----------------------------------------------------------------------------*/
void app_auracast_open_in_other_mode()
{
    if (is_need_resume_auracast()) {
        struct app_mode *mode = app_get_current_mode();
        if (mode) {
            le_audio_ops_register(mode->name);
        }
        //下面的代码，会导致在关闭蓝牙后台后，切模式时会提前打开广播
        /* app_auracast_resume(); */
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 非蓝牙后台情况下，在其他音源模式关闭BIG
 */
/* ----------------------------------------------------------------------------*/
void app_auracast_close_in_other_mode()
{
    app_auracast_suspend();
}

static int auracast_source_media_open(uint8_t index)
{
    g_printf("auracast_source_media_open");

    le_audio_switch_ops = get_broadcast_audio_sw_ops();
    //关闭本地音频播放
    if (le_audio_switch_ops && le_audio_switch_ops->local_audio_close) {
        le_audio_switch_ops->local_audio_close();
    }

    u16 frame_dms;
    if (auracast_code_list[user_config.config_sampling_frequency][user_config.config_variant].SDU_interval >= 10000) {
        frame_dms = 100;
    } else {
        frame_dms = 75;
    }

    struct le_audio_stream_params params;
    params.fmt.nch = 1;
    params.fmt.coding_type = AUDIO_CODING_LC3;
    params.fmt.frame_dms = frame_dms;
    params.fmt.bit_rate = auracast_code_list[user_config.config_sampling_frequency][user_config.config_variant].bit_rate;
    params.fmt.sdu_period = auracast_code_list[user_config.config_sampling_frequency][user_config.config_variant].SDU_interval;
    params.fmt.isoIntervalUs = auracast_code_list[user_config.config_sampling_frequency][user_config.config_variant].SDU_interval;
    params.fmt.sample_rate = auracast_code_list[user_config.config_sampling_frequency][user_config.config_variant].sample_rate;
    params.fmt.dec_ch_mode = LEA_TX_DEC_OUTPUT_CHANNEL;
    params.latency = TCFG_LE_AUDIO_PLAY_LATENCY;
    params.conn = app_auracast.latch_bis_hdl;

    //打开广播音频播放
    if (le_audio_switch_ops && le_audio_switch_ops->tx_le_audio_open) {
        app_auracast.bis_hdl_info[index].recorder = le_audio_switch_ops->tx_le_audio_open(&params);
        g_printf("auracast_source_tx_le_audio_open");
    }
    return 0;
}

static int auracast_source_media_close(uint8_t index)
{
    u8 i;
    void *recorder = 0;

    for (i = 0; i < app_auracast.bis_num; i++) {
        if (0xff != index && i != index) {
            continue;
        }

        if (app_auracast.bis_hdl_info[i].recorder) {
            recorder = app_auracast.bis_hdl_info[i].recorder;
            app_auracast.bis_hdl_info[i].recorder = NULL;
        }

        if (recorder) {
            if (le_audio_switch_ops && le_audio_switch_ops->tx_le_audio_close) {
                le_audio_switch_ops->tx_le_audio_close(recorder);
                recorder = NULL;
                g_printf("auracast_source_media_close");
            }
        }
    }

    return 0;
}

int auracast_source_media_reset()
{
    auracast_source_media_close(0xff);
    auracast_source_media_open(0);
    return 0;
}

static int auracast_sink_media_open(uint8_t index, uint8_t *packet, uint16_t length)
{
    g_printf("auracast_sink_media_open");

    auracast_sink_source_info_t *config = (auracast_sink_source_info_t *)packet;

    le_audio_switch_ops = get_broadcast_audio_sw_ops();
    //关闭本地音频播放
    if (le_audio_switch_ops && le_audio_switch_ops->local_audio_close) {
        le_audio_switch_ops->local_audio_close();
    }

    struct le_audio_stream_params params;
    params.fmt.nch = 1;
    params.fmt.coding_type = AUDIO_CODING_LC3;
    params.fmt.dec_ch_mode = LEA_RX_DEC_OUTPUT_CHANNEL;

    g_printf("nch:%d, coding_type:0x%x, dec_ch_mode:%d",
             params.fmt.nch, params.fmt.coding_type, params.fmt.dec_ch_mode);

    if (config->frame_duration == FRAME_DURATION_7_5) {
        params.fmt.frame_dms = 75;
    } else if (config->frame_duration == FRAME_DURATION_10) {
        params.fmt.frame_dms = 100;
    } else {
        ASSERT(0, "frame_dms err:%d", config->frame_duration);
    }
    params.fmt.sdu_period = config->sdu_period;
    params.fmt.isoIntervalUs = config->sdu_period;
    params.fmt.sample_rate = config->sample_rate;
    params.fmt.bit_rate = config->bit_rate;
    params.conn = app_auracast.latch_bis_hdl;

    g_printf("frame_dms:%d, sdu_period:%d, sample_rate:%d, bit_rate:%d,latch_bis_hdl:%d",
             params.fmt.frame_dms, config->sdu_period, config->sample_rate, config->bit_rate, params.conn);

    //打开广播音频播放
    if (le_audio_switch_ops && le_audio_switch_ops->rx_le_audio_open) {
        le_audio_switch_ops->rx_le_audio_open(&app_auracast.bis_hdl_info[index].rx_player, &params);
        g_printf("auracast_sink_rx_le_audio_open");
    }

    return 0;
}

static int auracast_sink_media_close()
{
    u8 i;
    struct le_audio_player_hdl player;
    player.le_audio = 0;
    player.rx_stream = 0;

    for (i = 0; i < app_auracast.bis_num; i++) {
        if (app_auracast.bis_hdl_info[i].rx_player.le_audio) {
            player.le_audio = app_auracast.bis_hdl_info[i].rx_player.le_audio;
            app_auracast.bis_hdl_info[i].rx_player.le_audio = NULL;
        }

        if (app_auracast.bis_hdl_info[i].rx_player.rx_stream) {
            player.rx_stream = app_auracast.bis_hdl_info[i].rx_player.rx_stream;
            app_auracast.bis_hdl_info[i].rx_player.rx_stream = NULL;
        }

        if (player.le_audio && player.rx_stream) {
            if (le_audio_switch_ops && le_audio_switch_ops->rx_le_audio_close) {
                le_audio_switch_ops->rx_le_audio_close(&player);
                player.le_audio = 0;
                player.rx_stream = 0;
                g_printf("auracast_sink_media_close");
            }
        }
    }

    return 0;
}

static void auracast_iso_rx_callback(uint8_t *packet, uint16_t size)
{
    bool plc_flag = 0;
    hci_iso_hdr_t hdr = {0};
    ll_iso_unpack_hdr(packet, &hdr);
    if ((hdr.pb_flag == 0b10) && (hdr.iso_sdu_length == 0)) {
        if (hdr.packet_status_flag == 0b00) {
            /* log_error("SDU empty"); */
            putchar('m');
            plc_flag = 1;
        } else {
            /* log_error("SDU lost"); */
            putchar('s');
            plc_flag = 1;
        }
    }
    if (((hdr.pb_flag == 0b10) || (hdr.pb_flag == 0b00)) && (hdr.packet_status_flag == 0b01)) {
        //log_error("SDU invalid, len=%d", hdr.iso_sdu_length);
        putchar('p');
        plc_flag = 1;
    }

    for (u8 i = 0; i < app_auracast.bis_num; i++) {
        if (app_auracast.bis_hdl_info[i].bis_hdl == hdr.handle && app_auracast.bis_hdl_info[i].rx_player.rx_stream) {
            if (plc_flag) {
                le_audio_stream_rx_frame(app_auracast.bis_hdl_info[i].rx_player.rx_stream, (void *)errpacket, 2, hdr.time_stamp + TCFG_LE_AUDIO_PLAY_LATENCY);
            } else {
                le_audio_stream_rx_frame(app_auracast.bis_hdl_info[i].rx_player.rx_stream, (void *)hdr.iso_sdu, hdr.iso_sdu_length, hdr.time_stamp + TCFG_LE_AUDIO_PLAY_LATENCY);
            }
        }
    }
}

#endif

