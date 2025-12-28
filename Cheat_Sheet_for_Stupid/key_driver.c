// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#include "key_driver.h"

// --- 全局状态 ---
volatile uint32_t Key_State_Now = 0;   
volatile uint32_t Key_Event_Trig = 0;
static uint8_t Debounce_Cnt = 0;

/**
 * @brief  初始化 (需在 main 初始化中调用)
 */
void Key_Init(void) {
    // 默认所有行保持空闲（高电平）
    ROW1_H(); ROW2_H(); ROW3_H(); ROW4_H();
    
    // 初始化状态
    Key_State_Now = 0;
    Key_Event_Trig = 0;
    Debounce_Cnt = 0;
}

/**
 * @brief  清空所有未处理的按键按下事件
 */
void Key_Clear_Event(void) {
    // 初始化状态
    Key_Event_Trig = 0;
}

/**
 * @brief 扫描一行的四个列
 * @param row_idx 行索引 (0-3)
 */
static uint16_t Scan_Single_Row(uint8_t row_idx) {
    uint16_t row_data = 0;
    
    // 1. 激活当前行
    switch(row_idx) {
        case 0: ROW1_L(); break;
        case 1: ROW2_L(); break;
        case 2: ROW3_L(); break;
        case 3: ROW4_L(); break;
    }

    // 2. 关键（并非）：等待电平稳定 (解决同时按下多个键时的电平漂移)
    //__NOP(); __NOP(); __NOP(); __NOP();

    // 3. 读取列状态 (逻辑取反：按下为1，松开为0)
    uint16_t cols = READ_COLS();
    if (!(cols & (1 << 12))) row_data |= (1 << 0);
    if (!(cols & (1 << 13))) row_data |= (1 << 1);
    if (!(cols & (1 << 14))) row_data |= (1 << 2);
    if (!(cols & (1 << 15))) row_data |= (1 << 3);

    // 4. 释放当前行
    switch(row_idx) {
        case 0: ROW1_H(); break;
        case 1: ROW2_H(); break;
        case 2: ROW3_H(); break;
        case 3: ROW4_H(); break;
    }

    return row_data;
}

/**
 * @brief  扫描一次按键状态 (中断调用)
 * @note   请在 main.c 的 HAL_TIM_PeriodElapsedCallback 中调用此函数
 */
void Key_Scan_IT(void) {
    uint32_t raw_data = 0;

    // 1. 扫描矩阵键盘 (逐行扫描)
    for (uint8_t i = 0; i < 4; i++) {
        raw_data |= (uint32_t)(Scan_Single_Row(i) << (i * 4));
    }

    // 2. 扫描独立按键
    if (!(BTN_A_GPIO_Port->IDR & BTN_A_Pin)) {
        raw_data |= (1 << KEY_ID_BTN_A);
    }
    if (!(BTN_B_GPIO_Port->IDR & BTN_B_Pin)) {
        raw_data |= (1 << KEY_ID_BTN_B);
    }

    // 3. 消抖逻辑
    static uint32_t last_raw = 0;
    if (raw_data == last_raw) {
        if (++Debounce_Cnt >= 2) { 
            Debounce_Cnt = 2; // 维持上限
            if (Key_State_Now != raw_data) {
                // 边沿检测：记录刚被按下的位
                Key_Event_Trig |= (raw_data & (raw_data ^ Key_State_Now));
                Key_State_Now = raw_data;
            }
        }
    } else {
        Debounce_Cnt = 0;
        last_raw = raw_data;
    }
}

// --- API ---

/**
 * @brief  获取按键按下事件 (读后清除)
 * @param  key_id: 0-15(矩阵), 16(PA5), 17(PB1)
 * @return 1: 刚刚按下, 0: 无事件
 */
uint8_t Key_GetEvent_Press(uint8_t key_id) {
    if (key_id > 31) return 0;
    uint32_t mask = (1U << key_id);
    if (Key_Event_Trig & mask) {
        Key_Event_Trig &= ~mask; // 清除
        return 1;
    }
    return 0;
}

/**
 * @brief  查询按键是否按住 (长按检测)
 * @param  key_id: 0-17
 * @return 1: 按住中, 0: 松开
 */
uint8_t Key_IsDown(uint8_t key_id) {
    if (key_id > 31) return 0;
    return (Key_State_Now & (1U << key_id)) ? 1 : 0;
}

/**
 * @brief 获取所有按键的原始状态位图 (用于调试)
 * @return bit0-15:矩阵, bit16:BtnA, bit17:BtnB
 */
uint32_t Key_GetRawState(void) {
    return Key_State_Now;
}