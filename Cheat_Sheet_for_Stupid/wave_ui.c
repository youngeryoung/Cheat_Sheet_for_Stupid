// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#include "wave_ui.h"
#include <stdio.h>

#define PRINT_FLOAT(val)  (int)(val), (int)(((val) - (int)(val)) * 100)

int16_t UI_Baseline_Offset = 0; // 0 表示居中，正数向上，负数向下

/**
 * @brief 设置基线
 * @param offset 偏移量 (正数向上，负数向下)
 */
void Wave_UI_SetBaseline(int16_t offset) {
    UI_Baseline_Offset = offset;
}

/**
 * @brief 绘制网格
 */
void Wave_UI_DrawGrid(void) {
    // 画个虚线框或者十字准星，增加专业感
    // 横中线
    for(int x=0; x<128; x+=4) OLED_SetPixel(x, 32, OLED_COLOR_NORMAL);
    // 竖中线
    for(int y=0; y<64; y+=4)  OLED_SetPixel(64, y, OLED_COLOR_NORMAL);
    // 边框
    OLED_DrawRectangle(0, 0, 128, 64, OLED_COLOR_NORMAL);
}

/**
 * @brief 绘制波形曲线
 * @param y_data Y坐标数据数组
 * @param len 数据长度 (最大128)
 * @param offset_x X轴偏移
 * @param offset_y_base Y轴基线偏移
 */
void Wave_UI_DrawCurve(uint8_t *y_data, uint8_t len, uint8_t offset_x, uint8_t offset_y_base) {
    if (len > 128) len = 128;
    for (int i = 0; i < len - 1; i++) {
        uint8_t x1 = i + offset_x;
        uint8_t x2 = i + 1 + offset_x;
        
        // 叠加可调基线 UI_Baseline_Offset
        // 注意防止溢出 (0-63)
        int16_t y1_temp = y_data[i] + offset_y_base - UI_Baseline_Offset; // OLED 坐标系下，减去 offset 实际上是向上移(如果是反向坐标)
        int16_t y2_temp = y_data[i+1] + offset_y_base - UI_Baseline_Offset;
        
        // 简单的限幅保护
        if (y1_temp < 0) y1_temp = 0; 
        if (y1_temp > 63) y1_temp = 63;
        if (y2_temp < 0) y2_temp = 0; 
        if (y2_temp > 63) y2_temp = 63;

        OLED_DrawLine(x1, (uint8_t)y1_temp, x2, (uint8_t)y2_temp, OLED_COLOR_NORMAL);
    }
}

/**
 * @brief 显示波形统计信息
 * @param res 波形结果结构体指针
 */
void Wave_UI_ShowStats(Wave_Stats_t *res) {
    // 顶部状态栏，反色显示
    char buf[32];
    // 自动判断单位
    if (res->Freq > 9999)
        sprintf(buf, "%d.%02dV %ldkHz", PRINT_FLOAT(res->Vpp), (long)res->Freq/1000);
    else
        sprintf(buf, "%d.%02dV %ldHz", PRINT_FLOAT(res->Vpp), (long)res->Freq);
    
    // 画一个黑底白字的背景条
    OLED_DrawFilledRectangle(0, 0, 128, 10, OLED_COLOR_NORMAL); 
    OLED_PrintString(2, 0, buf, OLED_COLOR_REVERSE);
}