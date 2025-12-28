// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#include "hcsr04.h"

// --- 内部状态变量 ---
typedef enum {
    HC_IDLE = 0,    // 空闲
    HC_WAITING,     // 等待响应
    HC_DONE         // 完成
} HC_State_t;

static volatile HC_State_t sensor_state = HC_IDLE;
static volatile uint32_t   last_result_01mm = 0;
static volatile uint32_t last_start_tick = 0; // 记录最近一次触发的时间

/**
 * @brief 初始化 HC-SR04 传感器相关硬件
 */
void HCSR04_Init(void)
{
    // 1. 初始化 Trig 引脚为低电平
    HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_RESET);
    
    // 2. 开启 DWT 计数器 (用于高精度阻塞测量)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    // 3. 配置 TIM1 为复位模式 (外部触发复位 CNT)
    HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_RESET);
    
    TIM_SlaveConfigTypeDef sSlaveConfig = {0};

    // 配置从模式：检测到触发信号就复位计数器 CNT
    sSlaveConfig.SlaveMode = TIM_SLAVEMODE_RESET;
    // 配置输入源：使用滤波后的 TI1 输入 (即 CH1 引脚)
    sSlaveConfig.InputTrigger = TIM_TS_TI1FP1;
    // 极性配置 (虽然后面Start时会再配，但这里保持一致)
    sSlaveConfig.TriggerPolarity = TIM_TRIGGERPOLARITY_RISING;
    sSlaveConfig.TriggerPrescaler = TIM_TRIGGERPRESCALER_DIV1;
    sSlaveConfig.TriggerFilter = 0;

    if (HAL_TIM_SlaveConfigSynchro(ECHO_TIM_HANDLE, &sSlaveConfig) != HAL_OK)
    {
        // 初始化错误处理
        Error_Handler(); 
    }
}

// ================== 非阻塞模式实现 ==================

/**
 * @brief  触发一次测距 (完全非阻塞)
 * @note   发送 Trig 信号并开启输入捕获中断，立即返回
 */
void HCSR04_Start(void)
{
    // --- 1. 智能屏蔽与强制回收 ---
    if (sensor_state == HC_WAITING)
    {
        if (HAL_GetTick() - last_start_tick < 30) {
            return; // 还没到 30ms，上一次测量还没凉透，忽略本次请求
        } else {
            // 已经过了 30ms 还没动静，说明上次硬件捕获已经死锁
            // 必须手动停止 IT，否则 HAL 库内部状态位可能无法更新
            HAL_TIM_IC_Stop_IT(ECHO_TIM_HANDLE, TIM_CHANNEL_2);
            HAL_TIM_IC_Stop(ECHO_TIM_HANDLE, TIM_CHANNEL_1);
        }
    }

    sensor_state = HC_WAITING;

    // 1. 发送 Trig 脉冲
    HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_SET);

    // 11us 延时开始
    uint32_t t0 = DWT->CYCCNT;

    // 2. 启动输入捕获 (双通道)
    // 清除之前的标志位，防止误触发
    __HAL_TIM_CLEAR_FLAG(ECHO_TIM_HANDLE, TIM_FLAG_CC1 | TIM_FLAG_CC2 | TIM_FLAG_UPDATE);
    // 开启 CH1 (上升沿) 的捕获
    HAL_TIM_IC_Start(ECHO_TIM_HANDLE, TIM_CHANNEL_1);
    // 开启 CH2 (下降沿) 的中断
    HAL_TIM_IC_Start_IT(ECHO_TIM_HANDLE, TIM_CHANNEL_2);
    // 重置超时计时器
    last_start_tick = HAL_GetTick();

    // 11us 延时结束
    while ((DWT->CYCCNT - t0) < TRIG_PULSE_TICKS);


    HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_RESET);
}

/**
 * @brief  中断回调处理函数
 * @param  htim 定时器句柄
 * @note   请在 main.c 的 HAL_TIM_IC_CaptureCallback 中调用此函数
 */
