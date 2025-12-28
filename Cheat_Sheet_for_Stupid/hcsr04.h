// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#ifndef __HCSR04_H
#define __HCSR04_H

#include "main.h"

// =============================================================
//                   用户硬件配置区 (User Config)
// =============================================================
// 请根据 CubeMX 中的配置修改此处

// Trig: PB10 (GPIO Output)
// Echo: PA8  (TIM1 CH1 Input Capture)

// 延时 相关参数
#define US_TICKS  84  // 84MHz 下，1us = 84 ticks
#define TRIG_PULSE_TICKS (10 * US_TICKS)

#define TRIG_PORT                 GPIOB
#define TRIG_PIN                  GPIO_PIN_10

#define ECHO_TIM_INSTANCE         TIM1
#define ECHO_TIM_HANDLE           &htim1
#define ECHO_CHANNEL              TIM_CHANNEL_1
#define ECHO_TIM_ACTIVE_CHANNEL   HAL_TIM_ACTIVE_CHANNEL_2

// Echo 引脚定义（已在main.h中定义）
// #define Echo_GPIO_Port       GPIOA
// #define Echo_Pin        GPIO_PIN_8

extern TIM_HandleTypeDef htim1;

// =============================================================
//                     驱动接口定义区 (API)
// =============================================================

/**
 * @brief 初始化 HC-SR04 传感器相关硬件
 */
void HCSR04_Init(void);

// ================== 非阻塞模式 (推荐) ==================

/**
 * @brief  触发一次测距 (完全非阻塞)
 * @note   发送 Trig 信号并开启输入捕获中断，立即返回
 */
void HCSR04_Start(void);

 /**
 * @brief  获取非阻塞测量结果
 * @return 距离值 (0.1mm)，0 表示测量中，45000 表示超出量程
 * @note   读取成功后会自动清除完成标志，需再次调用 Start 才能开始新测量
 */
uint32_t HCSR04_Get_Result(void);

/**
 * @brief  中断回调处理函数
 * @param  htim 定时器句柄
 * @note   请在 main.c 的 HAL_TIM_IC_CaptureCallback 中调用此函数
 */
void HCSR04_IC_EventHandler(TIM_HandleTypeDef *htim);


// ================== 阻塞模式 (高精度) ==================

/**
 * @brief 使用阻塞方式进行高精度测量
 * @return 距离值：微米(0.001mm)，0 表示测量失败或超出量程
 */
uint32_t HCSR04_Measure_Blocking_HighRes(void);

#endif