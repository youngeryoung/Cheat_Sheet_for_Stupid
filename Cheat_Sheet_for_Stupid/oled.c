// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#include "oled.h"
#include "i2c.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// --- 总线时序参数 ---
// I2C 位敲解锁只需要 >=0.6us 的半周期(1MHz 快速模式)，这里取 2us 留余量。
// 原先用 HAL_Delay(1) 把时序放大了约 1000 倍，而且 HAL_Delay 依赖最低
// 优先级(15)的 SysTick，在 I2C/DMA 中断(优先级 2)里调用会直接死锁。
#define OLED_BUS_HALF_PERIOD_US   2U

// --- 整屏帧传输时间 (供 DMA 超时判死使用) ---
// 一帧 = 1 个地址字节 + 1 个控制字(0x40) + 1024 字节显存；
// I2C 每字节占 9 个 SCL(8 数据 + 1 ACK)，地址字节同理。
#define OLED_GRAM_BYTES           (OLED_WIDTH * OLED_HEIGHT / 8U)
#define OLED_I2C_ADDR_BITS        9U                     /* 7 位地址 + R/W + ACK */
#define OLED_I2C_FRAME_BYTES      (OLED_GRAM_BYTES + 1U) /* + 0x40 控制字 */
#define OLED_I2C_BITS_PER_BYTE    9U                     /* 8 数据 + 1 ACK */

// 整帧传输时间的理论下界(ms，向上取整)。I2C 速率运行时不变，这一步整体交给
// 编译器做常量折叠，运行时不再出现除法。
#define OLED_DMA_MIN_MS           \
    (((OLED_I2C_ADDR_BITS + OLED_I2C_FRAME_BYTES * OLED_I2C_BITS_PER_BYTE) * 1000U \
      + OLED_I2C_CLK_SPEED - 1U) / OLED_I2C_CLK_SPEED)

// 固定冗余：START/STOP 建立时间、DMA 启动与 HAL 状态机开销，再加 I2C 的 CCR
// 取整误差 —— F401 上 PCLK1=42MHz、1MHz/16:9 时 CCR=2，实际 SCL 只有 840kHz
// (标称的 84%)，整帧由 9.3ms 变成 11.0ms(+1.7ms)。这部分是系统性的，不随时
// 间变化。(STM32 的 I2C 是 v1 外设，CCR 只能取整；v2 的 TIMINGR 也一样有量化误差)
#define OLED_DMA_FIXED_MARGIN_MS  5U

// 阈值下限 = 理论下界 + 固定冗余，同样是编译期常量
#define OLED_DMA_BASE_MS          (OLED_DMA_MIN_MS + OLED_DMA_FIXED_MARGIN_MS)

// --- 系统状态 ---
static volatile OLED_State_t ScreenState = OLED_STATE_OK;
static uint32_t Last_DMA_Start_Tick = 0; // 上次 DMA 传输开始时间
static uint32_t Last_Recovery_Tick = 0; // 上次尝试恢复的时间
static uint32_t OledDmaWorstMs = 0;     // 实测最坏整帧 DMA 耗时(动态冗余来源)

static const int32_t Powt[] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};

/**
 * 显存结构说明:
 * 为了支持一次性 DMA 连续传输，我们使用【水平寻址模式】。
 * OLED_DMA_Buffer[0] = 0x40 (I2C 数据流控制字)
 * 其后 1024 字节为 GRAM 数据。
 */
static uint8_t OLED_DMA_Buffer[OLED_I2C_FRAME_BYTES]; 

// ========================== 底层通信 ==========================

/**
 * @brief 发送单字节命令 (阻塞式，仅在初始化或恢复时使用)
 * @return 1 = 成功, 0 = 掉线/失败
 */
static uint8_t OLED_SendCmd(uint8_t cmd) {
    // 如果已经判定掉线，且不是在恢复过程中，则禁止发送，防止阻塞
    if (ScreenState == OLED_STATE_OFFLINE) return 0;

    uint8_t buf[2] = {0x00, cmd};
    // 使用短超时 (10ms)，防止卡死
    if(HAL_I2C_Master_Transmit(&I2C_Channel, OLED_ADDRESS, buf, 2, 10) != HAL_OK) {
        ScreenState = OLED_STATE_OFFLINE; // 发送失败直接判死
        return 0;
    }
    return 1;
}


/**
 * @brief 使能 DWT 周期计数器 (幂等)
 * @note  不要依赖别的模块去使能 DWT：调用顺序不保证，而 CYCCNT 冻结时
 *        直接读会死循环。重复使能是幂等的，谁先谁后都不影响。
 */
