#define NOMINMAX
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../../src/s2n_mc.h"
#include "../../src/s2m_cgcmc_logic.h"

namespace ag
{
#include "../../src/s2n_ag.inc"
}

static int g_fail = 0, g_ok = 0;
#define CHECK(c, ...) do { if (c) ++g_ok; else { ++g_fail; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace legacy
{
    struct VRow { double x, y, z; const char* kind; const char* sid; };
    struct VPos { uintptr_t o; double x, y; };
    int vend_pick(const VRow* kVend, size_t n, double natvendr, const std::vector<VPos>& live, double cx, double cy, double R, std::unordered_set<uintptr_t>& out)
    {
        out.clear();
        int rows = 0;
        const double vr2 = natvendr * natvendr, R2 = (R + natvendr) * (R + natvendr);
        for (size_t i = 0; i < n; ++i)
        {
            const VRow& v = kVend[i];
            if ((v.x - cx) * (v.x - cx) + (v.y - cy) * (v.y - cy) > R2) continue;
            ++rows;
            uintptr_t best = 0;
            double bd = vr2;
            for (const VPos& p : live)
            {
                double d = (p.x - v.x) * (p.x - v.x) + (p.y - v.y) * (p.y - v.y);
                if (d < bd) { bd = d; best = p.o; }
            }
            if (best) out.insert(best);
        }
        return rows;
    }

    std::vector<uintptr_t> select(double rx, double ry, const std::vector<s2nmc::Item>& items, double R, double hys, int max, const std::unordered_set<uintptr_t>& sel,
                                  const std::unordered_set<uintptr_t>& dropped)
    {
        struct Cand { uintptr_t o; double d2; bool alive; double rk; int ci; };
        std::vector<Cand> cs;
        const double R2 = R * R;
        const double hy = std::max(1.0, hys), Rh = R * hy, Rh2 = Rh * Rh;
        for (const auto& it : items)
        {
            double d2 = (it.x - rx) * (it.x - rx) + (it.y - ry) * (it.y - ry);
            if (d2 <= (sel.count(it.key) ? Rh2 : R2)) cs.push_back({it.key, d2, true, sel.count(it.key) ? d2 / (hy * hy) : d2, 0});
        }
        std::sort(cs.begin(), cs.end(), [](const Cand& a, const Cand& b) { return a.rk < b.rk; });
        std::vector<uintptr_t> ord;
        for (const auto& c : cs)
        {
            if ((int)ord.size() >= max) break;
            if (dropped.count(c.o)) continue;
            ord.push_back(c.o);
        }
        return ord;
    }

    struct Sel { std::string rec, mm, bb, look; bool ok = true, fixed = false; int uid = 0; int lookSent = 0; };
    struct St { double lookRR = 0; size_t rr = 0; int looks = 0, sent = 0; size_t mBytes = 0; };
    std::string snapshot(std::vector<Sel>& order, St& H, double now, int natlookn, int natlookmax, double natlookp, const std::string& t, const std::string& nu, const std::string& nd)
    {
        std::string nn, nl, mm, bb;
        int n = 0;
        for (Sel& s : order)
        {
            if (!s.ok) continue;
            if (!s.mm.empty()) { if (!mm.empty()) mm += ";"; mm += s.mm; }
            if (!s.bb.empty()) { if (!bb.empty()) bb += ";"; bb += s.bb; }
            if (!nn.empty()) nn += ";";
            nn += s.rec;
            ++n;
            if (!s.fixed && s.lookSent < natlookn && !s.look.empty() && (nl.empty() || (int)(nl.size() + s.look.size()) < natlookmax))
            {
                if (!nl.empty()) nl += ";";
                nl += std::to_string(s.uid) + "=" + s.look;
                ++s.lookSent; ++H.looks;
            }
        }
        if (!order.empty() && now - H.lookRR >= natlookp)
        {
            H.lookRR = now;
            H.rr = (H.rr + 1) % order.size();
            Sel& it = order[H.rr];
            if (!it.fixed && !it.look.empty() && it.lookSent >= natlookn && (nl.empty() || (int)(nl.size() + it.look.size()) < natlookmax))
            {
                if (!nl.empty()) nl += ";";
                nl += std::to_string(it.uid) + "=" + it.look;
                ++H.looks;
            }
        }
        H.sent += n;
        if (n == 0) return nu.empty() ? nd : "#NN:t" + t + ";" + nu + nd;
        if (!nu.empty()) nn += ";" + nu;
        if (!mm.empty()) { nn += ";" + mm; H.mBytes += mm.size(); }
        if (!bb.empty()) nn += ";" + bb;
        std::string out = "#NN:t" + t + ";" + nn;
        if (!nl.empty()) out += "#NL:" + nl;
        out += nd;
        return out;
    }

    struct AgOld { volatile uint32_t tid; volatile uint8_t on; uint8_t pad[3]; volatile LONG hits; };
    ag::AgStubs emit(uint8_t* st, uint8_t* base, AgOld* d)
    {
        uint8_t* Pid = base + 0x9eec140;
        uint8_t* tid = reinterpret_cast<uint8_t*>(const_cast<uint32_t*>(&d->tid));
        uint8_t* on = reinterpret_cast<uint8_t*>(const_cast<uint8_t*>(&d->on));
        uint8_t* hits = reinterpret_cast<uint8_t*>(const_cast<LONG*>(&d->hits));
        ag::AgEmit e{st};
        ag::AgStubs s{};
        s.a = e.p;
        e.b({0x3B, 0x05}); e.rel(Pid);
        e.b({0x0F, 0x84}); e.rel(base + 0xb852e8);
        e.b({0x80, 0x3D}); e.rel(on, 1); e.b({0x00});
        e.b({0x74}); uint8_t* aNo = e.j8();
        e.b({0x3B, 0x05}); e.rel(tid);
        e.b({0x0F, 0x84}); e.rel(base + 0xb852e8);
        e.at(aNo);
        e.b({0xE9}); e.rel(base + 0xb84f05);
        s.b = e.p;
        e.b({0x8B, 0x2D}); e.rel(Pid);
        e.b({0x66, 0x45, 0x0F, 0x57, 0xFF});
        e.b({0x39, 0xEF});
        e.b({0x0F, 0x84}); e.rel(base + 0xb850e0);
        e.b({0x80, 0x3D}); e.rel(on, 1); e.b({0x00});
        e.b({0x74}); uint8_t* bNp = e.j8();
        e.b({0x3B, 0x3D}); e.rel(tid);
        e.b({0x75}); uint8_t* bNp2 = e.j8();
        e.b({0xF0, 0xFF, 0x05}); e.rel(hits);
        e.b({0xE9}); e.rel(base + 0xb850e0);
        e.at(bNp); e.at(bNp2);
        e.b({0x66, 0x45, 0x0F, 0x28, 0xF8});
        e.b({0xE9}); e.rel(base + 0xb850e0);
        s.c = e.p;
        e.b({0x31, 0xC0, 0x39, 0xEF});
        e.b({0x74}); uint8_t* c1 = e.j8();
        e.b({0x80, 0x3D}); e.rel(on, 1); e.b({0x00});
        e.b({0x74}); uint8_t* c0a = e.j8();
        e.b({0x3B, 0x3D}); e.rel(tid);
        e.b({0x75}); uint8_t* c0b = e.j8();
        e.at(c1); e.b({0xB0, 0x01});
        e.at(c0a); e.at(c0b);
        e.b({0x31, 0xD2, 0x3B, 0x0D}); e.rel(Pid);
        e.b({0x74}); uint8_t* d1 = e.j8();
        e.b({0x80, 0x3D}); e.rel(on, 1); e.b({0x00});
        e.b({0x74}); uint8_t* d0a = e.j8();
        e.b({0x3B, 0x0D}); e.rel(tid);
        e.b({0x75}); uint8_t* d0b = e.j8();
        e.at(d1); e.b({0xB2, 0x01});
        e.at(d0a); e.at(d0b);
        e.b({0xE9}); e.rel(base + 0xb851c9);
        return s;
    }
}

