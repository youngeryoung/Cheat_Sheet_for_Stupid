// ================================  
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// ================================

#include "pwm_driver.h"
#include "main.h"
#include "stm32f401xc.h"
#include <stdint.h>


// ================= 1. 核心引擎实现 (必须放在业务函数前面) =================

/**
 * @brief 全能设置 (PSC, ARR, CCR 全算)
 * @param htim 定时器句柄
 * @param Channel 通道
 * @param Freq_Hz 频率 (Hz)
 * @param Duty_Percent 占空比 (0-100%)
 */
void PWM_Set_Freq_Duty(TIM_HandleTypeDef *htim, uint32_t Channel, float Freq_Hz, float Duty_Percent)
{
    if (Freq_Hz < 1.0f) Freq_Hz = 1.0f;
    
    // 获取时钟源频率 (STM32F401 APB1/APB2 Timer Clocks 通常配置为 84MHz)
    uint32_t timer_clock = 84000000; 
    
    // 计算总周期数
    uint32_t total_cycles = (uint32_t)((float)timer_clock / Freq_Hz);
    
    uint16_t psc = 0;
    uint32_t arr = 0;

    // 自动计算 PSC 和 ARR
    // if (total_cycles <= 65535 || htim == &htim2 || htim == &htim5) {
    if (total_cycles <= 65535 || htim == &htim2) {
        psc = 0;
        arr = total_cycles - 1;
    } else {
        // 超过16位范围，需要分频
        psc = (uint16_t)(total_cycles / 65536);
        arr = (timer_clock / ((psc + 1) * (uint32_t)Freq_Hz)) - 1;
    }

    // 防止占空比越界
    if(Duty_Percent > 100.0f) Duty_Percent = 100.0f;
    if(Duty_Percent < 0.0f)   Duty_Percent = 0.0f;

    // 计算 CCR
    uint32_t ccr = (uint32_t)((float)(arr + 1) * Duty_Percent / 100.0f);

    __HAL_TIM_SET_PRESCALER(htim, psc);
    __HAL_TIM_SET_AUTORELOAD(htim, arr);
    __HAL_TIM_SET_COMPARE(htim, Channel, ccr);
    
    // 产生更新事件使设置生效
    HAL_TIM_GenerateEvent(htim, TIM_EVENTSOURCE_UPDATE);
}

/**
 * @brief 快速设置占空比 (只改 CCR，不改频率)
 * @param htim 定时器句柄
 * @param Channel 通道
 * @param Duty_Percent 占空比 (0-100%)
 */
void PWM_Set_Duty_Fast(TIM_HandleTypeDef *htim, uint32_t Channel, float Duty_Percent)
{
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(htim);
    
    if(Duty_Percent > 100.0f) Duty_Percent = 100.0f;
    if(Duty_Percent < 0.0f)   Duty_Percent = 0.0f;

    uint32_t ccr = (uint32_t)((float)(arr + 1) * Duty_Percent / 100.0f);
    
    __HAL_TIM_SET_COMPARE(htim, Channel, ccr);
}

/**
 * @brief 智能设置频率 (保持 Duty% 不变)
 * @param htim 定时器句柄
 * @param Channel 通道
 * @param Freq_Hz 频率 (Hz)
 */
void PWM_Set_Freq_Smart(TIM_HandleTypeDef *htim, uint32_t Channel, float Freq_Hz)
{
    uint32_t now_arr = __HAL_TIM_GET_AUTORELOAD(htim);
    uint32_t now_ccr = __HAL_TIM_GET_COMPARE(htim, Channel);
    float current_duty = 0.0f;
    
    if (now_arr > 0) {
        current_duty = (float)now_ccr / (float)(now_arr + 1) * 100.0f;
    }
    
    PWM_Set_Freq_Duty(htim, Channel, Freq_Hz, current_duty);
}


// ================= 2. 业务封装实现 =================

/**
 * @brief 初始化 RGB 灯与 LED1
 */
void RGB_Init(void)
{
    HAL_TIM_PWM_Start(RGB_TIM, RGB_CH_R);
    HAL_TIM_PWM_Start(RGB_TIM, RGB_CH_G);
    HAL_TIM_PWM_Start(RGB_TIM, RGB_CH_B);
    HAL_TIM_PWM_Start(LED_TIM, LED_CH);
    
    // 初始化为 1kHz, 0% 亮度 (熄灭)
    PWM_Set_Freq_Duty(RGB_TIM, RGB_CH_R, 1000.0f, 0.0f);
    PWM_Set_Freq_Duty(RGB_TIM, RGB_CH_G, 1000.0f, 0.0f);
    PWM_Set_Freq_Duty(RGB_TIM, RGB_CH_B, 1000.0f, 0.0f);
    PWM_Set_Freq_Duty(LED_TIM, LED_CH, 1000.0f, 100.0f);
}

/**
 * @brief 设置 RGB 颜色
 * @param r 红色分量 (0-100)
 * @param g 绿色分量 (0-100)
 * @param b 蓝色分量 (0-100)
 */
void RGB_Set(uint8_t r, uint8_t g, uint8_t b)
{
    // 使用快速设置函数
    PWM_Set_Duty_Fast(RGB_TIM, RGB_CH_R, (float)r);
    PWM_Set_Duty_Fast(RGB_TIM, RGB_CH_G, (float)g);
    PWM_Set_Duty_Fast(RGB_TIM, RGB_CH_B, (float)b);
}

/**
 * @brief LED0 开关控制 (GPIO方式)
 * @param state 状态 (0=灭, 1=亮)
 */
void LED0_Set(uint8_t state) {
    if (state != 0) {
        // 点亮：输出低电平
        HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, GPIO_PIN_RESET);
    } else {
        // 熄灭：输出高电平
        HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, GPIO_PIN_SET);
    }
}

/**
 * @brief LED1 亮度控制 (PWM方式)
 * @param duty 亮度百分比 (0-100)
 */
void LED1_Set(uint8_t duty) {
    // 1. 限制输入范围
    if (duty > 100) duty = 100;
    if (duty < 0)   duty = 0;

    // 2. 计算实际占空比
    // 如果你的 LED 是高电平点亮，直接使用 duty
    // 如果你的 LED 是低电平点亮，使用 (100 - duty)
    // 这里假设 LED 是低电平点亮 (PB9 常见接法)
    uint8_t actual_duty = 100 - duty;

    // 3. 调用之前的快速设置函数修改 CCR
    PWM_Set_Duty_Fast(LED_TIM, LED_CH, (float)actual_duty);
}

/**
 * @brief 开蜂鸣器：指定频率和占空比
 * @param Freq_Hz 频率 (Hz)
 * @param Duty 占空比 (0-100%)
 */
void Buzzer_On(uint32_t Freq_Hz, float Duty)
{
    PWM_Set_Freq_Duty(BUZZER_TIM, BUZZER_CH, (float)Freq_Hz, Duty);
    HAL_TIM_PWM_Start(BUZZER_TIM, BUZZER_CH);
}

/**
 * @brief 关蜂鸣器
 */
void Buzzer_Off(void)
{
    HAL_TIM_PWM_Stop(BUZZER_TIM, BUZZER_CH);
}

/**
 * @brief 设置蜂鸣器占空比 (保持频率不变)
 * @param Duty 占空比 (0-100%)
 */
void Buzzer_Duty(float Duty)
{
    PWM_Set_Duty_Fast(BUZZER_TIM, BUZZER_CH, Duty);
}