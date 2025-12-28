// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#ifndef __IC_DRIVER_H
#define __IC_DRIVER_H

#include "main.h"

// =============================================================
//                   用户硬件配置区 (User Config)
// =============================================================
// 请根据 CubeMX 中的配置修改此处

// 建议：优先使用 32位定时器 (TIM2/TIM5) 以获得最佳低频测量性能

// 1. 定时器句柄 (在 main.c 中定义，如 &htim5)
extern TIM_HandleTypeDef htim5; 
#define IC_TIM_HANDLE      &htim5

// 2. 定时器实例 (寄存器基地址)
#define IC_TIM_INSTANCE    TIM5

// 3. 通道定义 (PWM输入模式通常使用两个通道)
// Direct Channel: 捕获上升沿，测量周期 (连接引脚的那个通道)
#define IC_CH_PERIOD       TIM_CHANNEL_1 
// Indirect Channel: 捕获下降沿，测量脉宽 (内部连接的从通道)
#define IC_CH_DUTY         TIM_CHANNEL_2

// 4. 定时器时钟频率 (Hz)
// F401: APB1定时器(TIM2/3/4/5)是 84MHz, APB2(TIM1/9/10/11)是 84MHz
#define IC_TIM_CLOCK       84000000 

// =============================================================
//                     驱动接口定义区 (API)
// =============================================================

// 测量结果结构体
typedef struct {
    uint32_t Frequency;  // 频率 (Hz)
    float    DutyCycle;  // 占空比 (0.0 - 100.0)
    uint32_t Period_Raw; // 周期计数值
    uint32_t Pulse_Raw;  // 脉宽计数值
} Signal_Info_t;

// 接口

/**
 * @brief 初始化输入捕获驱动
 */
void IC_Init(void);

/**
 * @brief 安全模式接口 (退出高频界面时调用)
 */
void IC_Set_Safe_Mode(void);

/**
 * @brief 输入捕获中断处理函数
 * @param htim 定时器句柄
 * @note 放在 HAL_TIM_IC_CaptureCallback 中调用
 */
void IC_EventHandler(TIM_HandleTypeDef *htim); 

/**
 * @brief 获取当前信号信息
 * @param info 指向 Signal_Info_t 结构体的指针
 * @return 1-有效数据，0-无效数据（超时）
 */
uint8_t IC_Get_Info(Signal_Info_t *info);

#endif