static void t_pick()
{
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> U(-20000, 20000);
    int same = 0;
    for (int it = 0; it < 2000; ++it)
    {
        std::vector<s2nmc::Item> items;
        int n = 5 + (int)(rng() % 40);
        for (int i = 0; i < n; ++i) items.push_back({(uintptr_t)(1000 + i), U(rng) * 0.6, U(rng) * 0.6});
        if (it % 3 == 0) for (int i = 0; i < 5; ++i) items.push_back({(uintptr_t)(5000 + i), 100.0 * (i % 2), 0.0});
        std::unordered_set<uintptr_t> sel, dropped;
        for (auto& x : items) { if (rng() % 3 == 0) sel.insert(x.key); if (rng() % 11 == 0) dropped.insert(x.key); }
        double rx = U(rng) * 0.3, ry = U(rng) * 0.3, hys = 1.0 + (rng() % 3) * 0.1;
        auto want = legacy::select(rx, ry, items, 7000, hys, 8, sel, dropped);
        auto got = s2nmc::pick({{rx, ry}}, items, 7000, hys, 8, {sel}, [&](uintptr_t k) { return dropped.count(k) > 0; });
        same += got.per[0] == want && got.all == want;
    }
    CHECK(same == 2000, "one player pick = legacy select: %d/2000", same);

    std::vector<s2nmc::Pt> cs = {{0, 0}, {100000, 0}, {0, 100000}, {100000, 100000}};
    std::vector<s2nmc::Item> items;
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i < 12; ++i) items.push_back({(uintptr_t)(c * 100 + i + 1), cs[c].x + 300.0 * i, cs[c].y});
    for (size_t np = 1; np <= 4; ++np)
    {
        std::vector<s2nmc::Pt> c2(cs.begin(), cs.begin() + (long)np);
        auto pk = s2nmc::pick(c2, items, 7000, 1.0, 8, {}, nullptr);
        CHECK(pk.all.size() == 8 * np, "%zu spread players: union %zu want %zu", np, pk.all.size(), 8 * np);
        bool okOwn = true;
        for (size_t i = 0; i < pk.all.size(); ++i) okOwn = okOwn && pk.own[i] == (int)((pk.all[i] - 1) / 100);
        CHECK(okOwn, "%zu spread players: owner = nearest player", np);
        for (size_t c = 0; c < np; ++c) CHECK(pk.per[c].size() == 8, "player %zu got %zu", c, pk.per[c].size());
    }
    std::vector<s2nmc::Pt> together = {{0, 0}, {50, 0}, {0, 50}, {-50, 0}};
    auto pk = s2nmc::pick(together, items, 7000, 1.0, 8, {}, nullptr);
    CHECK(pk.all.size() == 8, "4 players together: union %zu want 8 (no duplicates)", pk.all.size());
    std::vector<s2nmc::Pt> pair = {{0, 0}, {3300, 0}};
    pk = s2nmc::pick(pair, items, 7000, 1.0, 8, {}, nullptr);
    CHECK(pk.all.size() == 12, "2 players 33 m apart cover 12 NPCs: %zu", pk.all.size());
    std::vector<s2nmc::Item> edge = {{1, 7500, 0}};
    pk = s2nmc::pick({{0, 0}}, edge, 7000, 1.1, 8, {std::unordered_set<uintptr_t>{1}}, nullptr);
    CHECK(pk.all.size() == 1, "hysteresis keeps a listed NPC at 75 m");
    pk = s2nmc::pick({{0, 0}}, edge, 7000, 1.1, 8, {std::unordered_set<uintptr_t>{}}, nullptr);
    CHECK(pk.all.empty(), "unlisted NPC at 75 m stays out");
    pk = s2nmc::pick({{0, 0}, {7400, 0}}, edge, 7000, 1.0, 8, {}, nullptr);
    CHECK(pk.all.size() == 1 && pk.own[0] == 1, "second player brings the NPC in and owns it");
    std::vector<s2nmc::Item> spread = {{1, 30000, 0}, {2, 9000, 0}, {3, 200000, 0}};
    s2nmc::Pt host{30500, 0};
    pk = s2nmc::pick({{0, 0}}, spread, 10070, 1.0, 1 << 30, {}, nullptr, nullptr, &host);
    CHECK(pk.all.size() == 2 && pk.per[0].size() == 2, "host centre: NPCs near the host and the client both go to the client view (%zu)", pk.all.size());
    pk = s2nmc::pick({{0, 0}}, spread, 10070, 1.0, 1 << 30, {}, nullptr);
    CHECK(pk.all.size() == 1 && pk.all[0] == 2, "no host centre: only the NPC within R of the client (%zu)", pk.all.size());
    pk = s2nmc::pick({{0, 0}}, spread, 7000, 1.0, 1 << 30, {}, nullptr);
    CHECK(pk.all.empty(), "old 70 m radius drops the NPC at 90 m");
}

static void t_hdr()
{
    std::vector<const s2nmc::Chunk*> ord;
    std::vector<int*> lp;
    s2nmc::ViewSt vs{0, 0};
    s2nmc::Out st;
    CHECK(s2nmc::assemble("5", ord, lp, vs, {1, 100, 2.0}, 1, "", "", st).empty(), "empty list without header sends nothing (old)");
    CHECK(s2nmc::assemble("5", ord, lp, vs, {1, 100, 2.0}, 1, "", "", st, "r10070") == "#NN:t5;r10070;", "empty list with header still sends a snapshot");
    s2nmc::Chunk c;
    c.ok = true; c.uid = 7; c.rec = "7,1";
    int ls = 9;
    ord.push_back(&c); lp.push_back(&ls);
    CHECK(s2nmc::assemble("5", ord, lp, vs, {1, 100, 2.0}, 1, "", "", st, "r10070") == "#NN:t5;r10070;7,1", "header sits before the rows");
}

static void t_vend()
{
    std::vector<legacy::VRow> rows = {{0, 0, 0, "v", "a"}, {9000, 0, 0, "v", "b"}, {50000, 0, 0, "v", "c"}, {0, 60000, 0, "v", "d"}};
    struct VPos { uintptr_t o; double x, y; };
    std::vector<VPos> live = {{11, 100, 0}, {12, 9100, 0}, {13, 50200, 0}, {14, 0, 60300}, {15, 40000, 40000}};
    std::vector<legacy::VPos> lv;
    for (auto& p : live) lv.push_back({p.o, p.x, p.y});
    std::mt19937 rng(3);
    int same = 0;
    for (int it = 0; it < 500; ++it)
    {
        double cx = (double)(rng() % 70000) - 5000, cy = (double)(rng() % 70000) - 5000;
        std::unordered_set<uintptr_t> a, b;
        int ra = legacy::vend_pick(rows.data(), rows.size(), 1500, lv, cx, cy, 7000, a);
        int rb = s2nmc::vend_rows(rows.data(), rows.size(), live, {{cx, cy}}, 7000, 1500, b);
        same += ra == rb && a == b;
    }
    CHECK(same == 500, "vend one player = legacy: %d/500", same);
    std::unordered_set<uintptr_t> out;
    int r = s2nmc::vend_rows(rows.data(), rows.size(), live, {{0, 0}, {50000, 0}, {0, 60000}}, 7000, 1500, out);
    CHECK(r == 3 && out.size() == 3 && out.count(11) && !out.count(12) && out.count(13) && out.count(14), "3 players: vendors near each player kept (rows=%d n=%zu)", r, out.size());
}

static void t_assemble()
{
    std::mt19937 rng(11);
    int same = 0, frames = 0;
    for (int run = 0; run < 200; ++run)
    {
        int n = (int)(rng() % 10);
        std::vector<legacy::Sel> old(n);
        std::vector<s2nmc::Chunk> ch(n);
        std::vector<int> ls(n, 0);
        legacy::St H;
        H.lookRR = 0;
        s2nmc::ViewSt vs{0, 0};
        double now = 100;
        int lookn = 1 + (int)(rng() % 4), lookmax = 200 + (int)(rng() % 1400);
        for (int f = 0; f < 60; ++f)
        {
            for (int i = 0; i < n; ++i)
            {
                auto& o = old[(size_t)i];
                if (f == 0 || rng() % 10 == 0)
                {
                    o.uid = 100 + i;
                    o.look = rng() % 5 ? std::string(30 + rng() % 400, (char)('a' + i)) : std::string();
                    o.lookSent = 0;
                    ls[(size_t)i] = 0;
                }
                o.ok = rng() % 7 != 0;
                o.fixed = rng() % 6 == 0;
                o.rec = o.ok ? std::to_string(o.uid) + ",1,2,3," + std::to_string(rng() % 1000) : std::string();
                o.mm = o.ok && rng() % 3 == 0 ? "m" + std::to_string(o.uid) + ",b,1,2,/x" : std::string();
                o.bb = o.ok && rng() % 4 == 0 ? "b" + std::to_string(o.uid) + ",1,2" : std::string();
                auto& c = ch[(size_t)i];
                c.ok = o.ok; c.uid = o.uid; c.fixed = o.fixed; c.look = &o.look; c.rec = o.rec; c.mm = o.mm; c.bb = o.bb;
                if (o.lookSent != ls[(size_t)i]) ls[(size_t)i] = o.lookSent;
            }
            std::vector<const s2nmc::Chunk*> ord;
            std::vector<int*> lp;
            std::vector<int> mine = ls;
            for (int i = 0; i < n; ++i) { ord.push_back(&ch[(size_t)i]); lp.push_back(&mine[(size_t)i]); }
            std::string nu = rng() % 5 == 0 ? "u1,2,3" : std::string();
            std::string nd = rng() % 6 == 0 ? "#ND:x" : std::string();
            std::string t = std::to_string(1000 + f);
            s2nmc::Out st;
            std::string b = s2nmc::assemble(t, ord, lp, vs, {lookn, lookmax, 2.0}, now, nu, nd, st);
            std::string a = legacy::snapshot(old, H, now, lookn, lookmax, 2.0, t, nu, nd);
            for (int i = 0; i < n; ++i) ls[(size_t)i] = mine[(size_t)i];
            bool eq = a == b && vs.rr == H.rr && vs.lookRR == H.lookRR;
            for (int i = 0; i < n; ++i) eq = eq && old[(size_t)i].lookSent == ls[(size_t)i];
            same += eq;
            ++frames;
            now += 0.1 * (1 + rng() % 30);
        }
    }
    CHECK(same == frames, "assemble one view = old host_snapshot bytes: %d/%d frames", same, frames);
}

