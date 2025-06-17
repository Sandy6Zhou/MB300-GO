/*********************************************************************************************
    *   Filename        : le_broadcast_config.c

    *   Description     :

    *   Author          : Weixin Liang

    *   Email           : liangweixin@zh-jieli.com

    *   Last modifiled  : 2022-12-15 14:17

    *   Copyright:(c)JIELI  2011-2022  @ , All Rights Reserved.
*********************************************************************************************/
#include "app_config.h"
#include "app_main.h"
#include "audio_base.h"
#include "le_broadcast.h"
#include "wireless_trans.h"
#include "classic/tws_api.h"

#if (TCFG_LE_AUDIO_APP_CONFIG & (LE_AUDIO_JL_BIS_TX_EN | LE_AUDIO_JL_BIS_RX_EN))

/**************************************************************************************************
  Macros
**************************************************************************************************/
/*! \brief Broadcast Code */
#define BIG_BROADCAST_CODE      "JL_BROADCAST"

/*! \配置广播通道数，不同通道可发送不同数据，例如多声道音频  */
#define TX_USED_BIS_NUM                 1
#if TCFG_KBOX_1T3_MODE_EN
#define RX_USED_BIS_NUM                 2
#else
#define RX_USED_BIS_NUM                 1
#endif

#define SCAN_WINDOW_SLOT                10
#define SCAN_INTERVAL_SLOT              (28*2)
#define PRIMARY_ADV_INTERVAL_SLOT       (192)

/**************************************************************************************************
  Data Types
**************************************************************************************************/

/**************************************************************************************************
  Local Global Variables
**************************************************************************************************/
static big_parameter_t big_tx_param = {
    .cb        = &big_tx_cb,
    .num_bis   = TX_USED_BIS_NUM,
    .ext_phy   = 1,
    .enc       = 0,
    .bc        = BIG_BROADCAST_CODE,
    .form      = 1,

    .tx = {
        .phy            = BIT(1),
        .aux_phy        = 2,
        .eadv_int_slot  = PRIMARY_ADV_INTERVAL_SLOT,
        .padv_int_slot  = PRIMARY_ADV_INTERVAL_SLOT,
        .vdr = {
#if LEA_DUAL_STREAM_MERGE_TRANS_MODE //环绕音
            .tx_delay   = 300,
#else
            .tx_delay   = 1000,
#endif
        },
    },
};

static big_parameter_t big_rx_param = {
    .num_bis   = RX_USED_BIS_NUM,
    .cb        = &big_rx_cb,
    .ext_phy   = 1,
    .enc       = 0,
    .bc        = BIG_BROADCAST_CODE,

#if TCFG_KBOX_1T3_MODE_EN
    .form      = 2,
    .bst = {
        .sync_to_ms     = 1000,
    },
#else
    .rx = {
        .ext_scan_int = SCAN_INTERVAL_SLOT,
        .ext_scan_win = SCAN_WINDOW_SLOT,
        .psync_to_ms    = 2500,
        .bis            = {1},
        .bsync_to_ms    = 2000,
#if LEA_BIG_CUSTOM_DATA_EN
        .psync_keep     = 1,
#endif
    },
#endif
};

static big_parameter_t *tx_params = NULL;
static big_parameter_t *rx_params = NULL;
static u16 big_transmit_data_len = 0;
static u32 enc_output_frame_len = 0;
#if LEA_DUAL_STREAM_MERGE_TRANS_MODE //环绕音
static u32 enc_dual_output_frame_len = 0;	//立体声
static u32 enc_mono_output_frame_len = 0;	//单声道
#endif
static u32 dec_input_buf_len = 0;
static u32 enc_output_buf_len = 0;
static u8 platform_data_index = 0;
static u8 broadcast_num_br = 0;
static struct broadcast_platform_data platform_data;
const static u8 platform_data_mapping[] = {
    APP_MODE_BT,
    APP_MODE_MUSIC,
    APP_MODE_LINEIN,
    APP_MODE_PC,
    APP_MODE_IIS,
    APP_MODE_MIC,
#ifndef CONFIG_CPU_BR29
    APP_MODE_SPDIF,
#endif
    APP_MODE_FM,
    APP_MODE_SURROUND_SOUND,
    LE_AUDIO_1T3_MODE,
};

