// vim: set ft=cpp fenc=utf-8 ff=unix sw=4 ts=4 et :
#pragma once
#include "metrics.hpp"

// CPU 使用率（PDH）と温度（WMI）の収集
//
// PDH カウンタで全体使用率と論理コア別使用率を取得する。
// WMI で CPU 温度を取得する（管理者権限が必要）。
class CpuCollector {
public:
    // PDH カウンタを初期化し WMI 接続を確立する。
    // 成功しない項目は avail フラグを false のままにする。
    bool init();

    // メトリクスを更新する（WM_TIMER から 1 秒ごとに呼び出す）
    void update(CpuMetrics& out);

    void shutdown();

    ~CpuCollector() { shutdown(); }

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
