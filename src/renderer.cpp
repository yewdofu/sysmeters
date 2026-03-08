// vim: set ft=cpp fenc=utf-8 ff=unix sw=4 ts=4 et :
#include "renderer.hpp"
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#include <cmath>
#include <cstdio>
#include <cwchar>

// ウィンドウレイアウト定数（クライアント領域内）
static constexpr float PAD        = 8.f;    // 内側パディング
static constexpr float SECTION_H  = 24.f;   // セクションラベル高さ（18pt 対応）
static constexpr float GRAPH_H    = 72.f;   // 面グラフ高さ（60 * 1.2）
static constexpr float BAR_H      = 16.f;   // 横バー高さ（20 * 0.8）
static constexpr float CORE_BAR_H = 40.f;   // コア縦バー高さ
static constexpr float LINE_H     = 30.f;   // 1行テキスト高さ（22pt 対応）
static constexpr float GAP        = 6.f;    // 要素間ギャップ
static constexpr float SECTION_GAP = 2.f;  // セクション間の追加スペース
static constexpr float TOTAL_W    = 50.f;   // RAM/VRAM 総量テキスト幅（"64GB" 相当）

// リングバッファの最大値を返す
static auto buf_max = [](const RingBuffer<float, 60>& b) {
    float m = 0.f;
    for (std::size_t i = 0; i < b.size(); ++i) m = max(m, b.at(i));
    return m;
};

// 0xRRGGBB → D2D1_COLOR_F 変換
static D2D1_COLOR_F from_rgb(uint32_t rgb, float alpha = 1.f) {
    return D2D1::ColorF(
        ((rgb >> 16) & 0xFF) / 255.f,
        ((rgb >>  8) & 0xFF) / 255.f,
        ( rgb        & 0xFF) / 255.f,
        alpha);
}

uint32_t Renderer::temp_color(float c) {
    if (c >= 90.f) return 0xEF5350;   // 赤
    if (c >= 70.f) return 0xFFA726;   // オレンジ
    return 0x66BB6A;                   // 緑
}

bool Renderer::init(HWND hwnd, const AppConfig& cfg) {
    hwnd_ = hwnd;

    // D2D ファクトリ作成
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_))) return false;

    // DirectWrite ファクトリ作成
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&dwrite_factory_)))) return false;

    // フォント作成（失敗時は false を返す）
    // CPU/GPU 以外のセクションで使用（要求により 2 倍サイズ）
    if (FAILED(dwrite_factory_->CreateTextFormat(L"Consolas", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        22.f, L"ja-JP", &font_normal_))) return false;

    if (FAILED(dwrite_factory_->CreateTextFormat(L"Consolas", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        18.f, L"ja-JP", &font_small_))) return false;

    if (FAILED(dwrite_factory_->CreateTextFormat(L"Consolas", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        22.f, L"ja-JP", &font_large_))) return false;

    create_device_resources(cfg);
    return true;
}