struct Io { uint32_t eax, ecx, edi, edx, ebp, oeax, oedx, pad; uint64_t x8, x15, ox15; };

static uint8_t* g_base = nullptr;
static void commit(uint8_t* p, size_t n) { VirtualAlloc(p, n, MEM_COMMIT, PAGE_EXECUTE_READWRITE); }

static uint8_t* thunk(uint8_t* at, uint8_t* stub)
{
    uint8_t* p = at;
    auto b = [&](std::initializer_list<uint8_t> v) { for (uint8_t x : v) *p++ = x; };
    b({0x53, 0x55, 0x57, 0x56, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x20});
    b({0xF3, 0x44, 0x0F, 0x7F, 0x3C, 0x24, 0xF3, 0x44, 0x0F, 0x7F, 0x44, 0x24, 0x10});
    b({0x48, 0x89, 0xCB, 0x8B, 0x03, 0x8B, 0x4B, 0x04, 0x8B, 0x7B, 0x08, 0x8B, 0x53, 0x0C, 0x8B, 0x6B, 0x10, 0x44, 0x8B, 0x63, 0x10});
    b({0xF3, 0x44, 0x0F, 0x7E, 0x43, 0x20, 0xF3, 0x44, 0x0F, 0x7E, 0x7B, 0x28});
    b({0xE8});
    int32_t r = (int32_t)(stub - (p + 4));
    std::memcpy(p, &r, 4);
    p += 4;
    b({0x89, 0x43, 0x14, 0x89, 0x53, 0x18, 0x66, 0x44, 0x0F, 0xD6, 0x7B, 0x30});
    b({0xF3, 0x44, 0x0F, 0x6F, 0x3C, 0x24, 0xF3, 0x44, 0x0F, 0x6F, 0x44, 0x24, 0x10});
    b({0x48, 0x83, 0xC4, 0x20, 0x41, 0x5C, 0x5E, 0x5F, 0x5D, 0x5B, 0xC3});
    return p;
}

static void pad(uint32_t off, uint32_t id)
{
    uint8_t* p = g_base + off;
    p[0] = 0xC7; p[1] = 0x43; p[2] = 0x1C;
    std::memcpy(p + 3, &id, 4);
    p[7] = 0xC3;
}

struct Res { uint32_t padA, padB, padC; uint32_t oeax, oedx; uint64_t x15; LONG hits; };

static void t_aggro()
{
    const size_t span = 0xA000000;
    g_base = static_cast<uint8_t*>(VirtualAlloc(nullptr, span, MEM_RESERVE, PAGE_NOACCESS));
    CHECK(g_base != nullptr, "reserve fake exe region");
    if (!g_base) return;
    commit(g_base + 0x9eec000, 0x1000);
    commit(g_base + 0xb84000, 0x2000);
    commit(g_base + 0x9eda000, 0x1000);
    commit(g_base + 0xaee000, 0x2000);
    commit(g_base + 0x1000000, 0x4000);
    pad(0xb852e8, 1); pad(0xb84f05, 2); pad(0xb850e0, 3); pad(0xb851c9, 4);
    pad(0xaeefbe, 1); pad(0xaeea4f, 2); pad(0xaeec19, 3); pad(0xaeecfc, 4);
    const uint32_t PL = 0x1111;
    *reinterpret_cast<uint32_t*>(g_base + 0x9eec140) = PL;
    *reinterpret_cast<uint32_t*>(g_base + 0x9eda140) = PL;
    uint8_t* stN = g_base + 0x1000000;
    uint8_t* stO = g_base + 0x1001000;
    auto* dN = reinterpret_cast<ag::AgData*>(stN + 0x800);
    auto* dO = reinterpret_cast<legacy::AgOld*>(stO + 0x800);
    dN->hits = 0; ag::ag_set(dN, nullptr, 0);
    dO->tid = 0xFFFFFFFFu; dO->on = 0; dO->hits = 0;
    ag::AgStubs sn = ag::ag_emit(stN, g_base, dN);
    ag::AgStubs so = legacy::emit(stO, g_base, dO);
    CHECK(sn.c + 0x100 < stN + 0x800 && so.c + 0x100 < stO + 0x800, "stubs fit below their data");
    uint8_t* th = g_base + 0x1002000;
    using Fn = void (*)(Io*);
    uint8_t* q = th;
    Fn nA = (Fn)q; q = thunk(q, sn.a); Fn nB = (Fn)q; q = thunk(q, sn.b); Fn nC = (Fn)q; q = thunk(q, sn.c);
    Fn oA = (Fn)q; q = thunk(q, so.a); Fn oB = (Fn)q; q = thunk(q, so.b); Fn oC = (Fn)q; q = thunk(q, so.c);
    FlushInstructionCache(GetCurrentProcess(), g_base + 0x1000000, 0x4000);
    auto run = [&](Fn A, Fn B, Fn Cc, volatile LONG* hits, uint32_t x, uint32_t y) {
        Res r{};
        Io io{};
        io.eax = x; A(&io); r.padA = io.pad;
        LONG h0 = *hits;
        io = Io{}; io.edi = x; io.x8 = 0x4142434445464748ull; io.x15 = 0x9999; B(&io); r.padB = io.pad; r.x15 = io.ox15;
        r.hits = *hits - h0;
        io = Io{}; io.edi = x; io.ecx = y; io.ebp = PL; io.eax = 0xDEAD; io.edx = 0xBEEF; Cc(&io); r.padC = io.pad; r.oeax = io.oeax & 0xFF; r.oedx = io.oedx & 0xFF;
        return r;
    };
    uint32_t vals[] = {PL, 0x2001, 0x2002, 0x2003, 0x2004, 0x3333, 0xFFFFFFFFu};
    int cmpOld = 0, cmpN = 0, cmpModel = 0, nModel = 0;
    for (int n = 0; n <= 4; ++n)
    {
        uint32_t ids[4] = {0x2001, 0x2002, 0x2003, 0x2004};
        ag::ag_set(dN, ids, n);
        dO->on = 0; dO->tid = n ? ids[0] : 0xFFFFFFFFu; dO->on = n ? 1 : 0;
        for (uint32_t x : vals)
            for (uint32_t y : vals)
            {
                Res a = run(nA, nB, nC, &dN->hits, x, y);
                auto tgt = [&](uint32_t v) { for (int i = 0; i < n; ++i) if (ids[i] == v) return true; return false; };
                bool hx = x == PL || tgt(x), hy = y == PL || tgt(y);
                bool m = a.padA == (hx ? 1u : 2u) && a.padB == 3 && a.padC == 4 && a.oeax == (hx ? 1u : 0u) && a.oedx == (hy ? 1u : 0u) &&
                         a.x15 == (hx ? 0ull : 0x4142434445464748ull) && a.hits == ((x != PL && tgt(x)) ? 1 : 0);
                cmpModel += m; ++nModel;
                if (!m) std::printf("  model miss n=%d x=%x y=%x: A=%u B=%u C=%u eax=%u edx=%u x15=%llx hits=%ld\n", n, x, y, a.padA, a.padB, a.padC, a.oeax, a.oedx, (unsigned long long)a.x15, a.hits);
                if (n <= 1)
                {
                    Res b = run(oA, oB, oC, &dO->hits, x, y);
                    ++cmpN;
                    cmpOld += std::memcmp(&a, &b, sizeof a) == 0;
                }
            }
    }
    CHECK(cmpOld == cmpN, "new stub with 0-1 target = old single-tid stub: %d/%d", cmpOld, cmpN);
    CHECK(cmpModel == nModel, "new stub with 0-4 targets = model: %d/%d", cmpModel, nModel);
    VirtualFree(g_base, 0, MEM_RELEASE);
}