static void OLED_DwtInit(void) {
    static uint8_t DwtReady = 0;
    if (DwtReady) return;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    DwtReady = 1;
}

/**
 * @brief 微秒级阻塞延时 (仅用于总线解锁的位敲时序)
 * @note  不用 HAL_Delay：它依赖最低优先级(15)的 SysTick，在 I2C/DMA 中断
 *        (优先级 2)里调用会永久死锁。guard 用于兜底 —— 万一 CYCCNT 不递增，
 *        循环也会退出，绝不挂死。
 */
static void OLED_DelayUs(uint32_t us) {
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    uint32_t guard = ticks * 4U + 64U;
    uint32_t start;

    OLED_DwtInit();
    start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < ticks) {
        if (--guard == 0U) return;
    }
}

/**
 * @brief 判断 I2C 总线是否空闲 (SCL/SDA 都被释放为高)
 * @note  AF_OD 模式下 IDR 依然反映引脚真实电平，所以这里不需要改配置
 */
static uint8_t OLED_I2C_BusIdle(void) {
    return (uint8_t)((HAL_GPIO_ReadPin(SCL_GPIO_Port, SCL_Pin) == GPIO_PIN_SET) &&
                     (HAL_GPIO_ReadPin(SDA_GPIO_Port, SDA_Pin) == GPIO_PIN_SET));
}

/**
 * @brief 解锁被从机拉死的 I2C 总线 (原名 I2C_Hardware_Reset)
 * @note  它复位的是总线而不是外设，故改名。
 *        流程: 判断总线是否真被占用 -> DeInit -> GPIO 模拟时钟(最多 9 个) -> STOP -> ReInit
 *        总线本来就空闲时不推任何时钟，只把外设/引脚恢复成 AF 模式。
 */
static void OLED_I2C_BusRecover(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 1. 先用 IDR 判断总线是否真的被拉死
    uint8_t bus_busy = (uint8_t)(!OLED_I2C_BusIdle());

    // 2. 彻底关闭 I2C 外设，释放引脚控制权
    HAL_I2C_DeInit(&I2C_Channel);

    // 3. 总线空闲：没有从机卡在半途，不需要时钟解锁
    if (!bus_busy) {
        HAL_I2C_Init(&I2C_Channel);
        return;
    }

    // 4. 开启 GPIO 时钟
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // 5. SDA 释放为输入(上拉)，让从机有机会放开它
    GPIO_InitStruct.Pin = SDA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(SDA_GPIO_Port, &GPIO_InitStruct);

    // 6. SCL 开漏输出
    GPIO_InitStruct.Pin = SCL_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SCL_GPIO_Port, &GPIO_InitStruct);

    // 7. 最多推 9 个时钟：只要 SDA 被释放就提前结束
    for (uint8_t i = 0; i < 9U; i++) {
        if (HAL_GPIO_ReadPin(SDA_GPIO_Port, SDA_Pin) == GPIO_PIN_SET) break;

        HAL_GPIO_WritePin(SCL_GPIO_Port, SCL_Pin, GPIO_PIN_RESET);
        OLED_DelayUs(OLED_BUS_HALF_PERIOD_US);
        HAL_GPIO_WritePin(SCL_GPIO_Port, SCL_Pin, GPIO_PIN_SET);
        OLED_DelayUs(OLED_BUS_HALF_PERIOD_US);
    }

    // 8. 补一个干净的 STOP：先把 SCL 拉低，之后改 SDA 就不会误产生 START
    HAL_GPIO_WritePin(SCL_GPIO_Port, SCL_Pin, GPIO_PIN_RESET);
    OLED_DelayUs(OLED_BUS_HALF_PERIOD_US);

    GPIO_InitStruct.Pin = SDA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    HAL_GPIO_Init(SDA_GPIO_Port, &GPIO_InitStruct);

    HAL_GPIO_WritePin(SDA_GPIO_Port, SDA_Pin, GPIO_PIN_RESET);
    OLED_DelayUs(OLED_BUS_HALF_PERIOD_US);
    HAL_GPIO_WritePin(SCL_GPIO_Port, SCL_Pin, GPIO_PIN_SET);
    OLED_DelayUs(OLED_BUS_HALF_PERIOD_US);
    HAL_GPIO_WritePin(SDA_GPIO_Port, SDA_Pin, GPIO_PIN_SET);
    OLED_DelayUs(OLED_BUS_HALF_PERIOD_US);

    // 9. 重新初始化 I2C 外设
    HAL_I2C_Init(&I2C_Channel);
}

/**
 * @brief 下发一整串初始化命令并清空显存
 * @return 1 = 全部成功, 0 = 过程中掉线
 */
