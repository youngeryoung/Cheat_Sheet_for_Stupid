/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "key_driver.h"
#include "pwm_driver.h"
#include "oled.h"
#include "ic_driver.h"
#include "hcsr04.h"
#include "uart_driver.h"
#include "adc_hal.h"
#include "wave_proc.h"
#include "wave_ui.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

// 测试模式定义
typedef enum {
    MODE_INFO = 0,      // 0. 系统概览
    MODE_DUAL_GEN,      // 1. 双路信号发生器
    MODE_SCOPE,         // 2. 全能示波器 (整合了高低频)
    MODE_SENSOR,        // 3. 传感器
    MODE_UART_LOOP,     // 4. 串口透传
    MODE_MAX
} SystemMode_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

// --- 系统状态 ---
SystemMode_t CurrentMode = MODE_INFO;
uint8_t ModeNeedRefresh = 1; // 界面刷新标志
uint8_t PWM_Follow_Mode = 0; // 是否开启 PWM 相位跟随

// --- 信号发生器参数 ---
float Sig1_Freq = 1000.0f; // PA2 (TIM9)
float Sig2_Freq = 50.0f;   // PA1 (TIM2)
float Sig2_Duty = 5.0f;    // 舵机测试用

// --- 示波器/ADC相关 ---
Wave_Config_t WaveCfg = {
    .Zoom = 1, .RefVoltage = 3.3f, .ADC_Res = 4096, 
    .SampleRate = 420000000 / (3 + 12), // 需根据实际时钟校准
    .ScreenHeight = 64, .ScreenWidth = 128
};
Wave_Stats_t WaveRes;
Wave_Stats_t Stats;
uint8_t WavePlotBuf[128]; // 绘图缓存
Signal_Info_t InputSig;   // 测频结果

// --- 文本缓存 ---
char DispBuf[64];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

void User_Init(void);
void User_Loop(void);
float Get_Pot_Voltage(void); // 电位器读取工具函数

#define PRINT_FLOAT(val)  (int)(val), (int)(((val) - (int)(val)) * 100)

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// --- 1. 定时器回调 (负责 20ms 按键扫描) ---
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM10) {
        Key_Scan_IT(); 
    }
}

// --- 2. 输入捕获回调 (负责 超声波 & 频率计) ---
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    // 只有在跟随模式开启且是 TIM5 CH1 (PA0) 触发时才强行同步
    if (PWM_Follow_Mode && htim->Instance == TIM5 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) 
    {
        // 相位对齐
        __HAL_TIM_SET_COUNTER(SIGNAL2_TIM, 143);
    }
    
    HCSR04_IC_EventHandler(htim); // 超声波积木
    IC_EventHandler(htim);        // 测频积木 (TIM5)
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_TIM5_Init();
  MX_TIM9_Init();
  MX_USART6_UART_Init();
  MX_TIM10_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_TIM11_Init();
  /* USER CODE BEGIN 2 */

  User_Init();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
      User_Loop();

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

// 读取 PA3 电位器 (注入组模式)
float Get_Pot_Voltage(void) {
    HAL_ADCEx_InjectedStart(&hadc1);
    HAL_ADCEx_InjectedPollForConversion(&hadc1, 1);
    uint32_t raw = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_1);
    return (float)raw * 3.3f / 4096.0f;
}

