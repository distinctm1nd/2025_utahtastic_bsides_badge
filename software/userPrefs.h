#ifndef _USERPREFS_
#define _USERPREFS_

// Slipstream values:

#define USERPREFS_TZ_STRING "tzplaceholder                                         "

// Uncomment and modify to set device defaults

// #define USERPREFS_EVENT_MODE 1

#define USERPREFS_CONFIG_LORA_REGION meshtastic_Config_LoRaConfig_RegionCode_US
#define USERPREFS_LORACONFIG_MODEM_PRESET meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST
// #define USERPREFS_LORACONFIG_CHANNEL_NUM 31
// #define USERPREFS_CONFIG_LORA_IGNORE_MQTT true

// #define USERPREFS_CONFIG_GPS_MODE meshtastic_Config_PositionConfig_GpsMode_ENABLED

// #define USERPREFS_CHANNELS_TO_WRITE 3
/*
#define USERPREFS_CHANNEL_0_PSK \
    {                                                                                                                            \
        0x38, 0x4b, 0xbc, 0xc0, 0x1d, 0xc0, 0x22, 0xd1, 0x81, 0xbf, 0x36, 0xb8, 0x61, 0x21, 0xe1, 0xfb, 0x96, 0xb7, 0x2e, 0x55,  \
            0xbf, 0x74, 0x22, 0x7e, 0x9d, 0x6a, 0xfb, 0x48, 0xd6, 0x4c, 0xb1, 0xa1                                               \
    }
*/
// #define USERPREFS_CHANNEL_0_NAME "DEFCONnect"
// #define USERPREFS_CHANNEL_0_PRECISION 14
// #define USERPREFS_CHANNEL_0_UPLINK_ENABLED true
// #define USERPREFS_CHANNEL_0_DOWNLINK_ENABLED true
/*
#define USERPREFS_CHANNEL_1_PSK \
    {                                                                                                                            \
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  \
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00                                               \
    }
*/
// #define USERPREFS_CHANNEL_1_NAME "REPLACEME"
// #define USERPREFS_CHANNEL_1_PRECISION 14
// #define USERPREFS_CHANNEL_1_UPLINK_ENABLED true
// #define USERPREFS_CHANNEL_1_DOWNLINK_ENABLED true
/*
#define USERPREFS_CHANNEL_2_PSK \
    {                                                                                                                            \
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  \
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00                                              \
    }
*/
// #define USERPREFS_CHANNEL_2_NAME "REPLACEME"
// #define USERPREFS_CHANNEL_2_PRECISION 14
// #define USERPREFS_CHANNEL_2_UPLINK_ENABLED true
// #define USERPREFS_CHANNEL_2_DOWNLINK_ENABLED true

// #define USERPREFS_CONFIG_OWNER_LONG_NAME "My Long Name"
// #define USERPREFS_CONFIG_OWNER_SHORT_NAME "MLN"


#define USERPREFS_SPLASH_TITLE "UTAHtastic"
#define icon_width 100
#define icon_height 87
#define USERPREFS_HAS_SPLASH