/**************************************************************************************************
  Function Declarations
**************************************************************************************************/

/*! \brief 每包编码数据长度 */
/* (int)((LE_AUDIO_CODEC_FRAME_LEN / 10) * (JLA_CODING_BIT_RATE / 1000 / 8) + 2) */
/* 如果码率超过96K,即帧长超过122,就需要将每次传输数据大小 修改为一帧编码长度 */
//参数 code_type 如果为0的话，就用默认的，如果为其它值，则工具对应的code_type 去进行计算
//目前只有 Surround Sound 功能会用到最后的一个传参
static u32 calcul_big_enc_output_frame_len(u16 frame_len, u32 bit_rate, u32 code_type)
{
    int len = 0;
    if (code_type == 0) {
        //code_type 为0, 用默认参数
#if (LE_AUDIO_CODEC_TYPE == AUDIO_CODING_JLA || LE_AUDIO_CODEC_TYPE == AUDIO_CODING_JLA_V2)
        len = (frame_len * bit_rate / 1000 / 8 / 10 + 2);
#if JL_CC_CODED_EN
        len += (len & 1); //开fec 编码时会微调码率，使编出来一帧的数据长度是偶数,故如果计算出来是奇数需要+1;
#endif/*JL_CC_CODED_EN*/
#elif (LE_AUDIO_CODEC_TYPE == AUDIO_CODING_JLA_LL)
        len = jla_ll_enc_frame_len();
#elif(LE_AUDIO_CODEC_TYPE == AUDIO_CODING_JLA_LW)
        len = (frame_len * bit_rate / 1000 / 8 / 10);
#endif// WIERLESS_TRANS_CODING_TYPE == LIVE_AUDIO_CODING_JLA
    } else {
        switch (code_type) {
        case AUDIO_CODING_JLA:
        case AUDIO_CODING_JLA_V2:
            len = (frame_len * bit_rate / 1000 / 8 / 10 + 2);
            break;
        case AUDIO_CODING_JLA_LL:
#if ((LE_AUDIO_CODEC_TYPE == AUDIO_CODING_JLA_LL)|| (SURROUND_SOUND_DUAL_CODEC_TYPE == AUDIO_CODING_JLA_LL) || (SURROUND_SOUND_MONO_CODEC_TYPE == AUDIO_CODING_JLA_LL))
            len = jla_ll_enc_frame_len();
#endif
            break;
        case AUDIO_CODING_JLA_LW:
#if ((LE_AUDIO_CODEC_TYPE == AUDIO_CODING_JLA_LW) || (SURROUND_SOUND_DUAL_CODEC_TYPE == AUDIO_CODING_JLA_LW) || (SURROUND_SOUND_MONO_CODEC_TYPE == AUDIO_CODING_JLA_LW))
            len = (frame_len * bit_rate / 1000 / 8 / 10);
#endif
            break;
        }
    }

    return len;
}

u32 get_big_enc_output_frame_len(void)
{
    ASSERT(enc_output_frame_len, "enc_output_frame_len is 0");
    return enc_output_frame_len;
}

static u16 calcul_big_transmit_data_len(u32 encode_output_frame_len, u16 period, u16 codec_frame_len)
{
    return (encode_output_frame_len * (period * 10 / 1000 / codec_frame_len));
}

u16 get_big_transmit_data_len(void)
{
#if LEA_DUAL_STREAM_MERGE_TRANS_MODE
    //环绕声广播
    ASSERT(enc_output_frame_len, "enc_output_frame_len is 0");
    return enc_output_frame_len;
#endif
    ASSERT(big_transmit_data_len, "big_transmit_data_len is 0");
    return big_transmit_data_len;
}

static u32 calcul_big_enc_output_buf_len(u32 transmit_data_len)
{
    return (transmit_data_len * 2);
}

