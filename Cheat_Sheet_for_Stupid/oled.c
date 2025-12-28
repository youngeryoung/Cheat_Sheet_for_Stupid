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

// --- 系统状态 ---
static volatile OLED_State_t ScreenState = OLED_STATE_OK;
static uint32_t Last_DMA_Start_Tick = 0; // 上次 DMA 传输开始时间
static uint32_t Last_Recovery_Tick = 0; // 上次尝试恢复的时间

static const int32_t Powt[] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};

/**
 * 显存结构说明:
 * 为了支持一次性 DMA 连续传输，我们使用【水平寻址模式】。
 * OLED_DMA_Buffer[0] = 0x40 (I2C 数据流控制字)
 * 其后 1024 字节为 GRAM 数据。
 */
static uint8_t OLED_DMA_Buffer[1025]; 

// ========================== 底层通信 ==========================

/**
 * @brief 发送单字节命令 (阻塞式，仅在初始化或恢复时使用)
 */
static void OLED_SendCmd(uint8_t cmd) {
    // 如果已经判定掉线，且不是在恢复过程中，则禁止发送，防止阻塞
    if (ScreenState == OLED_STATE_OFFLINE) return;

    uint8_t buf[2] = {0x00, cmd};
    // 使用短超时 (10ms)，防止卡死
    if(HAL_I2C_Master_Transmit(&I2C_Channel, OLED_ADDRESS, buf, 2, 10) != HAL_OK) {
        ScreenState = OLED_STATE_OFFLINE; // 发送失败直接判死
    }
}


/**
 * @brief 强力复位 I2C 总线 (解决 SDA 死锁问题)
 * @note  步骤: DeInit -> GPIO模拟时钟(9个脉冲) -> STOP信号 -> ReInit
 */
static void I2C_Hardware_Reset(void) {
    // 1. 临时保存 GPIO 配置
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // 2. 彻底关闭 I2C 外设，释放引脚控制权
    HAL_I2C_DeInit(&I2C_Channel);
    
    // 3. 开启 GPIO 时钟
    __HAL_RCC_GPIOB_CLK_ENABLE(); 
    
    // 4. 配置 SCL (PB6) 为开漏输出
    GPIO_InitStruct.Pin = SCL_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SCL_GPIO_Port, &GPIO_InitStruct);
    
    // 5. 配置 SDA (PB7) 为输入 (用于检测释放)
    GPIO_InitStruct.Pin = SDA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT; // 先设为输入检测状态
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(SDA_GPIO_Port, &GPIO_InitStruct);
    
    // 6. 手动发送 9 个时钟脉冲 (Bus Clear)
    // 只要 SDA 为低，就继续推 SCL，迫使从机走完剩下的 bit 并释放 SDA
    for(int i = 0; i < 9; i++) {
        // 如果 SDA 已经被释放(变高)，提前结束
        if(HAL_GPIO_ReadPin(SDA_GPIO_Port, SDA_Pin) == GPIO_PIN_SET) {
            break;
        }
        
        // 模拟 SCL 时钟: Low -> High
        HAL_GPIO_WritePin(SCL_GPIO_Port,  SCL_Pin, GPIO_PIN_RESET);
        HAL_Delay(1); // 粗略延时，保证时序
        HAL_GPIO_WritePin(SCL_GPIO_Port,  SCL_Pin, GPIO_PIN_SET);
        HAL_Delay(1);
    }
    
    // 7. 发送一个 STOP 信号 (SCL高时，SDA由低变高)
    // 先把 SDA 切回输出模式
    GPIO_InitStruct.Pin = SDA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    HAL_GPIO_Init(SDA_GPIO_Port, &GPIO_InitStruct);
    
    HAL_GPIO_WritePin(SDA_GPIO_Port, SDA_Pin, GPIO_PIN_RESET); // SDA = 0
    HAL_GPIO_WritePin(SCL_GPIO_Port,  SCL_Pin, GPIO_PIN_SET);   // SCL = 1
    HAL_Delay(1);
    HAL_GPIO_WritePin(SDA_GPIO_Port, SDA_Pin, GPIO_PIN_SET);   // SDA = 0->1 (STOP)
    
    // 8. 重新初始化 I2C 外设
    HAL_I2C_Init(&I2C_Channel);
}

/**
 * @brief 尝试恢复连接
 * @note  每隔 RECOVERY_INTERVAL 毫秒调用一次
 */
