// =============================================================
// [Cheat_Sheet_for_Stupid]  
// Author: 烛鵼 Young 
// "The shadow-bird mends broken wings of hardware"  
// =============================================================

#include "wave_proc.h"
#include <string.h>
#include <math.h>

// 静态变量
static uint32_t last_stable_trig_idx = 0;
static Wave_Config_t ProcCfg;

/**
 * @brief 初始化波形处理模块
 * @param config 指向配置结构体的指针
 */
void Wave_Proc_Init(Wave_Config_t *config) {
    ProcCfg = *config; // 复制配置
}

/**
 * @brief 触发搜索：带迟滞逻辑
 * @param buf 输入的 ADC 数据缓冲区
 * @param len 缓冲区长度
 * @param threshold 触发电平
 * @return 触发点索引
 */
static uint32_t Find_Trigger_Point(uint16_t *buf, uint32_t len, uint16_t threshold) {
    uint32_t search_limit = len - (uint32_t)(ProcCfg.ScreenWidth * ProcCfg.Zoom);
    if (search_limit > len) search_limit = len / 2; 

    uint16_t lower_thresh = (threshold > TRIGGER_HYSTERESIS) ? (threshold - TRIGGER_HYSTERESIS) : 0;
    uint8_t armed = 0; // 触发器状态机：0-等待进入下方，1-已进入下方(Ready)

    for (uint32_t i = 1; i < search_limit; i++) {
        // 状态 0: 寻找信号低于下限的时刻（确认信号处于低位）
        if (!armed) {
            if (buf[i] < lower_thresh) armed = 1;
        } 
        // 状态 1: 信号已处于低位，寻找跨越中心阈值的上升沿
        else {
            if (buf[i-1] < threshold && buf[i] >= threshold) {
                // 找到触发点 i
                
                // --- 抗晃动逻辑 ---
                // 如果这次触发点和上一次极其接近，说明是采样抖动，返回旧点以保持画面静止
                int32_t diff = (int32_t)i - (int32_t)last_stable_trig_idx;
                if (diff < 0) diff = -diff;
                
                if (diff <= TRIGGER_LOCK_RANGE) {
                    return last_stable_trig_idx; 
                }
                
                last_stable_trig_idx = i;
                return i;
            }
        }
    }
    return 0; // 未触发
}

/**
 * @brief 内部算法：波形识别
 * @param vpp 峰峰值
 * @param vrms_ac AC分量的有效值
 * @return 波形类型枚举
 * @note 原理：利用波峰因数 (Crest Factor) = Vpeak / Vrms
*/
static Wave_Type_t Guess_Wave_Type(float vpp, float vrms_ac) {
    if (vpp < 0.2f) return WAVE_TYPE_DC; // 噪音或直流
    
    // 计算 AC 分量的峰值 (假设对称)
    float v_peak_ac = vpp / 2.0f;
    if (vrms_ac == 0) return WAVE_TYPE_UNKNOWN;

    float ratio = v_peak_ac / vrms_ac;

    // 方波: Peak ≈ RMS, Ratio ≈ 1.0
    // 正弦: Peak = 1.414 * RMS, Ratio ≈ 1.414
    // 三角: Peak = 1.732 * RMS, Ratio ≈ 1.732
    
    if (ratio < 1.25f) return WAVE_TYPE_SQUARE;
    if (ratio < 1.55f) return WAVE_TYPE_SINE;
    if (ratio < 1.90f) return WAVE_TYPE_TRIANGLE;
    
    return WAVE_TYPE_UNKNOWN;
}

/**
 * @brief 根据目标频率和周期数，自动计算合适的 Zoom 值
 * @param signal_freq 信号频率 (Hz)
 * @param target_cycles 目标周期数 (屏幕上显示的周期数)
 * @return 计算得到的 Zoom 值
 * @note 原理：Step = (Points_Per_Sec / Signal_Freq) / (Screen_Width / Target_Cycles)
 */
float Wave_AutoZoom(uint32_t signal_freq, uint8_t target_cycles) {
    if (signal_freq == 0) return 1.0f; // 无信号默认1:1
    
    // 1. 一个周期有多少个采样点？
    float points_per_cycle = (float)ProcCfg.SampleRate / (float)signal_freq;
    
    // 2. 一个周期要在屏幕上占多少像素？
    float pixels_per_cycle = (float)ProcCfg.ScreenWidth / (float)target_cycles;
    
    // 3. 计算步长 (Step)
    float step = points_per_cycle / pixels_per_cycle;
    
    // 限制：步长不能小于0.1 (过度插值无意义)，也不能太大导致越界
    if (step < 0.1f) step = 0.1f;
    
    return step; 
}

/**
 * @brief 波形数据分析
 * @param input_buf ADC原始数据
 * @param len 数据长度
 * @param ext_freq 外部测得的频率 (若为0，则使用软件自动计算)
 * @param stats [输出] 统计结果
 */
