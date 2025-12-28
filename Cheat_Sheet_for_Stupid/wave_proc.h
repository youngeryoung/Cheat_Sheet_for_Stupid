// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#ifndef __WAVE_PROC_H
#define __WAVE_PROC_H

#include <stdint.h>

// =============================================================
//                    用户配置区 (User Config)
// =============================================================

// 定义触发迟滞的大小（根据ADC量程，4096下的 50-100 是个不错的选择）
#define TRIGGER_HYSTERESIS  80 
// 定义触发锁定阈值（如果新旧触发点距离小于此值，则认为波形静止，不更新触发位置）
#define TRIGGER_LOCK_RANGE  5
// 最大扫描长度
#define MAX_SCAN_LEN 2250

// ================= 数据类型定义 =================

// 波形类型枚举
typedef enum {
    WAVE_TYPE_UNKNOWN = 0,
    WAVE_TYPE_SINE,
    WAVE_TYPE_SQUARE,
    WAVE_TYPE_TRIANGLE,
    WAVE_TYPE_DC
} Wave_Type_t;

// 统计结果
typedef struct {
    // --- 电压信息 ---
    float Vmax;      // 峰电压 (Peak)
    float Vmin;      // 谷电压 (Valley)
    float Vpp;       // 峰峰值
    float Vavg;      // 平均值 (DC Bias)
    float Vrms;      // 有效值 (AC+DC)
    
    // --- 时间信息 ---
    uint32_t Freq;   // 频率 (Hz) - 可是软件计算也可是外部传入
    float Duty;      // 占空比 (%)
    
    // --- 辅助信息 ---
    Wave_Type_t Type; // 波形类型猜测
    uint32_t TrigIdx; // 触发点在Buffer中的索引
    uint8_t IsTriggered; // 是否成功触发
} Wave_Stats_t;

// 配置参数
typedef struct {
    float Zoom;          
    float RefVoltage;    
    uint16_t ADC_Res;    
    uint32_t SampleRate; 
    uint8_t ScreenHeight;
    uint8_t ScreenWidth; 
} Wave_Config_t;

// =============================================================
//                       接口定义区 (API)
// =============================================================

/**
 * @brief 初始化波形处理模块
 * @param config 指向配置结构体的指针
 */
void Wave_Proc_Init(Wave_Config_t *config);

/**
 * @brief 根据目标频率和周期数，自动计算合适的 Zoom 值
 * @param signal_freq 信号频率 (Hz)
 * @param target_cycles 目标周期数 (屏幕上显示的周期数)
 * @return 计算得到的 Zoom 值
 * @note 原理：Step = (Points_Per_Sec / Signal_Freq) / (Screen_Width / Target_Cycles)
 */
float Wave_AutoZoom(uint32_t signal_freq, uint8_t target_cycles);

/**
 * @brief 波形数据分析
 * @param input_buf ADC原始数据
 * @param len 数据长度
 * @param ext_freq 外部测得的频率 (若为0，则使用软件自动计算)
 * @param stats [输出] 统计结果
 */
void Wave_Analyze(uint16_t *input_buf, uint32_t len, uint32_t ext_freq, Wave_Stats_t *stats);

/**
 * @brief 波形绘图生成
 * @param input_buf ADC原始数据
 * @param len 数据长度
 * @param stats 已计算好的统计信息 (利用其中的 TrigIdx 和 Vpp 等)
 * @param output_buf [输出] OLED绘图数据 (Y坐标数组)
 */
void Wave_GeneratePlot(uint16_t *input_buf, uint32_t len, Wave_Stats_t *stats, uint8_t *output_buf);

#endif // __WAVE_PROC_H