static void t_secat()
{
    const float st[3] = {0.f, 1.f, 3.f};
    int how = 0;
    double r = s2nmc::sec_at(st, 3, 4.0, 1.5, 1.8, {1, 1, -1}, how);
    CHECK(std::fabs(r - 1.8) < 1e-6 && how == 0, "secat: inside the section stays: %f %d", r, how);
    r = s2nmc::sec_at(st, 3, 4.0, 1.5, 3.5, {1, 1, -1}, how);
    CHECK(std::fabs(r - 1.5) < 1e-6 && how == 1, "secat: a self loop wraps inside the section, never into the exit: %f %d", r, how);
    r = s2nmc::sec_at(st, 3, 4.0, 1.5, 7.5, {1, 1, -1}, how);
    CHECK(std::fabs(r - 1.5) < 1e-6 && how == 1, "secat: several loops wrap: %f %d", r, how);
    r = s2nmc::sec_at(st, 3, 4.0, 0.5, 1.5, {1, 1, -1}, how);
    CHECK(std::fabs(r - 1.5) < 1e-6 && how == 0, "secat: chain into the next section: %f %d", r, how);
    r = s2nmc::sec_at(st, 3, 4.0, 0.5, 3.5, {1, 1, -1}, how);
    CHECK(std::fabs(r - 1.5) < 1e-6 && how == 1, "secat: chain then loop: %f %d", r, how);
    r = s2nmc::sec_at(st, 3, 4.0, 1.5, 3.4, {1, 2, -1}, how);
    CHECK(std::fabs(r - 3.4) < 1e-6 && how == 0, "secat: host chain to the exit follows it: %f %d", r, how);
    r = s2nmc::sec_at(st, 3, 4.0, 3.5, 4.5, {1, 2, -1}, how);
    CHECK(r < 4.0 && r >= 3.0 && how == 2, "secat: never past the montage end: %f %d", r, how);
    r = s2nmc::sec_at(st, 3, 4.0, 0.5, 1.2, {-1, 1, -1}, how);
    CHECK(r < 1.0 && how == 2, "secat: a section that ends the montage keeps the copy at its end: %f %d", r, how);
    r = s2nmc::sec_at(st, 3, 4.0, 1.5, 9.0, {}, how);
    CHECK(r < 4.0 && how == 2, "secat: no host chain clamps only to the montage end: %f %d", r, how);
    r = s2nmc::sec_at(st, 3, 4.0, 1.5, 2.5, {}, how);
    CHECK(std::fabs(r - 2.5) < 1e-6 && how == 0, "secat: no host chain inside the montage is unchanged: %f %d", r, how);
    const float us[3] = {3.f, 0.f, 1.f};
    r = s2nmc::sec_at(us, 3, 4.0, 1.5, 3.5, {0, 2, 2}, how);
    CHECK(std::fabs(r - 1.5) < 1e-6 && how == 1, "secat: unsorted sections: %f %d", r, how);
    const float cov[15] = {0.f, 2.33333f, 4.23333f, 5.33333f, 6.33333f, 7.33333f, 8.33333f, 10.0667f, 11.6667f, 12.4667f, 13.5667f, 14.7667f, 15.5667f, 16.5667f, 17.5667f};
    const std::vector<int> hostRight = {4, 5, 4, 5, 9, -1, 7, 9, 14, 14, 14, 14, 14, 14, -1};
    r = s2nmc::sec_at(cov, 15, 17.5667, 7.333, 7.333 + 0.2 * 1.2, hostRight, how, 1e-4);
    CHECK(r > 12.4 && how == 0, "secat: 0.1 ms tolerance puts the ms-rounded IdleRight start into IdleLeft and follows the stand-up link: %f %d", r, how);
    r = s2nmc::sec_at(cov, 15, 17.5667, 7.333, 7.333 + 0.2 * 1.2, hostRight, how, 1e-3);
    CHECK(std::fabs(r - 7.573) < 1e-6 && how == 0, "secat: 1 ms tolerance keeps the ms-rounded IdleRight start in IdleRight: %f %d", r, how);
    const std::vector<int> hostLeft = {4, 5, 4, 5, -1, 12, 7, 9, 14, 14, 14, 14, 14, 14, -1};
    r = s2nmc::sec_at(cov, 15, 17.5667, 6.333, 6.333 + 0.2 * 1.2, hostLeft, how, 1e-3);
    CHECK(std::fabs(r - 6.573) < 1e-6 && how == 0, "secat: IdleLeft start stays in IdleLeft, not one section ahead: %f %d", r, how);
    r = s2nmc::sec_at(cov, 15, 17.5667, 6.333, 6.333 + 1.5 * 1.2, hostLeft, how, 1e-3);
    CHECK(r < 7.34 && r >= 6.33 && how == 2, "secat: a late copy of a host section that ends stays inside it: %f %d", r, how);
}

static void t_bandwidth()
{
    const int perNpc[] = {109, 360, 520};
    const char* lbl[] = {"109 B/NPC (idle, log)", "360 B/NPC (mid, log)", "520 B/NPC (combat, log)"};
    std::printf("BANDWIDTH model: per-view NN bytes = sum of chunk bytes of that view's list (natmax=8 per player)\n");
    for (int k = 0; k < 3; ++k)
        for (int np = 1; np <= 4; ++np)
        {
            std::vector<s2nmc::Chunk> ch(8 * (size_t)np);
            for (size_t i = 0; i < ch.size(); ++i) { ch[i].ok = true; ch[i].uid = (int)i + 1; ch[i].fixed = true; ch[i].rec = std::string((size_t)perNpc[k], 'x'); }
            auto size_for = [&](size_t first, size_t cnt) {
                std::vector<const s2nmc::Chunk*> ord;
                std::vector<int> l(cnt, 9);
                std::vector<int*> lp;
                for (size_t i = 0; i < cnt; ++i) { ord.push_back(&ch[first + i]); lp.push_back(&l[i]); }
                s2nmc::ViewSt vs;
                s2nmc::Out st;
                return s2nmc::assemble("1760000000000", ord, lp, vs, {4, 1400, 2.0}, 0, "", "", st).size();
            };
            size_t personal = 0, together = 0;
            for (int v = 0; v < np; ++v) { personal += size_for((size_t)v * 8, 8); together += size_for(0, 8); }
            size_t unionCommon = size_for(0, 8 * (size_t)np) * (size_t)np;
            std::printf("  %-26s players=%d spread: host_up=%6zu B/snap (personal) vs %6zu (one shared union NN) | together: %6zu B/snap, per view %zu B\n", lbl[k], np, personal,
                        unionCommon, together, together / (size_t)np);
        }
}

