// vim: set ft=cpp fenc=utf-8 ff=unix sw=4 ts=4 et :
#include "collector_mem.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

void MemCollector::update(MemMetrics& out) {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return;

    out.usage_pct = static_cast<float>(ms.dwMemoryLoad);
    out.total_gb  = static_cast<float>(ms.ullTotalPhys) / (1024.f * 1024.f * 1024.f);
    out.used_gb   = out.total_gb - static_cast<float>(ms.ullAvailPhys) / (1024.f * 1024.f * 1024.f);

    // vmmemWSL プロセスの Working Set 合計を取得（WSL 未使用時は 0）
    // 前方一致で拾う（"vmmemWSL (vhdx)" などのバリアント名にも対応）
    ULONGLONG wsl_bytes = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsnicmp(pe.szExeFile, L"vmmemWSL", 8) == 0) {
                    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                    if (hp) {
                        PROCESS_MEMORY_COUNTERS pmc{};
                        if (K32GetProcessMemoryInfo(hp, &pmc, sizeof(pmc)))
                            wsl_bytes += static_cast<ULONGLONG>(pmc.WorkingSetSize);
                        CloseHandle(hp);
                    }
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    out.wsl_gb = static_cast<float>(wsl_bytes) / (1024.f * 1024.f * 1024.f);
}