static uint8_t OLED_SendInitSequence(void) {
    // --- 适配 DMA 的水平寻址初始化序列 ---
    static const uint8_t init_cmds[] = {
        0xAE,               // 1. 关闭显示 (Display Off)

        // --- 寻址模式核心配置 ---
        0x20, 0x00,         // 2. 内存寻址模式 = 0x00 (水平寻址，写满 128 字节自动换页)
        0x21, 0x00, 0x7F,   // 3. 列起始/结束地址 (0-127)
        0x22, 0x00, 0x07,   // 4. 页起始/结束地址 (0-7)

        // --- 硬件物理配置 ---
        0xC8,               // 上下翻转 (COM Output Scan Direction)
        0xA1,               // 左右翻转 (Segment Re-map)
        0xA8, 0x3F,         // 64 行多路复用
        0xD3, 0x00,         // 无偏移
        0x40,               // 显示起始行

        // --- 亮度与驱动配置 ---
        0x81, 0xCF,         // 对比度
        0xA4,               // 依照 GDDRAM 显示
        0xA6,               // 正常显示

        // --- 时钟与电源 ---
        #if OLED_I2C_CLK_SPEED > 400000
        0xD5, 0xF0,         // 分频比 (超频模式)
        #else
        0xD5, 0x80,         // 分频比
        #endif
        0xD9, 0xF1,         // 预充电周期
        0xDA, 0x12,         // COM 引脚硬件配置
        0xDB, 0x40,         // VCOMH 脱离电平
        0x8D, 0x14,         // 开启电荷泵

        0xAF                // 5. 开启显示 (Display On)
    };

    // 阻塞模式发送初始化命令，确保屏幕先"醒过来"
    for (uint8_t i = 0; i < (uint8_t)sizeof(init_cmds); i++) {
        if (!OLED_SendCmd(init_cmds[i])) return 0;
    }

    OLED_DMA_Buffer[0] = 0x40;                // Data Stream 标识符
    memset(&OLED_DMA_Buffer[1], 0, OLED_GRAM_BYTES);   // 清屏，避免残留上一帧
    return 1;
}

/**
 * @brief 在"设备已应答"的前提下重发命令序列，成功则恢复显示
 */
static void OLED_ReinitPanel(void) {
    ScreenState = OLED_STATE_RECOVERING;    // 放行 OLED_SendCmd 的离线门禁

    if (OLED_SendInitSequence()) {
        ScreenState = OLED_STATE_OK;
        OLED_ShowFrame();                   // 立即重绘一帧，防止黑屏
    } else {
        ScreenState = OLED_STATE_OFFLINE;
    }
}

/**
 * @brief 尝试恢复连接
 * @note  每隔 RECOVERY_INTERVAL 毫秒调用一次。
 *        先走廉价快路径(总线干净 + 设备应答 -> 只重发命令)，不行再解锁总线。
 *        快路径天然覆盖了"NACK 类错误不必解锁总线"的情形，因此不需要再区分
 *        hi2c->ErrorCode：直接看引脚状态比回溯错误码更准。
 */
static void OLED_Try_Recovery(void) {
    // 快路径：零延时、零总线扰动，覆盖绝大多数瞬时错误
    if (OLED_I2C_BusIdle() &&
        HAL_I2C_IsDeviceReady(&I2C_Channel, OLED_ADDRESS, 2, 10) == HAL_OK) {
        OLED_ReinitPanel();
        return;
    }

    // 慢路径：总线被拉死或设备无应答 -> 条件解锁后再探一次
    OLED_I2C_BusRecover();
    if (HAL_I2C_IsDeviceReady(&I2C_Channel, OLED_ADDRESS, 2, 10) == HAL_OK) {
        OLED_ReinitPanel();
    }
    // 仍无应答：保持 OFFLINE，等下一个 RECOVERY_INTERVAL
}

/**
 * @brief 检查 I2C 是否正在传输
 */
uint8_t OLED_IsBusy(void) {
    return (uint8_t)(HAL_I2C_GetState(&I2C_Channel) == HAL_I2C_STATE_BUSY_TX);
}

// ========================== 核心驱动 ==========================

/**
 * @brief 初始化 OLED 屏幕
 */