static void t_qg()
{
    const std::string Z = "BE942D4E0123456789ABCDEF00000001";
    CHECK(s2nmc::qg_ok(Z), "qg_ok valid");
    CHECK(!s2nmc::qg_ok("00000000000000000000000000000000"), "qg_ok zero guid");
    CHECK(!s2nmc::qg_ok("BE942D4E"), "qg_ok short");
    CHECK(!s2nmc::qg_ok("BE942D4E0123456789ABCDEF0000000G"), "qg_ok non hex");
    int32_t g[4] = {0, 0, 0, 0};
    CHECK(s2nmc::qg_hex(g).empty(), "qg_hex zero is empty");
    CHECK(s2nmc::qg_parse(Z, g), "qg_parse ok");
    CHECK((uint32_t)g[0] == 0xBE942D4Eu && g[3] == 1, "qg_parse words %08X %08X", (uint32_t)g[0], (uint32_t)g[3]);
    CHECK(s2nmc::qg_hex(g) == Z, "qg_hex round trip %s", s2nmc::qg_hex(g).c_str());
    CHECK(s2nmc::qg_up("be942d4e0123456789abcdef00000001") == Z, "qg_up");

    std::string rec = "1488";
    for (int i = 1; i < 21; ++i) rec += "," + std::to_string(i);
    CHECK(s2nmc::qg_field(rec).empty(), "21-field row has no guid");
    std::string r22 = s2nmc::with_qg(rec, Z);
    CHECK(r22 == rec + "," + Z, "with_qg appends field 22");
    CHECK(s2nmc::qg_field(r22) == Z, "qg_field reads field 22");
    CHECK(s2nmc::with_qg(rec, "") == rec, "with_qg empty keeps row");
    CHECK(s2nmc::with_qg(rec, "00000000000000000000000000000000") == rec, "with_qg zero keeps row");
    CHECK(s2nmc::qg_field(rec + ",be942d4e0123456789abcdef00000001") == Z, "qg_field uppercases");
    CHECK(s2nmc::qg_field(rec + ",junk").empty(), "qg_field rejects junk");

    std::string nu;
    s2nmc::nu_add(nu, 1488, Z);
    s2nmc::nu_add(nu, 1681, "");
    s2nmc::nu_add(nu, 1426, "be942d4e0123456789abcdef00000002");
    CHECK(nu == "1488/" + Z + ",1681,1426/be942d4e0123456789abcdef00000002", "nu_add format: %s", nu.c_str());
    std::string nu2;
    s2nmc::nu_add(nu2, 7, "BE942D4E0123456789ABCDEF00000002");
    std::vector<std::pair<int32_t, std::string>> got;
    s2nmc::nu_each("u" + nu + "," + nu2, 1, [&](int32_t u, const std::string& q) { got.emplace_back(u, q); });
    CHECK(got.size() == 4, "nu_each count %zu", got.size());
    CHECK(got.size() == 4 && got[0].first == 1488 && got[0].second == Z && got[1].first == 1681 && got[1].second.empty() && got[2].second == "BE942D4E0123456789ABCDEF00000002" && got[3].first == 7 && got[3].second == "BE942D4E0123456789ABCDEF00000002", "nu_each values");
    got.clear();
    s2nmc::nu_each("u1488,1681", 1, [&](int32_t u, const std::string& q) { got.emplace_back(u, q); });
    CHECK(got.size() == 2 && got[0].first == 1488 && got[0].second.empty() && got[1].first == 1681, "nu_each old format");

    std::unordered_map<int32_t, int*> byUid;
    std::unordered_map<std::string, int*> byQg;
    int zLocal = 2089, other = 1681, same = 1488;
    std::unordered_map<int*, int32_t> uidOf = {{&zLocal, 2089}, {&other, 1681}, {&same, 1488}};
    std::unordered_set<int32_t> rows = {1488, 1681};
    auto uo = [&](int* p) { auto it = uidOf.find(p); return it == uidOf.end() ? 0 : it->second; };
    auto orow = [&](int32_t u) { return rows.count(u) > 0; };
    bool vq = true;
    byQg[Z] = &zLocal;
    CHECK(s2nmc::local_pick(1488, Z, true, byUid, byQg, uo, orow, vq) == &zLocal && vq, "local_pick: other uid, same quest guid adopts the local quest actor");
    CHECK(s2nmc::local_pick(1488, Z, false, byUid, byQg, uo, orow, vq) == nullptr && !vq, "local_pick: natqg=0 old behaviour");
    CHECK(s2nmc::local_pick(1488, "", true, byUid, byQg, uo, orow, vq) == nullptr && !vq, "local_pick: no guid, no match");
    byUid[1488] = &same;
    CHECK(s2nmc::local_pick(1488, Z, true, byUid, byQg, uo, orow, vq) == &same && !vq, "local_pick: same uid wins");
    byUid.clear();
    byQg[Z] = &other;
    CHECK(s2nmc::local_pick(1488, Z, true, byUid, byQg, uo, orow, vq) == nullptr && !vq, "local_pick: guid actor holds the uid of another host row");
    rows.erase(1681);
    CHECK(s2nmc::local_pick(1488, Z, true, byUid, byQg, uo, orow, vq) == &other && vq, "local_pick: guid actor uid not a host row");
    CHECK(s2nmc::local_pick(0, Z, true, byUid, byQg, uo, orow, vq) == &other && vq, "local_pick: uid 0 row with guid");
}

struct FdPawn { uintptr_t k; double born, died, x, y; bool quest; };

static bool fd_listed(bool on, const std::vector<FdPawn>& ps, uintptr_t want, double tEnd, std::vector<std::pair<double, double>> host = {}, bool all = false)
{
    s2nmc::FreshDead fd;
    fd.all = all;
    std::unordered_set<uintptr_t> sel;
    const double R2 = 7000.0 * 7000.0;
    double hx = 0, px = 0;
    bool listed = false;
    size_t hi = 0;
    for (double t = 0; t <= tEnd + 1e-9; t += 1.0)
    {
        while (hi < host.size() && host[hi].first <= t) { hx = host[hi].second; ++hi; }
        fd.begin(t, (hx - px) * (hx - px));
        px = hx;
        std::vector<std::pair<uintptr_t, bool>> nb;
        for (const auto& p : ps)
        {
            if (t < p.born) continue;
            bool al = t < p.died;
            double d2 = (p.x - hx) * (p.x - hx) + p.y * p.y;
            fd.seen(p.k, al && d2 <= R2, t);
            if (d2 <= R2) nb.push_back({p.k, al});
        }
        for (auto& [k, al] : nb)
        {
            bool quest = false;
            for (const auto& p : ps) if (p.k == k) quest = p.quest;
            if (!al && !sel.count(k) && !(on && fd.dead_ok(k, t, quest))) continue;
            sel.insert(k);
            if (k == want) listed = true;
        }
        fd.end(t);
    }
    return listed;
}

static void t_fd()
{
    std::vector<FdPawn> run3 = {{1754, 0.30, 0.55, 400, 0, false}};
    CHECK(!fd_listed(false, run3, 1754, 5), "fd: run 3 repro, old rule never lists a bandit alive 0.25 s between two rebuilds");
    CHECK(fd_listed(true, run3, 1754, 5), "fd: natfd lists the bandit that died between two rebuilds");
    std::vector<FdPawn> run1 = {{2367, 0.30, 2.10, 400, 0, false}};
    CHECK(fd_listed(false, run1, 2367, 5) && fd_listed(true, run1, 2367, 5), "fd: a bandit alive over a rebuild is listed with both rules");
    std::vector<FdPawn> old = {{9, -100, -50, 400, 0, false}};
    CHECK(!fd_listed(true, old, 9, 5), "fd: a corpse present at the first scan is not listed");
    std::vector<FdPawn> quest = {{11, 0.3, 0.3, 400, 0, true}};
    CHECK(!fd_listed(true, quest, 11, 5), "fd: a body that appears dead with a quest guid is not listed");
    std::vector<FdPawn> walk = {{13, -100, -50, 9000, 0, false}};
    CHECK(!fd_listed(true, walk, 13, 6, {{0, 0}, {2.0, 2500}}), "fd: an old corpse seen far and walked up to is not listed");
    std::vector<FdPawn> tele = {{14, 2.6, -50, 50400, 0, false}};
    CHECK(!fd_listed(true, tele, 14, 6, {{0, 0}, {3.0, 50000}}), "fd: corpses around a teleport target are not listed");
    std::vector<FdPawn> bulk = {{21, 2.4, 2.4, 400, 0, false}, {22, 2.4, 2.4, 410, 0, false}, {23, 2.4, 2.4, 420, 0, false}};
    int n = 0;
    for (uintptr_t k : {21, 22, 23}) n += fd_listed(true, bulk, k, 5);
    CHECK(n == 2, "fd: at most 2 new dead bodies per scan, got %d", n);
    s2nmc::FreshDead fd;
    fd.begin(0, 0); fd.seen(5, true, 0); fd.end(0);
    fd.begin(1, 0); fd.seen(5, false, 1);
    CHECK(fd.dead_ok(5, 1, false), "fd: seen alive near, died off the list (natmax overflow) is listed");
    fd.end(1);
    fd.begin(20, 0); fd.seen(5, false, 20);
    CHECK(!fd.dead_ok(5, 20, false), "fd: seen alive more than 10 s ago is not listed");
    fd.end(20);
    fd.begin(40, 0);
    CHECK(!fd.scanOk, "fd: a scan gap over 2.5 s (load) only seeds");
    CHECK(s2nmc::dead_row_copy(true, false, false), "fd: client copies a row that arrives dead");
    CHECK(!s2nmc::dead_row_copy(true, false, true), "fd: client does not respawn a dead row whose copy it already had");
    CHECK(!s2nmc::dead_row_copy(false, false, false), "fd: natfd=0 old client rule");
    CHECK(s2nmc::dead_row_copy(false, true, true) && s2nmc::dead_row_copy(true, true, false), "fd: live rows unchanged");
}

