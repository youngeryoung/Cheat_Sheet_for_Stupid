// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#ifndef __UART_DRIVER_H
#define __UART_DRIVER_H

#include "main.h"
#include <stdio.h>

// =============================================================
//                   用户硬件配置区 (User Config)
// =============================================================
// 请根据 CubeMX 中的配置修改此处

// --- 缓冲区配置 ---
#define RX_BUF_SIZE 128  // 接收缓冲区大小
#define UART_Timeout 15  // 超时时间（单位：ms）

// --- 波特率配置 ---
// 9600、115200、921600 常用波特率
#define UART1_BAUDRATE 115200
#define UART6_BAUDRATE 115200

// --- 硬件句柄引用 ---
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart6;

// --- 数据结构 ---
typedef struct {
    uint8_t  HasData;            // 收到新数据标志
    char     Buffer[RX_BUF_SIZE];// 原始数据字符串
    uint16_t Length;             // 数据长度
} UART_Data_t;


// =============================================================
//                     驱动接口定义区 (API)
// =============================================================

// 1. 初始化函数

/**
 * @brief 初始化 UART 接收
 */
void UART_Init(void);

// 2. 发送函数

/**
 * @brief 变参发送实现 (e.g. UART1_Send("Val=%d", 100))
 * @param fmt 格式字符串
 */
void UART1_Send(char *fmt, ...);

/**
 * @brief 变参发送实现 (e.g. UART6_Send("Val=%d", 100))
 * @param fmt 格式字符串
 */
void UART6_Send(char *fmt, ...); 

// 3. 接收查询 (配合业务逻辑)

/**
 * @brief 获取 UART1 接收到的数据包
 * @param out_buf 指向输出缓冲区的指针
 * @return 1=有新数据，0=无新数据
 */
uint8_t UART1_Get_Packet(char *out_buf);

/**
 * @brief 获取 UART6 接收到的数据包
 * @param out_buf 指向输出缓冲区的指针
 * @return 1=有新数据，0=无新数据
 */
uint8_t UART6_Get_Packet(char *out_buf);

// 4. 指令解析器 (核心积木)

/**
 * @brief 通用指令解析器
 * @param input_str 输入的指令字符串
 * @param split_char 指令与数值的分隔符
 * @param out_cmd   输出的指令部分
 * @param out_val   输出的数值部分
 * @return 1=解析成功, 0=格式错误
 */
uint8_t CMD_Parse(char *input_str, char split_char, char *out_cmd, int *out_val);

/**
 * @brief UART 接收完成回调 (空闲中断触发)
 * @param huart 指向 UART 句柄的指针
 * @param Size  本次接收到的数据长度
 * @note 用户无需手动调用此函数
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size);

// 5. Printf 重定向 
// 让 printf 直接输出到 USART1
// 已适配 GCC (STM32CubeIDE，VScode) 与 Keil MDK

#endif