#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace nola_title
{
    inline constexpr const wchar_t* kDir = L"assets";
    inline constexpr const wchar_t* kFile = L"nola_title.png";
    inline constexpr int kMinSide = 16;
    inline constexpr int kMaxSide = 4096;
    inline constexpr size_t kMaxBytes = size_t(32) << 20;
    inline constexpr int kDefGap = 14;
    inline constexpr int kMaxGap = 200;
    inline constexpr double kMinBadge = 16;
    inline constexpr double kMaxCanvas = 8192;

    enum class Why : uint8_t { ok = 0, off, missing, unreadable, empty, too_big, not_png, bad_header, bad_size, no_texture, no_widget };

    inline const char* why_text(Why w)
    {
        switch (w)
        {
        case Why::ok: return "ok";
        case Why::off: return "nolatitleimg=0";
        case Why::missing: return "file not found";
        case Why::unreadable: return "file not readable";
        case Why::empty: return "file is empty";
        case Why::too_big: return "file is over 32 MB";
        case Why::not_png: return "not a PNG file";
        case Why::bad_header: return "PNG has no IHDR header";
        case Why::bad_size: return "PNG size outside 16..4096 px";
        case Why::no_texture: return "KismetRenderingLibrary::ImportFileAsTexture2D gave no Texture2D";
        case Why::no_widget: return "UMG Image widget not built";
        }
        return "?";
    }

    inline bool is_sep(wchar_t c) { return c == L'\\' || c == L'/'; }

    inline bool ieq(std::wstring_view a, std::wstring_view b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            wchar_t x = a[i] >= L'A' && a[i] <= L'Z' ? wchar_t(a[i] + 32) : a[i];
            wchar_t y = b[i] >= L'A' && b[i] <= L'Z' ? wchar_t(b[i] + 32) : b[i];
            if (x != y) return false;
        }
        return true;
    }

    inline std::wstring asset_path(std::wstring_view dll)
    {
        size_t s = dll.size();
        while (s > 0 && !is_sep(dll[s - 1])) --s;
        if (s == 0) return {};
        std::wstring_view dir = dll.substr(0, s - 1);
        size_t p = dir.size();
        while (p > 0 && !is_sep(dir[p - 1])) --p;
        if (p > 0 && ieq(dir.substr(p), L"dlls")) dir = dir.substr(0, p - 1);
        if (dir.empty()) return {};
        return std::wstring(dir) + L"\\" + kDir + L"\\" + kFile;
    }

    inline uint32_t be32(const unsigned char* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); }

    inline Why png_check(std::string_view b, int& w, int& h)
    {
        static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
        w = h = 0;
        if (b.empty()) return Why::empty;
        if (b.size() > kMaxBytes) return Why::too_big;
        const auto* u = reinterpret_cast<const unsigned char*>(b.data());
        if (b.size() < 8) return Why::not_png;
        for (int i = 0; i < 8; ++i)
            if (u[i] != sig[i]) return Why::not_png;
        if (b.size() < 33 || be32(u + 8) != 13 || u[12] != 'I' || u[13] != 'H' || u[14] != 'D' || u[15] != 'R') return Why::bad_header;
        uint32_t pw = be32(u + 16), ph = be32(u + 20);
        if (pw < (uint32_t)kMinSide || ph < (uint32_t)kMinSide || pw > (uint32_t)kMaxSide || ph > (uint32_t)kMaxSide) return Why::bad_size;
        w = (int)pw;
        h = (int)ph;
        return Why::ok;
    }

    struct Box
    {
        double x = 0, y = 0, w = 0, h = 0;
    };

    inline constexpr int kArtW = 1567;
    inline constexpr int kArtH = 342;
    inline constexpr double kArtLeft = 26;
    inline constexpr double kArtTop = 37;
    inline constexpr double kArtSubtitleBottom = 327;
    inline constexpr double kAnchorArtTop = kArtTop;
    inline constexpr double kAnchorArtBottom = kArtSubtitleBottom;
    inline constexpr double kAnchorBadgeTopF = 0.0;
    inline constexpr double kAnchorBadgeBottomF = 1.0;
    inline constexpr double kOpacity = 0.85;

    inline constexpr Box kStockBadge{104.7, 893.7, 135, 52.6};

    inline int clamp_gap(int64_t gap) { return gap < 0 ? 0 : gap > kMaxGap ? kMaxGap : (int)gap; }

    inline bool sane(const Box& b)
    {
        auto ok = [](double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; };
        return ok(b.w, kMinBadge, kMaxCanvas) && ok(b.h, kMinBadge, kMaxCanvas) && ok(b.x, -kMaxCanvas, kMaxCanvas) && ok(b.y, -kMaxCanvas, kMaxCanvas);
    }

    inline Box unite(const Box& a, const Box& b)
    {
        double l = a.x < b.x ? a.x : b.x, t = a.y < b.y ? a.y : b.y;
        double r = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w, d = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
        return {l, t, r - l, d - t};
    }

    inline Box corner(const Box& badge, int64_t gap, int tw, int th)
    {
        Box r;
        if (tw <= 0 || th <= 0 || !sane(badge)) return r;
        double unit = badge.h * (kAnchorBadgeBottomF - kAnchorBadgeTopF) / (kAnchorArtBottom - kAnchorArtTop);
        r.h = unit * kArtH;
        r.w = r.h * tw / th;
        r.x = badge.x + badge.w + clamp_gap(gap) - kArtLeft / kArtW * r.w;
        r.y = badge.y + kAnchorBadgeTopF * badge.h - kArtTop / kArtH * r.h;
        return r;
    }

    inline bool moved(const Box& a, const Box& b, double tol = 0.5)
    {
        return std::fabs(a.x - b.x) > tol || std::fabs(a.y - b.y) > tol || std::fabs(a.w - b.w) > tol || std::fabs(a.h - b.h) > tol;
    }

    inline Why decide(int64_t flag, bool exists, bool readable, Why png)
    {
        if (flag != 1) return Why::off;
        if (!exists) return Why::missing;
        if (!readable) return Why::unreadable;
        return png;
    }
}
