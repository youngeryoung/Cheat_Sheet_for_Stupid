// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#ifndef __OLED_H
#define __OLED_H

#include "main.h"
#include "font.h" 

extern const ASCIIFont fonta;
extern const UnicodeFont fontu;

// =============================================================
//                  用户硬件配置区 (User Config)
// =============================================================
// 请根据 CubeMX 中的配置修改此处

#define SCL_Pin GPIO_PIN_6
#define SCL_GPIO_Port GPIOB
#define SDA_Pin GPIO_PIN_7
#define SDA_GPIO_Port GPIOB

#define I2C_Channel hi2c1 // 使用的 I2C 通道句柄

// --- 颜色模式 ---
typedef enum {
    OLED_COLOR_NORMAL = 0, // 正常 (白字黑底)
    OLED_COLOR_REVERSE = 1 // 反色 (黑字白底)
} OLED_ColorMode;

// --- 宏定义与显存 ---
#define OLED_ADDRESS 0x78
#define OLED_WIDTH   (uint8_t)128
#define OLED_HEIGHT  (uint8_t)64
#define RECOVERY_INTERVAL 500 // 掉线恢复尝试间隔 (ms)

// I2C 通讯频率设定 (Hz)
// 标准: 400000 (400kHz)
// 建议超频: 1000000 (1MHz) 上限: 1675000 (1.675MHz)
// 注意：若发现屏幕显示花屏或无法初始化，请降回 400000
#define OLED_I2C_CLK_SPEED    1000000 

// --- 系统状态 ---
typedef enum {
    OLED_STATE_OK = 0,      // 正常工作
    OLED_STATE_OFFLINE,     // 已掉线
    OLED_STATE_RECOVERING   // 正在尝试恢复
} OLED_State_t;

// =============================================================
//                     驱动接口定义区 (API)
// =============================================================

// --- 基础控制 ---

/**
 * @brief 检查 I2C 是否正在传输
 */
uint8_t OLED_IsBusy(void);

/**
 * @brief 初始化 OLED 屏幕
 */
void OLED_Init(void);

/**
 * @brief 准备新一帧的显存 (清空显存缓冲区)
 */
void OLED_NewFrame(void);

/**
 * @brief 异步显示刷新：开启 DMA 冲刺
 */
void OLED_ShowFrame(void);

// --- 绘图 ---

/**
 * @brief 在显存中画一个点
 * @param x   x坐标
 * @param y   y坐标
 * @param color  颜色模式
 */
void OLED_SetPixel(uint8_t x, uint8_t y, OLED_ColorMode color);

/**
 * @brief 画线 (Bresenham 算法，带裁剪)
 * @param x1  起点x坐标
 * @param y1  起点y坐标
 * @param x2  终点x坐标
 * @param y2  终点y坐标
 * @param color  颜色模式
 */
void OLED_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, OLED_ColorMode color);

/**
 * @brief 画圆 (中点圆算法)
 * @param x0  圆心x坐标
 * @param y0  圆心y坐标
 * @param r   半径
 * @param color  颜色模式
 */
void OLED_DrawCircle(uint8_t x0, uint8_t y0, uint8_t r, OLED_ColorMode color);

/**
 * @brief 实心圆 (使用快速水平线填充)
 * @param x0  圆心x坐标
 * @param y0  圆心y坐标
 * @param r   半径
 * @param color  颜色模式
 */
void OLED_DrawFilledCircle(uint8_t x0, uint8_t y0, uint8_t r, OLED_ColorMode color);

/**
 * @brief 画矩形 (拆分为2条横线+2条竖线，比Bresenham快)
 * @param x   起始x坐标
 * @param y   起始y坐标
 * @param w   宽度
 * @param h   高度
 * @param color  颜色模式
 */
void OLED_DrawRectangle(uint8_t x, uint8_t y, uint8_t w, uint8_t h, OLED_ColorMode color);

/**
 * @brief 实心矩形
 * @param x   起始x坐标
 * @param y   起始y坐标
 * @param w   宽度
 * @param h   高度
 * @param color  颜色模式
 */
void OLED_DrawFilledRectangle(uint8_t x, uint8_t y, uint8_t w, uint8_t h, OLED_ColorMode color);


/**
 * @brief 高性能位图绘制引擎
 * @param x, y   起始坐标
 * @param w, h   位图宽高
 * @param bitmap 位图数据指针 (垂直字节序)
 * @param color  OLED_COLOR_NORMAL(覆盖写入), OLED_COLOR_REVERSE(反色覆盖)
 */
void _Draw_Bitmap(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *bitmap, OLED_ColorMode color);

// --- 文本 API ---

/**
 * @brief    打印字符串
 * @param x  起始x坐标
 * @param y  起始y坐标
 * @param str 字符串指针 (UTF-8 编码)
 * @param color  颜色模式
 */
void OLED_PrintString(uint8_t x, uint8_t y, const char *str, OLED_ColorMode color);

/**
 * @brief 打印整数
 * @param x  起始x坐标
 * @param y  起始y坐标
 * @param num  要打印的整数
 * @param color  颜色模式
 */
void OLED_PrintInt(uint8_t x, uint8_t y, int32_t num, OLED_ColorMode color);

/**
 * @brief 打印浮点数
 * @param x  起始x坐标
 * @param y  起始y坐标
 * @param num  要打印的浮点数
 * @param precision 小数点后位数
 * @param color  颜色模式
 */
void OLED_PrintFloat(uint8_t x, uint8_t y, double num, uint8_t precision, OLED_ColorMode color);

// --- 错误回调 ---
/**
 * @brief I2C 错误回调处理
 * @param hi2c 指向 I2C 句柄的指针
 * @note 用户无需手动调用此函数
 */
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c);

#endif