u32 get_big_enc_output_buf_len(void)
{
    ASSERT(enc_output_buf_len, "enc_output_buf_len is 0");
    return enc_output_buf_len;
}

static u32 calcul_big_dec_input_buf_len(u32 transmit_data_len)
{
    return (transmit_data_len * 10);
}

u32 get_big_dec_input_buf_len(void)
{
    ASSERT(dec_input_buf_len, "dec_input_buf_len is 0");
    return dec_input_buf_len;
}

u32 get_big_sdu_period_us(void)
{
    return platform_data.args[platform_data_index].iso_interval / platform_data.frame_len / 100;
}

u32 get_big_iso_period_us(void)
{
    return platform_data.args[platform_data_index].iso_interval;
}

u32 get_big_tx_latency(void)
{
    return platform_data.args[platform_data_index].tx_latency;
}

u32 get_big_play_latency(void)
{
    return platform_data.args[platform_data_index].play_latency;
}


u32 get_big_mtl_time(void)
{
    return tx_params->tx.mtl;
}

u8 get_bis_num(u8 role)
{
    u8 num = 0;
    if ((role == BROADCAST_ROLE_TRANSMITTER) && tx_params) {
        num = tx_params->num_bis;
    } else if ((role == BROADCAST_ROLE_RECEIVER) && rx_params) {
        num = rx_params->num_bis;
    }
    return num;
}

void set_big_hdl(u8 role, u8 big_hdl)
{
    if ((role == BROADCAST_ROLE_TRANSMITTER) && tx_params) {
        tx_params->big_hdl = big_hdl;
    } else if ((role == BROADCAST_ROLE_RECEIVER) && rx_params) {
        rx_params->big_hdl = big_hdl;
    }
}

#if LEA_DUAL_STREAM_MERGE_TRANS_MODE
//环绕声项目两个解码器参数
//立体声
int get_dual_big_audio_coding_nch(void)
{
    return platform_data.dual_nch;
}

int get_dual_big_audio_coding_bit_rate(void)
{
    return platform_data.dual_bit_rate;
}

int get_dual_big_audio_coding_frame_duration(void)
{
    return platform_data.dual_frame_len;
}

//单声道
int get_mono_big_audio_coding_nch(void)
{
    return platform_data.mono_nch;
}

int get_mono_big_audio_coding_bit_rate(void)
{
    return platform_data.mono_bit_rate;
}

int get_mono_big_audio_coding_frame_duration(void)
{
    return platform_data.mono_frame_len;
}
#endif

int get_big_audio_coding_nch(void)
{
    return platform_data.nch;
}

int get_big_audio_coding_bit_rate(void)
{
    return platform_data.args[platform_data_index].bitrate;
}

int get_big_audio_coding_frame_duration(void)
{
    return platform_data.frame_len;
}


int get_big_tx_rtn(void)
{
    if (tx_params) {
        return tx_params->tx.rtn;
    }

    return 0;
}

int get_big_tx_delay(void)
{
    if (tx_params) {
        return tx_params->tx.vdr.tx_delay;
    }

    return 0;
}

void update_receiver_big_codec_params(void *sync_data)
{
    struct broadcast_sync_info *data_sync = (struct broadcast_sync_info *)sync_data;

    platform_data.args[platform_data_index].bitrate = data_sync->bit_rate;
    platform_data.sample_rate = data_sync->sample_rate;
    enc_output_frame_len = calcul_big_enc_output_frame_len(platform_data.frame_len, platform_data.args[platform_data_index].bitrate, 0);
    big_transmit_data_len = calcul_big_transmit_data_len(enc_output_frame_len, platform_data.args[platform_data_index].iso_interval, platform_data.frame_len);
    dec_input_buf_len = calcul_big_dec_input_buf_len(big_transmit_data_len);
}

/* 更新接收端的一些参数：数据包长度(PDU)、发包间隔(iso_Interval) */
void update_receiver_big_params(uint16_t Max_PDU, uint16_t iso_Interval)
{
    big_transmit_data_len = Max_PDU;
    platform_data.args[platform_data_index].iso_interval = iso_Interval;
}