void Renderer::create_device_resources(const AppConfig& cfg) {
    if (!d2d_factory_ || render_target_) return;

    RECT rc;
    GetClientRect(hwnd_, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props = D2D1::HwndRenderTargetProperties(hwnd_, size);

    d2d_factory_->CreateHwndRenderTarget(props, hwnd_props, &render_target_);
    if (!render_target_) return;

    render_target_->CreateSolidColorBrush(from_rgb(cfg.col_text),       &brush_text_);
    render_target_->CreateSolidColorBrush(from_rgb(cfg.col_graph_fill), &brush_fill_);
}

void Renderer::release_device_resources() {
    auto safe_release = [](auto** p) { if (*p) { (*p)->Release(); *p = nullptr; } };
    safe_release(&brush_text_);
    safe_release(&brush_fill_);
    safe_release(&render_target_);
}

void Renderer::resize(UINT w, UINT h) {
    if (render_target_) render_target_->Resize(D2D1::SizeU(w, h));
}

void Renderer::shutdown() {
    release_device_resources();
    auto safe_release = [](auto** p) { if (*p) { (*p)->Release(); *p = nullptr; } };
    safe_release(&font_normal_);
    safe_release(&font_small_);
    safe_release(&font_large_);
    safe_release(&dwrite_factory_);
    safe_release(&d2d_factory_);
}

// 指定色でブラシを使い回す（毎回 SetColor する）
static void set_brush_color(ID2D1SolidColorBrush* b, uint32_t rgb, float alpha = 1.f) {
    b->SetColor(from_rgb(rgb, alpha));
}

// セクションラベル描画（例："CPU"）
void Renderer::draw_section_label(float x, float y, const wchar_t* label, const AppConfig& cfg) {
    set_brush_color(brush_text_, cfg.col_text, 0.6f);
    D2D1_RECT_F r = D2D1::RectF(x, y, x + 400.f, y + SECTION_H);
    render_target_->DrawText(label, static_cast<UINT32>(wcslen(label)),
                             font_small_, r, brush_text_);
}

// グラフ領域にグリッド線を描画する（10 秒間隔の縦線 6 本 + 25% 間隔の横線 4 本）
void Renderer::draw_grid(D2D1_RECT_F rect) {
    float w = rect.right - rect.left;
    float h = rect.bottom - rect.top;

    set_brush_color(brush_fill_, 0x3A3A3A, 0.8f);

    // 縦線（10 秒間隔）
    for (int i = 1; i <= 5; ++i) {
        float x = rect.left + w * (i * 10.f / 60.f);
        render_target_->DrawLine(
            D2D1::Point2F(x, rect.top), D2D1::Point2F(x, rect.bottom),
            brush_fill_, 0.5f);
    }
    // 横線（25% 間隔）
    for (int i = 1; i <= 3; ++i) {
        float y = rect.top + h * (i / 4.f);
        render_target_->DrawLine(
            D2D1::Point2F(rect.left, y), D2D1::Point2F(rect.right, y),
            brush_fill_, 0.5f);
    }
}

// 面グラフ描画
void Renderer::draw_area_graph(const RingBuffer<float, 60>& buf,
                               float max_val, D2D1_RECT_F rect, uint32_t color_rgb) {
    if (max_val <= 0.f) return;

    // グラフ背景（ほぼ黒）
    set_brush_color(brush_fill_, 0x0D0D0D);
    render_target_->FillRectangle(rect, brush_fill_);

    // グリッド線
    draw_grid(rect);

    if (buf.empty()) return;

    float w = rect.right - rect.left;
    float h = rect.bottom - rect.top;
    float dx = w / 60.f;

    ID2D1PathGeometry* path = nullptr;
    if (FAILED(d2d_factory_->CreatePathGeometry(&path))) return;

    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(path->Open(&sink))) { path->Release(); return; }

    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    sink->BeginFigure(D2D1::Point2F(rect.left, rect.bottom), D2D1_FIGURE_BEGIN_FILLED);

    std::size_t sz = buf.size();
    std::size_t start = 60 - sz;
    for (std::size_t i = 0; i < 60; ++i) {
        float v = (i >= start) ? buf.at(i - start) : 0.f;
        v = min(v, max_val);
        float px = rect.left + static_cast<float>(i) * dx;
        float py = rect.bottom - (v / max_val) * h;
        sink->AddLine(D2D1::Point2F(px, py));
    }

    sink->AddLine(D2D1::Point2F(rect.right, rect.bottom));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) {
        sink->Release();
        path->Release();
        return;
    }
    sink->Release();

    // 塗りつぶし（半透明）
    set_brush_color(brush_fill_, color_rgb, 0.3f);
    render_target_->FillGeometry(path, brush_fill_);
    // 輪郭線（不透明）
    set_brush_color(brush_fill_, color_rgb, 1.f);
    render_target_->DrawGeometry(path, brush_fill_, 1.5f);
    path->Release();
}

