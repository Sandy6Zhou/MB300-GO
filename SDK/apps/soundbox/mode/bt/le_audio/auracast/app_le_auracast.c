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
#include "iis.h"
#include "mic.h"
#include "pc_spk_player.h"
#include "btstack/le/auracast_delegator_api.h"
#include "btstack/le/att.h"
#include "btstack/le/ble_api.h"
#include "bt_slience_detect.h"
#include "bt_event_func.h"
#include "spdif_file.h"
#include "le_audio_common.h"
#include "le_auracast_config.h"
#include "le_auracast_pawr.h"

#if (THIRD_PARTY_PROTOCOLS_SEL)
static int ble_connect_dev_detect_timer = 0;
#if (THIRD_PARTY_PROTOCOLS_SEL & RCSP_MODE_EN)
#include "ble_rcsp_server.h"
#include "btstack_rcsp_user.h"
#endif
#endif

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_AURACAST_SINK_EN | LE_AUDIO_AURACAST_SOURCE_EN))

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

/****
| sampling_frequency| variant  | 采样率 | 帧间隔(us) | 包长(字节) | 码率(kbps) |
| ----------------- | -------- | ------ | ---------- | -----------| -----------|
|        0          |    0     | 8000   | 7500       | 26         | 27.732     |
|        0          |    1     | 8000   | 10000      | 30         | 24         |
|        1          |    0     | 16000  | 7500       | 30         | 32         |
|        1          |    1     | 16000  | 10000      | 40         | 32         |
|        2          |    0     | 24000  | 7500       | 45         | 48         |
|        2          |    1     | 24000  | 10000      | 60         | 48         |
|        3          |    0     | 32000  | 7500       | 60         | 64         |
|        3          |    1     | 32000  | 10000      | 80         | 64         |
|        4          |    0     | 44100  | 8163       | 97         | 95.06      |
|        4          |    1     | 44100  | 10884      | 130        | 95.55      |
|        5          |    0     | 48000  | 7500       | 75         | 80         |
|        5          |    1     | 48000  | 10000      | 100        | 80         |
****/

/**************************************************************************************************
  Macros
**************************************************************************************************/
#ifndef AURACAST_SOURCE_BIS_NUMS
#define AURACAST_SOURCE_BIS_NUMS             (2)
#endif
#ifndef AURACAST_SINK_BIS_NUMS
#define AURACAST_SINK_BIS_NUMS               (2)
#endif
#define MAX_BIS_NUMS   MAX(AURACAST_SOURCE_BIS_NUMS, AURACAST_SINK_BIS_NUMS)

#define AURACAST_BIS_ENCRYPTION_ENABLE      (0)
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
};

struct broadcast_source_endpoint_notify {
    uint8_t prd_delay[3];
    uint8_t num_subgroups;
    uint8_t num_bis;
    uint8_t codec_id[5];
    uint8_t codec_spec_length;
    uint8_t codec_spec_data[0];
    uint8_t metadata_length;
    uint8_t bis_data[0];
} __attribute__((packed));

struct auracast_adv_info {
    uint16_t length;
    uint8_t flag;
    uint8_t op;
    uint8_t sn;
    //uint8_t seq;
    uint8_t data[0];
    uint8_t crc[0];
} __attribute__((packed));

struct app_auracast_t {
    u8 status;
    u8 role;
    u8 bis_num;
    u8 big_hdl;
    u32 presentation_delay_us;
    void *recorder;
    struct le_audio_player_hdl rx_player;
    struct app_auracast_info_t bis_hdl_info[MAX_BIS_NUMS];
};

struct broadcast_featrue_notify {
    uint8_t feature;
    uint8_t metadata_len;
    uint8_t metadata[20];
} __attribute__((packed));

struct broadcast_base_info_notify {
    uint8_t address_type;
    uint8_t address[6];
    uint8_t adv_sid;
    uint16_t pa_interval;
} __attribute__((packed));

struct broadcast_codec_info {
    uint8_t nch;
    u32 coding_type;
    s16 frame_len;
    s16 sdu_period;
    int sample_rate;
    int bit_rate;
} __attribute__((packed));

typedef struct {
    uint8_t  save_auracast_addr[NO_PAST_MAX_BASS_NUM_SOURCES][6];
    uint8_t encryp_addr[NO_PAST_MAX_BASS_NUM_SOURCES][6];
    uint8_t  broadcast_name[28];
    uint32_t  broadcast_id;
    uint8_t enc;
    struct  broadcast_featrue_notify fea_data;
    struct broadcast_base_info_notify base_data;
    struct broadcast_codec_info codec_data;
} bass_no_past_source_t;

