// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#include "adc_hal.h"
#include "stm32f4xx_hal_adc.h"

// 初始化当前速度变量
static ADC_Speed_t current_speed = ADC_SPEED_LOW;

// 目标数组
__attribute__((aligned(4))) static uint16_t adc_raw_buffer[ADC_BUFFER_SIZE];
/**
 * @brief 初始化 ADC HAL
 */
void ADC_HAL_Init(void) {
    // 校准代码可在此处添加
    current_speed = ADC_SPEED_LOW;
}

/**
 * @brief 开始 ADC DMA 采集
 */
void ADC_HAL_Start(void) {
    // 使用宏定义的句柄
    HAL_ADC_Start_DMA(WAVE_ADC_HANDLE, (uint32_t*)adc_raw_buffer, ADC_BUFFER_SIZE);
}


/**
 * @brief 停止 ADC DMA 采集
 */
void ADC_HAL_Stop(void) {
    HAL_ADC_Stop_DMA(WAVE_ADC_HANDLE);
}

/**
 * @brief 设置 ADC 采样速度
 * @param speed 速度等级
 */
void ADC_Set_Speed(ADC_Speed_t speed) {
    if (speed == current_speed) return; 

    ADC_HAL_Stop(); 

    HAL_ADC_DeInit(WAVE_ADC_HANDLE); 

    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = ADC_CHANNEL_4;
    sConfig.Rank = 1;

    switch (speed) {
        case ADC_SPEED_LOW:
            // --- 低速挡: ~21.3 kSps ---
            // Div8 (10.5M) / (480 + 12)
            hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV8;
            sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;
            break;

        case ADC_SPEED_MID:
            // --- 中速挡: ~525 kSps ---
            // Div4 (21M) / (28 + 12)
            hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
            sConfig.SamplingTime = ADC_SAMPLETIME_28CYCLES;
            break;

        case ADC_SPEED_HIGH:
        default:
            // --- 高速挡: ~2.8 Msps ---
            // Div2 (42M) / (3 + 12)
            hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
            sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
            break;
    }

    // 重新初始化 ADC (应用分频系数修改)
    if (HAL_ADC_Init(&hadc1) != HAL_OK) { return; }
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { return; }

    current_speed = speed;
    ADC_HAL_Start(); 
}

/**
 * @brief 获取当前 ADC 采样速度对应的采样率
 * @return 采样率 (Sps)
 */
uint32_t ADC_Get_SampleRate(void) {
    switch (current_speed) {
        case ADC_SPEED_LOW:  return 10500000 / (480 + 12); // 21,341 Hz
        case ADC_SPEED_MID:  return 21000000 / (28 + 12);  // 525,000 Hz
        default:             return 42000000 / (3 + 12);   // 2,800,000 Hz
    }
}

/**
 * @brief 获取 ADC DMA 缓冲区指针
 * @return 指向 ADC 数据缓冲区的指针
 */
uint16_t* ADC_HAL_Get_Buffer_Ptr(void) {
    return adc_raw_buffer;
}