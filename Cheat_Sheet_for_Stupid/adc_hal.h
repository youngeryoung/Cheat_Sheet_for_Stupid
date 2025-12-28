// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#ifndef __ADC_HAL_H
#define __ADC_HAL_H

#include "main.h"

// =============================================================
//                   用户硬件配置区 (User Config)
// =============================================================
// 请根据 CubeMX 中的配置修改此处

// 1. ADC 句柄定义
extern ADC_HandleTypeDef hadc1;
#define WAVE_ADC_HANDLE  &hadc1

// 2. 定义 DMA 句柄
extern DMA_HandleTypeDef hdma_adc1;

// 3. 采样深度 (缓冲区大小)
// 越大低频越好，但占用 RAM 越多
#define ADC_BUFFER_SIZE  8500

// 4. 采样速度枚举定义
typedef enum {
    ADC_SPEED_HIGH = 2, // ~2.8 Msps (适合 > 80kHz)
    ADC_SPEED_MID  = 1, // ~525 kSps (适合 1kHz - 80kHz)
    ADC_SPEED_LOW  = 0  // ~21 kSps  (适合 < 1kHz)
} ADC_Speed_t;


// =============================================================
//                   ADC HAL 接口函数声明
// =============================================================

/**
 * @brief 初始化 ADC HAL
 */
void ADC_HAL_Init(void);

/**
 * @brief 开始 ADC DMA 采集
 */
void ADC_HAL_Start(void);

/**
 * @brief 停止 ADC DMA 采集
 */
void ADC_HAL_Stop(void);

/**
 * @brief 设置 ADC 采样速度
 * @param speed 速度等级
 */
void ADC_Set_Speed(ADC_Speed_t speed);

/**
 * @brief 获取当前 ADC 采样速度对应的采样率
 * @return 采样率 (Sps)
 */
uint32_t ADC_Get_SampleRate(void); 

/**
 * @brief 获取 ADC DMA 缓冲区指针
 * @return 指向 ADC 数据缓冲区的指针
 */
uint16_t* ADC_HAL_Get_Buffer_Ptr(void);

#endif