static void t_fd_review()
{
    {
        const double R = 7000, R2 = R * R;
        std::vector<s2nmc::Pt> cs = {{0, 0}};
        struct P { uintptr_t k; double x; };
        std::vector<P> ps = {{1, 8000}, {2, 8100}, {3, 500}};
        auto run = [&](bool gate, std::unordered_set<uintptr_t>& got, std::unordered_set<uintptr_t>& picked) {
            s2nmc::FreshDead fd;
            fd.begin(0, 0); fd.end(0);
            fd.begin(1, 0);
            for (auto& p : ps) fd.seen(p.k, false, 1);
            std::vector<s2nmc::Item> items;
            std::unordered_set<uintptr_t> fdk;
            for (auto& p : ps)
            {
                bool inR = s2nmc::near2(cs, p.x, 0) <= R2;
                if (!(gate ? fd.dead_ok(p.k, 1, false, inR) : fd.dead_ok(p.k, 1, false))) continue;
                got.insert(p.k);
                fdk.insert(p.k);
                items.push_back({p.k, p.x, 0});
            }
            fd.end(1);
            auto pk = s2nmc::pick(cs, items, R, 1.5, 8, {}, nullptr, &fdk);
            for (uintptr_t k : pk.all) picked.insert(k);
        };
        std::unordered_set<uintptr_t> g0, p0, g1, p1;
        run(false, g0, p0);
        run(true, g1, p1);
        CHECK(!g0.count(3) && !p0.count(1) && !p0.count(2), "fd review 1: ungated, never-alive corpses in R<d<=Rh take both slots and are never picked");
        CHECK(g1.count(3) && !g1.count(1) && !g1.count(2) && p1.count(3), "fd review 1: out-of-R corpses use no slot, the real fresh death is listed and picked");
    }
    {
        s2nmc::FreshDead fd;
        fd.begin(0, 0); fd.end(0);
        fd.begin(1, 0);
        for (uintptr_t k = 1; k <= 6; ++k) fd.seen(k, false, 1);
        int n = 0;
        for (uintptr_t k = 1; k <= 6; ++k) n += fd.dead_ok(k, 1, false);
        fd.end(1);
        CHECK(n == 0, "fd review 2: six corpses appearing in one scan (streaming) are not listed, got %d", n);
        fd.begin(2, 0);
        fd.seen(7, false, 2);
        CHECK(fd.dead_ok(7, 2, false), "fd review 2: one lone new corpse is still listed");
    }
    {
        s2nmc::FreshDead fd;
        fd.begin(0, 0);
        for (uintptr_t k = 1; k <= 3; ++k) fd.seen(k, true, 0);
        fd.end(0);
        fd.begin(1, 0);
        for (uintptr_t k = 1; k <= 3; ++k) fd.seen(k, false, 1);
        int n = 0;
        for (uintptr_t k = 1; k <= 3; ++k) n += fd.dead_ok(k, 1, false);
        CHECK(n == 2, "fd review 3: recently-alive path obeys the 2 per scan cap, got %d", n);
        fd.end(1);
        fd.begin(2, 0);
        for (uintptr_t k = 1; k <= 3; ++k) fd.seen(k, false, 2);
        CHECK(!fd.dead_ok(1, 2, false) && !fd.dead_ok(2, 2, false), "fd review 3: a dropped dead row does not come back under the same uid");
        CHECK(fd.dead_ok(3, 2, false), "fd review 3: the capped corpse is listed on the next scan");
    }
}

struct FdFull { int sent[2] = {}; int accepts = 0; size_t maxList = 0; bool aliveAll = true; bool late = false; };

static FdFull fd_full(bool on, bool outCap = true)
{
    s2nmc::FreshDead fd;
    struct S { bool dead = false, fd = false; double deadAt = 0; };
    std::unordered_map<uintptr_t, S> sel;
    std::unordered_set<uintptr_t> prevSel;
    FdFull r;
    const double natdead = 30;
    for (double t = 0; t <= 40 + 1e-9; t += 1.0)
    {
        fd.begin(t, 0);
        std::vector<s2nmc::Item> items;
        std::unordered_set<uintptr_t> fdk;
        std::unordered_map<uintptr_t, bool> alive;
        for (int i = 0; i < 10; ++i)
        {
            uintptr_t k = 100 + i;
            bool al = !(i >= 8 && t >= 5.5);
            alive[k] = al;
            fd.seen(k, al, t);
            auto it = sel.find(k);
            bool listed = it != sel.end();
            bool fdr = on && listed && it->second.fd;
            if (!al && !listed) { if (!(on && fd.dead_ok(k, t, false))) continue; fdr = true; ++r.accepts; }
            if (fdr) fdk.insert(k);
            items.push_back({k, 300.0 + 100.0 * i, 0});
        }
        fd.end(t);
        auto drop = [&](uintptr_t k) { auto it = sel.find(k); return it != sel.end() && it->second.dead && t - it->second.deadAt > natdead; };
        auto pk = s2nmc::pick({{0, 0}}, items, 7000, 1.0, 8, {prevSel}, drop, outCap ? &fdk : nullptr);
        std::unordered_map<uintptr_t, S> next;
        for (uintptr_t k : pk.all)
        {
            S s = sel.count(k) ? sel[k] : S{};
            if (!alive[k] && !s.dead) { s.dead = true; s.deadAt = t; }
            if (s.dead && t - s.deadAt > natdead) continue;
            if (fdk.count(k)) s.fd = true;
            next[k] = s;
        }
        sel = std::move(next);
        prevSel.clear();
        for (uintptr_t k : pk.per[0]) if (sel.count(k)) prevSel.insert(k);
        r.maxList = std::max(r.maxList, prevSel.size());
        for (uintptr_t k = 100; k < 108; ++k) r.aliveAll = r.aliveAll && prevSel.count(k) > 0;
        for (int d = 0; d < 2; ++d) if (prevSel.count(108 + d)) { ++r.sent[d]; if (t >= 38) r.late = true; }
    }
    return r;
}

static void t_fdfull()
{
    FdFull o = fd_full(false);
    CHECK(o.sent[0] == 0 && o.sent[1] == 0, "fdfull: s378 repro, old rule never sends the 2 far bandits that died off a full 8-row list (%d %d)", o.sent[0], o.sent[1]);
    FdFull c2 = fd_full(true, false);
    CHECK(c2.sent[0] == 0 && c2.sent[1] == 0 && c2.accepts == 2, "fdfull: s378 cb2 repro, natfd accepted inside the cap: never sent, accepted once and not re-accepted every second (%d %d acc=%d)", c2.sent[0], c2.sent[1], c2.accepts);
    FdFull n = fd_full(true);
    CHECK(n.sent[0] == 31 && n.sent[1] == 31, "fdfull: natfd sends both fresh corpses outside the cap from the first dead scan until natdead (%d %d)", n.sent[0], n.sent[1]);
    CHECK(n.accepts == 2, "fdfull: each fresh corpse is accepted once, not every second (%d)", n.accepts);
    CHECK(n.aliveAll && o.aliveAll, "fdfull: the 8 nearest alive bandits stay listed");
    CHECK(n.maxList == 10 && o.maxList == 8, "fdfull: list = 8 capped + 2 fresh dead (%zu, old %zu)", n.maxList, o.maxList);
    CHECK(!n.late, "fdfull: a corpse past natdead is not listed again");
    std::vector<s2nmc::Item> items;
    for (int i = 0; i < 12; ++i) items.push_back({(uintptr_t)(1 + i), 300.0 * i, 0});
    std::unordered_set<uintptr_t> none;
    auto a = s2nmc::pick({{0, 0}}, items, 7000, 1.0, 8, {}, nullptr);
    auto b = s2nmc::pick({{0, 0}}, items, 7000, 1.0, 8, {}, nullptr, &none);
    CHECK(a.all == b.all && a.all.size() == 8, "fdfull: an empty fresh set picks as before");
}

