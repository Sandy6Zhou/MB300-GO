#ifndef _IAP2_H_
#define _IAP2_H_

#include "generic/typedef.h"

#define iAP2_LINK_PACKET_HEADER_LEN		9
#define iAP2_MESSAGE_LEN				6
#define iAP2_MESSAGE_PARAMETERS_LEN		4

#define MAX_PACKET_PAYLOAD			0x65525L	//means 65535
#define IAP2_START_OF_PACKET		0xFF5AL
#define START_OF_MESSAGE			0x4040L
#define PACKET_TOTAL_LEN(X)			(iAP2_LINK_PACKET_HEADER_LEN+sizeof(X)+0x1)
#define MESSAGE_TOTAL_LEN(X)		(sizeof(X))

///IAP2 CONTROL SESSION MESSAGES
#define REQUEST_CERTIFICATE		    0xAA00L
#define CERTIFICATE				    0xAA01L
#define REQUEST_CHALLENGE	        0xAA02L
#define AUTHENTICATION_RESPONSE	    0xAA03L
#define AUTHENTICATION_FAILED	    0xAA04L
#define AUTHENTICATION_SUCC	        0xAA05L
#define START_IDENTIFICATION	    0x1D00L
#define ID_INFORMATION			    0x1D01L
#define ID_ACCEPTED				    0x1D02L
#define START_HID				    0x6800L
#define DEVICE_HID_REPORT		    0x6801L
#define ACC_HID_REPORT		        0x6802L
#define STOP_HID				    0x6803L
#define START_POWER_UPDATES         0xAE00L
#define POWER_UPDATES               0xAE01L
#define STOP_POWER_UPDATES          0xAE02L
#define POWER_SOURCE_UPDATES        0xAE03L

#define START_USB_DEVICE_MODE_AUDIO 0xDA00L
#define USB_DEVICE_MODE_AUDIO_INFO  0xDA01L
#define STOP_USB_DEVICE_MODE_AUDIO  0xDA02L

//address
#define ID_Name                             0
#define ID_ModelIdentifier                  1
#define ID_Manufacturer                     2
#define ID_SerialNumber                     3
#define ID_FirmwareVersion                  4
#define ID_HardwareVersion                  5
#define ID_MessagesSentByAccessory          6
#define ID_MessagesReceivedFromDevice       7
#define ID_PowerProvidingCapability         8
#define ID_MaximumCurrentDrawnFromDevice    9
#define ID_SupportedExternalAccessoryProtocol 10
#define ID_AppMatchTeamID                   11
#define ID_CurrentLanguage                  12
#define ID_SupportedLanguage                13
#define ID_UARTTransportComponent           14
#define ID_USBDeviceTransportComponent      15
#define ID_USBHostTransportComponent        16
#define ID_BluetoothTransportComponent      17
#define ID_iAP2HIDComponent                 18
#define ID_LocationInformationComponent     22
#define ID_USBHostHIDComponent              23
#define ID_BluetoothHIDComponent            29
#define ID_ProductPlanUID                   34

//ID_USBHostTransportComponent 子ID
#define SUB_ID_TransportComponentIdentifier 0
#define SUB_ID_TransportComponentName       1
#define SUB_ID_TransportSupportsiAP2Connection 2

///variable
typedef enum {
    iAP2_SLP = 3,
    iAP2_RST,
    iAP2_EAK,
    iAP2_ACK,
    iAP2_SYN,
} iAP2_CONTROL_BYTE;

typedef enum {
    //Header
    STARTOFPACKET = 0,
    PACKETLENGTH = 2,
    CONTROLBYTE = 4,
    PACKETSEQUENCENUMBER,
    PACKETACKNUMBER,
    SESSIONIDENTIFIER,
    HEADERCHECKSUM,
    //Payload
    LINKVERSION,
    MAXNUMOFOUTSTANDINGPKTS,
    MAXPKTLENGTH = 11,
    RETRANSMISSIONTIMEOUT = 13,
    CUMULATIVEASKTIMEOUT = 15,
    MAXNUMOFRETRANSMISSIONS = 17,
    MAXCUMULATIVEACK,

    STARTOFMESSAGE = 9,
    MESSAGELENGTH = 11,
    MESSAGEID = 13,
} iAP2_PKT_INDEX;

typedef enum {
    CONTROL_SESSION = 0,
    FILE_TRANSFER_SESSION,
    EXTERNAL_ACCESSORY_SESSION,
} iAP2_SESSION_TYPE;

typedef union _U16_U8 {
    u16	wWord;
    u8	bByte[2];
} U16_U8;

typedef struct _iAP2_LINK_PACKET_HEADER {
    U16_U8	sStartofPacket;
    U16_U8	sPacketLength;
    u8	bControlByte;
    u8	bPacketSequenceNum;
    u8	bPacketAckNum;
    u8	bSessionIdentifier;
    u8	bHeaderChecksum;
} iAP2_LINK_PACKET_HEADER;

typedef struct _iAP2_SESSION {
    u8	bSessionIdentifier;
    u8	bSessionType;
    u8	bSessionVersion;
} iAP2_SESSION;

typedef struct _iAP2_LINK_PACKET_PAYLOAD {
    u8	bLinkVersion;	   			//both device & accessory must agree on !!!
    u8	bMaxNumofOutstandingPkts;
    U16_U8	sMaxPktLength;
    U16_U8	sRetransmissionTimeout;	//both device & accessory must agree on !!!
    U16_U8	sCumulativeAckTimeout;	//both device & accessory must agree on !!!
    u8	bMaxNumofRetransmissions;	//both device & accessory must agree on !!!
    u8	bMaxCumulativeAck; 			//both device & accessory must agree on !!!
} iAP2_LINK_PACKET_PAYLOAD;

typedef struct _iAP2_MESSAGE {
    U16_U8 sStartofMessage;
    U16_U8 sMessageLength;
    U16_U8 sMessageID;
} iAP2_MESSAGE;

typedef struct _iAP2_MESSAGE_PARAMETERS {
    u16 wParameterLength;
    u16 wParameterId;
} iAP2_MESSAGE_PARAMETERS;

struct IAP2_MSG {
    u16 len;        //长度
    u16 id;         //ID编号
    const u8 *data; //内容指针
};

///outside call
bool iAP2_link(void);
void iAP2_power_to_apple_PowerStart(void);
void iAP2_power_to_apple_PowerStop(void);
void iAP2_power_from_apple_PowerUpdate(u32 current_mA);

#endif	/*	_IAP2_H_  */