static unsigned char icon_bits[] = {
 0x00, 0x00, 0x00, 0x40, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2a, 
	0xe8, 0xa3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xff, 0x47, 0x50, 0x01, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0xff, 0x07, 0x7c, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0xff, 0x07, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0xfe, 0xe8, 0x83, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 
	0xc1, 0xf1, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0xff, 0xf8, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xff, 0x7f, 0x7c, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0xff, 0x3f, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0xf0, 0xff, 0x7f, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 
	0xff, 0x7f, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0xff, 0xff, 0x71, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0x83, 0x03, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x5f, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff, 0x3b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0xff, 0xff, 0xff, 0xff, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 
	0x7f, 0xf2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x7f, 0x01, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x03, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 
	0xff, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff, 0xff, 
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0x07, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0xfe, 0xff, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 
	0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0xff, 0xff, 0xff, 
	0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0f, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 
	0xff, 0xff, 0xff, 0xff, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
	0xff, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff, 0xff, 
	0xff, 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
	0x7f, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 
	0x00, 0x00, 0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0xf8, 
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00, 0xf4, 0xff, 0xff, 0xff, 
	0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x00, 0x00, 0x00, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
	0xff, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x04, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x15, 
	0x00, 0x00, 0x00, 0x0c, 0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 
	0x14, 0xf0, 0xff, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x00, 0x00, 0x00, 0x28, 0xe0, 0xff, 
	0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x55, 0xc0, 0xff, 0xff, 0xf1, 0xff, 
	0xff, 0xff, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x80, 0x00, 0xff, 0xff, 0x80, 0xff, 0xff, 0xff, 0xff, 
	0xff, 0x00, 0x00, 0x00, 0x51, 0x00, 0xff, 0xff, 0x05, 0x10, 0x51, 0xf5, 0xff, 0xff, 0x01, 0x00, 
	0x00, 0xb0, 0x00, 0xfc, 0xff, 0x3f, 0x60, 0x00, 0xe0, 0xff, 0xff, 0x03, 0x00, 0x00, 0x55, 0x00, 
	0xf0, 0xff, 0x7f, 0xc1, 0x05, 0xc4, 0xff, 0xff, 0x5f, 0x00, 0x00, 0xb4, 0x00, 0xe0, 0xff, 0xff, 
	0xff, 0xef, 0x8c, 0xff, 0xff, 0xff, 0x00, 0x00, 0x55, 0x00, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 
	0xff, 0xff, 0xff, 0x17, 0x00, 0xb5, 0x00, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
	0x7f, 0x00, 0x55, 0x00, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0xbc, 
	0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x07, 0x55, 0x00, 0x00, 0xff, 
	0xff, 0xff, 0xff, 0xff, 0x7f, 0xff, 0xff, 0xff, 0x07, 0xbd, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 
	0x87, 0x0f, 0xfe, 0xff, 0xff, 0x00, 0x55, 0x00, 0x00, 0xff, 0x5f, 0xfc, 0xff, 0x07, 0x04, 0xd0, 
	0xf7, 0xff, 0x01, 0xbc, 0x00, 0x00, 0xff, 0x0f, 0xe0, 0xff, 0x03, 0x00, 0x80, 0xe7, 0xff, 0x03, 
	0x7d, 0x00, 0x00, 0xff, 0x07, 0xc0, 0xdf, 0x01, 0x00, 0x00, 0x00, 0x55, 0x01, 0xbd, 0x02, 0x00, 
	0xfe, 0x03, 0x80, 0xbf, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfd, 0x02, 0x00, 0xff, 0x01, 0x00, 
	0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x02, 0x80, 0x7f, 0x00, 0x80, 0x0f, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x7f, 0x03, 0xc0, 0x3f, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0xff, 0x03, 0xe0, 0x1f, 0x00, 0x80, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x07, 
	0xf0, 0x07, 0x00, 0xc0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03, 0xf8, 0x03, 0x00, 
	0xe0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x07, 0xfc, 0x01, 0x00, 0xf0, 0x01, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x37, 0xfe, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0xff, 0xff, 0x7f, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe8, 
	0xff, 0xff, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf5, 0xf7, 0x07, 
	0x10, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9c, 0xbf, 0x1f, 0xf8, 0x3f, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xdd, 0xd1, 0x15, 0xff, 0xff, 0x15, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0xe2, 0x0f, 0x3c, 0x83, 0x3f, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0xf0, 0x41, 0x75, 0x51, 0x51, 0x5f, 0x11, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x07, 0x00, 
	0x3e, 0xa1, 0xff, 0x03, 0xa0, 0x3f, 0x00, 0x00, 0x00, 0x00, 0xfc, 0x5f, 0x00, 0x00, 0x00, 0x01, 
	0x00, 0x40, 0x40, 0x01, 0x00, 0x00, 0x00, 0xf8, 0x07, 0x00, 0x00, 0x00, 0x03, 0x00, 0xfe, 0x3f, 
	0x00, 0x00, 0x00, 0x00, 0xf0, 0x17, 0x00, 0x00, 0x00, 0x01, 0x00, 0x50, 0x55, 0x01, 0x00, 0x00, 
	0x00, 0xf0, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x82, 0xbf, 0x03, 0x00, 0x00, 0x00, 0x40, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xff, 0x07, 0x00, 0x00, 0x00
};


/*
#define USERPREFS_USE_ADMIN_KEY 1
static unsigned char USERPREFS_ADMIN_KEY[] = {0xcd, 0xc0, 0xb4, 0x3c, 0x53, 0x24, 0xdf, 0x13, 0xca, 0x5a, 0xa6,
                                       0x0c, 0x0d, 0xec, 0x85, 0x5a, 0x4c, 0xf6, 0x1a, 0x96, 0x04, 0x1a,
                                       0x3e, 0xfc, 0xbb, 0x8e, 0x33, 0x71, 0xe5, 0xfc, 0xff, 0x3c};
*/
#endif