// Mode 0: 系统概览 (测试按键、LED、电位器)
void Task_Info(void) {
    static uint32_t last_update = 0;
    
    // --- 按压时长统计逻辑 ---
    static uint32_t press_start_tick = 0;
    static uint32_t display_duration = 0; // 要显示的时间(ms)
    static uint8_t  is_pressing = 0;      // 是否有任意键被按住
    
    // 获取所有按键的原始状态位图 (Bit0-15:矩阵, Bit16:A, Bit17:B)
    uint32_t raw_state = Key_GetRawState();
    
    if (raw_state != 0) {
        // 有键按下
        if (is_pressing == 0) {
            is_pressing = 1; 
            press_start_tick = HAL_GetTick(); // 记录按下时刻
        }
        // 实时更新按住时长
        display_duration = HAL_GetTick() - press_start_tick;
    } else {
        // 没键按下
        if (is_pressing == 1) {
            is_pressing = 0;
            // 松开瞬间，display_duration 保持为最后一次的时长，不再更新
        }
    }

    // --- 界面刷新 (20FPS) ---
    if(HAL_GetTick() - last_update > 50 || ModeNeedRefresh) {
        last_update = HAL_GetTick(); ModeNeedRefresh = 0;
        
        OLED_NewFrame();
        
        // 1. 显示时长
        sprintf(DispBuf, "Time: %u ms", (unsigned)display_duration);
        OLED_PrintString(0, 0, DispBuf, OLED_COLOR_NORMAL);
        
        // 2. 绘制矩阵键盘状态 (4x4 Grid)
        // 起始坐标 (10, 16), 间距 12
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                int key_id = r * 4 + c;
                int x = 10 + c * 16;
                int y = 16 + r * 10;
                
                // 判断该位是否为1
                if ((raw_state >> key_id) & 0x01) {
                    // 按下：实心圆
                    OLED_SetPixel(x, y, OLED_COLOR_NORMAL);
                } else {
                    // 松开：空心圆 (画点模拟)
                    OLED_SetPixel(x, y-2, OLED_COLOR_NORMAL);
                    OLED_SetPixel(x, y+2, OLED_COLOR_NORMAL);
                    OLED_SetPixel(x-2, y, OLED_COLOR_NORMAL);
                    OLED_SetPixel(x+2, y, OLED_COLOR_NORMAL);
                }
            }
        }
        
        // 3. 绘制独立按键状态
        OLED_PrintString(80, 16, "Indep:", OLED_COLOR_NORMAL);
        
        // Btn A (ID 16)
        char btn_str[10];
        if ((raw_state >> KEY_ID_BTN_A) & 0x01) sprintf(btn_str, "A:DOWN");
        else sprintf(btn_str, "A: UP ");
        OLED_PrintString(80, 28, btn_str, OLED_COLOR_NORMAL);
        
        // Btn B (ID 17)
        if ((raw_state >> KEY_ID_BTN_B) & 0x01){RGB_Set(100,0,0);sprintf(btn_str, "B:DOWN");}
        else {RGB_Set(0,0,0);sprintf(btn_str, "B: UP ");}
        OLED_PrintString(80, 40, btn_str, OLED_COLOR_NORMAL);
        
        OLED_ShowFrame();
    }
}

// Mode 1: 双路信号发生器 (测试 PWM)
void Task_DualGen(void) {
    static uint32_t last = 0;
    static uint8_t need_update_pwm = 1;

    // --- 1. 按键调节 ---
    // 调节 Sig1 (PA2)
    if(Key_GetEvent_Press(0)) { Sig1_Freq += 1000; need_update_pwm=1; }
    if(Key_GetEvent_Press(1)) { Sig1_Freq -= 1000; if(Sig1_Freq<100) Sig1_Freq=1000; need_update_pwm=1; }
    
    // 调节 Sig2 (PA1)
    if(Key_GetEvent_Press(4)) { Sig2_Freq += 10; need_update_pwm=1; }
    if(Key_GetEvent_Press(5)) { Sig2_Freq -= 10; if(Sig2_Freq<10) Sig2_Freq=10; need_update_pwm=1; }

    // --- 2. 硬件更新 ---
    if(need_update_pwm) {
        PWM_Set_Freq_Duty(SIGNAL_TIM, SIGNAL_CH, Sig1_Freq, 50.0f);
        PWM_Set_Freq_Duty(SIGNAL2_TIM, SIGNAL2_CH, Sig2_Freq, Sig2_Duty);
        need_update_pwm = 0;
        
        // 串口打印调试
        printf("PWM Update: CH1=%dHz, CH2=%dHz\r\n", (int)Sig1_Freq, (int)Sig2_Freq);
    }

    // --- 3. 界面显示 ---
    if(HAL_GetTick() - last > 100 || ModeNeedRefresh) {
        last = HAL_GetTick(); ModeNeedRefresh = 0;
        
        OLED_NewFrame();
        OLED_PrintString(0, 0, "=== DUAL GEN ===", OLED_COLOR_NORMAL);
        sprintf(DispBuf, "PA2: %d Hz", (int)Sig1_Freq);
        OLED_PrintString(0, 20, DispBuf, OLED_COLOR_NORMAL);

        sprintf(DispBuf, "PA1: %d Hz", (int)Sig2_Freq);
        OLED_PrintString(0, 36, DispBuf, OLED_COLOR_NORMAL);
        OLED_PrintString(0, 54, "Keys: 0/1 & 4/5", OLED_COLOR_REVERSE);
        OLED_ShowFrame();
    }
}

