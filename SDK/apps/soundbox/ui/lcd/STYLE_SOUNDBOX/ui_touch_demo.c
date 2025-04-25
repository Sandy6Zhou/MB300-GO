#include "ui/ui.h"
#include "app_config.h"
#include "ui/ui_style.h"
#include "ui/ui_api.h"
#include "system/timer.h"
#include "ui/ui_pic.h"

#if (TCFG_UI_ENABLE&&(CONFIG_UI_STYLE == STYLE_JL_SOUNDBOX))

#define STYLE_NAME  JL

static int ui_touch_demo_onchange(void *ctr, enum element_change_event e, void *arg)
{
    struct ui_pic *pic = (struct ui_pic *)ctr;
    switch (e) {
    case ON_CHANGE_INIT:
        ui_pic_set_image_index(pic, 2);
        break;
    case ON_CHANGE_RELEASE:
        break;
    default:
        return false;
    }
    return false;
}

static int ui_touch_demo_ontouch(void *ctr, struct element_touch_event *e)
{
    static int index = 0;
    switch (e->event) {
    case ELM_EVENT_TOUCH_DOWN:
        printf("------ ELM_EVENT_TOUCH_DOWN");
        break;
    case ELM_EVENT_TOUCH_HOLD:
        printf("------ ELM_EVENT_TOUCH_HOLD");
        break;
    case ELM_EVENT_TOUCH_MOVE:
        printf("------ ELM_EVENT_TOUCH_MOVE");
        break;
    case ELM_EVENT_TOUCH_UP:
        printf("------ ELM_EVENT_TOUCH_UP");
        ui_pic_show_image_by_id(PNG_PIC, index % 3);
        index++;
        break;
    }
    return true; //焦点控件，false 非焦点控件
}

REGISTER_UI_EVENT_HANDLER(PNG_PIC)

.onchange = ui_touch_demo_onchange,
 .onkey = NULL,
  .ontouch = ui_touch_demo_ontouch,
};


#endif


