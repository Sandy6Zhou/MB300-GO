/*************************************************************************************************
 * Copyright  : 深圳市几米物联有限公司
 * File Name  : my_shell.h
 * Brief      : 设备平台APP AT CMD处理模块头文件
 * Version    : V1.0.0
 * Author     : 黄俊凯(huangjunkai@jimiiot.com)
 * Date       : 2025-12-12
************************************************************************************************/

#ifndef MY_SHELL_H
#define MY_SHELL_H

#define MAX_CMD_LEN     (256 + 128)
#define MAX_ARGS        10

#define CMD_MATCHED(data,header)  (strncmp(data,header,strlen(header))==0)
#define CMD_MATCHED2(data,header)  (strncasecmp(data,header,strlen(header))==0)
#define CMD_EQUAL(data,header)  (strcmp(data,header)==0)
#define CMD_EQUAL2(data,header)  (strcasecmp(data,header)==0)

typedef int (*cmdproc)(int argc, char *argv[]);
typedef struct {
    bool bSee;   // 是否可见
    char *cmd;
    char *hlp;
    cmdproc proc;
}CMD_STRUC;

extern const uint8 *bt_get_mac_addr(void);
extern void bt_update_mac_addr(uint8 *addr);
extern int bt_modify_mac(u8 *new_mac);

int sh_at_test(int argc, char *argv[]);

uint8 my_factory_test_mode_get(void);

void my_parse_cmd_line(char *cmdline, char flag, int *argc, char **argv);

void my_shell_uart_init(void);

void my_shell_task(void *p_arg);

#endif