void OLED_Init(void) {
    // 初始化时，暂时认为状态是正常的，允许 SendCmd 工作
    ScreenState = OLED_STATE_OK; 

    #if OLED_I2C_CLK_SPEED != 400000

    // 获取当前的 I2C 句柄并修改参数
    I2C_Channel.Init.ClockSpeed = OLED_I2C_CLK_SPEED;

    // 超频模式下（>400k），强制开启 16:9 占空比以确保上升沿稳定
    #if (OLED_I2C_CLK_SPEED > 400000)
        I2C_Channel.Init.DutyCycle = I2C_DUTYCYCLE_16_9;
    #else
        I2C_Channel.Init.DutyCycle = I2C_DUTYCYCLE_2;
    #endif

    // 2. 重新初始化 I2C 外设
    HAL_I2C_DeInit(&I2C_Channel);
    if (HAL_I2C_Init(&I2C_Channel) != HAL_OK) {
        ScreenState = OLED_STATE_OFFLINE;
        return;
    }
    #endif
    
    OLED_I2C_BusRecover(); // 总线解锁(仅在被拉死时才推时钟)

    // 上电稳定等待已外移到调用方(见 User_Init)：驱动内部不做阻塞延时。
    // 从错误中恢复时屏幕早已供电，再等也没有收益，所以恢复路径也不等。

    if (!OLED_SendInitSequence()) {
        // 初始化失败
        ScreenState = OLED_STATE_OFFLINE;
        Last_Recovery_Tick = HAL_GetTick();
        return;
    }

    OLED_ShowFrame(); // 初始化成功，立即刷新一帧
}

/**
 * @brief 准备新一帧的显存 (清空显存缓冲区)
 */
void OLED_NewFrame(void) {
    // 如果 DMA 正在搬运上一帧，不动 Buffer，防止画面撕裂或 DMA 错误
    // 但如果掉线了，就不必等了
    if (ScreenState == OLED_STATE_OK && HAL_I2C_GetState(&I2C_Channel) == HAL_I2C_STATE_BUSY_TX) {
        return; 
    }
    memset(&OLED_DMA_Buffer[1], 0, OLED_GRAM_BYTES);
}

/**
 * @brief 整帧 DMA 的判死阈值 = 编译期下界 + 固定冗余 + 运行时动态冗余
 * @note  下界与固定冗余已由编译器折叠成常量(OLED_DMA_BASE_MS)，运行时只剩
 *        动态部分：实测最坏耗时超出下界的量，并限幅到一个下界，避免某次被
 *        长中断打断的传输把阈值永久撑飞。
 */
static uint32_t OLED_DmaTimeoutMs(void) {
    uint32_t dyn = (OledDmaWorstMs > OLED_DMA_MIN_MS) ? (OledDmaWorstMs - OLED_DMA_MIN_MS) : 0U;

    if (dyn > OLED_DMA_MIN_MS) dyn = OLED_DMA_MIN_MS;
    return OLED_DMA_BASE_MS + dyn;
}

/**
 * @brief 异步显示刷新：开启 DMA 冲刺
 */
void OLED_ShowFrame(void) {
    // Case 1: 正常模式
    if (ScreenState == OLED_STATE_OK) {
        
        // --- 1. 检查状态 ---
        uint32_t i2c_state = HAL_I2C_GetState(&I2C_Channel);

        // 如果 I2C 忙 (上一帧还没发完)
        if (i2c_state == HAL_I2C_STATE_BUSY_TX) {
            
            // 非阻塞超时检测：阈值 = 理论最短传输时间 + 固定冗余 + 实测动态冗余
            if (HAL_GetTick() - Last_DMA_Start_Tick > OLED_DmaTimeoutMs()) {
                // 超时了！卡死了!
                ScreenState = OLED_STATE_OFFLINE;
                Last_Recovery_Tick = HAL_GetTick();
            }

            // 忙着呢，先不发新帧
            return; 
        }
        
        // 如果处于 Error 状态，也直接判死
        else if (i2c_state == HAL_I2C_STATE_ERROR) {
            ScreenState = OLED_STATE_OFFLINE;
            Last_Recovery_Tick = HAL_GetTick();
            return;
        }

        // --- 2. 启动 DMA ---
        if (HAL_I2C_Master_Transmit_DMA(&I2C_Channel, OLED_ADDRESS, OLED_DMA_Buffer, OLED_I2C_FRAME_BYTES) == HAL_OK) {
            // 发送成功，更新时间戳
            Last_DMA_Start_Tick = HAL_GetTick();
        } else {
            // 启动瞬间报错 (通常是刚被拔掉)
            ScreenState = OLED_STATE_OFFLINE;
            Last_Recovery_Tick = HAL_GetTick();
        }
    }
    
    // Case 2: 掉线模式
    else if (ScreenState == OLED_STATE_OFFLINE) {
        if (HAL_GetTick() - Last_Recovery_Tick > RECOVERY_INTERVAL) {
            Last_Recovery_Tick = HAL_GetTick();
            OLED_Try_Recovery();
        }
    }
}