static void t_cpscale()
{
    CHECK(s2nmc::cli_gone(true, true, 1, 2, false), "cpscale: a record from an older snapshot not listed by the host is dropped");
    CHECK(!s2nmc::cli_gone(true, true, 1, 2, true), "cpscale: a uid the host lists by u token is kept");
    CHECK(!s2nmc::cli_gone(true, true, 2, 2, false), "cpscale: a record from the current snapshot is kept");
    CHECK(!s2nmc::cli_gone(true, false, 1, 2, false), "cpscale: no full snapshot yet, nothing dropped");
    CHECK(!s2nmc::cli_gone(false, true, 1, 2, false), "cpscale: natrhost off, nothing dropped");

    CHECK(s2nmc::hp_scale_tok("h4") == 10000 && s2nmc::hp_scale_tok("h2") == 100, "cpscale: h<n> token gives 10^n");
    CHECK(s2nmc::hp_scale_tok("r70") == 0 && s2nmc::hp_scale_tok("h7") == 0 && s2nmc::hp_scale_tok("h") == 0, "cpscale: non-scale tokens give 0");
    CHECK(s2nmc::hp_scale_of("r70;h4") == 10000 && s2nmc::hp_scale_of("h4") == 10000, "cpscale: header with h4 scales 10000");
    CHECK(s2nmc::hp_scale_of("r70") == 100 && s2nmc::hp_scale_of("") == 100, "cpscale: header without h token keeps percent scale 100");
    CHECK(std::fabs(s2nmc::r_tok("r70.5") - 70.5) < 1e-9 && s2nmc::r_tok("h4") == 0.0, "cpscale: r token parses the radius, others give 0");

    CHECK(s2nmc::hp_want(53, 10000, 10000) == 53 && s2nmc::hp_want(1, 10000, 10000) == 1, "cpscale: scale 10000 hp 53 -> 53, hp 1 -> 1");
    CHECK(s2nmc::hp_want(53, 100, 100) == 53, "cpscale: scale 100 percent 53 -> 53");
    CHECK(s2nmc::hp_pct(5300, 10000) == 53 && s2nmc::hp_pct(53, 100) == 53, "cpscale: hp_pct maps both scales back to percent");
    CHECK(s2nmc::hp_wire(50.f, 100.f, true) == 5000 && s2nmc::hp_wire(50.f, 100.f, false) == 50, "cpscale: hp_wire scaled and percent");
    CHECK(s2nmc::hp_wire(5.f, 0.f, true) == 0, "cpscale: hp_wire with no max health is 0");

    double g = 0;
    CHECK(std::fabs(s2nmc::radius_pick(true, 50, 1.2, 70, g) - 60) < 1e-9, "cpscale: online radius is r*k");
    CHECK(std::fabs(s2nmc::radius_pick(false, 0, 0, 70, g) - 60) < 1e-9, "cpscale: failed read keeps the last good radius");
    double never = 0;
    CHECK(std::fabs(s2nmc::radius_pick(false, 0, 0, 70, never) - 70) < 1e-9, "cpscale: failed read with no good radius falls back to natr 70");

    CHECK(!cgcmc::live_ok(50.f, 1.f) && !cgcmc::live_ok(500000.f, 1.f), "cpscale: live radius below 1000 or above 100000 is rejected");
    CHECK(!cgcmc::live_ok(5000.f, 3.f) && cgcmc::live_ok(5000.f, 1.5f), "cpscale: live factor outside 1..2 is rejected");

    s2nmc::Chunk c;
    c.ok = false;
    c.noData = true;
    c.uid = 42;
    std::vector<const s2nmc::Chunk*> ord{&c};
    int ls = 0;
    std::vector<int*> lookSent{&ls};
    s2nmc::ViewSt vs;
    s2nmc::LookCfg lc;
    s2nmc::Out st;
    std::string out = s2nmc::assemble("1", ord, lookSent, vs, lc, 0, std::string(), std::string(), st);
    CHECK(out == "#NN:t1;l42", "cpscale: a listed NPC with no readable location goes into its own l token, not u (%s)", out.c_str());
    out = s2nmc::assemble("1", ord, lookSent, vs, lc, 0, "u5,6", std::string(), st);
    CHECK(out == "#NN:t1;u5,6;l42", "cpscale: u keeps its own meaning next to the l token (%s)", out.c_str());
    out = s2nmc::assemble("1", ord, lookSent, vs, lc, 0, std::string(), std::string(), st, "h4");
    CHECK(out == "#NN:t1;h4;l42", "cpscale: h4 header rides along with a real token (%s)", out.c_str());
    s2nmc::Chunk d;
    d.ok = false;
    d.uid = 43;
    ord[0] = &d;
    out = s2nmc::assemble("1", ord, lookSent, vs, lc, 0, std::string(), std::string(), st);
    CHECK(out.empty(), "cpscale: a dropped chunk without noData is not sent (%s)", out.c_str());

    std::vector<const s2nmc::Chunk*> none0;
    std::vector<int*> noneL;
    CHECK(s2nmc::assemble("1", none0, noneL, vs, lc, 0, std::string(), std::string(), st, "h4").empty(), "cpscale: natrhost=0 with natcphp=1 and no rows sends no header-only packet");
    CHECK(s2nmc::assemble("1", none0, noneL, vs, lc, 0, std::string(), std::string(), st, "r70;h4") == "#NN:t1;r70;h4;", "cpscale: the r header alone still sends (old rule)");
    CHECK(s2nmc::assemble("1", none0, noneL, vs, lc, 0, std::string(), std::string(), st, "r70") == "#NN:t1;r70;", "cpscale: r only header sends as before");
    CHECK(s2nmc::assemble("1", none0, noneL, vs, lc, 0, std::string(), std::string(), st).empty(), "cpscale: no header no rows sends nothing");

    s2nmc::LostSeen seen;
    seen.add(5, 42);
    CHECK(seen.has(5, 42) && !seen.has(5, 43), "cpscale: lost uid of the current snapshot is listed");
    CHECK(!seen.has(6, 42), "cpscale: lost uid of an older snapshot is not listed in the current one");
    seen.add(6, 43);
    CHECK(seen.has(6, 43) && !seen.has(6, 42) && !seen.has(5, 42), "cpscale: a new snapshot drops the old lost uids");
    CHECK(s2nmc::cli_gone(true, true, 5, 6, seen.has(6, 42)), "cpscale: uid in an older no-location token, not the current one, is released at once");
    CHECK(!s2nmc::cli_gone(true, true, 5, 6, seen.has(6, 43)), "cpscale: uid in the current no-location token is kept");
    CHECK(s2nmc::lost_tok("").empty() && s2nmc::lost_tok("4,5") == "l4,5", "cpscale: l token letter");
}

static void t_far()
{
    const double live = 10070, dead = 4500;
    CHECK(s2nmc::far_keep_r(false, live, dead) == live * 0.9, "far: a live copy is made inside 0.9 x the A-Life radius");
    CHECK(s2nmc::far_keep_r(true, live, dead) == dead, "far: a corpse copy is made inside natfardead");
    CHECK(s2nmc::far_out(true, 8006, s2nmc::far_keep_r(true, live, dead)), "far: a corpse 80 m from the client player is out of range (pair 2 uid 3203)");
    CHECK(s2nmc::far_out(true, 11093, s2nmc::far_keep_r(false, live, dead)), "far: a live copy 110 m away is out of range (uid 3215)");
    CHECK(!s2nmc::far_out(true, 500, s2nmc::far_keep_r(true, live, dead)), "far: a corpse 5 m away is in range");
    CHECK(!s2nmc::far_out(true, 4500, dead), "far: on the radius still counts as in range");
    CHECK(!s2nmc::far_out(true, -1, dead), "far: unknown player position never blocks");
    CHECK(!s2nmc::far_out(false, 11093, dead), "far: natfar=0 old rule, nothing is out of range");
    CHECK(s2nmc::dead_row_copy(true, false, s2nmc::far_had_body(true, true, true)), "far: a dead row whose copy was lost far away is copied again");
    CHECK(!s2nmc::dead_row_copy(true, false, s2nmc::far_had_body(true, true, false)), "far: a dead row whose copy was lost near is not copied again (natlostfix rule kept)");
    CHECK(!s2nmc::dead_row_copy(true, false, s2nmc::far_had_body(false, true, true)), "far: natfar=0 keeps the old dead row rule");
    CHECK(s2nmc::dead_row_copy(true, false, s2nmc::far_had_body(true, false, false)), "far: a dead row that never had a copy is copied (natfd)");
    CHECK(s2nmc::host_dead_row(true, true, 300, 30), "far: the host keeps listing a corpse past natdead while its actor exists");
    CHECK(!s2nmc::host_dead_row(false, true, 31, 30), "far: natfar=0 host drops a corpse after natdead");
    CHECK(s2nmc::host_dead_row(false, true, 30, 30) && s2nmc::host_dead_row(false, false, 1e9, 30), "far: inside natdead or alive, listed as before");
    struct Row { bool dead; double deadAt; };
    std::unordered_map<uintptr_t, Row> rows = {{1, {true, 100}}, {2, {false, 0}}, {3, {true, 125}}};
    std::vector<s2nmc::Item> items = {{1, 600, 0}, {2, 300, 0}, {3, 900, 0}};
    double now = 131;
    for (int on = 0; on <= 1; ++on)
    {
        auto drop = [&](uintptr_t k) { auto it = rows.find(k); return it != rows.end() && s2nmc::host_row_drop(on != 0, it->second.dead, now - it->second.deadAt, 30); };
        auto pk = s2nmc::pick({{0, 0}}, items, 7000, 1.0, 8, {}, drop);
        std::unordered_set<uintptr_t> got(pk.all.begin(), pk.all.end());
        if (on) CHECK(got.size() == 3 && got.count(1), "far: natfar=1 the mc pick keeps a corpse listed 31 s after death (pair 2 uid 3154 was dropped at 30 s by the mc drop rule)");
        else CHECK(got.size() == 2 && !got.count(1) && got.count(3), "far: natfar=0 the mc pick drops a corpse after natdead and keeps one inside it");
    }
}