void Wave_Analyze(uint16_t *input_buf, uint32_t len, uint32_t ext_freq, Wave_Stats_t *stats) {
    uint32_t sum = 0;
    uint64_t sum_sq = 0;
    uint16_t raw_max = 0;
    uint16_t raw_min = 0xFFFF;
    
    // 1. 一次遍历统计基础值 (Max, Min, Sum, SumSq)
    // 为了效率，如果Buffer极大，可以只采样前1000-2000点
    uint32_t scan_len = (len > MAX_SCAN_LEN) ? MAX_SCAN_LEN : len;
    
    for (int i = 0; i < scan_len; i++) {
        uint16_t val = input_buf[i];
        if (val > raw_max) raw_max = val;
        if (val < raw_min) raw_min = val;
        sum += val;
        sum_sq += (uint64_t)val * val;
    }

    // 2. 转换物理量
    float scale = ProcCfg.RefVoltage / (float)ProcCfg.ADC_Res;
    stats->Vmax = raw_max * scale;
    stats->Vmin = raw_min * scale;
    stats->Vavg = (sum * scale) / scan_len;
    stats->Vpp  = stats->Vmax - stats->Vmin;
    
    // 计算 RMS (Root Mean Square)
    float mean_sq = (float)(sum_sq) / scan_len;
    float raw_rms = sqrtf(mean_sq);
    stats->Vrms = raw_rms * scale;

    // 计算 AC RMS (用于波形识别，去除直流分量影响)
    // Variance = E[X^2] - (E[X])^2
    float variance = mean_sq - ((float)sum/scan_len)*((float)sum/scan_len);
    float vrms_ac = (variance > 0) ? sqrtf(variance) * scale : 0;
    
    // 3. 触发搜索
    uint16_t threshold = (raw_max + raw_min) / 2;
    if (raw_max - raw_min > TRIGGER_HYSTERESIS) {
        stats->TrigIdx = Find_Trigger_Point(input_buf, len, threshold);
        stats->IsTriggered = (stats->TrigIdx > 0);
    } else {
        stats->TrigIdx = 0;
        stats->IsTriggered = 0;
    }

    // 4. 频率计算策略
    if (ext_freq > 0) {
        // --- 模式A: 外部中断协助 (使用 IC_Driver 的结果) ---
        stats->Freq = ext_freq;
        // 占空比可以在这里计算，也可以通过比较大于 Vavg 的点数来估算
    } else {
        // --- 模式B: DMA独立软件计算 (过零检测) ---
        if (stats->IsTriggered) {
            uint32_t cross_cnt = 0;
            uint32_t first_idx = 0, last_idx = 0;
            // 简单统计高电平点数来算占空比
            uint32_t high_pts = 0;
            
            // 从触发点后开始搜
            for (int i = stats->TrigIdx + 1; i < scan_len - 1; i++) {
                if (input_buf[i] > threshold) high_pts++;
                
                // 上升沿检测
                if (input_buf[i-1] < threshold && input_buf[i] >= threshold) {
                    if (cross_cnt == 0) first_idx = i;
                    last_idx = i;
                    cross_cnt++;
                }
            }
            
            if (cross_cnt > 1) {
                float dist = (float)(last_idx - first_idx);
                stats->Freq = (uint32_t)((float)(cross_cnt - 1) * ProcCfg.SampleRate / dist);
                stats->Duty = (float)high_pts / (float)(scan_len - stats->TrigIdx) * 100.0f; // 粗略估算
            } else {
                stats->Freq = 0;
                stats->Duty = 0;
            }
        } else {
            stats->Freq = 0;
        }
    }
    
    // 5. 波形识别
    stats->Type = Guess_Wave_Type(stats->Vpp, vrms_ac);
}

/**
 * @brief 波形绘图生成
 * @param input_buf ADC原始数据
 * @param len 数据长度
 * @param stats 已计算好的统计信息 (利用其中的 TrigIdx 和 Vpp 等)
 * @param output_buf [输出] OLED绘图数据 (Y坐标数组)
 */
void Wave_GeneratePlot(uint16_t *input_buf, uint32_t len, Wave_Stats_t *stats, uint8_t *output_buf) {
    uint32_t start_idx = stats->TrigIdx;
    
    // 自动 Zoom 计算 (如果需要的话，可以在这里调用，或者外部设好)
    // 这里直接使用 ProcCfg.Zoom

    // 预计算缩放因子
    float factor = (float)ProcCfg.ScreenHeight / (float)ProcCfg.ADC_Res;

    for (int x = 0; x < ProcCfg.ScreenWidth; x++) {
        // 坐标映射：Idx = Start + x * Zoom
        // 使用浮点数保证相位精度
        float exact_idx = (float)start_idx + ((float)x * ProcCfg.Zoom);
        uint32_t i = (uint32_t)exact_idx;
        
        if (i >= len) i = len - 1;
        
        uint16_t val = input_buf[i];
        
        // Y轴归一化: 0 ~ ScreenHeight
        // 公式：(Val / ADC_Res) * H
        // 注意：OLED通常 (0,0) 在左上角，需要反转Y轴: H - y
        float y_norm = (float)val * factor;
        
        // 四舍五入
        int y_pixel = (int)(y_norm + 0.5f);
        
        // 边界限制
        if (y_pixel < 0) y_pixel = 0;
        if (y_pixel >= ProcCfg.ScreenHeight) y_pixel = ProcCfg.ScreenHeight - 1;
        
        // 反转Y轴写入Buffer
        output_buf[x] = (ProcCfg.ScreenHeight - 1) - y_pixel;
    }
}