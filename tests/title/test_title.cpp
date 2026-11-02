#include "s2m_title_logic.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

static int fails = 0;
static void check(bool ok, const char* what)
{
    if (!ok)
    {
        ++fails;
        std::printf("FAIL %s\n", what);
    }
}

static std::string png(uint32_t w, uint32_t h)
{
    std::string b = "\x89PNG\r\n\x1a\n";
    auto be = [&](uint32_t v) {
        b += (char)(v >> 24);
        b += (char)(v >> 16);
        b += (char)(v >> 8);
        b += (char)v;
    };
    be(13);
    b += "IHDR";
    be(w);
    be(h);
    b += std::string("\x08\x06\x00\x00\x00", 5);
    be(0);
    return b;
}

static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

int main(int argc, char** argv)
{
    using nola_title::Why;
    using nola_title::asset_path;
    using nola_title::png_check;

    check(asset_path(L"C:\\G\\Stalker2\\Binaries\\Win64\\ue4ss\\Mods\\NolaNative\\dlls\\main.dll") == L"C:\\G\\Stalker2\\Binaries\\Win64\\ue4ss\\Mods\\NolaNative\\assets\\nola_title.png",
          "path: Mods\\NolaNative\\dlls\\main.dll -> Mods\\NolaNative\\assets\\nola_title.png");
    check(asset_path(L"C:\\G\\Mods\\NolaNative\\DLLS\\main.dll") == L"C:\\G\\Mods\\NolaNative\\assets\\nola_title.png", "path: dlls folder name is case-insensitive");
    check(asset_path(L"C:/G/Mods/NolaNative/dlls/main.dll") == L"C:/G/Mods/NolaNative\\assets\\nola_title.png", "path: forward slashes");
    check(asset_path(L"D:\\build\\NolaNative.dll") == L"D:\\build\\assets\\nola_title.png", "path: DLL outside a dlls folder -> assets next to the DLL");
    check(asset_path(L"main.dll").empty() && asset_path(L"").empty(), "path: no folder -> empty");
    check(asset_path(L"C:\\x\\dlls\\main.dll") == L"C:\\x\\assets\\nola_title.png", "path: short tree");
    check(std::wstring(nola_title::kDir) == L"assets" && std::wstring(nola_title::kFile) == L"nola_title.png", "path: constant name assets\\nola_title.png");

    int w = 0, h = 0;
    check(png_check(png(1600, 600), w, h) == Why::ok && w == 1600 && h == 600, "png: 1600x600 header ok");
    check(png_check("", w, h) == Why::empty && w == 0 && h == 0, "png: empty file");
    check(png_check("GIF89a....................................", w, h) == Why::not_png, "png: wrong signature");
    check(png_check(std::string("\x89PNG", 4), w, h) == Why::not_png, "png: truncated signature");
    check(png_check(png(1600, 600).substr(0, 20), w, h) == Why::bad_header, "png: truncated IHDR");
    std::string bad = png(1600, 600);
    bad[12] = 'X';
    check(png_check(bad, w, h) == Why::bad_header, "png: first chunk not IHDR");
    check(png_check(png(8, 600), w, h) == Why::bad_size, "png: too narrow");
    check(png_check(png(5000, 600), w, h) == Why::bad_size, "png: too wide");
    check(png_check(png(16, 4096), w, h) == Why::ok, "png: limits inclusive");
    std::string big = png(1600, 600);
    big.resize(nola_title::kMaxBytes + 1);
    check(png_check(big, w, h) == Why::too_big, "png: over 32 MB");

    using nola_title::Box;
    using nola_title::corner;
    const Box B = nola_title::kStockBadge;
    check(near(B.x + B.w, 239.7, 0.01) && near(B.y, 893.7, 0.01) && near(B.h, 52.6, 0.01), "mockup badge box: right edge 239.7, top 893.7, height 52.6 design units");
    auto a = corner(B, nola_title::kDefGap, 1567, 342);
    check(near(a.w, 285, 2) && near(a.h, 62, 0.5), "corner: 1567x342 art at the mockup badge = 286x62 units");
    check(near(a.x, 249, 1.5) && near(a.y, 887, 0.5), "corner: mockup position 249,887");
    check(near(a.y + 37.0 / 342 * a.h, B.y, 0.001), "corner: top of the letters = top of the badge");
    check(near(a.y + 327.0 / 342 * a.h, B.y + B.h, 0.001), "corner: bottom of the subtitle = bottom of the badge");
    check(near(a.x + 26.0 / 1567 * a.w, B.x + B.w + 14, 0.001), "corner: first art column sits 14 units right of the badge");
    auto s = corner(Box{100 * 0.7111, 893.7 * 0.7111, 135 * 0.7111, 52.6 * 0.7111}, 14, 1567, 342);
    check(near(s.h, 62.1 * 0.7111, 0.2) && near(s.y + 37.0 / 342 * s.h, 893.7 * 0.7111, 0.001), "corner: scales with a smaller measured badge (1366x768 geometry)");
    auto g0 = corner(B, 0, 1567, 342);
    check(near(g0.x + 26.0 / 1567 * g0.w, B.x + B.w, 0.001), "corner: gap 0 touches the badge");
    auto g1 = corner(B, -50, 1567, 342);
    check(near(g1.x, g0.x, 0.001), "corner: negative gap clamped to 0");
    auto g2 = corner(B, 5000, 1567, 342);
    check(near(g2.x + 26.0 / 1567 * g2.w, B.x + B.w + 200, 0.001), "corner: gap clamped to 200");
    auto z = corner(B, 14, 0, 342);
    check(z.w == 0 && z.h == 0, "corner: no texture size -> no box");
    auto u = corner(Box{0, 0, 0, 0}, 14, 1567, 342);
    check(u.w == 0, "corner: unpainted badge geometry -> no box");
    check(!nola_title::sane(Box{0, 0, 0, 0}) && !nola_title::sane(Box{0, 0, 5, 100}) && !nola_title::sane(Box{std::nan(""), 0, 135, 52}) && !nola_title::sane(Box{0, 0, 1e9, 52}),
          "sane: zero, tiny, NaN and huge boxes rejected (geometry not painted yet)");
    check(nola_title::sane(B), "sane: mockup badge box accepted");
    auto un = nola_title::unite(Box{10, 20, 30, 40}, Box{50, 10, 20, 20});
    check(near(un.x, 10, 0.001) && near(un.y, 10, 0.001) && near(un.w, 60, 0.001) && near(un.h, 50, 0.001), "unite: bounding box of two boxes");
    check(near(nola_title::kOpacity, 0.85, 0.0001), "opacity 85%");
    check(!nola_title::moved(a, a) && nola_title::moved(a, g0) && !nola_title::moved(a, Box{a.x + 0.4, a.y, a.w, a.h}), "moved: half-unit tolerance");
    check(nola_title::decide(0, true, true, Why::ok) == Why::off, "fallback: nolatitleimg=0 -> stock menu only");
    check(nola_title::decide(1, false, false, Why::ok) == Why::missing, "fallback: file missing -> stock menu only");
    check(nola_title::decide(1, true, false, Why::ok) == Why::unreadable, "fallback: file not readable -> stock menu only");
    check(nola_title::decide(1, true, true, Why::not_png) == Why::not_png, "fallback: bad PNG -> stock menu only");
    check(nola_title::decide(1, true, true, Why::ok) == Why::ok, "image used when flag on and PNG ok");
    for (int i = 0; i <= (int)Why::no_widget; ++i) check(std::string(nola_title::why_text((Why)i)) != "?", "why_text covers every reason");

    if (argc > 1)
    {
        std::ifstream in(argv[1], std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        Why r = png_check(bytes, w, h);
        std::printf("asset %s: %s %dx%d %zu bytes\n", argv[1], nola_title::why_text(r), w, h, bytes.size());
        check(r == Why::ok, "shipped asset assets\\nola_title.png is a valid PNG");
    }
    std::printf("%s %d fail(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
