// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#ifndef __WAVE_UI_H
#define __WAVE_UI_H

#include "main.h"
#include "oled.h"
#include "wave_proc.h" // 需要用到 Wave_Result_t 结构体

// =============================================================
//                       接口定义区 (API)
// =============================================================

/**
 * @brief 设置基线
 * @param offset 偏移量 (正数向上，负数向下)
 */
void Wave_UI_SetBaseline(int16_t offset);

/**
 * @brief 绘制网格
 */
void Wave_UI_DrawGrid(void);

/**
 * @brief 绘制波形曲线
 * @param y_data Y坐标数据数组
 * @param len 数据长度 (最大128)
 * @param offset_x X轴偏移
 * @param offset_y_base Y轴基线偏移
 */
void Wave_UI_DrawCurve(uint8_t *y_data, uint8_t len, uint8_t offset_x, uint8_t offset_y);

/**
 * @brief 显示波形统计信息
 * @param res 波形结果结构体指针
 */
void Wave_UI_ShowStats(Wave_Stats_t *res);

#endif