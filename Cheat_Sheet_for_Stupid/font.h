// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#ifndef __FONT_FONT_H__
#define __FONT_FONT_H__

#include "stdint.h"

typedef struct {
    uint8_t h;
    uint8_t w;
    const uint8_t *chars;
} ASCIIFont;

typedef struct {
    uint8_t h;
    uint8_t w;
    uint16_t len;           // 字符个数
    const uint8_t *chars;   // 数据数组起始地址
} UnicodeFont;

extern const ASCIIFont fonta;
extern const UnicodeFont fontu;

#endif // __FONT_FONT_H__