// Mode 2: 全能示波器
// ================【选项一】纯软件模式 Task_Scope =================
// 特点：仅需连接 ADC (PA4)，完全依靠算法计算频率
// =================================================================
void Task_Scope(void) {
    static uint32_t last = 0;
    static uint32_t last_log = 0;
    
    // 记录 ADC 当前挡位 (LOW/MID/HIGH)
    static ADC_Speed_t state = ADC_SPEED_LOW; 

    // 刷新率控制 (33ms 刷新一次屏幕，约 30FPS)
    if(HAL_GetTick() - last > 33 || ModeNeedRefresh) {
        last = HAL_GetTick(); ModeNeedRefresh = 0;
        
        // 1. 同步采样率配置
        WaveCfg.SampleRate = ADC_Get_SampleRate();
        Wave_Proc_Init(&WaveCfg);
        
        // 2. 软件全能分析
        Wave_Analyze(ADC_HAL_Get_Buffer_Ptr(), ADC_BUFFER_SIZE, 0, &Stats);

        // 3. ADC 自动换挡
        if (Stats.Freq > 0 || 1) {
            switch (state) {
                case ADC_SPEED_LOW: // 当前是低速 (适合 < 1kHz)
                    if (Stats.Freq > 1200) { 
                        ADC_Set_Speed(ADC_SPEED_MID); 
                        state = ADC_SPEED_MID; 
                    }
                    break;
                case ADC_SPEED_MID: // 当前是中速 (适合 1k - 80kHz)
                    if (Stats.Freq > 80000) { 
                        ADC_Set_Speed(ADC_SPEED_HIGH); 
                        state = ADC_SPEED_HIGH; 
                    }
                    else if (Stats.Freq < 800) { 
                        ADC_Set_Speed(ADC_SPEED_LOW); 
                        state = ADC_SPEED_LOW; 
                    }
                    break;
                case ADC_SPEED_HIGH: // 当前是高速 (适合 > 80kHz)
                    if (Stats.Freq < 60000) { 
                        ADC_Set_Speed(ADC_SPEED_MID); 
                        state = ADC_SPEED_MID; 
                    }
                    break;
            }
        }

        // 4. 自动缩放波形 (Zoom)
        uint32_t ref_freq = (Stats.Freq > 0) ? Stats.Freq : (state == ADC_SPEED_LOW ? 10 : (state == ADC_SPEED_MID ? 800 : 40000));
        WaveCfg.Zoom = Wave_AutoZoom(ref_freq, 4);
        
        // 5. 生成绘图数据
        Wave_GeneratePlot(ADC_HAL_Get_Buffer_Ptr(), ADC_BUFFER_SIZE, &Stats, WavePlotBuf);
        
        // 6. OLED 显示
        OLED_NewFrame();
        Wave_UI_DrawGrid();
        Wave_UI_DrawCurve(WavePlotBuf, 128, 0, 0); 
        
        // 顶部信息栏 (反色)
        char buf[64];
        if (Stats.Freq > 9999) sprintf(buf, "F:%lukHz Type:%d", (unsigned long)Stats.Freq/1000, Stats.Type);
        else sprintf(buf, "F:%luHz Type:%d", (unsigned long)Stats.Freq, Stats.Type);
        OLED_PrintString(1, 0, buf, OLED_COLOR_REVERSE);
        
        // 底部信息栏 (峰峰值 + 有效值 + 档位)
        // Vpp: 峰峰值, R: 有效值(RMS), L/M/H: 当前采样档位
        sprintf(buf, "Vpp:%d.%02d R:%d.%02d %c", PRINT_FLOAT(Stats.Vpp), PRINT_FLOAT(Stats.Vrms), 
                state == ADC_SPEED_LOW ? 'L' : (state == ADC_SPEED_MID ? 'M' : 'H'));
        OLED_PrintString(0, 54, buf, OLED_COLOR_NORMAL);
        
        OLED_ShowFrame();
    }

    // 串口日志 (每秒一次)
    if (HAL_GetTick() - last_log >= 1000) {
        last_log = HAL_GetTick();
        printf("[SW Mode] F:%luHz Vpp:%d.%02dV Type:%d\r\n", (unsigned long)Stats.Freq, PRINT_FLOAT(Stats.Vpp), Stats.Type);
    }
}