// 横バー描画（背景 + 塗り）
void Renderer::draw_hbar(float val, float max_val, D2D1_RECT_F rect, uint32_t color_rgb) {
    // 背景
    set_brush_color(brush_fill_, 0x2A2A2A);
    render_target_->FillRectangle(rect, brush_fill_);

    // 塗り
    float fill_w = (max_val > 0.f) ? ((val / max_val) * (rect.right - rect.left)) : 0.f;
    fill_w = min(fill_w, rect.right - rect.left);
    if (fill_w > 0.f) {
        D2D1_RECT_F filled = D2D1::RectF(rect.left, rect.top, rect.left + fill_w, rect.bottom);
        set_brush_color(brush_fill_, color_rgb);
        render_target_->FillRectangle(filled, brush_fill_);
    }
}

// 縦バー描画（0-100% の高さ）
void Renderer::draw_vbar(float pct, D2D1_RECT_F rect, uint32_t color_rgb) {
    // 背景
    set_brush_color(brush_fill_, 0x2A2A2A);
    render_target_->FillRectangle(rect, brush_fill_);

    float h = rect.bottom - rect.top;
    float fill_h = min(pct / 100.f, 1.f) * h;
    if (fill_h > 0.f) {
        D2D1_RECT_F filled = D2D1::RectF(rect.left, rect.bottom - fill_h, rect.right, rect.bottom);
        set_brush_color(brush_fill_, color_rgb);
        render_target_->FillRectangle(filled, brush_fill_);
    }
}

// ---- 各セクション描画 ----

float Renderer::draw_cpu(const CpuMetrics& m, const AppConfig& cfg, float y) {
    float x  = PAD;
    float ww = static_cast<float>(cfg.win_width) - PAD * 2;

    // 全体使用率 面グラフ（全幅）+ オーバーレイテキスト
    wchar_t buf[32];
    D2D1_RECT_F gr = D2D1::RectF(x, y, x + ww, y + GRAPH_H);
    draw_area_graph(m.total_history, 100.f, gr, cfg.col_graph_fill);

    // パーセンテージ（左寄せ、95% 超で赤）
    swprintf_s(buf, L"CPU  %4.1f%%", m.total_pct);
    uint32_t cpu_text_col = (m.total_pct > 95.f) ? 0xEF5350 : cfg.col_text;
    set_brush_color(brush_text_, cpu_text_col, 0.9f);
    D2D1_RECT_F ol = D2D1::RectF(x + 4.f, y + 4.f, x + ww - 4.f, y + GRAPH_H - 4.f);
    render_target_->DrawText(buf, static_cast<UINT32>(wcslen(buf)), font_large_, ol, brush_text_);

    // 温度（右寄せ、3 段階色。取得不可時は "--℃"）
    wchar_t tbuf[16];
    if (m.temp_avail) {
        swprintf_s(tbuf, L"%3.0f\u2103", m.temp_celsius);
        set_brush_color(brush_text_, temp_color(m.temp_celsius), 0.9f);
    }
    else {
        swprintf_s(tbuf, L"--\u2103");
        set_brush_color(brush_text_, 0x888888, 0.9f);
    }
    font_large_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    render_target_->DrawText(tbuf, static_cast<UINT32>(wcslen(tbuf)), font_large_, ol, brush_text_);
    font_large_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    y += GRAPH_H + GAP;

    // コア別縦バー（論理コア数本横並び、画面幅に合わせて動的計算）
    {
        const int   N_CORES = static_cast<int>(std::size(m.core_pct));
        constexpr float GAP_BAR = 2.f;  // バー間ギャップ
        float bar_w = (ww - GAP_BAR * (N_CORES - 1)) / N_CORES;
        float core_x = x;
        for (int i = 0; i < N_CORES; ++i) {
            D2D1_RECT_F cr = D2D1::RectF(core_x, y, core_x + bar_w, y + CORE_BAR_H);
            uint32_t core_col = (m.core_pct[i] > 95.f) ? 0xEF5350 : cfg.col_cpu_core;
            draw_vbar(m.core_pct[i], cr, core_col);
            core_x += bar_w + GAP_BAR;
        }
    }
    y += CORE_BAR_H + GAP;

    return y;
}

