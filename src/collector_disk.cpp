// vim: set ft=cpp fenc=utf-8 ff=unix sw=4 ts=4 et :
#include "collector_disk.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pdh.h>
#pragma comment(lib, "pdh.lib")
#include <array>

struct DiskCollector::Impl {
    PDH_HQUERY query = nullptr;
    // [0]=C:Read, [1]=C:Write, [2]=D:Read, [3]=D:Write
    std::array<PDH_HCOUNTER, 4> counters = {};
    char drives[2] = {'C', 'D'};
};

bool DiskCollector::init(char drive_c, char drive_d) {
    impl_ = new Impl();
    impl_->drives[0] = drive_c;
    impl_->drives[1] = drive_d;

    if (PdhOpenQuery(nullptr, 0, &impl_->query) != ERROR_SUCCESS) return false;

    const wchar_t* templates[4] = {
        L"\\LogicalDisk(%c:)\\Disk Read Bytes/sec",
        L"\\LogicalDisk(%c:)\\Disk Write Bytes/sec",
        L"\\LogicalDisk(%c:)\\Disk Read Bytes/sec",
        L"\\LogicalDisk(%c:)\\Disk Write Bytes/sec",
    };
    char drv_for[4] = {drive_c, drive_c, drive_d, drive_d};

    for (int i = 0; i < 4; ++i) {
        wchar_t buf[128];
        swprintf_s(buf, templates[i], static_cast<wchar_t>(drv_for[i]));
        PdhAddEnglishCounterW(impl_->query, buf, 0, &impl_->counters[i]);
    }

    PdhCollectQueryData(impl_->query);  // 初回サンプリング
    return true;
}

static float bytes_to_mb(double bytes) {
    return static_cast<float>(bytes / (1024.0 * 1024.0));
}

void DiskCollector::update(DiskMetrics& c, DiskMetrics& d) {
    if (!impl_) return;

    PdhCollectQueryData(impl_->query);

    auto get = [&](PDH_HCOUNTER hc) -> float {
        PDH_FMT_COUNTERVALUE v{};
        if (PdhGetFormattedCounterValue(hc, PDH_FMT_DOUBLE, nullptr, &v) == ERROR_SUCCESS) {
            return bytes_to_mb(v.doubleValue);
        }
        return 0.f;
    };

    c.read_mbps  = get(impl_->counters[0]);
    c.write_mbps = get(impl_->counters[1]);
    c.read_history.push(c.read_mbps);
    c.write_history.push(c.write_mbps);

    d.read_mbps  = get(impl_->counters[2]);
    d.write_mbps = get(impl_->counters[3]);
    d.read_history.push(d.read_mbps);
    d.write_history.push(d.write_mbps);
}

void DiskCollector::shutdown() {
    if (!impl_) return;
    if (impl_->query) { PdhCloseQuery(impl_->query); impl_->query = nullptr; }
    delete impl_;
    impl_ = nullptr;
}
