// vim: set ft=cpp fenc=utf-8 ff=unix sw=4 ts=4 et :
#pragma once
#include "metrics.hpp"

// ネットワーク通信量収集（PDH、全 NIC 合算）
class NetCollector {
public:
    bool init();
    void update(NetMetrics& out);
    void shutdown();

    ~NetCollector() { shutdown(); }

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