// ========================== 绘图函数 ==========================

/**
 * @brief 内部辅助：快速画水平线 (用于填充圆和矩形)
 * @note  不进行 Y 轴边界检查，调用前需保证 y < 64
 */
static void _DrawFastHLine(uint8_t x, uint8_t y, uint8_t w, OLED_ColorMode color) {
    // X轴防呆与裁剪
    if (x >= OLED_WIDTH) return;
    if (x + w > OLED_WIDTH) w = OLED_WIDTH - x;
    if (w == 0) return;

    // 计算显存地址
    // idx = 1 + (Page * 128) + x
    uint16_t idx = 1 + ((y >> 3) << 7) + x; 
    uint8_t bit_mask = 1 << (y & 7);

    // 批量操作
    uint8_t *pBuf = &OLED_DMA_Buffer[idx];
    if (color == OLED_COLOR_NORMAL) {
        for (uint8_t i = 0; i < w; i++) pBuf[i] |= bit_mask;
    } else {
        uint8_t clear_mask = ~bit_mask;
        for (uint8_t i = 0; i < w; i++) pBuf[i] &= clear_mask;
    }
}

/**
 * @brief 内部辅助：快速画垂直线
 */
static void _DrawFastVLine(uint8_t x, uint8_t y, uint8_t h, OLED_ColorMode color) {
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;
    if (y + h > OLED_HEIGHT) h = OLED_HEIGHT - y;
    if (h == 0) return;

    uint8_t *pBufBase = &OLED_DMA_Buffer[1];
    uint8_t y_end = y + h - 1;
    uint8_t page_start = y >> 3;
    uint8_t page_end = y_end >> 3;

    for (uint8_t p = page_start; p <= page_end; p++) {
        uint8_t mask = 0xFF;
        
        // 处理顶部非对齐
        if (p == page_start) mask &= (0xFF << (y & 7));
        // 处理底部非对齐
        if (p == page_end)   mask &= (0xFF >> (7 - (y_end & 7)));

        uint16_t idx = (p << 7) + x; // p * 128 + x
        
        if (color == OLED_COLOR_NORMAL) pBufBase[idx] |= mask;
        else pBufBase[idx] &= ~mask;
    }
}

/**
 * @brief 在显存中画一个点
 * @param x   x坐标
 * @param y   y坐标
 * @param color  颜色模式
 */
void OLED_SetPixel(uint8_t x, uint8_t y, OLED_ColorMode color) {
    // 1. 极速防呆
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;

    // 2. 数学运算
    // y % 8  -> y & 7
    uint16_t idx = 1 + ((y >> 3) << 7) + x;
    uint8_t bit = 1 << (y & 7);

    // 3. 写入
    if (color == OLED_COLOR_NORMAL) {
        OLED_DMA_Buffer[idx] |= bit;
    } else {
        OLED_DMA_Buffer[idx] &= ~bit;
    }
}

/**
 * @brief 画线 (Bresenham 算法，带裁剪)
 * @param x1  起点x坐标
 * @param y1  起点y坐标
 * @param x2  终点x坐标
 * @param y2  终点y坐标
 * @param color  颜色模式
 */
