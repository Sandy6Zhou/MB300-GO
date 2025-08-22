#ifndef TWS_A2DP_PLAYER_H
#define TWS_A2DP_PLAYER_H

#if(TCFG_USER_TWS_ENABLE)

enum {
    CMD_A2DP_PLAY = 1,
    CMD_A2DP_SLIENCE_DETECT,
    CMD_A2DP_CLOSE,
    CMD_SET_A2DP_VOL,
    CMD_A2DP_PLAY_REQ,
    CMD_A2DP_PLAY_RSP,
};



void tws_a2dp_play_send_cmd(u8 cmd, u8 *_data, u8 len, u8 tx_do_action);
void tws_a2dp_player_close(u8 *bt_addr);

u8 app_get_a2dp_play_status(void);


#endif
#endif

