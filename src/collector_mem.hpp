// vim: set ft=cpp fenc=utf-8 ff=unix sw=4 ts=4 et :
#pragma once
#include "metrics.hpp"

// 物理メモリ使用量の収集（GlobalMemoryStatusEx 使用）
class MemCollector {
public:
    void update(MemMetrics& out);
};