static const struct broadcast_platform_data *get_broadcast_platform_data(u8 mode)
{
    u8 find = 0;
    int len = syscfg_read(CFG_BIG_PARAMS, platform_data.args, sizeof(platform_data.args));

    if (len <= 0) {
        r_printf("ERR:Can not read the broadcast config\n");
        return NULL;
    }

    if (mode == APP_MODE_SINK) {
        mode = APP_MODE_BT;
    }

    for (platform_data_index = 0; platform_data_index < ARRAY_SIZE(platform_data_mapping); platform_data_index++) {
        if (mode == platform_data_mapping[platform_data_index]) {
            find = 1;
            g_printf("mode:%d, platform_data_index:%d, platform_data_mapping[platform_data_index]:%d\n", mode, platform_data_index, platform_data_mapping[platform_data_index]);
            break;
        } else {
            r_printf("mode:%d, platform_data_index:%d, platform_data_mapping[platform_data_index]:%d\n", mode, platform_data_index, platform_data_mapping[platform_data_index]);
        }
    }

    if (!find) {
        r_printf("ERR:Can not find the broadcast config\n");
        return NULL;
    }

#if LEA_DUAL_STREAM_MERGE_TRANS_MODE
    //环绕声项目需要两个解码器参数
    //立体声解码器
    platform_data.dual_nch = SURROUND_SOUND_DUAL_CODEC_CHANNEL;
    platform_data.dual_frame_len = SURROUND_SOUND_DUAL_CODEC_FRAME_LEN;
    platform_data.dual_sample_rate = SURROUND_SOUND_DUAL_CODEC_SAMPLERATE;
    platform_data.dual_coding_type = SURROUND_SOUND_DUAL_CODEC_TYPE;
    platform_data.dual_bit_rate = SURROUND_SOUND_DUAL_BIT_RATE;

    //单声道解码器
    platform_data.mono_nch = SURROUND_SOUND_MONO_CODEC_CHANNEL;
    platform_data.mono_frame_len = SURROUND_SOUND_MONO_CODEC_FRAME_LEN;
    platform_data.mono_sample_rate = SURROUND_SOUND_MONO_CODEC_SAMPLERATE;
    platform_data.mono_coding_type = SURROUND_SOUND_MONO_CODEC_TYPE;
    platform_data.mono_bit_rate = SURROUND_SOUND_MONO_BIT_RATE;

    if (platform_data.mono_frame_len != platform_data.dual_frame_len) {
        ASSERT(0, "err, mono_frame_len != dual_frame_len!\n");
    }
    platform_data.frame_len = platform_data.dual_frame_len;

#else
    //普通广播需要的单个解码器参数
    platform_data.nch = LE_AUDIO_CODEC_CHANNEL;
    platform_data.frame_len = LE_AUDIO_CODEC_FRAME_LEN;
    platform_data.sample_rate = LE_AUDIO_CODEC_SAMPLERATE;
    platform_data.coding_type = LE_AUDIO_CODEC_TYPE;

#if (LE_AUDIO_CODEC_TYPE == AUDIO_CODING_JLA_LL)
    platform_data.args[platform_data_index].bitrate = (jla_ll_enc_frame_len() - 3) * 8 * 10 * 1000 / LE_AUDIO_CODEC_FRAME_LEN;
#endif
#endif

    put_buf((const u8 *) & (platform_data.args[platform_data_index]), sizeof(struct broadcast_platform_data));
    g_printf("iso_interval:%d", platform_data.args[platform_data_index].iso_interval);
    g_printf("tx_latency:%d\n", platform_data.args[platform_data_index].tx_latency);
    g_printf("play_latency:%d\n", platform_data.args[platform_data_index].play_latency);
    g_printf("rtnCToP:%d", platform_data.args[platform_data_index].rtn);
    g_printf("mtlCToP:%d", platform_data.args[platform_data_index].mtl);
#if LEA_DUAL_STREAM_MERGE_TRANS_MODE		//环绕声项目两个解码器
    g_printf("dual_nch:%d", platform_data.dual_nch);
    g_printf("dual_frame_len:%d", platform_data.dual_frame_len);
    g_printf("dual_sample_rate:%d", platform_data.dual_sample_rate);
    g_printf("dual_coding_type:0x%x", platform_data.dual_coding_type);
    g_printf("dual_bit_rate:%d", platform_data.dual_bit_rate);
    g_printf("mono_nch:%d", platform_data.mono_nch);
    g_printf("mono_frame_len:%d", platform_data.mono_frame_len);
    g_printf("mono_sample_rate:%d", platform_data.mono_sample_rate);
    g_printf("mono_coding_type:0x%x", platform_data.mono_coding_type);
    g_printf("mono_bit_rate:%d", platform_data.mono_bit_rate);
    g_printf("frame_len:%d", platform_data.frame_len);
#else
    g_printf("bitrate:%d", platform_data.args[platform_data_index].bitrate);
    g_printf("nch:%d", platform_data.nch);
    g_printf("frame_len:%d", platform_data.frame_len);
    g_printf("sample_rate:%d", platform_data.sample_rate);
    g_printf("coding_type:0x%x", platform_data.coding_type);
#endif

    return &platform_data;
}

