// vim: set ft=cpp fenc=utf-8 ff=unix sw=4 ts=4 et :
#include "collector_mem.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pdh.h>
#pragma comment(lib, "pdh.lib")

void MemCollector::update(MemMetrics& out) {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return;

    out.usage_pct = static_cast<float>(ms.dwMemoryLoad);
    out.total_gb  = static_cast<float>(ms.ullTotalPhys) / (1024.f * 1024.f * 1024.f);
    out.used_gb   = out.total_gb - static_cast<float>(ms.ullAvailPhys) / (1024.f * 1024.f * 1024.f);

    // WSL2 VM の Working Set を PDH カウンタで取得（未起動時は 0）
    // vmmem は PPL のため OpenProcess 不可。PDH はハンドル不要で取得できる。
    // プロセス名は OS バージョンによって異なるため複数バリアントを登録して合算する
    out.wsl_gb = 0.f;
    PDH_HQUERY q = nullptr;
    if (PdhOpenQuery(nullptr, 0, &q) == ERROR_SUCCESS) {
        static constexpr const wchar_t* VMMEM_COUNTERS[] = {
            L"\\Process(vmmem)\\Working Set",     // Windows 10 WSL2
            L"\\Process(vmmemWSL)\\Working Set",  // Windows 11 WSL2
        };
        // 全カウンタを先に登録してから一括取得する（PDH の正しい使い方）
        PDH_HCOUNTER counters[2] = {};
        int n = 0;
        for (auto path : VMMEM_COUNTERS) {
            if (PdhAddEnglishCounterW(q, path, 0, &counters[n]) == ERROR_SUCCESS)
                ++n;
        }
        if (n > 0 && PdhCollectQueryData(q) == ERROR_SUCCESS) {
            LONGLONG wsl_bytes = 0;
            for (int i = 0; i < n; ++i) {
                PDH_FMT_COUNTERVALUE val{};
                if (PdhGetFormattedCounterValue(counters[i], PDH_FMT_LARGE, nullptr, &val) == ERROR_SUCCESS)
                    wsl_bytes += val.largeValue;
            }
            out.wsl_gb = static_cast<float>(wsl_bytes) / (1024.f * 1024.f * 1024.f);
        }
        PdhCloseQuery(q);
    }
}