float Renderer::draw_gpu(const GpuMetrics& m, const AppConfig& cfg, float y) {
    float x  = PAD;
    float ww = static_cast<float>(cfg.win_width) - PAD * 2;

    if (!m.avail) {
        draw_section_label(x, y, L"GPU", cfg);
        y += SECTION_H;
        set_brush_color(brush_text_, 0x888888);
        D2D1_RECT_F r = D2D1::RectF(x, y, x + ww, y + LINE_H);
        render_target_->DrawText(L"N/A", 3, font_normal_, r, brush_text_);
        return y + LINE_H + GAP;
    }

    // 使用率 面グラフ（全幅）+ オーバーレイテキスト
    wchar_t buf[32];
    D2D1_RECT_F gr = D2D1::RectF(x, y, x + ww, y + GRAPH_H);
    draw_area_graph(m.usage_history, 100.f, gr, cfg.col_graph_fill);

    // パーセンテージ（左寄せ、95% 超で赤）
    swprintf_s(buf, L"GPU  %4.1f%%", m.usage_pct);
    uint32_t gpu_text_col = (m.usage_pct > 95.f) ? 0xEF5350 : cfg.col_text;
    set_brush_color(brush_text_, gpu_text_col, 0.9f);
    D2D1_RECT_F ol = D2D1::RectF(x + 4.f, y + 4.f, x + ww - 4.f, y + GRAPH_H - 4.f);
    render_target_->DrawText(buf, static_cast<UINT32>(wcslen(buf)), font_large_, ol, brush_text_);

    // 温度（右寄せ、3 段階色）
    wchar_t tbuf[16];
    swprintf_s(tbuf, L"%3.0f\u2103", m.temp_celsius);
    set_brush_color(brush_text_, temp_color(m.temp_celsius), 0.9f);
    font_large_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    render_target_->DrawText(tbuf, static_cast<UINT32>(wcslen(tbuf)), font_large_, ol, brush_text_);
    font_large_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    y += GRAPH_H + GAP;

    return y;
}

float Renderer::draw_mem(const MemMetrics& m, const AppConfig& cfg, float y) {
    float x  = PAD;
    float ww = static_cast<float>(cfg.win_width) - PAD * 2;

    // RAM テキスト（90% 超で赤、WSL 使用中なら WSL 量を追記）
    // "RAM   " = 6 文字幅、"VRAM  " = 6 文字幅 → パーセンテージの縦位置を揃える
    wchar_t buf[64];
    if (m.wsl_gb > 0.f)
        swprintf_s(buf, L"RAM   %5.1f%%  %5.1fGB  WSL %4.1fGB", m.usage_pct, m.used_gb, m.wsl_gb);
    else
        swprintf_s(buf, L"RAM   %5.1f%%  %5.1fGB", m.usage_pct, m.used_gb);
    uint32_t ram_col = (m.usage_pct > 90.f) ? 0xEF5350 : cfg.col_text;
    set_brush_color(brush_text_, ram_col);
    D2D1_RECT_F tr = D2D1::RectF(x, y, x + ww, y + LINE_H);
    render_target_->DrawText(buf, static_cast<UINT32>(wcslen(buf)), font_normal_, tr, brush_text_);
    y += LINE_H + 2.f;

    // バー右側に総量テキスト（font_small_ で右寄せ）
    D2D1_RECT_F br = D2D1::RectF(x, y, x + ww - TOTAL_W - 4.f, y + BAR_H);
    uint32_t bar_col = (m.usage_pct > 90.f) ? 0xEF5350 : cfg.col_graph_fill;

    if (m.wsl_gb > 0.f && m.total_gb > 0.f) {
        // 2 色バー：左端から WSL 分を青、残りの使用分をアンバーで描画
        float bar_w      = br.right - br.left;
        float total_fill = min(m.usage_pct / 100.f, 1.f) * bar_w;
        // wsl_fill は total_fill を超えないようクランプ（計算基準の違いによる視覚矛盾を防止）
        float wsl_fill   = min(min(m.wsl_gb / m.total_gb, 1.f) * bar_w, total_fill);
        // 90% 超の警告時は WSL バーも赤にして警告を隠さない
        uint32_t wsl_col = (m.usage_pct > 90.f) ? 0xEF5350 : 0x42A5F5;

        set_brush_color(brush_fill_, 0x2A2A2A);
        render_target_->FillRectangle(br, brush_fill_);

        if (total_fill > 0.f) {
            D2D1_RECT_F r = D2D1::RectF(br.left, br.top, br.left + total_fill, br.bottom);
            set_brush_color(brush_fill_, bar_col);
            render_target_->FillRectangle(r, brush_fill_);
        }
        if (wsl_fill > 0.f) {
            D2D1_RECT_F r = D2D1::RectF(br.left, br.top, br.left + wsl_fill, br.bottom);
            set_brush_color(brush_fill_, wsl_col);
            render_target_->FillRectangle(r, brush_fill_);
        }
    }
    else {
        draw_hbar(m.usage_pct, 100.f, br, bar_col);
    }

    // 総量テキストをバー右側に表示（棒グラフと縦位置を合わせるため上にシフト）
    wchar_t totbuf[16];
    swprintf_s(totbuf, L"%2.0fGB", m.total_gb);
    set_brush_color(brush_text_, cfg.col_text);
    D2D1_RECT_F tr2 = D2D1::RectF(x + ww - TOTAL_W, y - 5.f, x + ww, y + BAR_H);
    font_small_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    render_target_->DrawText(totbuf, static_cast<UINT32>(wcslen(totbuf)), font_small_, tr2, brush_text_);
    font_small_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    y += BAR_H + GAP;

    return y;
}

