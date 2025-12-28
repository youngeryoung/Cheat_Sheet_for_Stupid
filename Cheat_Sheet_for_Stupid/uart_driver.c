// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#include "uart_driver.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h> 

// --- 内部变量 ---
// 接收缓冲区 (Double Buffering 的简化版)
static uint8_t  Rx1_Temp[RX_BUF_SIZE]; // DMA/IT 原始接收区
static uint8_t  Rx6_Temp[RX_BUF_SIZE];

// 处理后的数据包
static UART_Data_t U1_Packet = {0};
static UART_Data_t U6_Packet = {0};

/**
 * @brief 初始化 UART 接收
 */
void UART_Init(void)
{
    // --- 如果有必要，重新配置波特率 ---

    # if UART1_BAUDRATE!= 115200
    huart1.Init.BaudRate = UART1_BAUDRATE;
    if (HAL_UART_Init(&huart1) != HAL_OK) {
        Error_Handler();
    }
    # endif
    # if UART6_BAUDRATE!= 115200
    huart6.Init.BaudRate = UART6_BAUDRATE;
    if (HAL_UART_Init(&huart6) != HAL_OK) {
        Error_Handler();

    }
    # endif
    // 开启空闲中断接收 (最适合不定长数据)
    // 当接收到数据流并且线路空闲(IDLE)时，才会触发回调
    HAL_UARTEx_ReceiveToIdle_IT(&huart1, Rx1_Temp, RX_BUF_SIZE);
    HAL_UARTEx_ReceiveToIdle_IT(&huart6, Rx6_Temp, RX_BUF_SIZE);
}

/**
 * @brief UART 接收完成回调 (空闲中断触发)
 * @param huart 指向 UART 句柄的指针
 * @param Size  本次接收到的数据长度
 * @note 用户无需手动调用此函数
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART1)
    {
        // 搬运数据到逻辑缓冲区
        if (Size < RX_BUF_SIZE) {
            memcpy(U1_Packet.Buffer, Rx1_Temp, Size);
            U1_Packet.Buffer[Size] = '\0'; // 添加字符串结束符
            U1_Packet.Length = Size;
            U1_Packet.HasData = 1;         // 置位标志
        } else {
            // 异常处理：截断
            memcpy(U1_Packet.Buffer, Rx1_Temp, RX_BUF_SIZE - 1);
            U1_Packet.Buffer[RX_BUF_SIZE - 1] = '\0';
            U1_Packet.Length = RX_BUF_SIZE - 1;
            U1_Packet.HasData = 1;         // 置位标志
        }
        // 重新开启接收
        HAL_UARTEx_ReceiveToIdle_IT(&huart1, Rx1_Temp, RX_BUF_SIZE);
    }
    else if (huart->Instance == USART6)
    {
        if (Size < RX_BUF_SIZE) {
            memcpy(U6_Packet.Buffer, Rx6_Temp, Size);
            U6_Packet.Buffer[Size] = '\0';
            U6_Packet.Length = Size;
            U6_Packet.HasData = 1;
        } else {
            // 异常处理：截断
            memcpy(U6_Packet.Buffer, Rx6_Temp, RX_BUF_SIZE - 1);
            U6_Packet.Buffer[RX_BUF_SIZE - 1] = '\0';
            U6_Packet.Length = RX_BUF_SIZE - 1;
            U6_Packet.HasData = 1;         // 置位标志
        }
        // 重新开启接收
        HAL_UARTEx_ReceiveToIdle_IT(&huart6, Rx6_Temp, RX_BUF_SIZE);
    }
}

// --- 3. 发送函数封装 ---

/**
 * @brief 变参发送实现 (e.g. UART1_Send("Val=%d", 100))
 * @param fmt 格式字符串
 */
void UART1_Send(char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    HAL_UART_Transmit(&huart1, (uint8_t*)buf, strlen(buf), UART_Timeout);
}

/**
 * @brief 变参发送实现 (e.g. UART6_Send("Val=%d", 100))
 * @param fmt 格式字符串
 */
void UART6_Send(char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    HAL_UART_Transmit(&huart6, (uint8_t*)buf, strlen(buf), UART_Timeout);
}

// --- 4. 接收获取接口 ---

/**
 * @brief 获取 UART1 接收到的数据包
 * @param out_buf 指向输出缓冲区的指针
 * @return 1=有新数据，0=无新数据
 */
uint8_t UART1_Get_Packet(char *out_buf)
{
    if (U1_Packet.HasData) {
        strcpy(out_buf, U1_Packet.Buffer);
        U1_Packet.HasData = 0; // 清除标志
        return 1;
    }
    return 0;
}

/**
 * @brief 获取 UART6 接收到的数据包
 * @param out_buf 指向输出缓冲区的指针
 * @return 1=有新数据，0=无新数据
 */
uint8_t UART6_Get_Packet(char *out_buf)
{
    if (U6_Packet.HasData) {
        strcpy(out_buf, U6_Packet.Buffer);
        U6_Packet.HasData = 0;
        return 1;
    }
    return 0;
}

// --- 5. 通用指令解析器 ---

/**
 * @brief 通用指令解析器
 * @param input_str 输入的指令字符串
 * @param split_char 指令与数值的分隔符
 * @param out_cmd   输出的指令部分
 * @param out_val   输出的数值部分
 * @return 1=解析成功, 0=格式错误
 */
uint8_t CMD_Parse(char *input_str, char split_char, char *out_cmd, int *out_val)
{
    // 查找冒号的位置
    char *colon_pos = strchr(input_str, split_char);
    if (colon_pos == NULL) return 0; // 没找到冒号
    
    // 提取指令部分 (CMD)
    int cmd_len = colon_pos - input_str;
    if (cmd_len > 10) return 0; // 指令太长，认为是垃圾数据
    
    strncpy(out_cmd, input_str, cmd_len);
    out_cmd[cmd_len] = '\0'; // 封口
    
    // 提取数值部分 (VAL)
    // 冒号后移一位就是数字
    *out_val = atoi(colon_pos + 1);
    
    return 1; // 成功
}

// --- 6. Printf 重定向 ---
// 让 printf 直接输出到 USART1
// 适配 GCC (STM32CubeIDE)
#ifdef __GNUC__
int _write(int file, char *ptr, int len)
{
    HAL_UART_Transmit(&huart1, (uint8_t*)ptr, len, UART_Timeout);
    return len;
}
#else
// 适配 Keil MDK
int fputc(int ch, FILE *f)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, UART_Timeout);
    return ch;
}
#endif