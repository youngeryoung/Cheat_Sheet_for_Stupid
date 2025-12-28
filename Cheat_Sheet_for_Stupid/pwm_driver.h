// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#ifndef __PWM_DRIVER_H
#define __PWM_DRIVER_H

#include "main.h"

// =============================================================
//                   用户硬件配置区 (User Config)
// =============================================================
// 请根据 CubeMX 中连接的引脚修改此处

extern TIM_HandleTypeDef htim3;
#define RGB_TIM      &htim3
#define RGB_CH_R     TIM_CHANNEL_1 
#define RGB_CH_G     TIM_CHANNEL_2 
#define RGB_CH_B     TIM_CHANNEL_3 

extern TIM_HandleTypeDef htim4;
#define BUZZER_TIM   &htim4
#define BUZZER_CH    TIM_CHANNEL_3 

extern TIM_HandleTypeDef htim9;
#define SIGNAL_TIM   &htim9
#define SIGNAL_CH    TIM_CHANNEL_1 

// 如果用了 TIM2 (PA1)，需要引用它，如果没有生成 TIM2，请注释掉下面这行
extern TIM_HandleTypeDef htim2; 
#define SIGNAL2_TIM  &htim2
#define SIGNAL2_CH   TIM_CHANNEL_2

// 如果用了 TIM5，需要引用它，如果没有生成 TIM5，请注释掉下面这行
// extern TIM_HandleTypeDef htim5; 
// #define SIGNAL3_CH      &htim5
// #define SIGNAL3_CH   TIM_CHANNEL_1

extern TIM_HandleTypeDef htim11;
#define LED_TIM     &htim11
#define LED_CH      TIM_CHANNEL_1

// =============================================================
//                     驱动接口定义区 (API)
// =============================================================

// --- 核心控制接口 ---

/**
 * @brief 全能设置 (PSC, ARR, CCR 全算)
 * @param htim 定时器句柄
 * @param Channel 通道
 * @param Freq_Hz 频率 (Hz)
 * @param Duty_Percent 占空比 (0-100%)
 */
void PWM_Set_Freq_Duty(TIM_HandleTypeDef *htim, uint32_t Channel, float Freq_Hz, float Duty_Percent);

/**
 * @brief 快速设置占空比 (只改 CCR，不改频率)
 * @param htim 定时器句柄
 * @param Channel 通道
 * @param Duty_Percent 占空比 (0-100%)
 */
void PWM_Set_Duty_Fast(TIM_HandleTypeDef *htim, uint32_t Channel, float Duty_Percent);

/**
 * @brief 智能设置频率 (保持 Duty% 不变)
 * @param htim 定时器句柄
 * @param Channel 通道
 * @param Freq_Hz 频率 (Hz)
 */
void PWM_Set_Freq_Smart(TIM_HandleTypeDef *htim, uint32_t Channel, float Freq_Hz);

// --- 业务封装接口 ---

/**
 * @brief 初始化 RGB 灯与 LED1
 */
void RGB_Init(void);   

/**
 * @brief 设置 RGB 颜色
 * @param r 红色分量 (0-100)
 * @param g 绿色分量 (0-100)
 * @param b 蓝色分量 (0-100)
 */                         // 初始化：开启通道并熄灭
void RGB_Set(uint8_t r, uint8_t g, uint8_t b);  // 设置颜色 (0-100)

/**
 * @brief LED0 开关控制 (GPIO方式)
 * @param state 状态 (0=灭, 1=亮)
 */
void LED0_Set(uint8_t state);

/**
 * @brief LED1 亮度控制 (PWM方式)
 * @param duty 亮度百分比 (0-100)
 */
void LED1_Set(uint8_t duty);

// 蜂鸣器控制

/**
 * @brief 开蜂鸣器：指定频率和占空比
 * @param Freq_Hz 频率 (Hz)
 * @param Duty 占空比 (0-100%)
 */
void Buzzer_On(uint32_t Freq_Hz, float Duty);

/**
 * @brief 关蜂鸣器
 */
void Buzzer_Off(void);

/**
 * @brief 设置蜂鸣器占空比 (保持频率不变)
 * @param Duty 占空比 (0-100%)
 */
void Buzzer_Duty(float Duty);

#endif