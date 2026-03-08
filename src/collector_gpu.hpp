// vim: set ft=cpp fenc=utf-8 ff=unix sw=4 ts=4 et :
#pragma once
#include "metrics.hpp"

// NVIDIA GPU の使用率・温度・VRAM 収集
//
// nvml.dll を LoadLibrary で動的ロードする。
// GPU がない環境や NVML がロードできない環境では avail = false となり、クラッシュしない。
class GpuCollector {
public:
    // nvml.dll を動的ロードして初期化する
    bool init();
    // GPU 使用率・温度のみ更新する
    void update_gpu(GpuMetrics& gpu);
    // GPU 使用率・温度 + VRAM を更新する
    void update_all(GpuMetrics& gpu, VramMetrics& vram);
    void shutdown();

    ~GpuCollector() { shutdown(); }

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
