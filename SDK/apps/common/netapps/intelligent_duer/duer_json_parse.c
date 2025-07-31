#include "duer_common.h"

#if INTELLIGENT_DUER

TokenData *duer_parse_token_json(const char *json_str)
{
    my_cJSON *root = my_cJSON_Parse(json_str);
    if (!root) {
        MY_LOG_E("JSON parse error:\n");
        return NULL;
    }
    TokenData *data = (TokenData *)my_malloc(sizeof(TokenData));
    if (!data) {
        MY_LOG_E("Memory allocation failed");
        my_cJSON_Delete(root);
        return NULL;
    }
    memset(data, 0, sizeof(TokenData));
    my_cJSON *item;
    if ((item = my_cJSON_GetObjectItem(root, "refresh_token")) && my_cJSON_IsString(item)) {
        data->refresh_token = strdup(item->valuestring);
    }
    if ((item = my_cJSON_GetObjectItem(root, "expires_in")) && my_cJSON_IsNumber(item)) {
        data->expires_in = item->valueint;
    }
    if ((item = my_cJSON_GetObjectItem(root, "session_key")) && my_cJSON_IsString(item)) {
        data->session_key = strdup(item->valuestring);
    }
    if ((item = my_cJSON_GetObjectItem(root, "access_token")) && my_cJSON_IsString(item)) {
        data->access_token = strdup(item->valuestring);
    }
    if ((item = my_cJSON_GetObjectItem(root, "scope")) && my_cJSON_IsString(item)) {
        data->scope = strdup(item->valuestring);
    }
    if ((item = my_cJSON_GetObjectItem(root, "session_secret")) && my_cJSON_IsString(item)) {
        data->session_secret = strdup(item->valuestring);
    }
    my_cJSON_Delete(root);
    return data;
}


void duer_free_token_data(TokenData *data)
{
    if (data) {
        my_free(data->refresh_token);
        my_free(data->session_key);
        my_free(data->access_token);
        my_free(data->scope);
        my_free(data->session_secret);
        my_free(data);
    }
}


