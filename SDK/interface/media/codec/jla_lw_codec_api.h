#ifndef __jla_lw_codec_api_h
#define __jla_lw_codec_api_h

#include "typedef.h"

struct jla_lw_param {
    u32 sr;
    u32 bit_rate;
    u8 frame_len;
    u8 frame_pkt;
    u8 bitpool[5];
    u8 blocks[5];
};

#endif
