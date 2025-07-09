#ifndef __DUAL_CONN_H__
#define __DUAL_CONN_H__

extern bool check_page_mode_active(void);

#if TCFG_USER_TWS_ENABLE
extern void tws_dual_conn_close();
extern void tws_delete_pair_timer(void);
#else
extern void dual_conn_close();
#endif
extern void dual_conn_state_handler();

extern void dual_conn_page_device();

void clr_page_mode_active(void);
void dual_conn_user_bt_connect(u8 *addr);

#endif