// ================【选项二】硬件协助模式 Task_Scope ===============
// 特点：必须连接 PA0 (测频) 和 PA4 (ADC)。利用硬件定时器测频，ADC只负责画画。
// =================================================================
/*
void Task_Scope(void) {
    static uint32_t last = 0;
    static uint32_t last_log = 0;
    static ADC_Speed_t state = ADC_SPEED_LOW; 

    if(HAL_GetTick() - last > 30 || ModeNeedRefresh) {
        last = HAL_GetTick(); ModeNeedRefresh = 0;
        
        // 1. 获取硬件测频结果 (来自 ic_driver.c)
        IC_Get_Info(&InputSig);

        // 2. ADC 自动换挡 (使用硬件测得的精准频率)
        if (InputSig.Frequency > 0 || 1) {
            switch (state) {
                case ADC_SPEED_LOW:
                    if (InputSig.Frequency > 1200) { ADC_Set_Speed(ADC_SPEED_MID); state = ADC_SPEED_MID; }
                    break;
                case ADC_SPEED_MID:
                    if (InputSig.Frequency > 80000) { ADC_Set_Speed(ADC_SPEED_HIGH); state = ADC_SPEED_HIGH; }
                    else if (InputSig.Frequency < 800) { ADC_Set_Speed(ADC_SPEED_LOW); state = ADC_SPEED_LOW; }
                    break;
                case ADC_SPEED_HIGH:
                    if (InputSig.Frequency < 60000) { ADC_Set_Speed(ADC_SPEED_MID); state = ADC_SPEED_MID; }
                    break;
            }
        }

        // 3. 配置同步 & 数据分析
        WaveCfg.SampleRate = ADC_Get_SampleRate();
        Wave_Proc_Init(&WaveCfg);
        
        // 传入测得的 Frequency Duty
        Wave_Analyze(ADC_HAL_Get_Buffer_Ptr(), ADC_BUFFER_SIZE, InputSig.Frequency, &Stats);
        Stats.Duty = InputSig.DutyCycle;

        // 4. 自动缩放
        uint32_t ref_freq = (Stats.Freq > 0) ? Stats.Freq : (state == ADC_SPEED_LOW ? 10 : (state == ADC_SPEED_MID ? 800 : 40000));
        WaveCfg.Zoom = Wave_AutoZoom(ref_freq, 3);
        
        // 5. 生成绘图 & 刷新 OLED
        Wave_GeneratePlot(ADC_HAL_Get_Buffer_Ptr(), ADC_BUFFER_SIZE, &Stats, WavePlotBuf);
        
        OLED_NewFrame();
        Wave_UI_DrawGrid();
        Wave_UI_DrawCurve(WavePlotBuf, 128, 0, 0); 
        
        // 顶部显示 (显示 IC 标志，代表 Input Capture)
        char buf[64];
        if (InputSig.Frequency > 9999) sprintf(buf, "IC:%lukHz D:%lu%%", (unsigned long)(InputSig.Frequency/1000), (unsigned long)Stats.Duty);
        else sprintf(buf, "IC:%luHz D:%lu%%", (unsigned long)InputSig.Frequency, (unsigned long)Stats.Duty);
        OLED_PrintString(1, 0, buf, OLED_COLOR_REVERSE);
        
        // 底部显示 (峰峰值 + 有效值 + 档位)
        sprintf(buf, "Vpp:%d.%02d R:%d.%02d %c", PRINT_FLOAT(Stats.Vpp), PRINT_FLOAT(Stats.Vrms), 
                state == ADC_SPEED_LOW ? 'L' : (state == ADC_SPEED_MID ? 'M' : 'H'));
        OLED_PrintString(0, 54, buf, OLED_COLOR_NORMAL);
        
        OLED_ShowFrame();
    }

    if (HAL_GetTick() - last_log >= 1000) {
        last_log = HAL_GetTick();
        printf("[HW Mode] F:%luHz Vpp:%d.%02dV Type:%d\r\n", (unsigned long)Stats.Freq, PRINT_FLOAT(Stats.Vpp), Stats.Type);
    }
}
*/