const struct _auracast_code_list_t {
    u32 sample_rate;
    u32 frame_len;     // (us)
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
static int auracast_source_media_open();
static int auracast_source_media_close();
static int auracast_source_media_reset();
static void auracast_iso_rx_callback(uint8_t *packet, uint16_t size);
static int auracast_sink_media_open(uint16_t bis_hdl, uint8_t *packet, uint16_t length);
static int auracast_sink_media_close();
static int auracast_sink_record_mac_addr(u8 *mac_addr);
static int auracast_sink_get_mac_addr_is_recorded(u8 *mac_addr);
static int auracast_sink_get_recorded_addr_num(void);
static void auracast_sink_connect_filter_timeout(void *priv);

/**************************************************************************************************
  Local Global Variables
**************************************************************************************************/
static OS_MUTEX mutex;
static u8 app_auracast_init_flag = 0;
static u8 auracast_app_mode_exit = 0;  /*!< 音源模式退出标志 */
static u8 config_auracast_as_master = 0;   /*!< 配置广播强制做主机 */
static int cur_deal_scene = -1; /*< 当前系统处于的运行场景 */
static struct app_auracast_t app_auracast;
static struct le_audio_mode_ops *le_audio_switch_ops = NULL; /*!< 广播音频和本地音频切换回调接口指针 */
static char auracast_listen_name[28];
static u16 multi_bis_rx_temp_buf_len = 0;
static u8 *multi_bis_rx_buf[7];
static u16 multi_bis_data_offect[7];
static bool multi_bis_plc_flag[7];
static u8 g_sink_bn = 0;
static u8 *tx_temp_buf = 0;
static u16 tx_temp_buf_len = 0;

static unsigned char errpacket[2] = {
    0x02, 0x00
};

auracast_user_config_t user_config = {
    .config_num_bis = AURACAST_SOURCE_BIS_NUMS,
    .encryption = AURACAST_BIS_ENCRYPTION_ENABLE,
    .broadcast_id = 0x123456,
    /* .broadcast_name = "JL_auracast", */
};
auracast_advanced_config_t user_advanced_config = {
    .adv_cnt = 3,
    .big_offset = 1500,
};

static u16 auracast_sink_sync_timeout_hdl = 0;
static auracast_sink_source_info_t *cur_listening_source_info = NULL;						// 当前正在监听广播设备播歌的信息
static struct auracast_cfg_t *auracast_cfg_info = NULL;

static int auracast_event_to_user(int event, void *value, u32 len)
{
    int *evt = zalloc(sizeof(int) + len);
    ASSERT(evt);
    evt[0] = event << 16;
    evt[0] |= len;
    if (value) {
        memcpy(&evt[1], value, len);
    }
    app_send_message_from(MSG_FROM_AURACAST, sizeof(int) + len, (int *)evt);
    free(evt);
    return 0;
}

/* #if TCFG_AURACAST_SINK_CONNECT_BY_APP */
/* static u8 add_source_state = 0; */
/* static u8 add_source_mac[6]; */
/* static u8 no_past_broadcast_num = 0; */
/* static u8 encry_lock = 0; */
/* static u8 ccc[100]; */
/* static bass_no_past_source_t no_past_broadcast_sink_notify; */
/* static const struct conn_update_param_t con_param = { */
/* .interval_min = 86, */
/* .interval_max = 86, */
/* .latency = 2, */
/* .timeout = 500, */
/* }; */
/* #endif */

u8 lea_cfg_support_ll_hci_cmd_in_lea_lib = 1;


static u8 auracast_switch_onoff = 0;

int le_auracast_state = 0;
static u16 auracast_scan_time = 0;
extern void set_ext_scan_priority(u8 set_pr);


u32 get_auracast_sdu_size()
{
    u16 frame_dms;
    if (auracast_code_list[auracast_cfg_info->sample_rate][auracast_cfg_info->variant].frame_len >= 10000) {
        frame_dms = 100;
    } else {
        frame_dms = 75;
    }
    return (frame_dms * auracast_code_list[auracast_cfg_info->sample_rate][auracast_cfg_info->variant].bit_rate / 8 / 1000 / 10);
}

/* --------------------------------------------------------------------------*/
/**
 * @brief 广播资源初始化
 */
/* ----------------------------------------------------------------------------*/
void app_auracast_init(void)
{
    if (!g_bt_hdl.init_ok) {
        return;
    }

    if (app_auracast_init_flag) {
        return;
    }

    le_audio_common_init();

    int os_ret = os_mutex_create(&mutex);
    if (os_ret != OS_NO_ERR) {
        log_error("%s %d err, os_ret:0x%x", __FUNCTION__, __LINE__, os_ret);
        ASSERT(0);
    }

    app_auracast_init_flag = 1;
}
/* --------------------------------------------------------------------------*/
/**
 * @brief 广播资源初始化
 */
/* ----------------------------------------------------------------------------*/
void app_auracast_uninit(void)
{
    if (!g_bt_hdl.init_ok) {
        return;
    }

    if (!app_auracast_init_flag) {
        return;
    }

    app_auracast_init_flag = 0;
    int os_ret = os_mutex_del(&mutex, OS_DEL_NO_PEND);
    if (os_ret != OS_NO_ERR) {
        log_error("%s %d err, os_ret:0x%x", __FUNCTION__, __LINE__, os_ret);
    }

}
/* --------------------------------------------------------------------------*/
/**
 * @brief 申请互斥量，用于保护临界区代码，与app_broadcast_mutex_post成对使用
 *
 * @param mutex:已创建的互斥量指针变量
 */
/* ----------------------------------------------------------------------------*/
static inline void app_auracast_mutex_pend(OS_MUTEX *mutex, u32 line)
{
    if (!app_auracast_init_flag) {
        log_error("%s err, mutex uninit", __FUNCTION__);
        return;
    }

    int os_ret;
    os_ret = os_mutex_pend(mutex, 0);
    if (os_ret != OS_NO_ERR) {
        log_error("%s err, os_ret:0x%x", __FUNCTION__, os_ret);
        ASSERT(os_ret != OS_ERR_PEND_ISR, "line:%d err, os_ret:0x%x", line, os_ret);
    }
}


/* --------------------------------------------------------------------------*/
/**
 * @brief 释放互斥量，用于保护临界区代码，与app_broadcast_mutex_pend成对使用
 *
 * @param mutex:已创建的互斥量指针变量
 */
/* ----------------------------------------------------------------------------*/
static inline void app_auracast_mutex_post(OS_MUTEX *mutex, u32 line)
{
    if (!app_auracast_init_flag) {
        log_error("%s err, mutex uninit", __FUNCTION__);
        return;
    }

    int os_ret;
    os_ret = os_mutex_post(mutex);
    if (os_ret != OS_NO_ERR) {
        log_error("%s err, os_ret:0x%x", __FUNCTION__, os_ret);
        ASSERT(os_ret != OS_ERR_PEND_ISR, "line:%d err, os_ret:0x%x", line, os_ret);
    }
}
void auracast_scan_switch_priority(void *_sw)
{
    int sw = (int)_sw;
    auracast_scan_time = 0;
    u8 bt_addr[6];
    int timeout = 150;
    u8 a2dp_play = 0;
    if (a2dp_player_get_btaddr(bt_addr)) {
        a2dp_play = 1;
    }
    /* r_printf("auracast_scan_switch=%d,%d\n",sw,timeout ); */
    //edr classic acl priority 30-11=19
    if (sw) {
        /* putchar('S'); */
        set_ext_scan_priority(12);//30-12=18
    } else {
        /* putchar('s'); */
        set_ext_scan_priority(8);//30-8=22
        timeout = a2dp_play ? 300 : 400;
    }
    sw = !sw;
    auracast_scan_time = sys_timeout_add((void *)sw, auracast_scan_switch_priority, timeout);
}



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
    struct app_mode *cur_mode = app_get_current_mode();

#if (LEA_BIG_FIX_ROLE == LEA_ROLE_AS_TX)
    return true;
#elif (LEA_BIG_FIX_ROLE == LEA_ROLE_AS_RX)
    return false;
#endif

#if (TCFG_BT_BACKGROUND_ENABLE)
    //如果能量检测中则等待能量检测完成再触发做发送的流程，避免重复打开数据流
    u8 addr[6];
    if (cur_mode->name == APP_MODE_BT && bt_slience_get_detect_addr(addr)) {
        return false;
    }
#endif

    //当前处于蓝牙模式并且已连接手机设备时，
    //(1)播歌作为广播发送设备；
    //(2)暂停作为广播接收设备。
    if ((cur_mode->name == APP_MODE_BT) &&
        (bt_get_connect_status() != BT_STATUS_WAITINT_CONN)) {
        if (((bt_a2dp_get_status() == BT_MUSIC_STATUS_STARTING &&  bt_get_connect_status() == BT_STATUS_PLAYING_MUSIC) ||
             get_a2dp_decoder_status() ||
             a2dp_player_runing()) && g_play_addr_vaild()) {
            return true;
        } else {
            return false;
        }
    }

#if TCFG_APP_LINEIN_EN
    if (cur_mode->name == APP_MODE_LINEIN)  {
        if ((linein_get_status() || config_auracast_as_master) && g_bt_hdl.background.broadcast_mode == 1) {
            return true;
        } else {
            return false;
        }
    }
#endif

#if TCFG_APP_IIS_EN
    if (cur_mode->name == APP_MODE_IIS)  {
        if ((iis_get_status() || config_auracast_as_master) && g_bt_hdl.background.broadcast_mode == 1) {
            return true;
        } else {
            return false;
        }
    }
#endif

#if TCFG_APP_MIC_EN
    if (cur_mode->name == APP_MODE_MIC)  {
        if ((mic_get_status() || config_auracast_as_master) && g_bt_hdl.background.broadcast_mode == 1) {
            return true;
        } else {
            return false;
        }
    }
#endif

#if TCFG_APP_MUSIC_EN
    if (cur_mode->name == APP_MODE_MUSIC) {
        if (((music_file_get_player_status(get_music_file_player()) == FILE_PLAYER_START) || config_auracast_as_master) && g_bt_hdl.background.broadcast_mode == 1) {
            return true;
        } else {
            return false;
        }
    }
#endif

#if TCFG_APP_FM_EN
    //当处于下面几种模式时，作为广播发送设备
    if (cur_mode->name == APP_MODE_FM)  {
        if ((fm_get_fm_dev_mute() == 0 || config_auracast_as_master) && g_bt_hdl.background.broadcast_mode == 1) {
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
        if (!get_spdif_mute_state() && spdif_player_runing() && g_bt_hdl.background.broadcast_mode == 1) {
            //spdif 模式默认会先进接收端
            y_printf("spdif_player_runing?\n");
            return true;
        } else {
            r_printf("spdif_player_runing? no\n");
            return false;
        }
    }
#endif

#if TCFG_APP_PC_EN
    //当处于下面几种模式时，作为广播发送设备
    if (cur_mode->name == APP_MODE_PC) {
#if defined(TCFG_USB_SLAVE_AUDIO_SPK_ENABLE) && TCFG_USB_SLAVE_AUDIO_SPK_ENABLE
        if (pc_get_status() && g_bt_hdl.background.broadcast_mode == 1) {
            return true;
        } else {
            return false;
        }
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

    app_auracast_open();
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
/* --------------------------------------------------------------------------*/
/**
 * @brief 开启广播
 *
 * @return >=0:success
 */
/* ----------------------------------------------------------------------------*/
int app_auracast_open()
{
    u8 i;
    struct app_mode *mode;

    if (!g_bt_hdl.init_ok || app_var.goto_poweroff_flag) {
        return -EPERM;
    }

    if (!app_auracast_init_flag) {
        return -EPERM;
    }

    mode = app_get_current_mode();
    if (mode && (mode->name == APP_MODE_BT) &&
        (bt_get_call_status() != BT_CALL_HANGUP)) {
        return -EPERM;
    }

    log_info("auracast_open");

    app_auracast_mutex_pend(&mutex, __LINE__);
#if TCFG_KBOX_1T3_MODE_EN
    le_audio_ops_register(APP_MODE_NULL);
#endif

    auracast_cfg_info = get_auracast_cfg_data(app_get_current_mode()->name);
    ASSERT(auracast_cfg_info, "can not read auracast cfg info\n");

    if (is_auracast_as_source()) {
        //初始化广播发送端参数
        app_auracast_source_open();
    } else {
        //初始化广播接收端参数
        app_auracast_sink_open();
    }
    g_bt_hdl.background.broadcast_mode = 1;
    app_auracast_mutex_post(&mutex, __LINE__);
    return 0;
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

void read_auracast_listen_name(void)
{
    int len = syscfg_read(CFG_AURACAST_LISTEN_NAME, auracast_listen_name, sizeof(auracast_listen_name));
    if (len <= 0) {
        r_printf("ERR:Can not read the auracast listen name\n");
        return;
    }

    put_buf((const u8 *)auracast_listen_name, sizeof(auracast_listen_name));
    y_printf("sink_listen_name:%s", auracast_listen_name);
}

static void auracast_sync_info_report(uint8_t *packet, uint16_t length)
{
    if (!app_auracast_init_flag) {
        return;
    }
    /* app_auracast_mutex_pend(&mutex, __LINE__); */
    if (app_auracast.status == APP_AURACAST_STATUS_STOP || app_auracast.status == APP_AURACAST_STATUS_SUSPEND) {
        /* app_auracast_mutex_post(&mutex, __LINE__); */
        return ;
    }
    auracast_sink_source_info_t *config = (auracast_sink_source_info_t *)packet;
    ASSERT(config, "config is NULL");
    printf("sync create\n");
    printf("Advertising_SID[%d]Address_Type[%d]ADDR:\n", config->Advertising_SID, config->Address_Type);
    put_buf(config->source_mac_addr, 6);
    printf("auracast name:%s\n", config->broadcast_name);
    if (match_name(auracast_listen_name, "no_match_name", strlen((void *)auracast_listen_name))) {
        //不匹配设备名，搜到直接同步，如需匹配设备名，请#if 0
        y_printf("no need match name");
        app_auracast_sink_big_sync_create(config);
        /* app_auracast_mutex_post(&mutex, __LINE__); */
        return ;
    }

#if 0
    printf("last_connect_addr:\n");
    put_buf(auracast_sink_last_connect_mac_addr, 6);

    if ((!memcmp(auracast_sink_last_connect_mac_addr, config->source_mac_addr, 6)) && auracast_sink_start_record) {
#if AURACAST_SINK_FILTER_TIMEOUT
        if (!auracast_sink_connect_timeout) {
            auracast_sink_connect_timeout = sys_timeout_add(NULL, auracast_sink_connect_filter_timeout, AURACAST_SINK_FILTER_TIMEOUT);
        }
#endif
        /* app_auracast_mutex_post(&mutex, __LINE__); */
        //auracast_sink_rescan();
        return ;
    }

    if (auarcast_sink_mac_addr_filter) {
        /* for(int i = 0; i < AURACAST_SINK_MAX_RECORD_NUM; i++){ */
        /*     printf("recorded_addr[%d]:", i); */
        /*     put_buf(auracast_sink_record_connect_mac_addr[i],6); */
        /* } */
        if ((auracast_sink_get_mac_addr_is_recorded(config->source_mac_addr))) {
#if AURACAST_SINK_FILTER_TIMEOUT
            if (auracast_sink_connect_timeout) {
                sys_timeout_del(auracast_sink_connect_timeout);
                auracast_sink_connect_timeout =  0;
            }
#endif
            app_auracast_sink_big_sync_create(config);
        } else {
            /* app_auracast_mutex_post(&mutex, __LINE__); */
            //auracast_sink_rescan();
            return ;
        }
    } else {
#if AURACAST_SINK_FILTER_TIMEOUT
        if (auracast_sink_connect_timeout) {
            sys_timeout_del(auracast_sink_connect_timeout);
            auracast_sink_connect_timeout =  0;
        }
#endif
        app_auracast_sink_big_sync_create(config);
    }
    /* app_auracast_mutex_post(&mutex, __LINE__); */
    return;

#endif


    if (match_name((void *)config->broadcast_name, (void *)auracast_listen_name, strlen((void *)auracast_listen_name))) {
        g_printf("auracast name match\n");
        app_auracast_sink_big_sync_create(config);
        /* app_auracast_mutex_post(&mutex, __LINE__); */
        return;
    } else {
        r_printf("auracast name no match\n");
        /* app_auracast_mutex_post(&mutex, __LINE__); */
        //auracast_sink_rescan();
        return;
    }

}

static int auracast_sink_sync_create(uint8_t *packet, uint16_t length)
{
    u8 i;
    auracast_sink_source_info_t *config = (auracast_sink_source_info_t *)packet;

    if (!app_auracast_init_flag) {
        return -1;
    }
    /* app_auracast_mutex_pend(&mutex, __LINE__); */
    if (app_auracast.status == APP_AURACAST_STATUS_STOP || app_auracast.status == APP_AURACAST_STATUS_SUSPEND) {
        /* app_auracast_mutex_post(&mutex, __LINE__); */
        return -1;
    }

    if (config->Num_BIS > AURACAST_SINK_BIS_NUMS) {
        app_auracast.bis_num = AURACAST_SINK_BIS_NUMS;
    } else {
        app_auracast.bis_num = config->Num_BIS;
    }

    app_auracast.big_hdl = config->BIG_Handle;
    app_auracast.status = APP_AURACAST_STATUS_SYNC;

    u16 frame_dms = 0;
    if (config->frame_duration == FRAME_DURATION_7_5) {
        frame_dms = 75;
    } else if (config->frame_duration == FRAME_DURATION_10) {
        frame_dms = 100;
    } else {
        ASSERT(0, "frame_dms err:%d", config->frame_duration);
    }
    y_printf("bis_num:%d, big_hdl:0x%x, config->Num_BIS:%d", app_auracast.bis_num, app_auracast.big_hdl, config->Num_BIS);

    auracast_sink_media_open(config->Connection_Handle[0], packet, length);
    if (app_auracast.bis_num > 1) {
        for (i = 0; i < g_sink_bn; i++) {
            if (!multi_bis_rx_buf[i]) {
                multi_bis_rx_temp_buf_len = app_auracast.bis_num * config->bit_rate * frame_dms / 8 / 1000 / 10;
                g_printf("multi_bis_rx_temp_buf_len:%d", multi_bis_rx_temp_buf_len);
                multi_bis_rx_buf[i] = zalloc(multi_bis_rx_temp_buf_len);
            }
        }
    }

    for (i = 0; i < app_auracast.bis_num; i++) {
        app_auracast.bis_hdl_info[i].bis_hdl = config->Connection_Handle[i];
        app_auracast.bis_hdl_info[i].init_ok = 1;
        y_printf("bis_hdl:0x%x", app_auracast.bis_hdl_info[i].bis_hdl);
    }

#if TCFG_AURACAST_PAWR_ENABLE
    pawr_slave_init();
#endif
    /* app_auracast_mutex_post(&mutex, __LINE__); */
    return 0;
}

static int auracast_sink_sync_terminate(uint8_t *packet, uint16_t length)
{
    u8 i;

    if (!app_auracast_init_flag) {
        return -1;
    }
    /* app_auracast_mutex_pend(&mutex, __LINE__); */
    if (app_auracast.status == APP_AURACAST_STATUS_STOP || app_auracast.status == APP_AURACAST_STATUS_SUSPEND) {
        /* app_auracast_mutex_post(&mutex, __LINE__); */
        return -1;
    }
    for (i = 0; i < app_auracast.bis_num; i++) {
        app_auracast.bis_hdl_info[i].init_ok = 0;
    }

    for (i = 0; i < g_sink_bn; i++) {
        if (multi_bis_rx_buf[i]) {
            free(multi_bis_rx_buf[i]);
            multi_bis_rx_buf[i] = 0;
        }
    }
    auracast_sink_media_close();

#if TCFG_AURACAST_PAWR_ENABLE
    app_speaker_cancle_pawr_sync();
#endif

    app_auracast.bis_num = 0;
    app_auracast.big_hdl = 0;
    app_auracast.status = APP_AURACAST_STATUS_SCAN;

    app_auracast_sink_scan_start();
    /* app_auracast_mutex_post(&mutex, __LINE__); */
    return 0;
}

/**
 * @brief 设备收到广播设备信息汇报给手机APP
 */
void auracast_sink_source_info_report_event_deal(uint8_t *packet, uint16_t length)
{
    auracast_sync_info_report(packet, length);
}

static void auracast_sink_big_info_report_event_deal(uint8_t *packet, uint16_t length)
{
    auracast_sink_source_info_t *param = (auracast_sink_source_info_t *)packet;
    g_sink_bn = param->bn;
    printf("auracast_sink_big_info_report_event_deal\n");
    y_printf("num bis : %d, bn : %d\n", param->Num_BIS, g_sink_bn);
    if (param->Num_BIS > AURACAST_SINK_BIS_NUMS) {
        param->Num_BIS = AURACAST_SINK_BIS_NUMS;
    }
    //param->Num_BIS = 1;
}

static void auracast_sink_event_callback(uint16_t event, uint8_t *packet, uint16_t length)
{
    if (!app_auracast_init_flag) {
        return ;
    }

    switch (event) {
    /*=================Sink Event===============*/
    case AURACAST_SINK_SOURCE_INFO_REPORT_EVENT:
        printf("AURACAST_SINK_SOURCE_INFO_REPORT_EVENT\n");
        auracast_sink_source_info_report_event_deal(packet, length);
        break;
    case AURACAST_SINK_BLE_CONNECT_EVENT:
        printf("AURACAST_SINK_BLE_CONNECT_EVENT\n");
        break;
    case AURACAST_SINK_BIG_SYNC_CREATE_EVENT:
        auracast_event_to_user(event, packet, length);
        break;
    case AURACAST_SINK_BIG_SYNC_TERMINATE_EVENT:
        //主动解除同步
        printf("AURACAST_SINK_BIG_SYNC_TERMINATE_EVENT\n");
        break;
    case AURACAST_SINK_BIG_SYNC_FAIL_EVENT:
    case AURACAST_SINK_BIG_SYNC_LOST_EVENT:
        auracast_event_to_user(event, packet, length);
        break;
    case AURACAST_SINK_PERIODIC_ADVERTISING_SYNC_LOST_EVENT:
        auracast_event_to_user(event, packet, length);
        break;
    case AURACAST_SINK_BIG_INFO_REPORT_EVENT:
        printf("AURACAST_SINK_BIG_INFO_REPORT_EVENT\n");
        auracast_sink_big_info_report_event_deal(packet, length);
        break;
    case AURACAST_SINK_ISO_RX_CALLBACK_EVENT:
        //printf("AURACAST_SINK_ISO_RX_CALLBACK_EVENT\n");
        //获取音频数据
        auracast_iso_rx_callback(packet, length);
        break;
    case AURACAST_SINK_EXT_SCAN_COMPLETE_EVENT:
        printf("AURACAST_SINK_EXT_SCAN_COMPLETE_EVENT\n");
        break;
    case AURACAST_SINK_PADV_REPORT_EVENT:
        /* log_info(" AURACAST_SINK_PADV_REPORT_EVENT"); */
        /* put_buf(packet, length); */
        break;
    default:
        printf("auracast sink unknow event %x\n", event);
        break;
    }
}

int app_auracast_bass_server_event_callback(uint8_t event, uint8_t *packet, uint16_t size)
{
    int ret = 0;
    struct le_audio_bass_add_source_info_t *bass_source_info = (struct le_audio_bass_add_source_info_t *)packet;
    auracast_sink_source_info_t add_source_param = {0};
    switch (event) {
    case BASS_SERVER_EVENT_SCAN_STOPPED:
        break;
    case BASS_SERVER_EVENT_SCAN_STARTED:
        break;
    case BASS_SERVER_EVENT_SOURCE_ADDED:
        printf("BASS_SERVER_EVENT_SOURCE_ADDED\n");
        if ((bass_source_info->pa_sync == BASS_PA_SYNC_SYNCHRONIZE_TO_PA_PAST_AVAILABLE) \
            || (bass_source_info->pa_sync == BASS_PA_SYNC_SYNCHRONIZE_TO_PA_PAST_NOT_AVAILABLE)) {
            memcpy(add_source_param.source_mac_addr, bass_source_info->address, 6);
            bass_source_info->bis_sync_state = 0x03;
            app_auracast_sink_big_sync_create(&add_source_param);
        }
        break;
    case BASS_SERVER_EVENT_SOURCE_MODIFIED:
        printf("BASS_SERVER_EVENT_SOURCE_MODIFIED\n");
        if ((bass_source_info->pa_sync == BASS_PA_SYNC_SYNCHRONIZE_TO_PA_PAST_AVAILABLE) \
            || (bass_source_info->pa_sync == BASS_PA_SYNC_SYNCHRONIZE_TO_PA_PAST_NOT_AVAILABLE)) {
            app_auracast_sink_big_sync_terminate();
            memcpy(add_source_param.source_mac_addr, bass_source_info->address, 6);
            app_auracast_sink_big_sync_create(&add_source_param);
        } else {
            app_auracast_sink_big_sync_terminate();
        }
        break;
    case BASS_SERVER_EVENT_SOURCE_DELETED:
        break;
    case BASS_SERVER_EVENT_BROADCAST_CODE:
        printf("BASS_SERVER_EVENT_BROADCAST_CODE id=%d\n", packet[0]);
        put_buf(packet, size);

        ASSERT(cur_listening_source_info);
        auracast_sink_set_broadcast_code(&packet[1]);
        ret = app_auracast_sink_big_sync_create(cur_listening_source_info);
        if (ret != 0) {
        }
        break;
    }
    return 0;
}

#include "user_cfg.h"
extern void bt_le_audio_adv_enable(u8 enable);
extern void le_audio_init(u8 mode);
extern void le_audio_uninit(u8 mode);
extern void le_audio_name_reset(u8 *name, u8 len);
void app_auracast_sink_init(void)
{
    printf("app_auracast_sink_init\n");
#if !(TCFG_LE_AUDIO_APP_CONFIG & LE_AUDIO_UNICAST_SINK_EN)
    // BASS
    // char le_audio_name[LOCAL_NAME_LEN] = "le_audio_";     //le_audio蓝牙名
    // u8 tem_len = 0;//strlen(le_audio_name);
    // memcpy(&le_audio_name[tem_len], (u8 *)bt_get_local_name(), LOCAL_NAME_LEN - tem_len);
    // le_audio_name_reset((u8 *)le_audio_name, strlen(le_audio_name));

    // le_audio_init(3);
    // bt_le_audio_adv_enable(1);
#endif
    auracast_sink_init(AURACAST_SINK_API_VERSION);
    auracast_sink_event_callback_register(auracast_sink_event_callback);
    le_audio_bass_event_callback_register(app_auracast_bass_server_event_callback);
}

static int __app_auracast_sink_big_sync_terminate(void)
{
    if (cur_listening_source_info) {
        free(cur_listening_source_info);
        cur_listening_source_info = NULL;
    }
    int ret = auracast_sink_big_sync_terminate();
    if (0 == ret) {
        //le_auracast_audio_close();
        if (auracast_sink_sync_timeout_hdl != 0) {
            sys_timeout_del(auracast_sink_sync_timeout_hdl);
            auracast_sink_sync_timeout_hdl = 0;
        }
    }
    return ret;
}

/**
 * @brief 关闭所有正在监听播歌的广播设备
 */
int app_auracast_sink_big_sync_terminate(void)
{
    printf("app_auracast_sink_big_sync_terminate\n");
    int ret = __app_auracast_sink_big_sync_terminate();
    return ret;
}

static int __app_auracast_sink_scan_start(void)
{
    int ret = auracast_sink_scan_start();
    printf("auracast_sink_scan_start ret:%d\n", ret);
    return ret;
}

/**
 * @brief 手机通知设备开始搜索auracast广播
 */
int app_auracast_sink_scan_start(void)
{
    // BASS
    // return 0;
    printf("app_auracast_sink_scan_start\n");
    return __app_auracast_sink_scan_start();
}

static int __app_auracast_sink_scan_stop(void)
{
    int ret = auracast_sink_scan_stop();
    printf("auracast_sink_scan_stop ret:%d\n", ret);
    return ret;
}

/**
 * @brief 手机通知设备关闭搜索auracast广播
 */
int app_auracast_sink_scan_stop(void)
{
    printf("app_auracast_sink_scan_stop\n");
    return __app_auracast_sink_scan_stop();
}

static void auracast_sink_sync_timeout_handler(void *priv)
{
    if (app_auracast.role == APP_AURACAST_AS_SINK) {
        printf("auracast_sink_sync_timeout_handler\n");
        auracast_sink_scan_stop();
        auracast_sink_big_sync_terminate();
    }
    //app_auracast_app_notify_listening_status(0, 2);
    auracast_sink_sync_timeout_hdl = 0;
}

static int __app_auracast_sink_big_sync_create(auracast_sink_source_info_t *param, u8 tws_malloc)
{
    if (cur_listening_source_info == NULL) {
        cur_listening_source_info = malloc(sizeof(auracast_sink_source_info_t));
    } else {
        printf("cur_listening_source_info already malloc!\n");
        ASSERT(0);
        return -1;
    }
    memcpy(cur_listening_source_info, param, sizeof(auracast_sink_source_info_t));
    int ret = auracast_sink_big_sync_create(cur_listening_source_info);
    if (0 == ret) {
        //auracast_sink_sync_timeout_hdl = sys_timeout_add(NULL, auracast_sink_sync_timeout_handler, 15000);
    } else {
        printf("__app_auracast_sink_big_sync_create ret:%d\n", ret);
    }
    if (tws_malloc) {
        free(param);
    }

    return ret;
}

/**
 * @brief 手机选中广播设备开始播歌
 *
 * @param param 要监听的广播设备
 */
int app_auracast_sink_big_sync_create(auracast_sink_source_info_t *param)
{
    printf("app_auracast_sink_big_sync_create\n");
    u8 bt_addr[6];
    if (auracast_sink_sync_timeout_hdl != 0) {
        printf("auracast_sink_sync_timeout_hdl is not null\n");
        return -1;
    }

    /* if (esco_player_runing()) { */
    /* printf("app_auracast_sink_big_sync_create esco_player_runing\n"); */
    /* // 暂停auracast的播歌 */
    /* return -1; */
    /* } */

    /* if (a2dp_player_get_btaddr(bt_addr)) { */
    /* #if TCFG_A2DP_PREEMPTED_ENABLE */
    /* memcpy(a2dp_auracast_preempted_addr, bt_addr, 6); */
    /* a2dp_player_close(bt_addr); */
    /* a2dp_media_mute(bt_addr); */
    /* void *device = btstack_get_conn_device(bt_addr); */
    /* if (device) { */
    /* btstack_device_control(device, USER_CTRL_AVCTP_OPID_PAUSE); */
    /* } */
    /* #else */
    /* // 不抢播, 暂停auracast的播歌 */
    /* return -1; */
    /* #endif */
    /* } */

    if (cur_listening_source_info) {
        free(cur_listening_source_info);
        cur_listening_source_info = NULL;
    }
    int ret = __app_auracast_sink_big_sync_create(param, 0);

    return ret;
}

#if (defined CONFIG_CPU_BR27) || (defined CONFIG_CPU_BR28)
#if THIRD_PARTY_PROTOCOLS_SEL
static void app_auracast_retry_open(void *priv)
{
    u32 role = (u32)priv;

    if (role == APP_AURACAST_AS_SOURCE) {
        app_auracast_source_open();
    } else if (role == APP_AURACAST_AS_SINK) {
        app_auracast_sink_open();
    }
}

#endif
#endif

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

    if (!app_auracast_init_flag) {
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

#if (LEA_BIG_FIX_ROLE == LEA_ROLE_AS_RX)
    le_audio_switch_ops = get_broadcast_audio_sw_ops();
    //关闭本地音频播放
    if (le_audio_switch_ops && le_audio_switch_ops->local_audio_close) {
        le_audio_switch_ops->local_audio_close();
    }
#endif

    auracast_switch_onoff = 1;

#if (defined CONFIG_CPU_BR27) || (defined CONFIG_CPU_BR28)
#if THIRD_PARTY_PROTOCOLS_SEL
    multi_protocol_bt_ble_enable(0);
#if THIRD_PARTY_PROTOCOLS_SEL & RCSP_MODE_EN
    u8 conn_num = bt_rcsp_ble_conn_num();
#else
    u8 conn_num = multi_protocol_bt_ble_connect_num();
#endif
    if (conn_num > 0) {
        ble_connect_dev_detect_timer = sys_timeout_add((void *)APP_AURACAST_AS_SINK,  app_auracast_retry_open, 250); //由于非标准广播使用私有hci事件回调所以需要等RCSP断连事件处理完后才能开广播
        return -EPERM;
    } else {
        ble_connect_dev_detect_timer = 0;
    }
#endif
#endif
    app_auracast_mutex_pend(&mutex, __LINE__);
    log_info("auracast_sink_open");

    read_auracast_listen_name();

    le_auracast_state = 0;
    app_auracast_sink_init();
    //auracast_sink_init();
    //auracast_sink_event_callback_register(auracast_sink_event_callback);
    /* #if TCFG_AURACAST_SINK_CONNECT_BY_APP */
    /* auracast_delegator_user_config_t auracast_delegator_user_parm; */
    /* auracast_delegator_user_parm.adv_edr = 1; */
    /* auracast_delegator_user_parm.adv_interval = 40; */
    /* memcpy(&auracast_delegator_user_parm.device_name, (u8 *)bt_get_local_name(), LOCAL_NAME_LEN); */
    /* auracast_delegator_user_parm.device_name_len = strlen(auracast_delegator_user_parm.device_name); */
    /* [> auracast_delegator_user_config_t param; <] */
    /* auracast_delegator_config(&auracast_delegator_user_parm); */
    /* auracast_delegator_event_callback_register(auracast_delegator_event_callback); */
    /* auracast_delegator_adv_enable(1); */
    /* #else */
    app_auracast_sink_scan_start();
    /* #endif */

    app_auracast.role = APP_AURACAST_AS_SINK;
    app_auracast.status = APP_AURACAST_STATUS_SCAN;
#if LEA_BIG_RX_CLOSE_EDR_EN
    bt_close_discoverable_and_connectable();
#endif

    app_auracast_mutex_post(&mutex, __LINE__);
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

    if (!app_auracast_init_flag) {
        return -EPERM;
    }

    if (auracast_sink_sync_timeout_hdl != 0) {
        sys_timeout_del(auracast_sink_sync_timeout_hdl);
        auracast_sink_sync_timeout_hdl = 0;
    }

    if (cur_listening_source_info) {
        free(cur_listening_source_info);
        cur_listening_source_info = NULL;
    }

    app_auracast_mutex_pend(&mutex, __LINE__);
    log_info("auracast_sink_close");

#if (defined CONFIG_CPU_BR27) || (defined CONFIG_CPU_BR28)
#if THIRD_PARTY_PROTOCOLS_SEL
    if (ble_connect_dev_detect_timer) {
        sys_timeout_del(ble_connect_dev_detect_timer);
    }
#endif
#endif

    /* auracast_sink_set_audio_state(0); */
    if (app_auracast.status == APP_AURACAST_STATUS_SYNC) {
        auracast_sink_big_sync_terminate();
    }
    auracast_sink_scan_stop();

#if !(TCFG_LE_AUDIO_APP_CONFIG & LE_AUDIO_UNICAST_SINK_EN)
    // BASS
    // bt_le_audio_adv_enable(0);
    // le_audio_uninit(3);
#endif

    os_time_dly(10);
    auracast_sink_uninit();
    app_auracast.status = status;
    auracast_sink_media_close();

#if TCFG_AURACAST_PAWR_ENABLE
    app_speaker_cancle_pawr_sync();
#endif

#if LEA_BIG_RX_CLOSE_EDR_EN
    if (app_auracast.role == APP_AURACAST_AS_SINK) {
        //恢复经典蓝牙可发现可连接
        bt_discovery_and_connectable_using_loca_mac_addr(1, 1);
    }
#endif
    app_auracast.bis_num = 0;
    app_auracast.role = 0;
    app_auracast.big_hdl = 0;

    u8 i;
    for (i = 0; i < MAX_BIS_NUMS; i++) {
        memset(&app_auracast.bis_hdl_info[i], 0, sizeof(struct app_auracast_info_t));
    }

#if TCFG_AUTO_SHUT_DOWN_TIME
    sys_auto_shut_down_enable();   // 恢复自动关机
#endif

    app_auracast_mutex_post(&mutex, __LINE__);

    auracast_switch_onoff = 0;
#if (defined CONFIG_CPU_BR27) || (defined CONFIG_CPU_BR28)
#if THIRD_PARTY_PROTOCOLS_SEL
    if (status != APP_AURACAST_STATUS_SUSPEND) {
        ll_set_private_access_addr_pair_channel(0);
        multi_protocol_bt_ble_enable(1);
    }
#endif
#endif
    return 0;
}

static void auracast_source_app_send_callback(uint8_t *buff, uint16_t length)
{
#if 0
    u8 i;
    int rlen = 0;
    u32 timestamp;
    auracast_event_send_t *send_packet = (auracast_event_send_t *)buff;
    u32 sdu_interval_us = auracast_code_list[AURACAST_BIS_SAMPLING_RATE][AURACAST_BIS_VARIANT].frame_len;
    timestamp = (auracast_source_read_iso_tx_sync(send_packet->bis_index) \
                 + auracast_source_get_sync_delay()) & 0xfffffff;
    timestamp += ((send_packet->bis_sub_event_counter - 1) * sdu_interval_us);
    /* printf("0x%x %d %d\n", send_packet->bis_index, send_packet->bis_sub_event_counter, timestamp); */
    if (app_auracast.recorder) {
        rlen = le_audio_stream_tx_data_handler(app_auracast.recorder, send_packet->buffer, length, timestamp, TCFG_LE_AUDIO_PLAY_LATENCY);
        if (!rlen) {
            putchar('^');
        }
    }
    if (!rlen) {
        memset(send_packet->buffer, 0, length);
    }
#endif
}

int auracast_source_user_can_send_now_callback(uint8_t big_hdl)
{
    u8 bis_index;
    u8 bis_sub_event_counter;
    int rlen = 0;
    u16 tx_offset = 0;
    u32 timestamp;
    timestamp = (auracast_source_read_iso_tx_sync(0) \
                 + auracast_source_get_sync_delay() + auracast_cfg_info->bn * auracast_code_list[auracast_cfg_info->sample_rate][auracast_cfg_info->variant].frame_len) & 0xfffffff;
    if (app_auracast.recorder) {
        rlen = le_audio_stream_tx_data_handler(app_auracast.recorder, tx_temp_buf, tx_temp_buf_len, timestamp, auracast_cfg_info->play_latency); //发送延时,其他都是播放延时
        if (!rlen) {
            putchar('^');
        }
    }
    if (!rlen) {
        memset(tx_temp_buf, 0, tx_temp_buf_len);
    }

    for (bis_sub_event_counter = 0; bis_sub_event_counter < auracast_cfg_info->bn; bis_sub_event_counter++) {
        for (bis_index = 0; bis_index < AURACAST_SOURCE_BIS_NUMS; bis_index++) {
            auracast_source_user_send_iso_packet(bis_index, bis_sub_event_counter, tx_temp_buf + tx_offset, get_auracast_sdu_size());
            tx_offset += get_auracast_sdu_size();
        }
    }

    if (get_vm_ram_storage_enable()) {
        if (get_vm_ram_data_used_size()) {
            app_send_message(APP_MSG_VM_FLUSH_FLASH, 0);
        }
    }

    return 1;// 就是用户使用接口自己发iso数据
    /* return 0; */
}

static void auracast_source_create(uint8_t *packet, uint16_t length)
{

    if (!app_auracast_init_flag) {
        return ;
    }
    app_auracast_mutex_pend(&mutex, __LINE__);
    if (app_auracast.status == APP_AURACAST_STATUS_STOP || app_auracast.status == APP_AURACAST_STATUS_SUSPEND) {
        app_auracast_mutex_post(&mutex, __LINE__);
        return ;
    }
    app_auracast.role = APP_AURACAST_AS_SOURCE;
    app_auracast.bis_num = AURACAST_SOURCE_BIS_NUMS;

    auracast_source_media_open();

    for (u8 i = 0; i < app_auracast.bis_num; i++) {
        app_auracast.bis_hdl_info[i].init_ok = 1;
    }

    app_auracast_mutex_post(&mutex, __LINE__);
}

static void auracast_source_app_event_callback(uint16_t event, uint8_t *packet, uint16_t length)
{
    switch (event) {
    case AURACAST_SOURCE_BIG_CREATED:
        g_printf("AURACAST_SOURCE_BIG_CREATED from bt_stack");
        auracast_event_to_user(event, packet, length);
        break;
    case AURACAST_SOURCE_BIG_TERMINATED:
        g_printf("AURACAST_SOURCE_BIG_TERMINATED\n");
        break;
    case AURACAST_SOURCE_SEND_CALLBACK:
        auracast_source_app_send_callback(packet, length);
        break;
    default:
        printf("auracast source unknow event %x\n", event);
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
int app_auracast_source_open()
{
    if (!g_bt_hdl.init_ok || app_var.goto_poweroff_flag) {
        return -EPERM;
    }

    if (!app_auracast_init_flag) {
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

    auracast_switch_onoff = 1;

#if (defined CONFIG_CPU_BR27) || (defined CONFIG_CPU_BR28)
#if THIRD_PARTY_PROTOCOLS_SEL
    multi_protocol_bt_ble_enable(0);
#if THIRD_PARTY_PROTOCOLS_SEL & RCSP_MODE_EN
    u8 conn_num = bt_rcsp_ble_conn_num();
#else
    u8 conn_num = multi_protocol_bt_ble_connect_num();
#endif
    if (conn_num > 0) {
        ble_connect_dev_detect_timer = sys_timeout_add((void *)APP_AURACAST_AS_SOURCE, app_auracast_retry_open, 250); //由于非标准广播使用私有hci事件回调所以需要等RCSP断连事件处理完后才能开广播
        return -EPERM;
    } else {
        ble_connect_dev_detect_timer = 0;
    }
#endif
#endif
    app_auracast_mutex_pend(&mutex, __LINE__);
    log_info("auracast_source_open");

    /* memcpy(user_config.broadcast_name, get_le_audio_pair_name(), sizeof(user_config.broadcast_name)); */
    strcpy(user_config.broadcast_name, get_le_audio_pair_name());

    if (tx_temp_buf) {
        free(tx_temp_buf);
        tx_temp_buf = 0;
    }

    u16 frame_dms;
    if (auracast_code_list[auracast_cfg_info->sample_rate][auracast_cfg_info->variant].frame_len >= 10000) {
        frame_dms = 100;
    } else {
        frame_dms = 75;
    }
    tx_temp_buf_len = AURACAST_SOURCE_BIS_NUMS * auracast_cfg_info->bn * get_auracast_sdu_size();
    g_printf("tx_temp_buf_len:%d", tx_temp_buf_len);
    tx_temp_buf = zalloc(tx_temp_buf_len);

    auracast_source_init(AURACAST_SOURCE_API_VERSION);
    if (auracast_cfg_info) {
        user_config.config_sampling_frequency = auracast_cfg_info->sample_rate;
        user_config.config_variant = auracast_cfg_info->variant;
        user_config.presentation_delay_us = auracast_cfg_info->play_latency;
        user_advanced_config.bn = auracast_cfg_info->bn;
        user_advanced_config.rtn = auracast_cfg_info->rtn;
    }
    auracast_source_config(&user_config);
    auracast_source_advanced_config(&user_advanced_config);
    auracast_source_event_callback_register(auracast_source_app_event_callback);
    auracast_source_start();
    app_auracast.role = APP_AURACAST_AS_SOURCE;
    app_auracast.status = APP_AURACAST_STATUS_BROADCAST;

#if TCFG_AUTO_SHUT_DOWN_TIME
    sys_auto_shut_down_disable();
#endif
    app_auracast_mutex_post(&mutex, __LINE__);
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

    app_auracast_mutex_pend(&mutex, __LINE__);
    log_info("auracast_source_close");
    app_auracast.status = status;

    auracast_source_stop();
    os_time_dly(10);
    auracast_source_uninit();
    auracast_source_media_close(0xff);

#if TCFG_AURACAST_PAWR_ENABLE
    app_speaker_stop_pawr();
#endif

    app_auracast.bis_num = 0;
    app_auracast.role = 0;
    app_auracast.big_hdl = 0;

    for (u8 i = 0; i < MAX_BIS_NUMS; i++) {
        memset(&app_auracast.bis_hdl_info[i], 0, sizeof(struct app_auracast_info_t));
    }

    if (tx_temp_buf) {
        free(tx_temp_buf);
        tx_temp_buf = 0;
    }

#if TCFG_AUTO_SHUT_DOWN_TIME
    sys_auto_shut_down_enable();   // 恢复自动关机
#endif

    app_auracast_mutex_post(&mutex, __LINE__);

    auracast_switch_onoff = 0;

#if (defined CONFIG_CPU_BR27) || (defined CONFIG_CPU_BR28)
#if (THIRD_PARTY_PROTOCOLS_SEL & RCSP_MODE_EN)
    if (status != APP_AURACAST_STATUS_SUSPEND) {
        ll_set_private_access_addr_pair_channel(0);
        multi_protocol_bt_ble_enable(1);
    }
#endif
#endif
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
#if TCFG_APP_SPDIF_EN
    if (mode && (mode->name == APP_MODE_SPDIF)) {
        //开启广播时让spdif stream stop, 由spdif le_audio_tx 回调里去start
        spdif_stream_stop();
    }
#endif

    if (!tone_player_runing()) {
        if (app_auracast.status == APP_AURACAST_STATUS_STOP || app_auracast.status == APP_AURACAST_STATUS_SUSPEND) {
            bt_work_mode_select(BT_MODE_AURACAST);
            play_tone_file_alone_callback(get_tone_files()->le_broadcast_open,
                                          (void *)TONE_INDEX_AURACAST_OPEN,
                                          auracast_tone_play_end_callback);
        } else {
            bt_work_mode_select(g_bt_hdl.last_work_mode);
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

    if (g_bt_hdl.work_mode != BT_MODE_AURACAST) {
        return -EPERM;
    }
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
        if (!auracast_app_mode_exit) {
            log_error("app_auracast_deal,scene has entered");
            break;
        }
        log_info("LE_AUDIO_APP_MODE_ENTER");
        //进入当前模式
        auracast_app_mode_exit = 0;
        config_auracast_as_master = 1;
    case LE_AUDIO_APP_OPEN:
        mode = app_get_current_mode();
        if (mode) {
            le_audio_ops_register(mode->name);
        }
        le_audio_switch_ops = get_broadcast_audio_sw_ops();

        if (is_need_resume_auracast()) {
            /* if (mode->name == APP_MODE_BT) { */
            /*     //处于其他模式时，手机后台播歌使设备跳回蓝牙模式，此时获取蓝牙底层a2dp状态为正在播放， */
            /*     //但BT_STATUS_A2DP_MEDIA_START事件还没到来，无法获取设备信息，导致直接开关tx_le_audio_open使用了空设备地址引起死机 */
            /*     app_auracast_sink_open(); */
            /* } else { */
            app_auracast_resume();
            /* } */
            ret = 1;
        }
        config_auracast_as_master = 0;
        break;

    case LE_AUDIO_APP_MODE_EXIT:
        if (auracast_app_mode_exit) {
            log_error("app_auracast_deal,scene has exited");
            break;
        }
        log_info("auracast_app_mode_exit");
        //退出当前模式
        auracast_app_mode_exit = 1;
        if (get_vm_ram_storage_enable()) {
            vm_flush2flash(0);
        }
    case LE_AUDIO_APP_CLOSE:
        phone_start_cnt = 0;
        app_auracast_suspend();
        le_audio_ops_unregister();
        le_audio_switch_ops = NULL;
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
        g_printf("app_auracast.bis_num:%d\n", app_auracast.bis_num);
        //BIS赋值之后才可以调用media_reset, 处理后台拉回蓝牙模式时, auracast_open直接打开了source
        //协议栈source_create之前，app_core又收到a2dp_media_start消息跑到这里，导致数据流重复打开
        if (app_auracast.role == APP_AURACAST_AS_SOURCE && app_auracast.bis_num) {
            //固定收发角色重启广播数据流
            auracast_source_media_reset();
            ret = 1;
            break;
        }
#endif

        if (is_need_resume_auracast()) {
            /* app_auracast_resume(); */
            app_auracast_source_open();
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
            /* app_auracast_resume(); */
            app_auracast_sink_open();
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
            /* app_auracast_resume(); */
            if (is_auracast_as_source()) {
                //初始化广播发送端参数
                app_auracast_source_open();
            } else {
                //初始化广播接收端参数
                app_auracast_sink_open();
            }
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
            /* app_auracast_resume(); */
            app_auracast_sink_open();
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
    app_auracast_init();
    if (is_need_resume_auracast()) {
        struct app_mode *mode = app_get_current_mode();
        if (mode) {
            le_audio_ops_register(mode->name);
        }
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

static void auracast_source_param_prepare(struct le_audio_stream_params *params)
{
    u16 frame_dms;
    if (auracast_code_list[auracast_cfg_info->sample_rate][auracast_cfg_info->variant].frame_len >= 10000) {
        frame_dms = 100;
    } else {
        frame_dms = 75;
    }

    params->fmt.nch = AURACAST_TX_CODEC_CHANNEL;
    params->fmt.coding_type = AUDIO_CODING_LC3;
    params->fmt.frame_dms = frame_dms;
    ASSERT(auracast_cfg_info, "auracast cfg NULL!\n");
    if (AURACAST_TX_CODEC_CHANNEL == 2 &&  AURACAST_SOURCE_BIS_NUMS == 1) {
        params->fmt.bit_rate = auracast_code_list[auracast_cfg_info->sample_rate][auracast_cfg_info->variant].bit_rate;
    } else {
        params->fmt.bit_rate = params->fmt.nch * auracast_code_list[auracast_cfg_info->sample_rate][auracast_cfg_info->variant].bit_rate;
    }
    params->fmt.sdu_period = auracast_code_list[auracast_cfg_info->sample_rate][auracast_cfg_info->variant].frame_len;
    params->fmt.isoIntervalUs = auracast_cfg_info->bn * auracast_code_list[auracast_cfg_info->sample_rate][auracast_cfg_info->variant].frame_len;
    params->fmt.sample_rate = auracast_code_list[auracast_cfg_info->sample_rate][auracast_cfg_info->variant].sample_rate;
    params->fmt.dec_ch_mode = LEA_TX_DEC_OUTPUT_CHANNEL;
    params->latency = auracast_cfg_info->tx_latency;
    params->conn = auracast_source_get_bis_hdl(0);
}

static int auracast_source_media_open()
{
    g_printf("auracast_source_media_open");

    app_auracast_mutex_pend(&mutex, __LINE__);
    le_audio_switch_ops = get_broadcast_audio_sw_ops();
    //关闭本地音频播放
    if (le_audio_switch_ops && le_audio_switch_ops->local_audio_close) {
        le_audio_switch_ops->local_audio_close();
    }

    struct le_audio_stream_params params = {0};
    auracast_source_param_prepare(&params);
    //打开广播音频播放
    if (le_audio_switch_ops && le_audio_switch_ops->tx_le_audio_open) {
        app_auracast.recorder = le_audio_switch_ops->tx_le_audio_open(&params);
        g_printf("auracast_source_tx_le_audio_open:0x%x", (u32)app_auracast.recorder);
    }
    app_auracast_mutex_post(&mutex, __LINE__);
    return 0;
}

static int auracast_source_media_close()
{
    u8 i;
    void *recorder = 0;
    u8 player_status = 0;

    app_auracast_mutex_pend(&mutex, __LINE__);
    //获取当前播放器状态
    r_printf("le_audio_switch_ops:%d\n", (u32)le_audio_switch_ops);
    if (le_audio_switch_ops && le_audio_switch_ops->play_status) {
        player_status = le_audio_switch_ops->play_status();
    }

    if (app_auracast.recorder) {
        recorder = app_auracast.recorder;
        app_auracast.recorder = NULL;
    }

    if (recorder) {
        if (le_audio_switch_ops && le_audio_switch_ops->tx_le_audio_close) {
            le_audio_switch_ops->tx_le_audio_close(recorder);
            recorder = NULL;
            g_printf("auracast_source_media_close");
        }
    }

    //当前处于播放状态，关闭le audio音频流后恢复本地播放
    if (player_status == LOCAL_AUDIO_PLAYER_STATUS_PLAY) {
        if (le_audio_switch_ops && le_audio_switch_ops->local_audio_open) {
            le_audio_switch_ops->local_audio_open();
        }
    }

    app_auracast_mutex_post(&mutex, __LINE__);
    return 0;
}

int auracast_source_close_media_stream(void)
{
    if (app_auracast.role == APP_AURACAST_AS_SOURCE) {
        app_auracast_mutex_pend(&mutex, __LINE__);
        if (app_auracast.recorder) {
            if (le_audio_switch_ops && le_audio_switch_ops->tx_le_audio_close) {
                le_audio_switch_ops->tx_le_audio_close(app_auracast.recorder);
                app_auracast.recorder = NULL;
                g_printf("auracast_source_media_stream_close");
            }
        }
        app_auracast_mutex_post(&mutex, __LINE__);
        return 0;
    }
    return -1;
}

int auracast_source_open_media_stream(void)
{
    if (app_auracast.role == APP_AURACAST_AS_SOURCE) {
        app_auracast_mutex_pend(&mutex, __LINE__);
        struct le_audio_stream_params params = {0};
        auracast_source_param_prepare(&params);
        //打开广播音频播放
        if (le_audio_switch_ops && le_audio_switch_ops->tx_le_audio_open) {
            app_auracast.recorder = le_audio_switch_ops->tx_le_audio_open(&params);
            g_printf("auracast_source_media_stream_open");
            put_buf(get_g_play_addr(), 6);
        }
        app_auracast_mutex_post(&mutex, __LINE__);
        return 0;
    }
    return -1;
}

int auracast_source_media_reset()
{
    auracast_source_media_close();
    auracast_source_media_open();
    return 0;
}

static int auracast_sink_media_open(uint16_t bis_hdl, uint8_t *packet, uint16_t length)
{
    g_printf("auracast_sink_media_open");

    auracast_sink_source_info_t *config = (auracast_sink_source_info_t *)packet;

    le_audio_switch_ops = get_broadcast_audio_sw_ops();
    //关闭本地音频播放
    if (le_audio_switch_ops && le_audio_switch_ops->local_audio_close) {
        le_audio_switch_ops->local_audio_close();
    }

    struct le_audio_stream_params params = {0};
    //默认解码所有bis链路的数据
    params.fmt.nch = AURACAST_RX_CODEC_CHANNEL;
    //解码器最多支持双声道数据解码,超过2条声道的情况,只能选其中一条声道进行解码
    if (params.fmt.nch > 2) {
        params.fmt.nch = 1;
    }
    params.fmt.coding_type = AUDIO_CODING_LC3;
    params.fmt.dec_ch_mode = LEA_RX_DEC_OUTPUT_CHANNEL;

    g_printf("nch:%d, coding_type:0x%x, dec_ch_mode:%d",
             params.fmt.nch, params.fmt.coding_type, params.fmt.dec_ch_mode);

    app_auracast.presentation_delay_us = config->presentation_delay_us;
    printf("presentation_delay_us:%d\n", config->presentation_delay_us);

    if (config->frame_duration == FRAME_DURATION_7_5) {
        params.fmt.frame_dms = 75;
    } else if (config->frame_duration == FRAME_DURATION_10) {
        params.fmt.frame_dms = 100;
    } else {
        ASSERT(0, "frame_dms err:%d", config->frame_duration);
    }
    params.fmt.sdu_period = config->sdu_period;
    params.fmt.isoIntervalUs = g_sink_bn * config->sdu_period;
    params.fmt.sample_rate = config->sample_rate;
    if (AURACAST_RX_CODEC_CHANNEL == 2 && AURACAST_SINK_BIS_NUMS == 1) {
        params.fmt.bit_rate = config->bit_rate;
    } else {
        params.fmt.bit_rate = params.fmt.nch * config->bit_rate;
    }
    params.conn = bis_hdl;

    g_printf("frame_dms:%d, sdu_period:%d, sample_rate:%d, bit_rate:%d",
             params.fmt.frame_dms, config->sdu_period, config->sample_rate, config->bit_rate);

    //打开广播音频播放
    ASSERT(le_audio_switch_ops, "le_audio_sw_ops == NULL\n");
    if (le_audio_switch_ops && le_audio_switch_ops->rx_le_audio_open) {
        g_printf("auracast_sink_rx_le_audio_open");
        le_audio_switch_ops->rx_le_audio_open(&app_auracast.rx_player, &params);
    }

    return 0;
}

static int auracast_sink_media_close()
{
    u8 i;
    u8 player_status = 0;
    struct le_audio_player_hdl player;
    player.le_audio = 0;
    player.rx_stream = 0;

    //获取当前播放器状态
    if (le_audio_switch_ops && le_audio_switch_ops->play_status) {
        player_status = le_audio_switch_ops->play_status();
    }

    if (app_auracast.rx_player.le_audio) {
        player.le_audio = app_auracast.rx_player.le_audio;
        app_auracast.rx_player.le_audio = NULL;
    }

    if (app_auracast.rx_player.rx_stream) {
        player.rx_stream = app_auracast.rx_player.rx_stream;
        app_auracast.rx_player.rx_stream = NULL;
    }

    if (player.le_audio && player.rx_stream) {
        if (le_audio_switch_ops && le_audio_switch_ops->rx_le_audio_close) {
            le_audio_switch_ops->rx_le_audio_close(&player);
            player.le_audio = 0;
            player.rx_stream = 0;
            g_printf("auracast_sink_media_close");
        }
    }

    for (i = 0; i < g_sink_bn; i++) {
        if (multi_bis_rx_buf[i]) {
            free(multi_bis_rx_buf[i]);
            multi_bis_rx_buf[i] = 0;
        }
    }

    //当前处于播放状态，关闭le audio音频流后恢复本地播放
    if (player_status == LOCAL_AUDIO_PLAYER_STATUS_PLAY) {
        if (le_audio_switch_ops && le_audio_switch_ops->local_audio_open) {
            le_audio_switch_ops->local_audio_open();
        }
    }

    return 0;
}

bool are_all_zeros(uint8_t *array, int length)
{
    for (int i = 0; i < length; i++) {
        if (array[i] != 0) {
            return false;
        }
    }
    return true;
}

static void auracast_iso_rx_callback(uint8_t *packet, uint16_t size)
{
    //putchar('o');
    u8 i = 0;
    static u8 j = 0;
    s8 index = -1;
    bool plc_flag = 0;
    hci_iso_hdr_t hdr = {0};
    ll_iso_unpack_hdr(packet, &hdr);

    if (hdr.iso_sdu_length) {
        if (are_all_zeros(hdr.iso_sdu, hdr.iso_sdu_length)) {
            /* log_error("SDU empty"); */
            putchar('z');
            plc_flag = 1;
        }
    }

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

    for (i = 0; i < app_auracast.bis_num; i++) {
        if (app_auracast.bis_hdl_info[i].bis_hdl == hdr.handle) {
            if (!app_auracast.rx_player.rx_stream || !app_auracast.bis_hdl_info[i].init_ok) {
                return;
            }
            index = i;
            break;
        }
    }

    if (index == -1) {
        return;
    }

    j++;
    if (j >= g_sink_bn) {
        j = 0;
    }

    /* printf("[%d][%x]\n",hdr.handle,(u32)app_auracast.rx_player.rx_stream); */
    if (plc_flag || multi_bis_plc_flag[j]) {
        if (multi_bis_rx_buf[j]) {
            multi_bis_plc_flag[j] = 1;
            for (i = 0; i < app_auracast.bis_num; i++) {
                if (app_auracast.bis_hdl_info[i].bis_hdl == hdr.handle) {
                    break;
                }
            }
            if (i == (app_auracast.bis_num - 1)) {
                memcpy(multi_bis_rx_buf[j], errpacket, 2);
                le_audio_stream_rx_frame(app_auracast.rx_player.rx_stream, (void *)multi_bis_rx_buf[j], 2,
                                         hdr.time_stamp + app_auracast.presentation_delay_us);
                multi_bis_data_offect[j] = 0;
                multi_bis_plc_flag[j] = 0;
            }
        } else {
            le_audio_stream_rx_frame(app_auracast.rx_player.rx_stream, (void *)errpacket, 2, hdr.time_stamp + app_auracast.presentation_delay_us);
        }
    } else {
        /* printf("%d 0x%x", j, hdr.handle); */
        if (multi_bis_rx_buf[j]) {
            memcpy(multi_bis_rx_buf[j] + multi_bis_data_offect[j], hdr.iso_sdu, hdr.iso_sdu_length);
            multi_bis_data_offect[j] += hdr.iso_sdu_length;
            ASSERT(multi_bis_data_offect[j] <= multi_bis_rx_temp_buf_len, "offset:%d iso_sdu:%d\n", multi_bis_data_offect[j], hdr.iso_sdu_length);
            if (multi_bis_data_offect[j] >= multi_bis_rx_temp_buf_len) {
                le_audio_stream_rx_frame(app_auracast.rx_player.rx_stream, (void *)multi_bis_rx_buf[j], multi_bis_rx_temp_buf_len, hdr.time_stamp + app_auracast.presentation_delay_us);
                multi_bis_data_offect[j] -= multi_bis_rx_temp_buf_len;
            }
        } else {
            le_audio_stream_rx_frame(app_auracast.rx_player.rx_stream, (void *)hdr.iso_sdu, hdr.iso_sdu_length, hdr.time_stamp + app_auracast.presentation_delay_us);
        }
    }
}









u8 check_local_not_accept_sniff_by_remote()
{
    /* printf("le_auracast_state=%d\n",le_auracast_state ); */
    if (le_audio_player_is_playing() || le_auracast_state == BROADCAST_STATUS_SCAN_START) {
        return TRUE;
    }

    return FALSE;
}




u8 get_auracast_switch_onoff(void)
{
    return auracast_switch_onoff;
}

static int app_aruacast_conn_status_event_handler(int *msg)
{
    uint16_t event = msg[0] >> 16;
    uint16_t length = msg[0] & 0xffff;
    uint8_t *packet = (uint8_t *)(msg + 1);
    switch (event) {
    /*=================Sink Event===============*/
    case AURACAST_SINK_BIG_SYNC_CREATE_EVENT:
        printf("AURACAST_SINK_BIG_SYNC_CREATE_EVENT\n");

        if (cur_listening_source_info == NULL || app_auracast.role == APP_AURACAST_AS_SOURCE) {
            break;
        }
        memcpy(cur_listening_source_info, auracast_sink_listening_source_info_get(), sizeof(auracast_sink_source_info_t));
        /* ASSERT(cur_listening_source_info); */
        if (auracast_sink_sync_timeout_hdl != 0) {
            sys_timeout_del(auracast_sink_sync_timeout_hdl);
            auracast_sink_sync_timeout_hdl = 0;
        }

#if TCFG_AUTO_SHUT_DOWN_TIME
        sys_auto_shut_down_disable();   // 恢复自动关机
#endif
        auracast_sink_sync_create(packet, length);
        break;
    case AURACAST_SINK_BIG_SYNC_FAIL_EVENT:
    case AURACAST_SINK_BIG_SYNC_LOST_EVENT:
        printf("big lost or fail\n");
        if (auracast_sink_sync_timeout_hdl != 0) {
            sys_timeout_del(auracast_sink_sync_timeout_hdl);
            auracast_sink_sync_timeout_hdl = 0;
        }

        auracast_sink_sync_terminate(packet, length);
#if TCFG_AUTO_SHUT_DOWN_TIME
        sys_auto_shut_down_enable();   // 恢复自动关机
#endif
        break;
    case AURACAST_SINK_PERIODIC_ADVERTISING_SYNC_LOST_EVENT:
        if (cur_listening_source_info) {
            printf("AURACAST_SINK_PERIODIC_ADVERTISING_SYNC_LOST_EVENT\n");
            auracast_sink_big_sync_create(cur_listening_source_info);
        } else {
            printf("AURACAST_SINK_PERIODIC_ADVERTISING_SYNC_LOST_EVENT FAIL!\n");
        }
        break;

    /*=================Source Event===============*/
    case AURACAST_SOURCE_BIG_CREATED:
        g_printf("AURACAST_SOURCE_BIG_CREATED\n");
        if (app_auracast.bis_num == 0 && app_auracast.role == APP_AURACAST_AS_SOURCE) {
#if TCFG_AURACAST_PAWR_ENABLE
            app_speaker_start_pawr();
#endif
            auracast_source_create(packet, length);
        } else {
            printf("AURACAST ROLE ERR!\n");
        }
        break;

    default:
        printf("auracast unknow event %x\n", event);
        break;
    }
    return 0;
}

APP_MSG_PROB_HANDLER(app_le_auracast_msg_entry) = {
    .owner = 0xff,
    .from = MSG_FROM_AURACAST,
    .handler = app_aruacast_conn_status_event_handler,
};

#endif






