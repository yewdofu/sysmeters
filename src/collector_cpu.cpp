// vim: set ft=cpp fenc=utf-8 ff=unix sw=4 ts=4 et :
#include "collector_cpu.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
// WMI
#define _WIN32_DCOM
#include <comdef.h>
#include <wbemidl.h>
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "pdh.lib")

#include <vector>
#include <string>
#include <cstdio>

// PDH カウンタ + WMI 接続の実装詳細
struct CpuCollector::Impl {
    // PDH
    PDH_HQUERY   query          = nullptr;
    PDH_HCOUNTER counter_total  = nullptr;
    std::vector<PDH_HCOUNTER> counter_cores;

    // WMI
    IWbemLocator*  wbem_locator  = nullptr;
    IWbemServices* wbem_services = nullptr;
    bool           wmi_ok        = false;
};

// 0xRRGGBB 形式の uint32_t を D2D1_COLOR_F に変換（ここでは使わないが一貫性のため保持）

bool CpuCollector::init() {
    impl_ = new Impl();

    // --- PDH 初期化 ---
    if (PdhOpenQuery(nullptr, 0, &impl_->query) != ERROR_SUCCESS) return false;

    // 全体使用率カウンタ
    if (PdhAddEnglishCounterW(impl_->query, L"\\Processor(_Total)\\% Processor Time",
                              0, &impl_->counter_total) != ERROR_SUCCESS) {
        return false;
    }

    // 論理コア別カウンタ（最大 16 まで試みる）
    for (int i = 0; i < 16; ++i) {
        wchar_t buf[64];
        swprintf_s(buf, L"\\Processor(%d)\\%% Processor Time", i);
        PDH_HCOUNTER hc = nullptr;
        if (PdhAddEnglishCounterW(impl_->query, buf, 0, &hc) == ERROR_SUCCESS) {
            impl_->counter_cores.push_back(hc);
        }
    }

    // 最初のサンプリング（PDH は 2 回目以降が有効）
    PdhCollectQueryData(impl_->query);

    // --- WMI 初期化 ---
    // CoInitializeEx は main で呼ばれている前提
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, reinterpret_cast<void**>(&impl_->wbem_locator));
    if (FAILED(hr)) return true;  // PDH は OK なので true を返す

    hr = impl_->wbem_locator->ConnectServer(
        _bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr, nullptr,
        &impl_->wbem_services);
    if (FAILED(hr)) { impl_->wbem_locator->Release(); impl_->wbem_locator = nullptr; return true; }

    hr = CoSetProxyBlanket(impl_->wbem_services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                           nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                           nullptr, EOAC_NONE);
    if (FAILED(hr)) {
        impl_->wbem_services->Release();
        impl_->wbem_services = nullptr;
        impl_->wbem_locator->Release();
        impl_->wbem_locator = nullptr;
        return true;
    }

    impl_->wmi_ok = true;
    return true;
}

void CpuCollector::update(CpuMetrics& out) {
    if (!impl_) return;

    // PDH サンプル収集
    PdhCollectQueryData(impl_->query);

    // 全体使用率
    PDH_FMT_COUNTERVALUE val{};
    if (PdhGetFormattedCounterValue(impl_->counter_total, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS) {
        out.total_pct = static_cast<float>(val.doubleValue);
        out.total_history.push(out.total_pct);
    }

    // コア別使用率
    for (int i = 0; i < static_cast<int>(impl_->counter_cores.size()) && i < 16; ++i) {
        PDH_FMT_COUNTERVALUE cv{};
        if (PdhGetFormattedCounterValue(impl_->counter_cores[i], PDH_FMT_DOUBLE, nullptr, &cv) == ERROR_SUCCESS) {
            out.core_pct[i] = static_cast<float>(cv.doubleValue);
        }
    }

    // WMI 温度取得
    if (!impl_->wmi_ok || !impl_->wbem_services) {
        out.temp_avail = false;
        return;
    }

    IEnumWbemClassObject* enumerator = nullptr;
    HRESULT hr = impl_->wbem_services->ExecQuery(
        _bstr_t(L"WQL"),
        _bstr_t(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator);
    if (FAILED(hr) || !enumerator) { out.temp_avail = false; return; }

    IWbemClassObject* obj = nullptr;
    ULONG returned = 0;
    float sum = 0.f;
    int   cnt = 0;

    // タイムアウト 500ms（WBEM_INFINITE だと WMI 無応答時に UI スレッドがブロックされる）
    while (enumerator->Next(500, 1, &obj, &returned) == WBEM_S_NO_ERROR) {
        VARIANT vtProp;
        VariantInit(&vtProp);
        if (SUCCEEDED(obj->Get(L"CurrentTemperature", 0, &vtProp, nullptr, nullptr))) {
            // WMI 温度は 10 分の 1 ケルビン（デシケルビン）
            float kelvin = static_cast<float>(vtProp.uintVal) / 10.f;
            sum += kelvin - 273.15f;
            ++cnt;
        }
        VariantClear(&vtProp);
        obj->Release();
    }
    enumerator->Release();

    if (cnt > 0) {
        out.temp_celsius = sum / static_cast<float>(cnt);
        out.temp_avail   = true;
    }
    else {
        out.temp_avail = false;
    }
}

void CpuCollector::shutdown() {
    if (!impl_) return;
    if (impl_->query) { PdhCloseQuery(impl_->query); impl_->query = nullptr; }
    if (impl_->wbem_services) { impl_->wbem_services->Release(); impl_->wbem_services = nullptr; }
    if (impl_->wbem_locator)  { impl_->wbem_locator->Release();  impl_->wbem_locator  = nullptr; }
    delete impl_;
    impl_ = nullptr;
}
