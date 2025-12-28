// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#ifndef __KEY_DRIVER_H
#define __KEY_DRIVER_H

#include "main.h"

// =============================================================
//                   用户硬件配置区 (User Config)
// =============================================================
// 请根据 CubeMX 中连接的引脚修改此处
// 1. 矩阵键盘配置 (如果不用矩阵，把行数设为0)

// --- 矩阵键盘硬件定义 ---
// Rows: PA15, PB3, PB4, PB5 (Open Drain + Pull-up)
// Columns: PB12, PB13, PB14, PB15 (Input + Pull-up)

// 行操作：0 为激活（拉低），1 为空闲（开漏浮空由上拉电阻维持高）
#define ROW1_L()    (GPIOA->BSRR = (1 << (15 + 16)))
#define ROW1_H()    (GPIOA->BSRR = (1 << 15))
#define ROW2_L()    (GPIOB->BSRR = (1 << (3 + 16)))
#define ROW2_H()    (GPIOB->BSRR = (1 << 3))
#define ROW3_L()    (GPIOB->BSRR = (1 << (4 + 16)))
#define ROW3_H()    (GPIOB->BSRR = (1 << 4))
#define ROW4_L()    (GPIOB->BSRR = (1 << (5 + 16)))
#define ROW4_H()    (GPIOB->BSRR = (1 << 5))

// 列读取：返回 0 表示按键按下
#define READ_COLS() (GPIOB->IDR & 0xF000) // 读取 PB12-15
#define KEY_ROWS 4
#define KEY_COLS 4

// 2. 独立按键配置
// 定义独立按键的 ID 起始值 (矩阵有16个，所以独立按键从16开始)
#define KEY_ID_BTN_A    16
#define KEY_ID_BTN_B    17

// --- 独立按键硬件定义（已在main.h中定义）---
// #define BTN_A_GPIO_Port  GPIOA
// #define BTN_A_Pin   GPIO_PIN_5
// #define BTN_B_GPIO_Port  GPIOB
// #define BTN_B_Pin   GPIO_PIN_1

// =============================================================
//                     驱动接口定义区 (API)
// =============================================================

/**
 * @brief  初始化 (需在 main 初始化中调用)
 */
void Key_Init(void);

/**
 * @brief  清空所有未处理的按键按下事件
 */
void Key_Clear_Event(void);

/**
 * @brief  扫描一次按键状态 (中断调用)
 * @note   请在 main.c 的 HAL_TIM_PeriodElapsedCallback 中调用此函数
 */
void Key_Scan_IT(void);

/**
 * @brief  获取按键按下事件 (读后清除)
 * @param  key_id: 0-15(矩阵), 16(PA5), 17(PB1)
 * @return 1: 刚刚按下, 0: 无事件
 */
uint8_t Key_GetEvent_Press(uint8_t key_id);

/**
 * @brief  查询按键是否按住 (长按检测)
 * @param  key_id: 0-17
 * @return 1: 按住中, 0: 松开
 */
uint8_t Key_IsDown(uint8_t key_id);

/**
 * @brief 获取所有按键的原始状态位图 (用于调试)
 * @return bit0-15:矩阵, bit16:BtnA, bit17:BtnB
 */
uint32_t Key_GetRawState(void);

#endif