void OLED_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, OLED_ColorMode color) {
    // 简单的线段端点裁剪 (完全在屏幕外的线不画)
    if ((x1 >= OLED_WIDTH && x2 >= OLED_WIDTH) || (y1 >= OLED_HEIGHT && y2 >= OLED_HEIGHT)) return;

    int16_t dx = abs((int16_t)x2 - x1);
    int16_t dy = -abs((int16_t)y2 - y1);
    int16_t sx = x1 < x2 ? 1 : -1;
    int16_t sy = y1 < y2 ? 1 : -1;
    int16_t err = dx + dy;
    int16_t e2;

    while (1) {
        OLED_SetPixel(x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

/**
 * @brief 画圆 (中点圆算法)
 * @param x0  圆心x坐标
 * @param y0  圆心y坐标
 * @param r   半径
 * @param color  颜色模式
 */
void OLED_DrawCircle(uint8_t x0, uint8_t y0, uint8_t r, OLED_ColorMode color) {
    int16_t x = 0;
    int16_t y = r;
    int16_t d = 3 - 2 * r;

    while (x <= y) {
        OLED_SetPixel(x0 + x, y0 + y, color);
        OLED_SetPixel(x0 - x, y0 + y, color);
        OLED_SetPixel(x0 + x, y0 - y, color);
        OLED_SetPixel(x0 - x, y0 - y, color);
        OLED_SetPixel(x0 + y, y0 + x, color);
        OLED_SetPixel(x0 - y, y0 + x, color);
        OLED_SetPixel(x0 + y, y0 - x, color);
        OLED_SetPixel(x0 - y, y0 - x, color);
        if (d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

/**
 * @brief 实心圆 (使用快速水平线填充)
 * @param x0  圆心x坐标
 * @param y0  圆心y坐标
 * @param r   半径
 * @param color  颜色模式
 */
void OLED_DrawFilledCircle(uint8_t x0, uint8_t y0, uint8_t r, OLED_ColorMode color) {
    int16_t x = 0;
    int16_t y = r;
    int16_t d = 3 - 2 * r;

    while (x <= y) {
        // 上半圆
        _DrawFastHLine(x0 - x, y0 - y, 2 * x + 1, color);
        _DrawFastHLine(x0 - y, y0 - x, 2 * y + 1, color);
        // 下半圆
        _DrawFastHLine(x0 - x, y0 + y, 2 * x + 1, color);
        _DrawFastHLine(x0 - y, y0 + x, 2 * y + 1, color);

        if (d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

/**
 * @brief 画矩形 (拆分为2条横线+2条竖线，比Bresenham快)
 * @param x   起始x坐标
 * @param y   起始y坐标
 * @param w   宽度
 * @param h   高度
 * @param color  颜色模式
 */
void OLED_DrawRectangle(uint8_t x, uint8_t y, uint8_t w, uint8_t h, OLED_ColorMode color) {
    if (w == 0 || h == 0) return;
    // 裁剪放在 FastLine 内部处理，这里只需计算坐标
    _DrawFastHLine(x, y, w, color);             // Top
    _DrawFastHLine(x, y + h - 1, w, color);     // Bottom
    _DrawFastVLine(x, y, h, color);             // Left
    _DrawFastVLine(x + w - 1, y, h, color);     // Right
}

/**
 * @brief 实心矩形
 * @param x   起始x坐标
 * @param y   起始y坐标
 * @param w   宽度
 * @param h   高度
 * @param color  颜色模式
 */
void OLED_DrawFilledRectangle(uint8_t x, uint8_t y, uint8_t w, uint8_t h, OLED_ColorMode color) {
    // 1. 边界裁剪
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;
    if (x + w > OLED_WIDTH) w = OLED_WIDTH - x;
    if (y + h > OLED_HEIGHT) h = OLED_HEIGHT - y;
    if (w == 0 || h == 0) return;

    uint8_t *pBufBase = &OLED_DMA_Buffer[1];
    uint8_t y_end = y + h - 1;
    uint8_t page_start = y >> 3;       // y / 8
    uint8_t page_end = y_end >> 3;

    // 2. 按页遍历 (纵向)
    for (uint8_t p = page_start; p <= page_end; p++) {
        uint8_t mask = 0xFF;

        // 计算当前页的有效 Mask
        // 如果是起始页，遮掉顶部的无效位
        if (p == page_start) mask &= (0xFF << (y & 7));
        // 如果是结束页，遮掉底部的无效位
        if (p == page_end)   mask &= (0xFF >> (7 - (y_end & 7)));

        // 3. 内存批量操作 (横向)
        // 计算显存偏移：Page * 128 + x
        uint16_t idx = (p << 7) + x; 

        if (color == OLED_COLOR_NORMAL) {
            // 如果 Mask 是 0xFF (中间的完整页)，直接 memset，速度最快
            if (mask == 0xFF) {
                memset(&pBufBase[idx], 0xFF, w);
            } else {
                // 边缘页，需要 |=
                for (uint8_t i = 0; i < w; i++) pBufBase[idx + i] |= mask;
            }
        } else {
            // 反色/擦除模式
            if (mask == 0xFF) {
                memset(&pBufBase[idx], 0x00, w);
            } else {
                uint8_t clear_mask = ~mask;
                for (uint8_t i = 0; i < w; i++) pBufBase[idx + i] &= clear_mask;
            }
        }
    }
}

// ========================== 高级文本引擎 ==========================

/**
 * @brief UTF-8 编码长度识别
 * @param str 指向 UTF-8 字符的指针
 * @return 字符所占字节数 (1-4)
 */
static uint8_t _Get_UTF8_Len(const char *str) {
    if (((uint8_t)str[0] & 0x80) == 0x00) return 1;
    if (((uint8_t)str[0] & 0xE0) == 0xC0) return 2;
    if (((uint8_t)str[0] & 0xF0) == 0xE0) return 3;
    if (((uint8_t)str[0] & 0xF8) == 0xF0) return 4;
    return 1;
}

/**
 * @brief 二分查找汉字位图
 * @param utf8 指向 UTF-8 字符的指针
 * @param len  字符所占字节数 (1-4)
 * @return 指向位图数据的指针，未找到返回 NULL
 */
static const uint8_t* _Find_Unicode_Bitmap(const char *utf8, uint8_t len) {
    if (fontu.len == 0) return NULL;
    int low = 0;
    int high = (int)fontu.len - 1;
    uint8_t bytes_per_col = (uint8_t)((fontu.h + 7) / 8);
    uint8_t entry_size = (uint8_t)(4 + (fontu.w * bytes_per_col));

    while (low <= high) {
        int mid = low + (high - low) / 2;
        const uint8_t *entry = fontu.chars + (mid * entry_size);
        int cmp = memcmp(utf8, entry, (size_t)len); 
        if (cmp == 0) return entry + 4; 
        else if (cmp < 0) high = mid - 1;
        else low = mid + 1;
    }
    return NULL;
}

/**
 * @brief 高性能位图绘制引擎
 * @param x, y   起始坐标
 * @param w, h   位图宽高
 * @param bitmap 位图数据指针 (垂直字节序)
 * @param color  OLED_COLOR_NORMAL(覆盖写入), OLED_COLOR_REVERSE(反色覆盖)
 */
void _Draw_Bitmap(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *bitmap, OLED_ColorMode color) {
    // 1. 边界预判
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;
    
    // 2. 计算基础参数
    uint8_t *pBufBase = &OLED_DMA_Buffer[1]; // 跳过 0x40 命令字
    uint8_t byte_height = (h + 7) / 8;       // 字符占用多少个垂直字节
    uint8_t y_shift = y % 8;                 // 页内偏移量 (0-7)
    uint8_t start_page = y / 8;              // 起始页索引

    // 3. 列遍历 (Horizontal Loop)
    for (uint8_t col = 0; col < w; col++) {
        // X轴防止越界
        if (x + col >= OLED_WIDTH) break;

        // 当前列在源数据中的指针
        const uint8_t *src_col_ptr = &bitmap[col * byte_height];
        
        // 显存中的起始偏移 (当前列 Page0 的位置)
        // 计算公式：Page * 128 + Col
        uint16_t screen_offset = start_page * 128 + (x + col);
        
        // 垂直字节遍历
        for (uint8_t b = 0; b < byte_height; b++) {
            // A. 准备源数据
            uint8_t src = src_col_ptr[b];
            
            // 处理非8倍数高度的尾部 (Mask掉无效位)
            // 比如高度12，第二个字节只有低4位有效
            int valid_bits = h - b * 8;
            if (valid_bits < 8) {
                src &= (0xFF >> (8 - valid_bits));
            }
            
            // 处理反色
            if (color == OLED_COLOR_REVERSE) src = ~src;

            // B. 准备写入的数据和掩码 (16位)
            // data: 要写入的像素位
            // mask: 要操作的区域 (1表示要覆盖，0表示保留原背景)
            uint16_t data = (uint16_t)src;
            uint16_t mask = 0x00FF;
            
            // 如果是最后一部分，且有无效位，掩码也要限制，防止擦除下方无关像素
            if (valid_bits < 8) {
                mask &= (0xFF >> (8 - valid_bits));
            }

            // C. 核心：位移对齐
            // 将源数据和掩码都移动到正确的位置
            data <<= y_shift;
            mask <<= y_shift;

            // D. 写入当前页 (Upper part)
            if (start_page + b < 8) {
                uint16_t idx = screen_offset + b * 128;
                // 先清除背景 ( &= ~mask )，再写入数据 ( |= data )
                pBufBase[idx] &= ~(mask & 0xFF);
                pBufBase[idx] |= (data & 0xFF);
            }

            // E. 写入下一页 (Lower part, 溢出部分)
            // 如果 y_shift > 0，数据会跨越到下一页
            if (y_shift > 0 && (start_page + b + 1) < 8) {
                uint16_t idx = screen_offset + (b + 1) * 128;
                pBufBase[idx] &= ~(mask >> 8);
                pBufBase[idx] |= (data >> 8);
            }
        }
    }
}

/**
 * @brief    打印字符串
 * @param x  起始x坐标
 * @param y  起始y坐标
 * @param str 字符串指针 (UTF-8 编码)
 * @param color  颜色模式
 */
void OLED_PrintString(uint8_t x, uint8_t y, const char *str, OLED_ColorMode color) {
    uint8_t x0 = x;
    while (*str) {
        if (*str == '\n') {
            y = (uint8_t)(y + fonta.h);
            x = x0;
            str++;
            continue;
        }

        uint8_t utf8_len = _Get_UTF8_Len(str);
        uint8_t char_w = 0;
        uint8_t char_h = fonta.h;
        const uint8_t *bitmap = NULL;

        if (utf8_len == 1) { // ASCII
            if ((uint8_t)*str >= 32 && (uint8_t)*str <= 126) {
                uint8_t bytes_per_col = (uint8_t)((fonta.h + 7) / 8);
                bitmap = fonta.chars + (size_t)(((uint8_t)*str - 32) * fonta.w * bytes_per_col);
                char_w = fonta.w;
            }
        } else { // Unicode
            bitmap = _Find_Unicode_Bitmap(str, utf8_len);
            char_w = fontu.w;
            char_h = fontu.h;
        }

        if (x + char_w > (uint8_t)128) {
            x = 0;
            y = (uint8_t)(y + char_h);
        }
        
        if (bitmap) {
            _Draw_Bitmap(x, y, char_w, char_h, bitmap, color);
            x = (uint8_t)(x + char_w);
        } else {
            x = (uint8_t)(x + ((utf8_len == 1) ? fonta.w : fontu.w));
        }
        str += utf8_len;
    }
}

/**
 * @brief 打印整数
 * @param x  起始x坐标
 * @param y  起始y坐标
 * @param num  要打印的整数
 * @param color  颜色模式
 */
void OLED_PrintInt(uint8_t x, uint8_t y, int32_t num, OLED_ColorMode color) {
    char buf[16];
    sprintf(buf, "%ld", (long)num);
    OLED_PrintString(x, y, buf, color);
}

/**
 * @brief 打印带浮点数（四舍五入）
 * @param x  起始x坐标
 * @param y  起始y坐标
 * @param num  要打印的浮点数
 * @param precision 小数点后位数(1-9)
 * @param color  颜色模式
 */
void OLED_PrintFloat(uint8_t x, uint8_t y, double num, uint8_t precision, OLED_ColorMode color) {
    char buf[32];

    // 1. 限制精度范围 (防止数组越界)
    precision = (precision > 9) ? 9 : ((precision < 1) ? 1 : precision);

    // 2. 处理正负号
    char sign_str[2] = {0,0}; // 默认为空
    if (num < 0) {
        sign_str[0] = '-';
        num = -num;  // 转为正数处理
    }

    // 3. 提取整数部分
    int32_t i_part = (int32_t)num;

    // 4. 提取小数部分
    // 减去整数部分 -> 乘倍率 -> 加0.5做四舍五入 -> 转整数
    double f_part_val = (num - (double)i_part) * (double)Powt[precision];
    int32_t d_part = (int32_t)(f_part_val + 0.5); 

    // 5. 处理进位
    if (d_part >= Powt[precision]) {
        d_part = 0;
        i_part++;
    }

    // 6. 格式化输出
    // %s: 符号 %ld: 整数 %0*ld: 动态宽度的补零小数 (例如 precision=2, d_part=5 -> "05")
    sprintf(buf, "%s%ld.%0*ld", sign_str, (long)i_part, (int)precision, (long)d_part);
    
    // 7. 最终显示
    OLED_PrintString(x, y, buf, color);
}

// ========================== 中断回调 ==========================

/**
 * @brief I2C 主机发送完成回调 (整帧 DMA 搬运结束)
 * @note  在 I2C 中断上下文执行，只做一次比较与赋值，不含任何阻塞操作。
 *        用途：为超时判死的"动态冗余"积累实测最坏整帧耗时。
 *        注意：main.c 里那段注释掉的同名演示回调必须保持注释，否则重复定义。
 */
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c->Instance != I2C_Channel.Instance) return;

    uint32_t elapsed = HAL_GetTick() - Last_DMA_Start_Tick;
    if (elapsed > OledDmaWorstMs) {
        OledDmaWorstMs = elapsed;
    }
}

/**
 * @brief I2C 错误回调处理
 * @param hi2c 指向 I2C 句柄的指针
 * @note 用户无需手动调用此函数
 */
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c->Instance == I2C_Channel.Instance) {
        // 只有当前认为是在线时，才转为离线，避免状态机混乱
        if (ScreenState == OLED_STATE_OK) {
            ScreenState = OLED_STATE_OFFLINE;
            Last_Recovery_Tick = HAL_GetTick(); // 开始冷却计时
        }
        // 停止可能正在进行的传输
        HAL_I2C_Master_Abort_IT(hi2c, OLED_ADDRESS);
    }
}