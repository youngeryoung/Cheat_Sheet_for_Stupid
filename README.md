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

## 5. 快速上手 (Quick Start)

### 5.1 初始化
在 `main.c` 的 `User_Init()` 中，CSfS 会自动接管硬件：
```c
void User_Init(void) {
    // 系统级启动：RGB、按键、OLED
    // 信号链启动：ADC DMA、输入捕获、PWM 发生器
    // 用户只需关注应用逻辑
    Wave_Proc_Init(&WaveCfg); // 初始化示波器算法
}
```

### 5.2 极简 API 示例
**场景：输出 1kHz PWM，测量外部信号，并在 OLED 显示。**

```c
void User_Loop(void) {
    // 1. 设置 PA2 输出 1kHz, 50% 占空比方波
    PWM_Set_Freq_Duty(SIGNAL_TIM, SIGNAL_CH, 1000.0f, 50.0f);

    // 2. 获取硬件测频结果
    Signal_Info_t Info;
    IC_Get_Info(&Info);

    // 3. 屏幕显示 (非阻塞)
    if (ModeNeedRefresh) {
        OLED_NewFrame();
        // 即使打印浮点数也不需要链接庞大的标准库
        OLED_PrintString(0, 0, "Freq Check:", OLED_COLOR_NORMAL);
        OLED_PrintInt(0, 16, Info.Frequency, OLED_COLOR_NORMAL);
        OLED_ShowFrame(); // DMA 后台发送
    }
}
```

---

## 6. 结语

> "The shadow-bird mends broken wings of hardware."

Cheat_Sheet_for_Stupid 不仅仅是一个代码库，它是对嵌入式底层复杂性的一次封装与反抗。对于电赛初学者而言，**时间**是最宝贵的资源。本代码库旨在让你从翻阅 1000 页参考手册的泥潭中解放出来，将精力集中在控制算法与系统逻辑的创新上。

翅膀已备好，起飞吧。