static void t_deadall()
{
    std::vector<FdPawn> old = {{9, -100, -50, 400, 0, false}};
    CHECK(fd_listed(true, old, 9, 5, {}, true), "deadall: a corpse present at the first scan (hot reload, save load) is listed");
    CHECK(!fd_listed(true, old, 9, 5, {}, false), "deadall: natdeadall=0 keeps the old rule");
    std::vector<FdPawn> walk = {{13, -100, -50, 9000, 0, false}};
    CHECK(fd_listed(true, walk, 13, 6, {{0, 0}, {2.0, 2500}}, true), "deadall: an old corpse walked up to is listed");
    std::vector<FdPawn> tele = {{14, 2.6, -50, 50400, 0, false}};
    CHECK(fd_listed(true, tele, 14, 6, {{0, 0}, {3.0, 50000}}, true), "deadall: corpses around a teleport target are listed");
    std::vector<FdPawn> quest = {{11, -100, -50, 400, 0, true}};
    CHECK(!fd_listed(true, quest, 11, 5, {}, true), "deadall: a quest body is still excluded");
    std::vector<FdPawn> outR = {{15, -100, -50, 9000, 0, false}};
    CHECK(!fd_listed(true, outR, 15, 5, {}, true), "deadall: a corpse outside R is not listed");
    {
        s2nmc::FreshDead fd;
        fd.all = true;
        std::unordered_set<uintptr_t> listed;
        int perScan[12] = {};
        for (int t = 0; t < 12; ++t)
        {
            fd.begin(t, 0);
            for (uintptr_t k = 1; k <= 20; ++k) fd.seen(k, false, t);
            for (uintptr_t k = 1; k <= 20; ++k)
                if (!listed.count(k) && fd.dead_ok(k, t, false)) { listed.insert(k); ++perScan[t]; }
            fd.end(t);
        }
        bool two = true;
        for (int t = 0; t < 10; ++t) two = two && perScan[t] == 2;
        CHECK(two && perScan[10] == 0 && listed.size() == 20 && fd.nAll == 20, "deadall: 20 old corpses are listed 2 per scan, all 20 after 10 scans (scan0=%d scan9=%d total=%zu nAll=%d)", perScan[0], perScan[9], listed.size(), fd.nAll);
    }
    {
        s2nmc::FreshDead fd;
        fd.all = true;
        fd.begin(0, 0); fd.seen(1, false, 0);
        CHECK(fd.dead_ok(1, 0, false), "deadall: accepted on the first scan");
        fd.end(0);
        fd.begin(1, 0); fd.seen(1, false, 1);
        CHECK(!fd.dead_ok(1, 1, false), "deadall: a dropped dead row does not come back (held rule kept)");
        fd.end(1);
        fd.left(1, false);
        fd.begin(2, 0); fd.seen(1, false, 2);
        CHECK(fd.dead_ok(1, 2, false), "deadall: a corpse whose row left the list (every player walked away) is listed again on return");
        fd.end(2);
        fd.left(1, true);
        fd.begin(3, 0); fd.seen(1, false, 3);
        CHECK(!fd.dead_ok(1, 3, false), "deadall: a corpse row aged out by natdead (natfar=0) stays out, no 30 s flapping");
        fd.end(3);
    }
    {
        s2nmc::FreshDead fd;
        fd.all = true;
        fd.begin(0, 0); fd.seen(5, true, 0); fd.end(0);
        fd.left(5, true);
        fd.begin(40, 0); fd.seen(5, false, 40);
        CHECK(!fd.dead_ok(5, 40, false), "deadall: a row listed alive that died and aged out (natfar=0) is not listed again (no flapping)");
        fd.end(40);
    }
    {
        s2nmc::FreshDead fd;
        fd.begin(0, 0); fd.seen(1, true, 0);
        CHECK(fd.dead_ok(1, 0.5, false), "deadall off: a fresh death is listed");
        fd.end(0);
        fd.left(1, false);
        CHECK(fd.held.count(1) == 1, "deadall off: leaving the list keeps the old held rule");
    }
    {
        std::vector<s2nmc::Pt> cs = {{0, 0}, {3000, 0}};
        std::vector<s2nmc::Item> items = {{1, 1500, 0}};
        std::unordered_set<uintptr_t> fdk = {1};
        auto pk = s2nmc::pick(cs, items, 7000, 1.0, 8, {}, nullptr, &fdk);
        CHECK(pk.per.size() == 2 && pk.per[0].size() == 1 && pk.per[1].size() == 1 && pk.all.size() == 1, "deadall: an fd row reaches both player views (late join, 5 players)");
    }
}

static void t_hotdead()
{
    CHECK(s2nmc::hot_dead_hand(true, true, 2, true), "hotdead: a dead spawned copy with a body is handed to the next generation");
    CHECK(!s2nmc::hot_dead_hand(false, true, 2, true), "hotdead: nathotkeep=0 hands nothing over");
    CHECK(!s2nmc::hot_dead_hand(true, false, 2, true), "hotdead: a live copy is never handed over");
    CHECK(!s2nmc::hot_dead_hand(true, true, 1, true), "hotdead: an adopted local body is never handed over");
    CHECK(!s2nmc::hot_dead_hand(true, true, 2, false), "hotdead: a copy whose body is gone is not handed over");
    std::vector<s2nmc::HotDead> v = {{10, 2011, 47297, 5, 0x22703087220ull}, {11, 2012, 1, 2, 16}};
    std::string s = s2nmc::hot_dead_pack(v);
    auto u = s2nmc::hot_dead_unpack(s);
    bool same = u.size() == 2;
    for (size_t i = 0; same && i < u.size(); ++i)
        same = u[i].uid == v[i].uid && u[i].luid == v[i].luid && u[i].idx == v[i].idx && u[i].serial == v[i].serial && u[i].raw == v[i].raw;
    CHECK(same, "hotdead: pack then unpack keeps uid, uid_local, index, serial and address (n=%zu)", u.size());
    auto bad = s2nmc::hot_dead_unpack("v1\nbad line\n10\t2011\t1\t2\t16\n\n7\t8\tx\t1\t2");
    CHECK(bad.size() == 1 && bad[0].uid == 10 && bad[0].raw == 16, "hotdead: a malformed line is dropped, a good one kept (n=%zu)", bad.size());
    CHECK(s2nmc::hot_dead_unpack("").empty() && s2nmc::hot_dead_unpack("v2\n1\t2\t3\t4\t5").empty(), "hotdead: empty or unknown version gives nothing");
    std::unordered_map<int32_t, s2nmc::HotDead> m;
    for (auto& h : u) m[h.uid] = h;
    CHECK(s2nmc::hot_dead_due(true, 100.0, 100.0 + 29.0) == false && s2nmc::hot_dead_due(true, 100.0, 100.0 + 31.0), "hotdead: a kept body whose row never came is discarded after 30 s");
    CHECK(!s2nmc::hot_dead_due(false, 100.0, 200.0), "hotdead: nothing to discard when nothing was taken");
}

static void t_deadkeep()
{
    std::vector<std::pair<bool, uint64_t>> v = {{true, 5}, {false, 6}, {true, 7}, {true, 0}};
    std::vector<uint64_t> out;
    int n = s2nmc::dead_keep(true, v, out);
    CHECK(n == 2 && out.size() == 2 && out[0] == 5 && out[1] == 7, "deadkeep: only dead copies with a body join the stock-cleanup keep list (n=%d)", n);
    out.clear();
    CHECK(s2nmc::dead_keep(false, v, out) == 0 && out.empty(), "deadkeep: natdeadkeep=0 adds nothing");
}

static void t_deadpos()
{
    CHECK(s2nmc::pick_garbage(true, false) && s2nmc::pick_garbage(false, true) && !s2nmc::pick_garbage(false, false), "deadpos: a Garbage or Unreachable local actor is no local body (B1 re-copy refused at client.log:3384)");
    CHECK(s2nmc::dead_corpse_ok(true, 100, 88) && s2nmc::dead_corpse_ok(true, 351, 88), "deadpos: a corpse within its body length (4 x 88 cm) stays");
    CHECK(!s2nmc::dead_corpse_ok(true, 353, 88) && !s2nmc::dead_corpse_ok(true, 3500, 88), "deadpos: a corpse farther than its body length is re-made at the host corpse (B5)");
    CHECK(s2nmc::dead_corpse_ok(false, 3500, 88), "deadpos: natdeadpos=0 keeps the old rule");
    CHECK(!s2nmc::released_dead_visible(3, true) && !s2nmc::released_dead_visible(4, true) && s2nmc::released_dead_visible(4, false), "deadpos: a released dead b162/b169 copy is hidden, a live one stays visible");
    s2nmc::Pt a{10, 20}, p{15, 25};
    CHECK(s2nmc::dead_row_pos(true, a, p, true).x == 15 && s2nmc::dead_row_pos(true, a, p, false).x == 10 && s2nmc::dead_row_pos(false, a, p, true).x == 10, "deadpos: a dead row carries the pelvis, a live row the actor");
}

int main()
{
    t_deadpos();
    t_hotdead();
    t_deadkeep();
    t_deadall();
    t_far();
    t_cpscale();
    t_fd();
    t_fd_review();
    t_fdfull();
    t_qg();
    t_pick();
    t_hdr();
    t_vend();
    t_assemble();
    t_aggro();
    t_bandwidth();
    t_secat();
    std::printf("END ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