float Renderer::draw_vram(const VramMetrics& m, const AppConfig& cfg, float y) {
    float x  = PAD;
    float ww = static_cast<float>(cfg.win_width) - PAD * 2;

    if (!m.avail) {
        set_brush_color(brush_text_, 0x888888);
        D2D1_RECT_F r = D2D1::RectF(x, y, x + ww, y + LINE_H);
        render_target_->DrawText(L"VRAM  N/A", 9, font_normal_, r, brush_text_);
        return y + LINE_H + GAP;
    }

    // VRAM テキスト（90% 超で赤）
    // "VRAM  " = 6 文字幅 → RAM の "RAM   " と揃える
    wchar_t buf[64];
    swprintf_s(buf, L"VRAM  %5.1f%%  %5.1fGB", m.usage_pct, m.used_gb);
    uint32_t vram_col = (m.usage_pct > 90.f) ? 0xEF5350 : cfg.col_text;
    set_brush_color(brush_text_, vram_col);
    D2D1_RECT_F tr = D2D1::RectF(x, y, x + ww, y + LINE_H);
    render_target_->DrawText(buf, static_cast<UINT32>(wcslen(buf)), font_normal_, tr, brush_text_);
    y += LINE_H + 2.f;

    D2D1_RECT_F br = D2D1::RectF(x, y, x + ww - TOTAL_W - 4.f, y + BAR_H);
    draw_hbar(m.usage_pct, 100.f, br, (m.usage_pct > 90.f) ? 0xEF5350 : cfg.col_graph_fill);

    // 総量テキストをバー右側に表示（棒グラフと縦位置を合わせるため上にシフト）
    wchar_t totbuf[16];
    swprintf_s(totbuf, L"%2.0fGB", m.total_gb);
    set_brush_color(brush_text_, cfg.col_text);
    D2D1_RECT_F tr2 = D2D1::RectF(x + ww - TOTAL_W, y - 5.f, x + ww, y + BAR_H);
    font_small_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    render_target_->DrawText(totbuf, static_cast<UINT32>(wcslen(totbuf)), font_small_, tr2, brush_text_);
    font_small_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    y += BAR_H + GAP;

    return y;
}

