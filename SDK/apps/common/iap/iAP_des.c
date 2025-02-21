#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".iAP_des.data.bss")
#pragma data_seg(".iAP_des.data")
#pragma const_seg(".iAP_des.text.const")
#pragma code_seg(".iAP_des.text")
#endif

#include "apple_dock/iAP.h"
#include "apple_dock/iAP2.h"
#include "apple_dock/iAP_device.h"
#include "app_config.h"

#if TCFG_USB_APPLE_DOCK_EN

//Name--can alter
const u8 iap2_accessory_name[] = "Dock speaker";
//ModelIdentifier(模式描述符)--can alter
const u8 iap2_accessory_model_identifier[] = "IPDLI13";
//Manufacturer(制造商)--can alter
const u8 iap2_accessory_manufacturer[] = "I want it";
//SerialNumber(序列号)--can alter
const u8 iap2_accessory_serialnumber[] = "iAP Interface";
//FirmwareVersion(固件版本)--can alter
const u8 iap2_accessory_firmware_ver[] = "1.0.0";
//HardwareVersion(硬件版本)--can alter
const u8 iap2_accessory_hardware_ver[] = "1.0.0";
//CurrentLanguage--can not alter
const u8 iap2_cur_language[] = "en";
//SupportedLanguage--can not alter
const u8 iap2_sup_language[] = "en";
//ProductPlanUID
const u8 iap2_product_plan_uid[] = "2adb94288a5b41cf";
//MessagesSentByAccessory
const u8 iap2_msg_sent_by_accessory[] = {
    (u8)(START_POWER_UPDATES >> 8), (u8)(START_POWER_UPDATES),
    (u8)(STOP_POWER_UPDATES >> 8), (u8)(STOP_POWER_UPDATES),
    (u8)(POWER_SOURCE_UPDATES >> 8), (u8)(POWER_SOURCE_UPDATES),
};
//MessagesReceivedFromDevice
const u8 iap2_msg_recv_from_device[] = {
    (u8)(POWER_UPDATES >> 8), (u8)(POWER_UPDATES),
};
//PowerSourceType
const u8 iap2_power_source_type[] = {0x02};
//MaximumCurrentDrawnFromDevice. if PowerSourceType is 2, set 0; else can set 0, 500, 1000
const u8 iap2_maximum_current[] = {0x00, 0x64};
//USBHostTransportComponent--can alter
//TransportComponentIdentifier
const u8 iap2_transport_component_id[] = {0x00, 0x01};
//USB device transport Component
const u8 iap2_transport_name[] = "iAP2H";

//IAP2描述文档表
const struct IAP2_MSG iap_msg_des_table[] = {
    {.id = ID_Name,                             .len = 4 + sizeof(iap2_accessory_name),             .data =  iap2_accessory_name},
    {.id = ID_ModelIdentifier,                  .len = 4 + sizeof(iap2_accessory_model_identifier), .data =  iap2_accessory_model_identifier},
    {.id = ID_Manufacturer,                     .len = 4 + sizeof(iap2_accessory_manufacturer),     .data =  iap2_accessory_manufacturer},
    {.id = ID_SerialNumber,                     .len = 4 + sizeof(iap2_accessory_serialnumber),     .data =  iap2_accessory_serialnumber},
    {.id = ID_FirmwareVersion,                  .len = 4 + sizeof(iap2_accessory_firmware_ver),     .data =  iap2_accessory_firmware_ver},
    {.id = ID_HardwareVersion,                  .len = 4 + sizeof(iap2_accessory_hardware_ver),     .data =  iap2_accessory_hardware_ver},
    {.id = ID_MessagesSentByAccessory,          .len = 4 + sizeof(iap2_msg_sent_by_accessory),      .data =  iap2_msg_sent_by_accessory},
    {.id = ID_MessagesReceivedFromDevice,       .len = 4 + sizeof(iap2_msg_recv_from_device),       .data =  iap2_msg_recv_from_device},
    {.id = ID_PowerProvidingCapability,         .len = 4 + sizeof(iap2_power_source_type),          .data =  iap2_power_source_type},
    {.id = ID_MaximumCurrentDrawnFromDevice,    .len = 4 + sizeof(iap2_maximum_current),            .data =  iap2_maximum_current},
    {.id = ID_CurrentLanguage,                  .len = 4 + sizeof(iap2_cur_language),               .data =  iap2_cur_language},
    {.id = ID_SupportedLanguage,                .len = 4 + sizeof(iap2_sup_language),               .data =  iap2_sup_language},
    //---------------------ID_USBHostTransportComponent start------------------------------------------------------------------------------------------------
    {.id = ID_USBHostTransportComponent,        .len = 4 + (4 + sizeof(iap2_transport_component_id)) + (4 + sizeof(iap2_transport_name)) + 4, .data =  NULL},
    {.id = SUB_ID_TransportComponentIdentifier, .len = 4 + sizeof(iap2_transport_component_id),     .data =  iap2_transport_component_id},
    {.id = SUB_ID_TransportComponentName,       .len = 4 + sizeof(iap2_transport_name),             .data =  iap2_transport_name},
    {.id = SUB_ID_TransportSupportsiAP2Connection, .len = 4,                                         .data =  NULL},
    //---------------------ID_USBHostTransportComponent end--------------------------------------------------------------------------------------------------
};

const u32 iAP2_str_tab[2] = {
    (u32)iap_msg_des_table,  ARRAY_SIZE(iap_msg_des_table),
};

#endif /* TCFG_USB_APPLE_DOCK_EN */


