// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#include "ic_driver.h"

// 滤波窗口
#define FILTER_SIZE 4
static uint32_t period_history[FILTER_SIZE];
static uint32_t pulse_history[FILTER_SIZE];
static uint8_t  filter_idx = 0;
static uint32_t last_capture_tick = 0;

// 当前分频系数 (1, 2, 4, 8)
static volatile uint8_t IC_Div_Factor = 8; // 默认安全模式 (8分频)

/** 
 * @brief 动态调整硬件预分频
 */
static void IC_Set_Strategy(uint32_t freq) {
    uint32_t new_prescaler;
    uint8_t next_factor = IC_Div_Factor; // 默认维持现状

    // --- 迟滞状态机逻辑 ---
    // 设定约 10%~20% 的迟滞区间
    switch (IC_Div_Factor) {
        case 1:
            // 当前 1 分频（直通），仅监控升档
            if (freq > 110000) next_factor = 2; // 阈值 100k -> 110k 升档
            break;

        case 2:
            // 当前 2 分频
            if (freq > 220000)      next_factor = 4; // 阈值 200k -> 220k 升档
            else if (freq < 90000)  next_factor = 1; // 阈值 100k -> 90k  降档
            break;

        case 4:
            // 当前 4 分频
            if (freq > 440000)      next_factor = 8; // 阈值 400k -> 440k 升档
            else if (freq < 180000) next_factor = 2; // 阈值 200k -> 180k 降档
            break;

        case 8:
            // 当前 8 分频，仅监控降档
            if (freq < 360000)      next_factor = 4; // 阈值 400k -> 360k 降档
            break;

        default:
            // 异常状态恢复
            next_factor = 8;
            break;
    }

    // --- 执行切换 ---
    if (next_factor != IC_Div_Factor) {
        switch (next_factor) {
            case 1: new_prescaler = TIM_ICPSC_DIV1; break;
            case 2: new_prescaler = TIM_ICPSC_DIV2; break;
            case 4: new_prescaler = TIM_ICPSC_DIV4; break;
            case 8: 
            default: new_prescaler = TIM_ICPSC_DIV8; break;
        }
        
        // 修改硬件寄存器

        // 1. 暂时关闭捕获使能（保证硬件状态机复位）
        IC_TIM_INSTANCE->CCER &= ~TIM_CCER_CC1E;;
        
        // 2. 修改预分频位
        __HAL_TIM_SET_ICPRESCALER(IC_TIM_HANDLE, IC_CH_PERIOD, new_prescaler);

        // 3. 重新开启捕获使能
        IC_TIM_INSTANCE->CCER |= (TIM_CCER_CC1E);

         // 4. 清除可能产生的虚假中断标志位
        __HAL_TIM_CLEAR_FLAG(IC_TIM_HANDLE, TIM_FLAG_CC1);

        // 更新状态
        IC_Div_Factor = next_factor;
        
        // 调试打印 (可选)
        // printf("[IC] Switch to DIV%d at %lu Hz\r\n", next_factor, freq);
    }
}

/**
 * @brief 初始化输入捕获驱动
 */
void IC_Init(void) {
    __HAL_TIM_SET_CAPTUREPOLARITY(IC_TIM_HANDLE, IC_CH_PERIOD, TIM_INPUTCHANNELPOLARITY_RISING);
    __HAL_TIM_SET_CAPTUREPOLARITY(IC_TIM_HANDLE, IC_CH_DUTY, TIM_INPUTCHANNELPOLARITY_FALLING);

    // 默认开启中断
    HAL_TIM_IC_Start_IT(IC_TIM_HANDLE, IC_CH_PERIOD);
    HAL_TIM_IC_Start(IC_TIM_HANDLE, IC_CH_DUTY);
    
    // 初始化直接进入安全模式 (8分频)，防止一上电就卡死
    IC_Div_Factor = 8;
    __HAL_TIM_SET_ICPRESCALER(IC_TIM_HANDLE, IC_CH_PERIOD, TIM_ICPSC_DIV8);
}

/**
 * @brief 安全模式接口 (退出高频界面时调用)
 */
void IC_Set_Safe_Mode(void) {
    IC_Div_Factor = 8;
    __HAL_TIM_SET_ICPRESCALER(IC_TIM_HANDLE, IC_CH_PERIOD, TIM_ICPSC_DIV8);
}

/**
 * @brief 输入捕获中断处理函数
 * @param htim 定时器句柄
 * @note 放在 HAL_TIM_IC_CaptureCallback 中调用
 */
void IC_EventHandler(TIM_HandleTypeDef *htim) {
    if (htim->Instance == IC_TIM_INSTANCE && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) 
    {
        uint32_t p = HAL_TIM_ReadCapturedValue(htim, IC_CH_PERIOD);
        uint32_t w = HAL_TIM_ReadCapturedValue(htim, IC_CH_DUTY);
        
        if (p > 0) {
            period_history[filter_idx] = p; 
            pulse_history[filter_idx] = w;
            
            filter_idx = (filter_idx + 1) % FILTER_SIZE;
            last_capture_tick = HAL_GetTick();
            
            // 计算当前频率用于换挡
            uint32_t current_freq = IC_TIM_CLOCK / (p + 1);
            IC_Set_Strategy(current_freq);
        }
    }
}

/**
 * @brief 获取当前信号信息
 * @param info 指向 Signal_Info_t 结构体的指针
 * @return 1-有效数据，0-无效数据（超时）
 */
uint8_t IC_Get_Info(Signal_Info_t *info) {
    if (HAL_GetTick() - last_capture_tick > 200) { 
        info->Frequency = 0; info->DutyCycle = 0;
        return 0;
    }

    uint64_t sum_period = 0;
    uint64_t sum_pulse = 0;
    for (int i = 0; i < FILTER_SIZE; i++) {
        sum_period += period_history[i];
        sum_pulse += pulse_history[i];
    }

    uint32_t avg_period = (uint32_t)(sum_period / FILTER_SIZE);
    uint32_t avg_pulse = (uint32_t)(sum_pulse / FILTER_SIZE);

    if (avg_period > 0) {
        // 1. 基础分母应该但似乎不是：CCR + 3
        uint32_t divisor = avg_period + (IC_Div_Factor == 1? 2 : 2);

        // 2. 四舍五入除法 (Rounding)
        // F = (Clock + (Divisor/2)) / Divisor
        info->Frequency = (IC_TIM_CLOCK + (divisor / 2)) / divisor;
        
        // 占空比计算
        info->DutyCycle = (float)avg_pulse / (float)divisor * 100.0f;
        
        info->Period_Raw = avg_period;
        info->Pulse_Raw = avg_pulse;
    } else {
        info->Frequency = 0; info->DutyCycle = 0;
    }
    return 1;
}