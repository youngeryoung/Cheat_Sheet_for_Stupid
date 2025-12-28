# Cheat_Sheet_for_Stupid

## 0. 鸣谢 (Acknowledgements)

本项目的诞生，离不开开源社区的无私奉献与奠基。我们站在巨人的肩膀上，得以在此探索嵌入式的无穷可能。我们谨向以下开源项目及其才华横溢的作者们，致以最诚挚的感谢与崇高的敬意：

*   ** 字体工具内核 (uFont)**:  
    由 **[AntonVanke](https://github.com/AntonVanke)** 开发的 **[MicroPython-uFont](https://github.com/AntonVanke/MicroPython-uFont)** 及其配套工具。  
    该项目优秀的字模提取逻辑为本项目重构 PC 端取模软件提供了核心算法支持，使其能够从 Python 环境平滑迁移至 C 语言嵌入式开发流。

*   ** 数据结构灵感 (LEDFont)**:  
    由 **[BaudDance (波特率动)](https://github.com/BaudDance)** 开发的 **[LEDFont](https://github.com/BaudDance/LEDFont)**。  
    其紧凑而优雅的 C 语言字库存储结构（Struct Design）为本项目提供了关键的架构灵感，极大地优化了单片机有限 Flash 空间内的中英文字符索引效率。

## 1. 摘要 (Executive Summary)

**Cheat_Sheet_for_Stupid (CSfS)** 是一套专为 STM32F401CCU6 平台打造的高性能硬件抽象层（HAL）二次封装库。本项目专为电子设计竞赛（电赛）初学者设计，旨在解决原生 HAL 库开发繁琐、外设配置复杂、实时性难以保证的痛点。

采用了“全异步驱动架构”，配套专用开发板与 PC 端取模工具。该框架在仅使用 FPU（浮点单元）而**摒弃 DSP 库**的前提下，实现了简易示波器、多路信号发生、高精度测频及鲁棒的人机交互功能，是新手“弯道超车”的硬件修理包。

---

## 2. 设计哲学 (Design Philosophy)

*   **傻瓜化接口 (API for Humans)**：隐藏 ARR/PSC 计算、DMA 内存对齐、中断标志位清除等底层细节。用户只需调用 `Set_Freq(1000)`，而非查阅手册计算分频系数。
*   **非阻塞优先 (Non-blocking First)**：OLED 刷新、串口接收、ADC 采样、超声波测距均采用 DMA 或中断状态机，彻底告别 `HAL_Delay` 导致的系统卡顿。
*   **鲁棒性 (Robustness)**：针对电赛常见的“I2C死锁”、“串口过载”、“波形抖动”问题，内置了硬件级复位与软件迟滞滤波算法。

---

## 3. 核心子系统架构 (Core Subsystems)

### 3.1 “永不卡死”的 OLED 显示引擎
CSfS 采用了一套极其激进的 SSD1306 驱动方案，解决了 I2C 协议在电赛现场常见的干扰死机问题。

*   **DMA + 水平寻址模式**：重构了显存映射，利用 `OLED_DMA_Buffer[1025]` 实现单次 DMA 传输刷整屏。CPU 仅需 10us 发起传输，随后即可处理其他逻辑，极大释放算力。
*   **智能掉线恢复 (Anti-Freeze)**：驱动内置心跳检测。一旦检测到 I2C 总线被拉死（SDA 锁低），立即启动 `I2C_Hardware_Reset` 序列，通过 GPIO 模拟 9 个时钟脉冲强制解锁从机，实现“毫秒级”无感重连。
*   **UTF-8 混合渲染**：配套 Python 取模工具，支持 ASCII (O(1)索引) 与 Unicode (二分查找) 混合渲染，按需生成字库，节省 Flash 空间。

### 3.2 全能信号链：示波器与波形分析
这是本框架最硬核的部分，利用 STM32F401 的 12bit ADC 实现了类似商用示波器的功能。

*   **智能换挡 (Auto-Gear ADC)**：
    *   低速档 (~21 kSps)：适合 <1kHz 信号，深度过采样。
    *   中速档 (~525 kSps)：适合音频段信号。
    *   高速档 (~2.8 Msps)：极限采样，适合 <100kHz 信号。
    *   *系统根据输入信号频率自动在 DMA 运行中动态切换分频系数与采样周期。*
*   **软件触发器 (Software Trigger)**：内置迟滞比较器算法，寻找稳定的上升沿触发点，并具备 `TRIGGER_LOCK_RANGE` 锁定逻辑，确保波形在屏幕上稳定不乱跳。
*   **波形识别**：基于波峰因数（Crest Factor = Vpeak/Vrms）算法，自动识别正弦波、方波、三角波与直流信号。
*   **DSP 的取舍**：经过评估，CMSIS-DSP 库会占用约 100KB Flash。CSfS 采用手写的优化算法配合 F4 的 FPU 硬件浮点，在保证精度的前提下，将代码体积控制在极小范围，留给用户更多空间。

### 3.3 自适应频率计 (Input Capture)
针对传统输入捕获在低频和高频无法兼顾的问题，设计了迟滞状态机。

*   **动态预分频策略**：
    *   驱动会自动监控输入频率。当频率 >440kHz 时，自动切换至 8 分频；频率 <90kHz 时，直通不分频。
    *   这使得测量范围覆盖 1Hz - 1MHz，且保持高精度。
*   **双通道测量**：利用 TIM 的 CH1 (Reset Mode) 和 CH2，一次中断同时获取周期与占空比。

### 3.4 交互与控制
*   **万能 PWM**：`PWM_Set_Freq_Duty` 函数内部集成了时钟树计算器。用户只需输入目标频率（如 50Hz），函数自动计算最佳 PSC 和 ARR 组合，无论是控制舵机还是蜂鸣器都能“一行代码搞定”。
*   **事件驱动按键**：支持 4x4 矩阵与独立按键。采用位图（Bitmap）管理按键状态，提供 `Key_GetEvent_Press`（短按）与 `Key_IsDown`（长按）接口，且完全消抖。
*   **不定长串口透传**：利用 IDLE（空闲中断）+ DMA 双缓冲接收，完美处理不定长数据包（如 GPS、蓝牙指令），彻底解决“数据断截”问题。

---

## 4. 配套工具链

### 4.1 PC 端字库生成器
为了在有限的 Flash 中显示中文，提供了基于 Python/Tkinter 的 GUI 工具。
*   **功能**：输入项目所需的特定汉字（如“电压”、“频率”），自动扫描并生成精简的 C 语言字库结构体。
*   **优势**：相比全字库节省 95% 空间，相比传统取模软件操作更直观，支持抗锯齿预览。
>   打包好的Windows/Linux应用程序:[123云盘链接:https://www.123865.com/s/LvggTd-HhB6H?pwd=0721#]https://www.123865.com/s/LvggTd-HhB6H?pwd=0721# 提取码：0721

### 4.2 硬件参考设计
*   **MCU**: STM32F401CCU6 (UFQFPN48)
*   **Display**: 0.96' OLED (I2C)
*   **Input**: 4x4 Matrix Keypad + 2 Independent Buttons
*   **Sensor**: HC-SR04 / Potentiometer
*   **Interface**: UART / SWD

---

## 5. 快速上手
### 5.0. 使用BMF(PC 端字库生成器)生成font.c/.h文件，并放入驱动文件夹

### 5.1. 硬件连接映射图 (Hardware Pinout Map)
基于 **STM32F401CCU6** 核心板构建（84MHz 主频, 开启 FPU, 关闭 DSP 库）。所有外设引脚分配已在 HAL 库底层固定，开发者需严格遵循以下连接：

### 显示与模拟前端
*   **OLED 显示屏**: 
    *   接口: I2C1 (DMA 模式)
    *   引脚: **PB6 (SCL)**, **PB7 (SDA)**
    *   地址: 0x78
*   **ADC 输入**: 
    *   通道: ADC1_IN4
    *   引脚: **PA4**
    *   *注意: 配合示波器功能使用。*

### 信号处理与发生
*   **信号发生器 (PWM Output)**:
    *   通道 1: **PA2** (TIM9_CH1)
    *   通道 2: **PA1** (TIM2_CH2)
*   **输入捕获 (Frequency Meter)**:
    *   通道: TIM5_CH1 (TI1FP1)
    *   引脚: **PA0**
*   **超声波传感器 (HC-SR04)**:
    *   Trig (触发): **PB10**
    *   Echo (回响): **PA8** (TIM1_CH1)

### 人机交互 (HMI)
*   **4x4 矩阵键盘**:
    *   行 (Rows, 开漏输出): **PA15, PB3, PB4, PB5**
    *   列 (Cols, 上拉输入): **PB12, PB13, PB14, PB15**
*   **独立按键**:
    *   Btn A: **PA5**
    *   Btn B: **PB1**
*   **状态指示灯**:
    *   LED0 (系统): **PC13**
    *   LED1 (PWM调光): **PB9** (TIM11_CH1)
    *   RGB LED: **PA6 (R), PA7 (G), PB0 (B)** (TIM3)
*   **蜂鸣器**:
    *   引脚: **PB8** (TIM4_CH3)

### 通信接口
*   **UART1 (Console/Printf)**: **PA9 (TX), PA10 (RX)**
*   **UART6 (Aux)**: **PA11 (TX), PA12 (RX)**

---

### 5.2. 驱动接口详解 (API Reference)

本章节详细描述各模块的头文件约束与核心函数。

### A. OLED 显示驱动 (`oled.h/c`)
基于 I2C DMA 的双缓冲显示驱动，支持 UTF-8 字符集。
*   **核心控制**:
    *   `void OLED_Init(void)`: 硬件复位与初始化序列。
    *   `void OLED_NewFrame(void)`: 清空 DMA 显存缓冲区 (memset 0)。
    *   `void OLED_ShowFrame(void)`: **必须调用**。触发 DMA 传输将缓冲区推送到屏幕。
    *   `uint8_t OLED_IsBusy(void)`: 查询 DMA 是否正在传输中。
*   **图形绘制**:
    *   `void OLED_SetPixel(x, y, color)`: 画点。
    *   `void OLED_DrawLine(x1, y1, x2, y2, color)`: Bresenham 直线算法。
    *   `void OLED_DrawCircle(x0, y0, r, color)` / `OLED_DrawFilledCircle(...)`: 空心/实心圆。
    *   `void OLED_DrawRectangle(x, y, w, h, color)` / `OLED_DrawFilledRectangle(...)`: 矩形。
    *   `void _Draw_Bitmap(x, y, w, h, bitmap, color)`: 位图绘制底层接口。
*   **文本引擎**:
    *   `void OLED_PrintString(x, y, str, color)`: 支持 ASCII 及自定义 Unicode 汉字混合显示。
    *   `void OLED_PrintInt(x, y, num, color)`: 整数打印。
    *   `void OLED_PrintFloat(x, y, val, precision, color)`: 浮点打印，支持 1-9 位小数精度。
*   **常量定义**:
    *   分辨率: `OLED_WIDTH` (128), `OLED_HEIGHT` (64)。
    *   颜色: `OLED_COLOR_NORMAL` (白字黑底), `OLED_COLOR_REVERSE` (反色)。
    *   时钟: `OLED_I2C_CLK_SPEED` 默认设定为 1MHz (超频模式)。

### B. ADC 硬件抽象层 (`adc_hal.h/c`)
管理 ADC1 的 DMA 循环采集。
*   **控制接口**:
    *   `void ADC_HAL_Init(void)`: 初始化外设。
    *   `void ADC_HAL_Start(void)`: 启动 DMA 循环传输。
    *   `void ADC_HAL_Stop(void)`: 停止采集。
    *   `void ADC_Set_Speed(ADC_Speed_t speed)`: 动态重配 ADC 分频系数与采样时间。
*   **数据访问**:
    *   `uint32_t ADC_Get_SampleRate(void)`: 获取当前物理采样率 (例如高速档约 2.8Msps)。
    *   `uint16_t* ADC_HAL_Get_Buffer_Ptr(void)`: 获取原始数据缓冲区指针。
*   **约束**:
    *   缓冲区深度: `ADC_BUFFER_SIZE` 固定为 **8500** 点。
    *   速度档位: `ADC_SPEED_LOW` (<1kHz), `ADC_SPEED_MID` (1k-80kHz), `ADC_SPEED_HIGH` (>80kHz)。

### C. 波形处理与 UI (`wave_proc.h/c`, `wave_ui.h/c`)
*   **信号分析 (`wave_proc`)**:
    *   `void Wave_Proc_Init(Wave_Config_t *config)`: 载入配置。
    *   `void Wave_Analyze(uint16_t *buf, uint32_t len, uint32_t ext_freq, Wave_Stats_t *stats)`:
        *   若 `ext_freq > 0`: 使用外部传入的频率（来自硬件输入捕获）。
        *   若 `ext_freq == 0`: 使用软件过零检测算法计算频率。
        *   计算结果填充至 `Wave_Stats_t`。
    *   `float Wave_AutoZoom(uint32_t freq, uint8_t cycles)`: 计算最佳 X 轴缩放比例。
    *   `void Wave_GeneratePlot(..., uint8_t *out_buf)`: 将 ADC 数值映射为 OLED Y 轴坐标。
*   **波形渲染 (`wave_ui`)**:
    *   `void Wave_UI_SetBaseline(int16_t offset)`: 设置波形显示的垂直偏移量。
    *   `void Wave_UI_DrawGrid(void)`: 绘制背景网格。
    *   `void Wave_UI_DrawCurve(y_data, len, off_x, off_y)`: 绘制波形曲线。
    *   `void Wave_UI_ShowStats(Wave_Stats_t *res)`: 在顶部栏绘制 Vpp/Freq 数据。
*   **数据结构**:
    *   `Wave_Stats_t`: 包含 `.Vpp`, `.Vrms`, `.Freq`, `.Duty`, `.Type` (SINE/SQUARE/TRIANGLE/DC)。

### D. 输入捕获/频率计 (`ic_driver.h/c`)
基于 TIM5 的高精度测频模块。
*   **接口**:
    *   `void IC_Init(void)`: 启动 TIM5。
    *   `void IC_Set_Safe_Mode(void)`: 强制设置为 DIV8 分频（防止高频信号导致中断死锁）。
    *   `void IC_EventHandler(TIM_HandleTypeDef *htim)`: **必须**在 ISR 回调中调用。
    *   `uint8_t IC_Get_Info(Signal_Info_t *info)`: 获取最新数据，返回 1 表示数据有效。
*   **机制**:
    *   内置迟滞比较逻辑，根据输入频率自动切换硬件预分频器 (DIV1/2/4/8)。

### E. PWM 驱动 (`pwm_driver.h/c`)
*   **核心功能**:
    *   `void PWM_Set_Freq_Duty(htim, ch, freq, duty)`: 自动计算 PSC 和 ARR 以匹配目标频率。
    *   `void PWM_Set_Duty_Fast(htim, ch, duty)`: 仅更新 CCR（高效率）。
    *   `void PWM_Set_Freq_Smart(htim, ch, freq)`: 更新频率但保持占空比百分比不变。
*   **外设封装**:
    *   RGB 控制: `RGB_Init()`, `RGB_Set(r, g, b)`。
    *   LED 控制: `LED0_Set(state)`, `LED1_Set(duty)`。
    *   蜂鸣器: `Buzzer_On(freq, duty)`, `Buzzer_Off()`, `Buzzer_Duty(duty)`。

### F. 键盘驱动 (`key_driver.h/c`)
*   **控制**:
    *   `void Key_Init(void)`: 初始化 IO。
    *   `void Key_Scan_IT(void)`: 扫描逻辑，**必须**在定时器中断中调用。
    *   `void Key_Clear_Event(void)`: 清除所有挂起的事件。
*   **状态获取**:
    *   `uint8_t Key_GetEvent_Press(uint8_t key_id)`: 获取按下事件（读后清除，上升沿触发）。
    *   `uint8_t Key_IsDown(uint8_t key_id)`: 获取当前实时按压状态。
    *   `uint32_t Key_GetRawState(void)`: 获取调试用位图。
*   **键值映射 (ID)**:
    *   `0-15`: 矩阵键盘按键。
    *   `16`: `KEY_ID_BTN_A` (PA5)。
    *   `17`: `KEY_ID_BTN_B` (PB1)。

### G. HC-SR04 驱动 (`hcsr04.h/c`)
*   **非阻塞模式 (推荐)**:
    *   `void HCSR04_Init(void)`: 初始化 IO 与定时器。
    *   `void HCSR04_Start(void)`: 触发一次测量。
    *   `uint32_t HCSR04_Get_Result(void)`: 返回测量距离（单位 0.1mm）。返回 `0` 表示测量中，`45000` 表示超量程。
    *   `void HCSR04_IC_EventHandler(TIM_HandleTypeDef *htim)`: **必须**在 ISR 回调中调用。
*   **阻塞模式**:
    *   `uint32_t HCSR04_Measure_Blocking_HighRes(void)`: 使用 DWT 计数的高精度阻塞测量，单位 um。

### H. UART 驱动 (`uart_driver.h/c`)
*   **功能**:
    *   `void UART_Init(void)`: 启动空闲中断 (Idle-Line) 接收。
    *   `void UART1_Send(fmt, ...)` / `void UART6_Send(fmt, ...)`: 变参 Printf 风格发送。
    *   `uint8_t UART1_Get_Packet(char *out_buf)`: 查询并获取接收缓冲区数据。
    *   `uint8_t CMD_Parse(str, cmd, val)`: 辅助解析 "CMD:123" 格式指令。
*   **系统集成**:
    *   `printf` 已重定向至 UART1。

---

### 5.3. 必要的系统回调 (System Callbacks)

为保证驱动正常工作，用户**必须**在 `main.c` 中实现以下 HAL 库回调函数：

```c
// 1. 定时器周期回调 (用于按键扫描)
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM10) {
        Key_Scan_IT(); // 执行按键消抖与扫描
    }
}

// 2. 输入捕获回调 (用于超声波与频率计)
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim) {
    HCSR04_IC_EventHandler(htim); // 处理超声波回响
    IC_EventHandler(htim);        // 处理频率计数据
}
```

---

### 5.4. 使用指南与避坑 (Guidelines & Pitfalls)

1.  **OLED 刷新机制**: 本驱动采用“双缓冲+DMA”策略。所有的绘图函数（如 `DrawLine`）仅修改内存缓冲区。**必须**显式调用 `OLED_ShowFrame()` 才会将图像更新到屏幕，否则屏幕将保持黑屏。
2.  **波形分析时序**: `Wave_Analyze` 函数需要遍历 8500 个数据点并进行浮点运算，耗时较长。**严禁**在中断服务函数 (ISR) 中调用此函数，应在主循环中以固定帧率（如 30fps）调用。
3.  **测频方案选择**: 虽然 ADC 软件分析 (`Wave_Analyze`) 可以计算频率，但基于 TIM5 硬件的 `ic_driver` 精度更高、响应更快。建议优先使用 `IC_Get_Info` 获取频率数据。
4.  **按键 ID 偏移**: 矩阵键盘占用 ID 0-15，独立按键 A 和 B 的 ID 分别为 16 和 17，使用时请勿越界。
5.  **DMA 冲突**: ADC 和 OLED 均使用 DMA。虽然 F401 有 DMA1 和 DMA2，但在修改底层配置时需确保流 (Stream) 和通道 (Channel) 不发生冲突。默认提供的 `F401CCU6.ioc` 已完成无冲突配置。

## 6. 结语

> "The shadow-bird mends broken wings of hardware."

Cheat_Sheet_for_Stupid 不仅仅是一个代码库，它是对嵌入式底层复杂性的一次封装与反抗。对于电赛初学者而言，**时间**是最宝贵的资源。本代码库旨在让你从翻阅 1000 页参考手册的泥潭中解放出来，将精力集中在控制算法与系统逻辑的创新上。

翅膀已备好，起飞吧。