static void OLED_Try_Recovery(void) {
    // 1. 硬件复位 I2C (清理总线上的半途而废的数据)
    I2C_Hardware_Reset();

    // 2. Ping 设备 (查看设备是否存在)
    // 尝试 2 次，超时 10ms
    if (HAL_I2C_IsDeviceReady(&I2C_Channel, OLED_ADDRESS, 2, 10) == HAL_OK) {
        // 1. 进入恢复模式
        ScreenState = OLED_STATE_RECOVERING; 
        
        // 3. 重新运行初始化序列
        OLED_Init(); 
        
        // 4. 恢复正常
        ScreenState = OLED_STATE_OK; 
        
        // 5. 立即重绘一帧，防止黑屏
        OLED_ShowFrame();
    }
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
    
    I2C_Hardware_Reset(); // 硬件复位 I2C 总线

    HAL_Delay((uint32_t)20); // 上电延时
    
    // --- 适配 DMA 的水平寻址初始化序列 ---
    static const uint8_t init_cmds[] = {
        0xAE,               // 1. 关闭显示 (Display Off)
        
        // --- 寻址模式核心配置 ---
        0x20, 0x00,         // 2. 设置内存寻址模式为 0x00 (水平寻址)
                            // 在此模式下，写满 128 字节会自动跳到下一页
                            
        0x21, 0x00, 0x7F,   // 3. 设置列起始和结束地址 (0-127)
                            // 告知芯片：DMA 冲锋的横向边界
                            
        0x22, 0x00, 0x07,   // 4. 设置页起始和结束地址 (0-7)
                            // 告知芯片：DMA 冲锋的纵向边界
        
        // --- 硬件物理配置 ---
        0xC8,               // 上下翻转 (Set COM Output Scan Direction)
        0xA1,               // 左右翻转 (Set Segment Re-map)
        0xA8, 0x3F,         // 64行多路复用 (Multiplex Ratio)
        0xD3, 0x00,         // 无偏移 (Display Offset)
        0x40,               // 设置显示起始行 (Display Start Line)
        
        // --- 亮度与驱动配置 ---
        0x81, 0xCF,         // 对比度 (Contrast Control)
        0xA4,               // 依照 GDDRAM 显示 (Entire Display On)
        0xA6,               // 正常显示 (Normal Display)
        
        // --- 时钟与电源 ---
        #if OLED_I2C_CLK_SPEED >= 1200000
        0xD5, 0xF0,         // 分频比 (Display Clock Divide Ratio) 超频模式下使用最快时钟
        #elif  OLED_I2C_CLK_SPEED >= 400000
        0xD5, 0xA0,         // 分频比 (Display Clock Divide Ratio) 中速模式下使用中等时钟
        #else
        0xD5, 0x80,         // 分频比 (Display Clock Divide Ratio)
        #endif
        0xD9, 0xF1,         // 预充电周期 (Pre-charge Period)
        0xDA, 0x12,         // COM 引脚硬件配置
        0xDB, 0x40,         // VCOMH 脱离电平
        0x8D, 0x14,         // 开启电荷泵 (Charge Pump)
        
        0xAF                // 5. 开启显示 (Display On)
    };

    // 使用阻塞模式发送初始化命令，确保屏幕先“醒过来”
    for(uint8_t i = 0; i < (uint8_t)sizeof(init_cmds); i++) {
        OLED_SendCmd(init_cmds[i]);
    }

    OLED_DMA_Buffer[0] = 0x40; // 这里的 0x40 是 Data Stream 标识符
    

    // 如果初始化过程中出错了（SendCmd里会改状态），则恢复原状态或保持OFFLINE
    if (ScreenState != OLED_STATE_OK) {
        // 初始化失败
        ScreenState = OLED_STATE_OFFLINE;
    } else {
        // 初始化成功
        OLED_DMA_Buffer[0] = 0x40; // 准备好 DMA 头字节
        memset(&OLED_DMA_Buffer[1], 0, 1024); // 清屏
        OLED_ShowFrame();
    }
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
    memset(&OLED_DMA_Buffer[1], 0, 1024);
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
            
            // 非阻塞超时检测
            // 检查是不是忙得太久了？(超过 25ms 肯定不正常)
            if (HAL_GetTick() - Last_DMA_Start_Tick > 25) {
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
        if (HAL_I2C_Master_Transmit_DMA(&I2C_Channel, OLED_ADDRESS, OLED_DMA_Buffer, 1025) == HAL_OK) {
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