float Renderer::draw_disk(const DiskMetrics& c, const DiskMetrics& d, const AppConfig& cfg, float y) {
    float x  = PAD;
    float ww = static_cast<float>(cfg.win_width) - PAD * 2;
    static constexpr float DISK_GAP = 20.f;  // ドライブ間ギャップ
    float hw = (ww - DISK_GAP) / 2.f;

    draw_section_label(x, y, L"Disk I/O", cfg);
    y += SECTION_H;

    auto draw_drive = [&](const DiskMetrics& dm, float bx) {
        // R/W を同一行に表示
        wchar_t buf[64];
        swprintf_s(buf, L"%c: R %.1f  W %.1f MB/s", dm.drive, dm.read_mbps, dm.write_mbps);
        set_brush_color(brush_text_, cfg.col_text);
        D2D1_RECT_F tr = D2D1::RectF(bx, y, bx + hw, y + LINE_H);
        render_target_->DrawText(buf, static_cast<UINT32>(wcslen(buf)), font_small_, tr, brush_text_);
        D2D1_RECT_F gr = D2D1::RectF(bx, y + LINE_H, bx + hw, y + LINE_H + GRAPH_H);

        // Read/Write の最大値でスケーリング（動的スケール）
        float max_val = max(1.f, max(buf_max(dm.read_history), buf_max(dm.write_history)));

        draw_area_graph(dm.read_history,  max_val, gr, cfg.col_disk_read);
        draw_area_graph(dm.write_history, max_val, gr, cfg.col_disk_write);
    };

    draw_drive(c, x);
    draw_drive(d, x + hw + DISK_GAP);

    y += LINE_H + GRAPH_H + GAP;
    return y;
}

float Renderer::draw_net(const NetMetrics& m, const AppConfig& cfg, float y) {
    float x  = PAD;
    float ww = static_cast<float>(cfg.win_width) - PAD * 2;

    draw_section_label(x, y, L"Network", cfg);
    y += SECTION_H;

    // 3 桁左スペースパディングで表示のガタつきを防ぐ
    auto fmt_kbps = [](float kbps, wchar_t* buf, int len) {
        if (kbps >= 1024.f) swprintf_s(buf, len, L"%5.1f MB/s", kbps / 1024.f);
        else                swprintf_s(buf, len, L"%3.0f KB/s", kbps);
    };

    wchar_t buf[32];
    fmt_kbps(m.recv_kbps, buf, 32);
    wchar_t labelbuf[48];
    swprintf_s(labelbuf, L"▼ %s", buf);
    set_brush_color(brush_text_, cfg.col_text);
    D2D1_RECT_F tr = D2D1::RectF(x, y, x + 120.f, y + LINE_H);
    render_target_->DrawText(labelbuf, static_cast<UINT32>(wcslen(labelbuf)), font_small_, tr, brush_text_);

    float max_val = max(1.f, max(buf_max(m.send_history), buf_max(m.recv_history)));

    D2D1_RECT_F gr = D2D1::RectF(x, y + LINE_H, x + ww, y + LINE_H + GRAPH_H);
    draw_area_graph(m.recv_history, max_val, gr, cfg.col_net_recv);
    draw_area_graph(m.send_history, max_val, gr, cfg.col_net_send);

    fmt_kbps(m.send_kbps, buf, 32);
    swprintf_s(labelbuf, L"▲ %s", buf);
    D2D1_RECT_F sr = D2D1::RectF(x + 130.f, y, x + ww, y + LINE_H);
    render_target_->DrawText(labelbuf, static_cast<UINT32>(wcslen(labelbuf)), font_small_, sr, brush_text_);

    y += LINE_H + GRAPH_H + GAP;
    return y;
}

