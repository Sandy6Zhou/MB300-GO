#include "duer_common.h"

#if INTELLIGENT_DUER
char *duer_start_frame(const char *appid, const char *appkey, const char *client_id, const char *pid,
                       const char *cuid, const char *format, int support_dcs, int sample,
                       const char *user_id, const char *access_token, int support_tts,
                       int support_text2dcs, const char *dialog_request_id, int access_rc,
                       const char *rc_version)
{
    my_cJSON *root = my_cJSON_CreateObject();
    my_cJSON_AddItemToObject(root, "type", my_cJSON_CreateString("start"));
    my_cJSON *data = my_cJSON_CreateObject();
    my_cJSON_AddItemToObject(root, "data", data);
    my_cJSON_AddItemToObject(data, "appid", my_cJSON_CreateString(appid));
    my_cJSON_AddItemToObject(data, "appkey", my_cJSON_CreateString(appkey));
    my_cJSON_AddItemToObject(data, "client_id", my_cJSON_CreateString(client_id));
    my_cJSON_AddItemToObject(data, "pid", my_cJSON_CreateString(pid));
    my_cJSON_AddItemToObject(data, "cuid", my_cJSON_CreateString(cuid));
    my_cJSON_AddItemToObject(data, "format", my_cJSON_CreateString(format));
    my_cJSON_AddItemToObject(data, "support_dcs", my_cJSON_CreateNumber(support_dcs));
    my_cJSON_AddItemToObject(data, "sample", my_cJSON_CreateNumber(sample));
    my_cJSON_AddItemToObject(data, "user_id", my_cJSON_CreateString(user_id));
    my_cJSON_AddItemToObject(data, "access_token", my_cJSON_CreateString(access_token));
    my_cJSON_AddItemToObject(data, "support_tts", my_cJSON_CreateBool(support_tts));
    my_cJSON_AddItemToObject(data, "support_text2dcs", my_cJSON_CreateBool(support_text2dcs));
    my_cJSON_AddItemToObject(data, "dialog_request_id", my_cJSON_CreateString(dialog_request_id));
    my_cJSON_AddItemToObject(data, "access_rc", my_cJSON_CreateBool(access_rc));
    my_cJSON_AddItemToObject(data, "rc_version", my_cJSON_CreateString(rc_version));
    char *json_string = my_cJSON_PrintUnformatted(root);
    my_cJSON_Delete(root);
    return json_string;
}



char *build_finish_frame()
{
    my_cJSON *root = my_cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    my_cJSON_AddStringToObject(root, "type", "finish");
    char *json_str = my_cJSON_PrintUnformatted(root);
    my_cJSON_Delete(root);
    return json_str;
}

char *duer_text_info_frame(const char *CUID,
                           const char *current_dialog_request_id,
                           const char *text_query)
{
    my_cJSON *root = my_cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    my_cJSON *event = my_cJSON_CreateObject();
    my_cJSON *header = my_cJSON_CreateObject();
    my_cJSON *payload = my_cJSON_CreateObject();
    my_cJSON *initiator = my_cJSON_CreateObject();
    if (!event || !header || !payload || !initiator) {
        my_cJSON_Delete(root);
        if (event) {
            my_cJSON_Delete(event);
        }
        if (header) {
            my_cJSON_Delete(header);
        }
        if (payload) {
            my_cJSON_Delete(payload);
        }
        if (initiator) {
            my_cJSON_Delete(initiator);
        }
        return NULL;
    }
    my_cJSON_AddStringToObject(root, "status", "ok");
    my_cJSON_AddStringToObject(root, "type", "dcs_decide");
    my_cJSON_AddStringToObject(root, "sn", CUID ? CUID : "");
    my_cJSON_AddNumberToObject(root, "end", 1);
    my_cJSON_AddItemToObject(root, "event", event);
    my_cJSON_AddItemToObject(event, "header", header);
    my_cJSON_AddItemToObject(event, "payload", payload);
    my_cJSON_AddStringToObject(header, "namespace", "ai.dueros.device_interface.screen");
    my_cJSON_AddStringToObject(header, "name", "LinkClicked");
    my_cJSON_AddStringToObject(header, "dialogRequestId", current_dialog_request_id ? current_dialog_request_id : "");
    char url[1024];
    if (text_query) {
        snprintf(url, sizeof(url), "dueros://server.dueros.ai/query?q=%s", text_query);
    } else {
        snprintf(url, sizeof(url), "dueros://server.dueros.ai/query");
    }
    my_cJSON_AddStringToObject(payload, "url", url);
    my_cJSON_AddItemToObject(payload, "initiator", initiator);
    my_cJSON_AddStringToObject(initiator, "type", "USER_CLICK");
    char *json_str = my_cJSON_Print(root);
    my_cJSON_Delete(root);
    return json_str;
}


#endif