void HCSR04_IC_EventHandler(TIM_HandleTypeDef *htim)
{
    // 检查是否是 TIM1 的 CH2 (下降沿捕获)
    if (htim->Instance == ECHO_TIM_INSTANCE && htim->Channel == ECHO_TIM_ACTIVE_CHANNEL)
    {
        // 读取 CCR2，因为计数器在上升沿被硬件清零了，
        // 所以 CCR2 里的值直接就是高电平持续时间的 Tick 数！
        uint32_t pulse_width = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2);
        
        // 计算距离 (1us/tick)
        // 距离 = Time(us) * 0.034 / 2  => Time * 0.017 (cm) => Time * 1.7 (0.1mm)
        last_result_01mm = (uint32_t)(pulse_width * 17 / 10);

        // 测量完成，关闭捕获以节省资源，等待下一次 Start
        HAL_TIM_IC_Stop_IT(ECHO_TIM_HANDLE, TIM_CHANNEL_2);
        HAL_TIM_IC_Stop(ECHO_TIM_HANDLE, TIM_CHANNEL_1);

        sensor_state = HC_DONE;
    }
}

 /**
 * @brief  获取非阻塞测量结果
 * @return 距离值 (0.1mm)，0 表示测量中，45000 表示超出量程
 * @note   读取成功后会自动清除完成标志，需再次调用 Start 才能开始新测量
 */
uint32_t HCSR04_Get_Result(void)
{
    // 1. 检查是否正在测量中，并进行超时判定
    if (sensor_state == HC_WAITING)
    {
        // 如果距离 Start 已经超过 30ms (5m 极限)
        if (HAL_GetTick() - last_start_tick > 30)
        {
            // --- 强制回收资源 ---
            HAL_TIM_IC_Stop_IT(ECHO_TIM_HANDLE, TIM_CHANNEL_2);
            HAL_TIM_IC_Stop(ECHO_TIM_HANDLE, TIM_CHANNEL_1);
            sensor_state = HC_IDLE; 
            
            // 返回 45000 (代表 4500.0mm)，提示 UI 超出量程
            return 45000; 
        }
        // 还没超时也没完成，返回 0 提示继续等待
        return 0; 
    }

    // 2. 检查是否正常完成
    if (sensor_state == HC_DONE)
    {
        sensor_state = HC_IDLE; // 重置状态，准备下一次触发
        
        // 容错：如果硬件捕获的值异常大，也限制在 45000
        if (last_result_01mm > 45000) return 45000;
        
        return last_result_01mm;
    }

    return 0; // 空闲状态
}


// ================== 阻塞模式实现 (DWT高精度) ==================

/**
 * @brief 使用阻塞方式进行高精度测量
 * @return 距离值：微米(0.001mm)，0 表示测量失败或超出量程
 */
uint32_t HCSR04_Measure_Blocking_HighRes(void)
{
    uint32_t start_cycle, end_cycle;
    uint32_t timeout = 0;

    // 1. Trig 脉冲
    HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_SET);
    
    // 11us 延时
    uint32_t t0 = DWT->CYCCNT;
    while ((DWT->CYCCNT - t0) < TRIG_PULSE_TICKS);

    HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_RESET);

    // 2. 等待 Echo 高电平 (超时退出防止死锁)
    timeout = 1000000; 
    while (Echo_GPIO_Port->IDR & Echo_Pin) // 这里假设 Echo 是 PA8
    {
        if (timeout-- == 0) return 0;
    }
    start_cycle = DWT->CYCCNT; // 记录 CPU 周期数 (84MHz)

    // 3. 等待 Echo 低电平
    timeout = 1000000;
    while ((Echo_GPIO_Port->IDR & Echo_Pin) != 0)
    {
        if (timeout-- == 0) return 0;
    }
    end_cycle = DWT->CYCCNT;

    // 4. 计算
    uint32_t cycles;
    if (end_cycle >= start_cycle)
        cycles = end_cycle - start_cycle;
    else
        cycles = (0xFFFFFFFF - start_cycle) + end_cycle + 1;

    // 5. 换算
    // 频率 84MHz, 1周期 = 1/84 us
    // 时间(us) = cycles / 84
    uint32_t distance_um = (uint32_t)((uint64_t)cycles * 170000 / 84); // 微米
    return distance_um;
}