InsideRCResponse *duer_parse_inside_rc_json(const char *json_string)
{
    InsideRCResponse *response = my_calloc(1, sizeof(InsideRCResponse));
    if (!response) {
        return NULL;
    }

    my_cJSON *root = my_cJSON_Parse(json_string);
    if (!root) {
        MY_LOG_E("Error parsing JSON: %s\n", my_cJSON_GetErrorPtr());
        my_free(response);
        return NULL;
    }

    my_cJSON *type = my_cJSON_GetObjectItem(root, "type");
    if (my_cJSON_IsString(type)) {
        response->type = strdup(type->valuestring);
    }

    if (!response->type || strcmp(response->type, "inside_rc") != 0) {
        my_cJSON_Delete(root);
        return response;
    }

    my_cJSON *status = my_cJSON_GetObjectItem(root, "status");
    if (my_cJSON_IsString(status)) {
        response->status = strdup(status->valuestring);
    }

    my_cJSON *sn = my_cJSON_GetObjectItem(root, "sn");
    if (my_cJSON_IsString(sn)) {
        response->sn = strdup(sn->valuestring);
    }

    my_cJSON *end = my_cJSON_GetObjectItem(root, "end");
    if (my_cJSON_IsNumber(end)) {
        response->end = end->valueint;
    }

    my_cJSON *data_obj = my_cJSON_GetObjectItem(root, "data");
    if (data_obj && my_cJSON_IsObject(data_obj)) {
        response->data = my_calloc(1, sizeof(Data));
        if (!response->data) {
            my_cJSON_Delete(root);
            duer_free_inside_rc_response(response);
            return NULL;
        }

        my_cJSON *code = my_cJSON_GetObjectItem(data_obj, "code");
        if (my_cJSON_IsNumber(code)) {
            response->data->code = code->valueint;
        }

        my_cJSON *msg = my_cJSON_GetObjectItem(data_obj, "msg");
        if (my_cJSON_IsString(msg)) {
            response->data->msg = strdup(msg->valuestring);
        }

        my_cJSON *logid = my_cJSON_GetObjectItem(data_obj, "logid");
        if (my_cJSON_IsString(logid)) {
            response->data->logid = strdup(logid->valuestring);
        }

        my_cJSON *qid = my_cJSON_GetObjectItem(data_obj, "qid");
        if (my_cJSON_IsString(qid)) {
            response->data->qid = strdup(qid->valuestring);
        }

        my_cJSON *is_end = my_cJSON_GetObjectItem(data_obj, "is_end");
        if (my_cJSON_IsNumber(is_end)) {
            response->data->is_end = is_end->valueint;
        }

        my_cJSON *need_clear_history = my_cJSON_GetObjectItem(data_obj, "need_clear_history");
        if (my_cJSON_IsNumber(need_clear_history)) {
            response->data->need_clear_history = need_clear_history->valueint;
        }

        my_cJSON *assistant_answer = my_cJSON_GetObjectItem(data_obj, "assistant_answer");
        if (assistant_answer) {
            response->data->assistant_answer = my_calloc(1, sizeof(AssistantAnswer));
            if (response->data->assistant_answer) {
                if (my_cJSON_IsString(assistant_answer)) {
                    my_cJSON *aa_root = my_cJSON_Parse(assistant_answer->valuestring);
                    if (aa_root) {
                        my_cJSON *content = my_cJSON_GetObjectItem(aa_root, "content");
                        if (my_cJSON_IsString(content)) {
                            response->data->assistant_answer->content = strdup(content->valuestring);
                        }

                        my_cJSON *nlu = my_cJSON_GetObjectItem(aa_root, "nlu");
                        if (my_cJSON_IsString(nlu)) {
                            response->data->assistant_answer->nlu = strdup(nlu->valuestring);
                        }

                        my_cJSON *aa_is_end = my_cJSON_GetObjectItem(aa_root, "is_end");
                        if (my_cJSON_IsNumber(aa_is_end)) {
                            response->data->assistant_answer->is_end = aa_is_end->valueint;
                        }

                        my_cJSON *metadata = my_cJSON_GetObjectItem(aa_root, "metadata");
                        if (metadata && my_cJSON_IsObject(metadata)) {
                            response->data->assistant_answer->metadata = my_calloc(1, sizeof(Metadata));
                            if (response->data->assistant_answer->metadata) {
                                my_cJSON *multi_round_info = my_cJSON_GetObjectItem(metadata, "multi_round_info");
                                if (multi_round_info && my_cJSON_IsObject(multi_round_info)) {
                                    response->data->assistant_answer->metadata->multi_round_info = my_calloc(1, sizeof(MultiRoundInfo));
                                    if (response->data->assistant_answer->metadata->multi_round_info) {
                                        my_cJSON *is_in_multi = my_cJSON_GetObjectItem(multi_round_info, "is_in_multi");
                                        if (my_cJSON_IsBool(is_in_multi)) {
                                            response->data->assistant_answer->metadata->multi_round_info->is_in_multi = is_in_multi->valueint;
                                        }

                                        my_cJSON *target_bot_id = my_cJSON_GetObjectItem(multi_round_info, "target_bot_id");
                                        if (my_cJSON_IsString(target_bot_id)) {
                                            response->data->assistant_answer->metadata->multi_round_info->target_bot_id = strdup(target_bot_id->valuestring);
                                        }

                                        my_cJSON *intent = my_cJSON_GetObjectItem(multi_round_info, "intent");
                                        if (my_cJSON_IsString(intent)) {
                                            response->data->assistant_answer->metadata->multi_round_info->intent = strdup(intent->valuestring);
                                        }
                                    }
                                }
                            }
                        }

                        my_cJSON_Delete(aa_root);
                    } else {
                        response->data->assistant_answer->content = strdup(assistant_answer->valuestring);
                    }
                } else if (my_cJSON_IsObject(assistant_answer)) {
                    my_cJSON *content = my_cJSON_GetObjectItem(assistant_answer, "content");
                    if (my_cJSON_IsString(content)) {
                        response->data->assistant_answer->content = strdup(content->valuestring);
                    }

                    my_cJSON *nlu = my_cJSON_GetObjectItem(assistant_answer, "nlu");
                    if (my_cJSON_IsString(nlu)) {
                        response->data->assistant_answer->nlu = strdup(nlu->valuestring);
                    }

                    my_cJSON *aa_is_end = my_cJSON_GetObjectItem(assistant_answer, "is_end");
                    if (my_cJSON_IsNumber(aa_is_end)) {
                        response->data->assistant_answer->is_end = aa_is_end->valueint;
                    }

                    my_cJSON *metadata = my_cJSON_GetObjectItem(assistant_answer, "metadata");
                    if (metadata && my_cJSON_IsObject(metadata)) {
                        response->data->assistant_answer->metadata = my_calloc(1, sizeof(Metadata));
                        if (response->data->assistant_answer->metadata) {
                            my_cJSON *multi_round_info = my_cJSON_GetObjectItem(metadata, "multi_round_info");
                            if (multi_round_info && my_cJSON_IsObject(multi_round_info)) {
                                response->data->assistant_answer->metadata->multi_round_info = my_calloc(1, sizeof(MultiRoundInfo));
                                if (response->data->assistant_answer->metadata->multi_round_info) {
                                    my_cJSON *is_in_multi = my_cJSON_GetObjectItem(multi_round_info, "is_in_multi");
                                    if (my_cJSON_IsBool(is_in_multi)) {
                                        response->data->assistant_answer->metadata->multi_round_info->is_in_multi = is_in_multi->valueint;
                                    }

                                    my_cJSON *target_bot_id = my_cJSON_GetObjectItem(multi_round_info, "target_bot_id");
                                    if (my_cJSON_IsString(target_bot_id)) {
                                        response->data->assistant_answer->metadata->multi_round_info->target_bot_id = strdup(target_bot_id->valuestring);
                                    }

                                    my_cJSON *intent = my_cJSON_GetObjectItem(multi_round_info, "intent");
                                    if (my_cJSON_IsString(intent)) {
                                        response->data->assistant_answer->metadata->multi_round_info->intent = strdup(intent->valuestring);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        my_cJSON *data_array = my_cJSON_GetObjectItem(data_obj, "data");
        if (data_array && my_cJSON_IsArray(data_array)) {
            int array_size = my_cJSON_GetArraySize(data_array);
            response->data->data_items_count = array_size;

            if (array_size > 0) {
                response->data->data_items = my_calloc(array_size, sizeof(DataItem *));
                if (!response->data->data_items) {
                    my_cJSON_Delete(root);
                    duer_free_inside_rc_response(response);
                    return NULL;
                }

                for (int i = 0; i < array_size; i++) {
                    DataItem *item = my_calloc(1, sizeof(DataItem));
                    if (!item) {
                        continue;
                    }
                    response->data->data_items[i] = item;

                    my_cJSON *array_item = my_cJSON_GetArrayItem(data_array, i);

                    my_cJSON *header = my_cJSON_GetObjectItem(array_item, "header");
                    if (header && my_cJSON_IsObject(header)) {
                        item->header = my_calloc(1, sizeof(Header));
                        if (item->header) {
                            my_cJSON *namespace = my_cJSON_GetObjectItem(header, "namespace");
                            if (my_cJSON_IsString(namespace)) {
                                item->header->namespace = strdup(namespace->valuestring);
                            }

                            my_cJSON *name = my_cJSON_GetObjectItem(header, "name");
                            if (my_cJSON_IsString(name)) {
                                item->header->name = strdup(name->valuestring);
                            }

                            my_cJSON *messageId = my_cJSON_GetObjectItem(header, "messageId");
                            if (my_cJSON_IsString(messageId)) {
                                item->header->messageId = strdup(messageId->valuestring);
                            }

                            my_cJSON *dialogRequestId = my_cJSON_GetObjectItem(header, "dialogRequestId");
                            if (my_cJSON_IsString(dialogRequestId)) {
                                item->header->dialogRequestId = strdup(dialogRequestId->valuestring);
                            }
                        }
                    }

                    my_cJSON *payload = my_cJSON_GetObjectItem(array_item, "payload");
                    if (payload && my_cJSON_IsObject(payload)) {
                        item->payload = my_calloc(1, sizeof(Payload));
                        if (item->payload) {
                            my_cJSON *rolling = my_cJSON_GetObjectItem(payload, "rolling");
                            if (my_cJSON_IsBool(rolling)) {
                                item->payload->rolling = rolling->valueint;
                            }

                            my_cJSON *text = my_cJSON_GetObjectItem(payload, "text");
                            if (my_cJSON_IsString(text)) {
                                item->payload->text = strdup(text->valuestring);
                            }

                            my_cJSON *content = my_cJSON_GetObjectItem(payload, "content");
                            if (my_cJSON_IsString(content)) {
                                item->payload->content = strdup(content->valuestring);
                            }

                            my_cJSON *format = my_cJSON_GetObjectItem(payload, "format");
                            if (my_cJSON_IsString(format)) {
                                item->payload->format = strdup(format->valuestring);
                            }

                            my_cJSON *token = my_cJSON_GetObjectItem(payload, "token");
                            if (my_cJSON_IsString(token)) {
                                item->payload->token = strdup(token->valuestring);
                            }

                            my_cJSON *url = my_cJSON_GetObjectItem(payload, "url");
                            if (my_cJSON_IsString(url)) {
                                item->payload->url = strdup(url->valuestring);
                                my_url_set(item->payload->url);
                                my_print_urls();
                            }

                            my_cJSON *type = my_cJSON_GetObjectItem(payload, "type");
                            if (my_cJSON_IsString(type)) {
                                item->payload->type = strdup(type->valuestring);
                            }

                            my_cJSON *answer = my_cJSON_GetObjectItem(payload, "answer");
                            if (my_cJSON_IsString(answer)) {
                                item->payload->answer = strdup(answer->valuestring);
                            }

                            my_cJSON *id = my_cJSON_GetObjectItem(payload, "id");
                            if (my_cJSON_IsString(id)) {
                                item->payload->id = strdup(id->valuestring);
                            }

                            my_cJSON *index = my_cJSON_GetObjectItem(payload, "index");
                            if (my_cJSON_IsNumber(index)) {
                                item->payload->index = index->valueint;
                            }

                            my_cJSON *payload_is_end = my_cJSON_GetObjectItem(payload, "is_end");
                            if (my_cJSON_IsNumber(payload_is_end)) {
                                item->payload->payload_is_end = payload_is_end->valueint;
                            }

                            my_cJSON *part = my_cJSON_GetObjectItem(payload, "part");
                            if (my_cJSON_IsString(part)) {
                                item->payload->part = strdup(part->valuestring);
                            }

                            my_cJSON *reasoning_part = my_cJSON_GetObjectItem(payload, "reasoning_part");
                            if (my_cJSON_IsString(reasoning_part)) {
                                item->payload->reasoning_part = strdup(reasoning_part->valuestring);
                            }

                            my_cJSON *tts = my_cJSON_GetObjectItem(payload, "tts");
                            if (my_cJSON_IsString(tts)) {
                                item->payload->tts = strdup(tts->valuestring);
                            }

                            my_cJSON *timeoutInMilliseconds = my_cJSON_GetObjectItem(payload, "timeoutInMilliseconds");
                            if (my_cJSON_IsNumber(timeoutInMilliseconds)) {
                                item->payload->timeoutInMilliseconds = timeoutInMilliseconds->valueint;
                            }
                        }
                    }

                    my_cJSON *property = my_cJSON_GetObjectItem(array_item, "property");
                    if (property && my_cJSON_IsObject(property)) {
                        my_cJSON *serviceCategory = my_cJSON_GetObjectItem(property, "serviceCategory");
                        if (my_cJSON_IsString(serviceCategory)) {
                            item->serviceCategory = strdup(serviceCategory->valuestring);
                        }
                    }
                }
            }
        }

        my_cJSON *lj_thread_id = my_cJSON_GetObjectItem(data_obj, "lj_thread_id");
        if (my_cJSON_IsString(lj_thread_id)) {
            response->data->lj_thread_id = strdup(lj_thread_id->valuestring);
        }

        my_cJSON *ab_conversation_id = my_cJSON_GetObjectItem(data_obj, "ab_conversation_id");
        if (my_cJSON_IsString(ab_conversation_id)) {
            response->data->ab_conversation_id = strdup(ab_conversation_id->valuestring);
        }

        my_cJSON *xiaoice_session_id = my_cJSON_GetObjectItem(data_obj, "xiaoice_session_id");
        if (my_cJSON_IsString(xiaoice_session_id)) {
            response->data->xiaoice_session_id = strdup(xiaoice_session_id->valuestring);
        }

        my_cJSON *dialog_request_id = my_cJSON_GetObjectItem(data_obj, "dialog_request_id");
        if (my_cJSON_IsString(dialog_request_id)) {
            response->data->dialog_request_id = strdup(dialog_request_id->valuestring);
        }
    }
    my_cJSON_Delete(root);
    if (response &&
        response->data &&
        response->data->is_end == 1) {
        extern void my_list_download_task();
        my_list_download_task();
    }
    return response;
}

void duer_free_inside_rc_response(InsideRCResponse *response)
{
    if (!response) {
        return;
    }

    my_free(response->status);
    my_free(response->type);
    my_free(response->sn);

    if (response->data) {
        my_free(response->data->msg);
        my_free(response->data->logid);
        my_free(response->data->qid);
        my_free(response->data->lj_thread_id);
        my_free(response->data->ab_conversation_id);
        my_free(response->data->xiaoice_session_id);
        my_free(response->data->dialog_request_id);

        if (response->data->assistant_answer) {
            my_free(response->data->assistant_answer->content);
            my_free(response->data->assistant_answer->nlu);

            if (response->data->assistant_answer->metadata) {
                if (response->data->assistant_answer->metadata->multi_round_info) {
                    my_free(response->data->assistant_answer->metadata->multi_round_info->target_bot_id);
                    my_free(response->data->assistant_answer->metadata->multi_round_info->intent);
                    my_free(response->data->assistant_answer->metadata->multi_round_info);
                }
                my_free(response->data->assistant_answer->metadata);
            }
            my_free(response->data->assistant_answer);
        }

        if (response->data->data_items) {
            for (int i = 0; i < response->data->data_items_count; i++) {
                DataItem *item = response->data->data_items[i];
                if (!item) {
                    continue;
                }

                if (item->header) {
                    my_free(item->header->namespace);
                    my_free(item->header->name);
                    my_free(item->header->messageId);
                    my_free(item->header->dialogRequestId);
                    my_free(item->header);
                }

                if (item->payload) {
                    my_free(item->payload->text);
                    my_free(item->payload->content);
                    my_free(item->payload->format);
                    my_free(item->payload->token);
                    my_free(item->payload->url);
                    my_free(item->payload->type);
                    my_free(item->payload->answer);
                    my_free(item->payload->id);
                    my_free(item->payload->part);
                    my_free(item->payload->reasoning_part);
                    my_free(item->payload->tts);
                    my_free(item->payload);
                }

                my_free(item->serviceCategory);
                my_free(item);
            }
            my_free(response->data->data_items);
        }

        my_free(response->data);
    }

    my_free(response);
}


#if 0
int cjson_demo()
{
    const char *json = "{\"status\":\"ok\",\"type\":\"inside_rc\",\"data\":{\"code\":0,\"msg\":\"ok\",\"logid\":\"1753665417\",\"qid\":\"1184118576_a2f3e6cc\",\"is_end\":1,\"assistant_answer\":\"{\\\"content\\\":\\\"\\\\n对不起,没有听到你说了什么\\\\n\\\",\\\"nlu\\\":\\\"event.dcs\\\",\\\"metadata\\\":{\\\"multi_round_info\\\":{\\\"is_in_multi\\\":false,\\\"target_bot_id\\\":\\\"\\\",\\\"intent\\\":\\\"\\\"}}}\",\"need_clear_history\":0,\"data\":[{\"header\":{\"namespace\":\"ai.dueros.device_interface.voice_output\",\"name\":\"Speak\",\"messageId\":\"Njg4NmNmODk2NWU4NjUzNjI=\",\"dialogRequestId\":\"1184118576_a2f3e6cc\"},\"payload\":{\"content\":\"对不起,没有听到你说了什么\",\"format\":\"AUDIO_MPEG\",\"token\":\"eyJib3RfaWQiOiJ1cyIsInJlc3VsdF90b2tlbiI6IjhkMWEwMDcxLTE4YjItNDMwYi05NDMxLWZkMTEwM2I1MGI1NyIsImJvdF90b2tlbiI6Im51bGwiLCJsYXVuY2hfaWRzIjpbIiJdfQ==\",\"url\":\"http://duer-kids.baidu.com/sandbox/tts-proxy/precache/tts?sn=9TI8gUtVespTyjLT3xEDnpjQ85mlnKWum0Od8%2FtK8FfJvQxcxtFoK1EMGQfPC2Yh&0026token=1753665417_rg8gfoGIKkCR\"},\"property\":{\"serviceCategory\":\"\"}}],\"lj_thread_id\":\"\",\"ab_conversation_id\":\"\",\"xiaoice_session_id\":\"\",\"dialog_request_id\":\"1184118576_a2f3e6cc\"},\"sn\":\"UFMira\",\"end\":0}";

    const char *jsona = "{\"status\":\"ok\",\"type\":\"inside_rc\",\"data\":{\"code\":0,\"msg\":\"ok\",\"logid\":\"1753666085\",\"qid\":\"1990458576_a952a448\",\"is_end\":0,\"assistant_answer\":\"小朋友，你是没听懂我刚才说的什么吗？没关系哦，\",\"need_clear_history\":0,\"data\":[{\"header\":{\"namespace\":\"ai.dueros.device_interface.screen\",\"name\":\"RenderStreamCard\",\"messageId\":\"\",\"dialogRequestId\":\"1990458576_a952a448\"},\"payload\":{\"answer\":\"\",\"id\":\"b327299b-5924-4441-b740-dec422bedfb4\",\"index\":13,\"is_end\":0,\"part\":\"，\",\"reasoning_part\":\"\",\"tts\":\"小朋友，你是没听懂我刚才说的什么吗？没关系哦，\"},\"property\":{\"serviceCategory\":\"\"}},{\"header\":{\"namespace\":\"ai.dueros.device_interface.voice_output\",\"name\":\"Speak\",\"messageId\":\"7be6d381-dd3d-49b9-880e-efc080297311\",\"dialogRequestId\":\"1990458576_a952a448\"},\"payload\":{\"format\":\"AUDIO_MPEG\",\"token\":\"iwLyLvVe3pKWsT9x\",\"url\":\"http://duer-kids.baidu.com/sandbox/tts-proxy/precache/tts?sn=HUYU%2BvFGxXcbCNyJNnEb6Lsq4uRNSZKUkBrnt%2BSVE40dHk3t6T0RJl%2FLi%2FMSA0Jm&0026token=1753666086_szo1p09GCQmC\"},\"property\":{\"serviceCategory\":\"\"}}],\"lj_thread_id\":\"\",\"ab_conversation_id\":\"\",\"xiaoice_session_id\":\"\",\"dialog_request_id\":\"1990458576_a952a448\"},\"sn\":\"g3BbCO\",\"end\":0}";

    const char *jsonb = "{\"status\":\"ok\",\"type\":\"inside_rc\",\"data\":{\"code\":0,\"msg\":\"ok\",\"logid\":\"1753666085\",\"qid\":\"1990458576_a952a448\",\"is_end\":0,\"assistant_answer\":\"小朋友，你是没听懂我刚才说的什么吗？没关系哦，你可以再跟我说一遍你想聊的话题，或者问我刚刚说的话是什么意思。\",\"need_clear_history\":0,\"data\":[{\"header\":{\"namespace\":\"ai.dueros.device_interface.screen\",\"name\":\"RenderStreamCard\",\"messageId\":\"\",\"dialogRequestId\":\"1990458576_a952a448\"},\"payload\":{\"answer\":\"\",\"id\":\"b327299b-5924-4441-b740-dec422bedfb4\",\"index\":27,\"is_end\":0,\"part\":\"。\",\"reasoning_part\":\"\",\"tts\":\"你可以再跟我说一遍你想聊的话题，或者问我刚刚说的话是什么意思。\"},\"property\":{\"serviceCategory\":\"\"}},{\"header\":{\"namespace\":\"ai.dueros.device_interface.voice_output\",\"name\":\"Speak\",\"messageId\":\"b2562170-a4e5-474f-88bd-30e55cb50b22\",\"dialogRequestId\":\"1990458576_a952a448\"},\"payload\":{\"format\":\"AUDIO_MPEG\",\"token\":\"18Qnpj1EizrPpcdm\",\"url\":\"http://duer-kids.baidu.com/sandbox/tts-proxy/precache/tts?sn=%2FhYTk3AMbnKc0%2FritQ5tSKPo5QX6pIyDUU8jKNSfuXopuB8Z44bQg%2BWSf77RZgIc&0026token=1753666087_W0JSVb0LnOEX\"},\"property\":{\"serviceCategory\":\"\"}}],\"lj_thread_id\":\"\",\"ab_conversation_id\":\"\",\"xiaoice_session_id\":\"\",\"dialog_request_id\":\"1990458576_a952a448\"},\"sn\":\"g3BbCO\",\"end\":0}";

    const char *jsonc = "{\"status\":\"ok\",\"type\":\"inside_rc\",\"data\":{\"code\":0,\"msg\":\"ok\",\"logid\":\"1753666085\",\"qid\":\"1990458576_a952a448\",\"is_end\":1,\"assistant_answer\":\"{\\\"content\\\":\\\"小朋友，你是没听懂我刚才说的什么吗？没关系哦，你可以再跟我说一遍你想聊的话题，或者问我刚刚说的话是什么意思。小度最喜欢帮小朋友解答问题啦！\\\",\\\"nlu\\\":\\\"Chat.Translation\\\",\\\"metadata\\\":{\\\"multi_round_info\\\":{\\\"is_in_multi\\\":false,\\\"target_bot_id\\\":\\\"\\\",\\\"intent\\\":\\\"\\\"}}}\",\"need_clear_history\":0,\"data\":[{\"header\":{\"namespace\":\"ai.dueros.device_interface.screen\",\"name\":\"RenderStreamCard\",\"messageId\":\"\",\"dialogRequestId\":\"1990458576_a952a448\"},\"payload\":{\"answer\":\"\",\"id\":\"b327299b-5924-4441-b740-dec422bedfb4\",\"index\":37,\"is_end\":1,\"part\":\"\",\"reasoning_part\":\"\",\"tts\":\"小度最喜欢帮小朋友解答问题啦！\"},\"property\":{\"serviceCategory\":\"\"}},{\"header\":{\"namespace\":\"ai.dueros.device_interface.voice_output\",\"name\":\"Speak\",\"messageId\":\"4765f38e-311a-4d8e-916b-59712d1de523\",\"dialogRequestId\":\"1990458576_a952a448\"},\"payload\":{\"format\":\"AUDIO_MPEG\",\"token\":\"b8KfiCB946Y0xUkF\",\"url\":\"http://duer-kids.baidu.com/sandbox/tts-proxy/precache/tts?sn=%2FhYTk3AMbnKc0%2FritQ5tSAha6q%2FlpY0%2FpYGDqc9kPjuGB3%2BsSRrN6tORd%2FrKSHUg&0026token=1753666087_2H8FD5p2iXsw\"},\"property\":{\"serviceCategory\":\"\"}},{\"header\":{\"namespace\":\"ai.dueros.device_interface.voice_input\",\"name\":\"Listen\",\"messageId\":\"0979acb1-76fe-4dfc-a76d-e06cd0380994\",\"dialogRequestId\":\"1990458576_a952a448\"},\"payload\":{\"timeoutInMilliseconds\":60000},\"property\":{\"serviceCategory\":\"\"}}],\"lj_thread_id\":\"\",\"ab_conversation_id\":\"\",\"xiaoice_session_id\":\"\",\"dialog_request_id\":\"1990458576_a952a448\"},\"sn\":\"g3BbCO\",\"end\":0}";


    InsideRCResponse *response = duer_parse_inside_rc_json(json);
    if (response) {
        duer_free_inside_rc_response(response);
    }

    InsideRCResponse *responsea = duer_parse_inside_rc_json(jsona);
    if (responsea) {
        duer_free_inside_rc_response(responsea);
    }

    InsideRCResponse *responseb = duer_parse_inside_rc_json(jsonb);
    if (responseb) {
        duer_free_inside_rc_response(responseb);
    }

    InsideRCResponse *responsec = duer_parse_inside_rc_json(jsonc);
    if (responsec) {
        duer_free_inside_rc_response(responsec);
    }

    return 0;
}
#endif

#endif