// Mode 3: 传感器 (测试 超声波)`
void Task_Sensor(void) {
    static uint32_t last = 0;
    static uint32_t valid_dist = 0; // 保持最后一次有效值
    
    if(HAL_GetTick() - last > 200 || ModeNeedRefresh) {
         last = HAL_GetTick(); ModeNeedRefresh = 0;
        
        // 1. 先尝试获取上一次的结果
        // 如果测距完成，Get_Result 会返回非0值并重置状态为 IDLE
        uint32_t new_val = HCSR04_Get_Result();
        if (new_val > 0) {
            valid_dist = new_val; // 更新显示值
        }
        
        // 2. 启动下一次测量 (非阻塞)
        // 只有当状态为 IDLE 时 Start 才会生效，所以这行代码很安全
        HCSR04_Start(); 
        
        // 3. 刷新显示
        OLED_NewFrame();
        OLED_PrintString(0, 0, "=== SENSOR ===", OLED_COLOR_NORMAL);
        
        char buf[32];
        if(valid_dist > 0) {
            sprintf(buf, "Dist: %lu.%lu cm", (unsigned long)valid_dist/100, (unsigned long)(valid_dist%100)/10);
        } else {
            // 如果刚上电还没数据
            sprintf(buf, "Dist: Measuring..."); 
        }
        OLED_PrintString(10, 25, buf, OLED_COLOR_NORMAL);
        
        // 进度条动画
        uint8_t bar_w = (valid_dist > 3000) ? 128 : (valid_dist * 128 / 3000);
        OLED_DrawRectangle(0, 45, 128, 10, OLED_COLOR_NORMAL);
        OLED_DrawFilledRectangle(0, 45, bar_w, 10, OLED_COLOR_NORMAL);
        
        OLED_ShowFrame();
    }
}