big_parameter_t *set_big_params(u8 app_task, u8 role, u8 big_hdl)
{
    u32 pair_code;
    int ret;

#if TCFG_KBOX_1T3_MODE_EN
    app_task = LE_AUDIO_1T3_MODE;
#endif

    const struct broadcast_platform_data *data = get_broadcast_platform_data(app_task);

    if (role == BROADCAST_ROLE_TRANSMITTER) {
        tx_params = &big_tx_param;
        memcpy(tx_params->pair_name, get_le_audio_pair_name(), sizeof(tx_params->pair_name));
#if LEA_DUAL_STREAM_MERGE_TRANS_MODE
        enc_dual_output_frame_len = calcul_big_enc_output_frame_len(data->dual_frame_len, data->dual_bit_rate, data->dual_coding_type);
        enc_mono_output_frame_len = calcul_big_enc_output_frame_len(data->mono_frame_len, data->mono_bit_rate, data->mono_coding_type);
        enc_output_frame_len = enc_dual_output_frame_len + enc_mono_output_frame_len;
#else
        //普通广播
        enc_output_frame_len = calcul_big_enc_output_frame_len(data->frame_len, data->args[platform_data_index].bitrate, 0);
#endif
        big_transmit_data_len = calcul_big_transmit_data_len(enc_output_frame_len, data->args[platform_data_index].iso_interval, data->frame_len);
        dec_input_buf_len = calcul_big_dec_input_buf_len(big_transmit_data_len);
        enc_output_buf_len = calcul_big_enc_output_buf_len(big_transmit_data_len);
        if (tx_params) {
            tx_params->big_hdl = big_hdl;
            tx_params->tx.mtl = data->args[platform_data_index].iso_interval * data->args[platform_data_index].mtl;
            tx_params->tx.rtn = data->args[platform_data_index].rtn - 1;
            tx_params->tx.sdu_int_us = data->args[platform_data_index].iso_interval;
            if (tx_params->tx.sdu_int_us < 10000) {
                tx_params->form = 1;
            } else {
                tx_params->form = 0;
            }

#if LEA_DUAL_STREAM_MERGE_TRANS_MODE
            //环绕声项目
            tx_params->tx.max_sdu = big_transmit_data_len;
#else
            //普通广播，声道分离
            if (data->nch == 2) {
#if (LEA_TX_CHANNEL_SEPARATION && (LE_AUDIO_CODEC_CHANNEL == 2))
                u8 packet_num;
                u16 single_ch_trans_data_len;
                packet_num = big_transmit_data_len / enc_output_frame_len;
                single_ch_trans_data_len = big_transmit_data_len / 2 + packet_num;
                tx_params->tx.max_sdu = single_ch_trans_data_len;
#else
                tx_params->tx.max_sdu = big_transmit_data_len;
#endif
            } else {
                tx_params->tx.max_sdu = big_transmit_data_len;
            }
#endif

            if (big_transmit_data_len > 251) {
                tx_params->tx.vdr.max_pdu = enc_output_frame_len;
            }
        }
        ret = syscfg_read(VM_WIRELESS_PAIR_CODE0, &pair_code, sizeof(u32));
        if (ret <= 0) {
            pair_code = 0;
        }
        tx_params->pri_ch = pair_code;
        g_printf("wireless_pair_code:0x%x", pair_code);
        return tx_params;
    }

    if (role == BROADCAST_ROLE_RECEIVER) {
        rx_params = &big_rx_param;
        memcpy(rx_params->pair_name, get_le_audio_pair_name(), sizeof(rx_params->pair_name));
#if LEA_DUAL_STREAM_MERGE_TRANS_MODE
        //环绕声项目
        enc_dual_output_frame_len = calcul_big_enc_output_frame_len(data->dual_frame_len, data->dual_bit_rate, data->dual_coding_type);
        enc_mono_output_frame_len = calcul_big_enc_output_frame_len(data->mono_frame_len, data->mono_bit_rate, data->mono_coding_type);
        enc_output_frame_len = enc_dual_output_frame_len + enc_mono_output_frame_len;
#else
        enc_output_frame_len = calcul_big_enc_output_frame_len(data->frame_len, data->args[platform_data_index].bitrate, 0);
#endif
        big_transmit_data_len = calcul_big_transmit_data_len(enc_output_frame_len, data->args[platform_data_index].iso_interval, data->frame_len);
        dec_input_buf_len = calcul_big_dec_input_buf_len(big_transmit_data_len);
        rx_params->big_hdl = big_hdl;

#if TCFG_KBOX_1T3_MODE_EN

#if (TCFG_USER_TWS_ENABLE && TCFG_APP_BT_EN)
        if (tws_api_get_tws_state() & TWS_STA_SIBLING_CONNECTED) {
            rx_params->num_bis   = 1;
            //r_printf("rx.num_bis %d\n",rx_params->num_bis);
        } else {

            rx_params->num_bis   = RX_USED_BIS_NUM;
            //r_printf("rx.num_bis %d\n",RX_USED_BIS_NUM);
        }
#endif

        rx_params->bst.rtn = data->args[platform_data_index].rtn - 1;
        rx_params->bst.enc.max_sdu = big_transmit_data_len;
        rx_params->bst.enc.sdu_int_us = data->args[platform_data_index].iso_interval;
        if (data->coding_type == AUDIO_CODING_JLA) {
            rx_params->bst.enc.format = WIRELESS_TRANS_CODEC_JLA;
        } else if (data->coding_type == AUDIO_CODING_JLA_LW) {
            rx_params->bst.enc.format = WIRELESS_TRANS_CODEC_JLA_LW;
        } else if (data->coding_type == AUDIO_CODING_JLA_LL) {
            rx_params->bst.enc.format = WIRELESS_TRANS_CODEC_JLA_LL;
        }

        if (data->sample_rate == 32000) {
            rx_params->bst.enc.sr = WIRELESS_TRANS_32K_SR;
        } else if (data->sample_rate == 48000) {
            rx_params->bst.enc.sr = WIRELESS_TRANS_48K_SR;
        }
#endif

        ret = syscfg_read(VM_WIRELESS_PAIR_CODE0, &pair_code, sizeof(u32));
        if (ret <= 0) {
            pair_code = 0;
        }
        rx_params->pri_ch = pair_code;
        g_printf("wireless_pair_code:0x%x", pair_code);
        return rx_params;
    }

    return NULL;
}

#if LEA_DUAL_STREAM_MERGE_TRANS_MODE //环绕音

u32 get_enc_dual_output_frame_len(void)
{
    return enc_dual_output_frame_len;
}
u32 get_enc_mono_output_frame_len(void)
{
    return enc_mono_output_frame_len;
}
#endif

#endif

