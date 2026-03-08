// vim: set ft=cpp fenc=utf-8 ff=unix sw=4 ts=4 et :
#pragma once
#include "ring_buffer.hpp"

// 全メトリクスデータ構造体

// CPU：全体使用率（面グラフ）+ コア別縦バー + 温度（横バー）
struct CpuMetrics {
    RingBuffer<float, 60> total_history;  // 全体使用率履歴（%）
    float total_pct    = 0.f;
    float core_pct[16] = {};             // 論理コア別使用率（%）
    float temp_celsius = 0.f;           // CPU 温度
    bool  temp_avail   = false;         // WMI 温度取得成功フラグ
    char  name[48]     = {};            // CPU ブランド名（CPUID 取得）
};

// GPU：使用率（面グラフ）+ 温度（横バー）
struct GpuMetrics {
    RingBuffer<float, 60> usage_history; // 使用率履歴（%）
    float usage_pct    = 0.f;
    float temp_celsius = 0.f;
    bool  avail        = false;         // NVML ロード成功フラグ
    char  name[48]     = {};            // GPU 名（NVML 取得）
};

// RAM：横バー + 使用量表示
struct MemMetrics {
    float usage_pct = 0.f;
    float used_gb   = 0.f;
    float total_gb  = 0.f;
    float wsl_gb    = 0.f;  // vmmemWSL プロセスの Working Set 合計（WSL 非使用時は 0）
};

// VRAM：面グラフ + 使用量表示
struct VramMetrics {
    RingBuffer<float, 60> usage_history;
    float usage_pct = 0.f;
    float used_gb   = 0.f;
    float total_gb  = 0.f;
    bool  avail     = false;
};

// Disk I/O：Read/Write 分離の面グラフ
struct DiskMetrics {
    RingBuffer<float, 60> read_history;  // Read MB/s
    RingBuffer<float, 60> write_history; // Write MB/s
    float read_mbps  = 0.f;
    float write_mbps = 0.f;
    char  drive      = 'C';
};

// Network：送受信分離の面グラフ
struct NetMetrics {
    RingBuffer<float, 60> send_history;  // 送信 KB/s
    RingBuffer<float, 60> recv_history;  // 受信 KB/s
    float send_kbps = 0.f;
    float recv_kbps = 0.f;
};

// Claude Code：レートリミット + セッション数
struct ClaudeMetrics {
    float five_h_pct    = 0.f;
    float seven_d_pct   = 0.f;
    wchar_t five_h_reset[20]  = {};      // L"HH:MM" 形式
    wchar_t seven_d_reset[32] = {};      // L"M/D 曜 HH:MM" 形式
    float five_h_expected_pct  = 0.f;   // 5h ウィンドウの均等消費ペース（%）
    float seven_d_expected_pct = 0.f;   // 7d ウィンドウの均等消費ペース（%）
    char  plan_label[16]    = {};       // "Max5", "Pro" 等
    int   session_count = 0;
    bool  avail         = false;
};

// 全メトリクスを束ねる集約構造体
struct AllMetrics {
    CpuMetrics  cpu;
    GpuMetrics  gpu;
    MemMetrics  mem;
    VramMetrics vram;
    DiskMetrics disk_c;
    DiskMetrics disk_d;
    NetMetrics  net;
    ClaudeMetrics claude;
};