// Mode 4: 串口透传 (测试 UART1/6)
void Task_UART(void) {
    char rx[128];
    static uint8_t update = 1;
    
    if(UART1_Get_Packet(rx)) {
        UART1_Send("Echo U1: %s\r\n", rx);
        snprintf(DispBuf, sizeof(DispBuf), "U1: %.20s", rx); 
        update = 1;
    }

    if(UART6_Get_Packet(rx)) {
        UART6_Send("Echo U6: %s\r\n", rx);
        snprintf(DispBuf, sizeof(DispBuf), "U6: %.20s", rx);
        update = 1;
    }
    
    if(update || ModeNeedRefresh) {
        ModeNeedRefresh = 0; update = 0;
        OLED_NewFrame();
        OLED_PrintString(0, 0, "=== UART LOOP ===", OLED_COLOR_NORMAL);
        OLED_PrintString(0, 16, "Send data to PC...", OLED_COLOR_NORMAL);
        OLED_PrintString(0, 32, "Last Recv:", OLED_COLOR_NORMAL);
        OLED_PrintString(0, 44, DispBuf, OLED_COLOR_NORMAL);
        OLED_ShowFrame();
    }
}

// =============================================================
//                    系统初始化与主循环
// =============================================================

void User_Init(void)
{
    // 1. 基础启动
    RGB_Init();

    LED0_Set(1); // 开始启动标识

    HAL_TIM_Base_Start_IT(&htim10); // 按键心跳
    Key_Init();
    OLED_Init();

    LED1_Set(10); RGB_Set(0,0,100); // 核心外设初始化完成标识
    
    // 2. 信号系统启动
    IC_Init();         // TIM5 A0
    ADC_HAL_Init();    // ADC1 A4
    ADC_HAL_Start();   // DMA Start
    Wave_Proc_Init(&WaveCfg);
    
    LED1_Set(30); RGB_Set(0,100,0); // 信号系统启动完成标识

    // 3. 发生器启动
    HAL_TIM_PWM_Start(SIGNAL_TIM, SIGNAL_CH);   // A2
    HAL_TIM_PWM_Start(SIGNAL2_TIM, SIGNAL2_CH); // A1
    PWM_Set_Freq_Duty(SIGNAL_TIM, SIGNAL_CH, Sig1_Freq, 50.0f);
    PWM_Set_Freq_Duty(SIGNAL2_TIM, SIGNAL2_CH, Sig2_Freq, Sig2_Duty);
    
    LED1_Set(50); RGB_Set(100,0,0); // 发生器启动完成标识

    // 4. 其他外设
    HCSR04_Init();
    UART_Init();
    
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET); // 熄灭 LED0
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); // 反转 LED0
    LED1_Set(0); RGB_Set(0,0,0);

    // 5. 欢迎音效
    printf("System Boot OK.\r\n");
    Buzzer_On(2000, 0.1); HAL_Delay(100); Buzzer_Off();
}

void User_Loop(void)
{
    if(Key_GetEvent_Press(KEY_ID_BTN_A)) {
        // [安全退出逻辑] 
        // 只要离开示波器模式，强制将 TIM5 设为最大分频(安全模式)
        // 这样即使切到菜单时输入信号还是 400kHz，中断也不会卡死 UI
        if (CurrentMode == MODE_SCOPE) {
            IC_Set_Safe_Mode();
        }

        CurrentMode++;
        if(CurrentMode >= MODE_MAX) CurrentMode = MODE_INFO;
        
        // 标记刷新，清除按键事件
        ModeNeedRefresh = 1; 
        
        // 反馈音效
        Buzzer_On(4000, 0.5); HAL_Delay(30); Buzzer_Off();
    }
    
    switch(CurrentMode) {
        case MODE_INFO:     Task_Info(); break;
        case MODE_DUAL_GEN: Task_DualGen(); break;
        case MODE_SCOPE:    Task_Scope(); break;
        case MODE_SENSOR:   Task_Sensor(); break;
        case MODE_UART_LOOP: Task_UART(); break;
        default: CurrentMode = MODE_INFO;
    }
}

// [演示逻辑] 屏幕刷完后让一个测试 LED 翻转，证明 DMA 跑通了
/*
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    if(hi2c->Instance == I2C1) {
        HAL_GPIO_TogglePin(LED0_GPIO_Port, LED0_Pin);
    }
}
*/

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