float Renderer::draw_claude(const ClaudeMetrics& m, const AppConfig& cfg, float y) {
    float x  = PAD;
    float ww = static_cast<float>(cfg.win_width) - PAD * 2;

    // ヘッダ行：「Claude」は font_normal_（RAM と同サイズ）、残りは font_small_（Disk I/O と同サイズ）
    static constexpr float CLAUDE_LBL_W = 90.f;  // "Claude" の描画幅
    set_brush_color(brush_text_, cfg.col_text);
    D2D1_RECT_F hlr = D2D1::RectF(x, y, x + CLAUDE_LBL_W, y + LINE_H);
    render_target_->DrawText(L"Claude", 6, font_normal_, hlr, brush_text_);

    wchar_t hdr[48];
    swprintf_s(hdr, L"%.15hs  Sessions: %d", m.plan_label, m.session_count);
    D2D1_RECT_F hsr = D2D1::RectF(x + CLAUDE_LBL_W, y, x + ww, y + LINE_H);
    render_target_->DrawText(hdr, static_cast<UINT32>(wcslen(hdr)), font_small_, hsr, brush_text_);
    y += LINE_H;

    // 5h / 7d バー：ラベル+パーセンテージ（左）、バー（中）、リセット時刻（右）の同一行レイアウト
    // テキストは Disk I/O と同じ font_small_（18pt）
    static constexpr float LBL_W   = 90.f;   // "5h 100.0%" が収まる幅（font_small_）
    static constexpr float RESET_W = 110.f;  // リセット時刻テキスト幅
    auto draw_bar = [&](const wchar_t* lbl, float pct, const char* reset, bool avail) {
        // ラベル + パーセンテージ（左寄せ、font_small_ = Disk I/O と同サイズ）
        wchar_t buf[64];
        if (avail) swprintf_s(buf, L"%s %5.1f%%", lbl, pct);
        else       swprintf_s(buf, L"%s   N/A",   lbl);
        set_brush_color(brush_text_, avail ? cfg.col_text : 0x888888);
        D2D1_RECT_F lr = D2D1::RectF(x, y, x + LBL_W, y + SECTION_H);
        render_target_->DrawText(buf, static_cast<UINT32>(wcslen(buf)), font_small_, lr, brush_text_);

        // バー（ラベル右端からリセット時刻左端まで）
        float bar_right = avail ? (x + ww - RESET_W - 4.f) : (x + ww);
        float bar_top   = y + (SECTION_H - BAR_H) / 2.f;  // 行内で縦中央揃え
        D2D1_RECT_F br  = D2D1::RectF(x + LBL_W + 4.f, bar_top, bar_right, bar_top + BAR_H);
        draw_hbar(avail ? pct : 0.f, 100.f, br, cfg.col_claude_bar);

        // リセット時刻（右端、未取得時は非表示、font_small_）
        if (avail) {
            wchar_t rtbuf[40];
            swprintf_s(rtbuf, L"%.38hs", reset);
            set_brush_color(brush_text_, cfg.col_text, 0.6f);
            D2D1_RECT_F rr = D2D1::RectF(x + ww - RESET_W, y, x + ww, y + SECTION_H);
            font_small_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            render_target_->DrawText(rtbuf, static_cast<UINT32>(wcslen(rtbuf)), font_small_, rr, brush_text_);
            font_small_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
        y += SECTION_H + GAP;
    };

    draw_bar(L"5h", m.five_h_pct,  m.five_h_reset,  m.avail);
    draw_bar(L"7d", m.seven_d_pct, m.seven_d_reset, m.avail);

    return y;
}

// ---- メイン描画 ----

void Renderer::paint(const AllMetrics& m, const AppConfig& cfg) {
    if (!render_target_) create_device_resources(cfg);
    if (!render_target_) return;

    render_target_->BeginDraw();
    render_target_->Clear(from_rgb(cfg.col_background));

    float y = PAD;
    y = draw_cpu(m.cpu, cfg, y);        y += SECTION_GAP;
    y = draw_gpu(m.gpu, cfg, y);        y += SECTION_GAP;
    y = draw_mem(m.mem, cfg, y);        y += SECTION_GAP;
    y = draw_vram(m.vram, cfg, y);      y += SECTION_GAP;
    y = draw_disk(m.disk_c, m.disk_d, cfg, y); y += SECTION_GAP;
    y = draw_net(m.net, cfg, y);        y += SECTION_GAP;
    y = draw_claude(m.claude, cfg, y);

    preferred_h_ = static_cast<int>(y + PAD);

    HRESULT hr = render_target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        release_device_resources();
    }
}
