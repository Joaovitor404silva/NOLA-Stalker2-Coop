#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <polyhook2/Detour/x64Detour.hpp>
#include "s2x_hot.h"

#include "s2m_lshold_logic.h"
#include "s2m_oodle.h"
#include "s2m_strip.h"
#include "s2m_wpkg.h"
#include "s2m_wsplice.h"
#include "s2r_net.h"
#include "s2r_x0.h"
#include "s2x.h"

extern "C" int s2m_load_begins(char* guid33);
extern "C" bool s2n_join_api_want(const std::wstring* br, const std::string* id);
extern "C" bool s2n_join_api_in(const std::wstring* br, const std::string* id, std::string* state, std::string* detail);

namespace
{
    uint8_t* exe() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }

    bool rd_seh(const void* p, void* out, size_t n)
    {
        __try { std::memcpy(out, p, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool eq_seh(const uint8_t* p, const uint8_t* b, size_t n)
    {
        __try { return std::memcmp(p, b, n) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    const uintptr_t kGate = 0x389e3b8;
    const uintptr_t kMalloc = 0x23cfda2;
    const uintptr_t kFree = 0x1fedb86;
    const uintptr_t kLoadDelegateVt = 0x8eb3030;
    const uintptr_t kLoadDelegateVt2 = 0x8eba5c0;
    const uint8_t kGateA[] = {0x56, 0x48, 0x83, 0xec, 0x40, 0x4c, 0x89, 0xc6, 0x48, 0x8b, 0x05};
    const uint8_t kGateB[] = {0x48, 0x31, 0xe0, 0x48, 0x89, 0x44, 0x24, 0x38, 0x83, 0x79, 0x08, 0x00, 0x74, 0x32, 0x48, 0x8b, 0x09, 0x48, 0x85, 0xc9, 0x74, 0x2a, 0x48, 0x8b, 0x06};
    const uint8_t kMallocSig[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x78};
    const uint8_t kFreeSig[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x58, 0x48, 0x85, 0xc9};

    struct Del
    {
        void* data;
        int32_t size;
        int32_t pad;
    };

    struct Arr
    {
        uint8_t* data;
        int32_t num;
        int32_t max;
    };

    using GateFn = void (*)(Del*, uint64_t, Arr*);
    using DtorFn = void* (*)(void*, int);
    using MallocFn = void* (*)(uint64_t, uint32_t);
    using FreeFn = void (*)(void*);

    x0::Flag F_lshold("lshold");
    x0::Flag F_ldstrip("ldstrip");

    uint64_t g_tramp = 0;
    PLH::x64Detour* g_det = nullptr;
    bool g_tried = false;
    bool g_pass = false;
    bool g_joined = false;
    int g_lbSeen = 0;
    std::string g_sess;
    int g_n = 0;

    struct Job
    {
        std::mutex m;
        int st = 0;
        std::string raw, detail;
    };

    struct Hold
    {
        bool on = false;
        Del del{};
        uint64_t code = 0;
        Arr raw{};
        std::string id;
        double t0 = 0, logAt = 0, rxAt = 0, pollAt = 0;
        uint64_t rx = 0;
        bool splicing = false;
        std::string ap, cid;
        int kh = -1, hs = -1;
        bool fresh = false;
    };

    Hold g_h;
    Job g_job;
    std::string g_last = "none";
    int g_stripN = 0, g_stripBodies = 0;
    std::string g_stripLast = "none";

    void strip_load(Arr* raw)
    {
        std::string in(reinterpret_cast<const char*>(raw->data), (size_t)raw->num);
        s2m_strip::Report rep;
        std::string err;
        int rc = -1;
        auto t0 = GetTickCount64();
        try { rc = s2m_strip::strip(in, rep, err); }
        catch (const std::exception& x) { err = std::string("exception: ") + x.what(); }
        catch (...) { err = "exception"; }
        unsigned long long ms = GetTickCount64() - t0;
        ++g_stripN;
        if (rc != s2m_strip::kOk)
        {
            g_stripLast = "refused " + std::to_string(rc);
            x0::logf("NOLA ldstrip: save load not cleaned (strip code %d: %s, %llu ms): the save loads unchanged and the world sweep handles any saved co-op bodies", rc, err.c_str(), ms);
            return;
        }
        if (rep.removed[0] == 0 || rep.raw.empty())
        {
            g_stripLast = "clean";
            x0::logf("NOLA ldstrip: save load has no co-op bodies (%llu ms)", ms);
            return;
        }
        void* p = reinterpret_cast<MallocFn>(exe() + kMalloc)((uint64_t)rep.raw.size(), 0);
        if (!p)
        {
            g_stripLast = "malloc failed";
            x0::log("NOLA ldstrip: FMemory::Malloc of the cleaned save failed: the save loads unchanged");
            return;
        }
        std::memcpy(p, rep.raw.data(), rep.raw.size());
        reinterpret_cast<FreeFn>(exe() + kFree)(raw->data);
        raw->data = static_cast<uint8_t*>(p);
        raw->num = (int32_t)rep.raw.size();
        raw->max = (int32_t)rep.raw.size();
        g_stripBodies += (int)rep.removed[0];
        g_stripLast = rep.summary();
        x0::logf("NOLA ldstrip: saved co-op bodies (Nola_ or old S2Coop_ units) removed from the save before it loads: %s, %zu -> %zu B raw, %llu ms", rep.summary().c_str(), in.size(),
                 rep.raw.size(), ms);
    }

    bool relay_rx(uint64_t& rx)
    {
        s2r::Stats st;
        if (!s2r::x0_stats(st)) return false;
        rx = st.tot_recv;
        return true;
    }

    std::string sess_key() { return std::to_string(s2r::x0_session_mode()) + "|" + s2r::x0_session_peer(); }

    void track_joined()
    {
        std::string k = sess_key();
        if (!x0::is_client() || !s2r::x0_active() || k != g_sess)
        {
            if (g_joined) x0::logf("NOLA lshold: session changed (%s): the next client load waits for the host world again", k.c_str());
            g_joined = false;
            g_sess = k;
            if (!x0::is_client() || !s2r::x0_active()) return;
        }
        if (!g_joined && !g_h.on && x0::peer().state == x0::Peer::up && x0::first_of(L"BP_Stalker2Character_C").valid())
        {
            g_joined = true;
            x0::log("NOLA lshold: client is in the host's session: later loads keep the client's save bytes and wait at the end of the loading screen for the host world (lsgate)");
        }
    }

    void del_free(Del* d)
    {
        auto fr = reinterpret_cast<FreeFn>(exe() + kFree);
        if (d->size != 0 && d->data)
        {
            auto vt = *reinterpret_cast<void (***)(void*, int)>(d->data);
            vt[0](d->data, 0);
        }
        if (d->data) fr(d->data);
        d->data = nullptr;
        d->size = 0;
    }

    void resume(Arr a, const char* how)
    {
        uint8_t obj[0x40] = {};
        std::memcpy(obj + 8, &g_h.del, sizeof(Del));
        obj[0x18] = (uint8_t)(g_h.code & 0xff);
        Del* d = reinterpret_cast<Del*>(obj + 8);
        double held = x0::now() - g_h.t0;
        g_h.on = false;
        g_h.del = Del{};
        g_h.raw = Arr{};
        x0::logf("NOLA lshold: load #%d released after %.1f s (%s, %d B raw)", g_n, held, how, a.num);
        g_pass = true;
        reinterpret_cast<GateFn>(g_tramp)(d, g_h.code, &a);
        g_pass = false;
        del_free(d);
        x0::logf("NOLA lshold: load #%d deserialised", g_n);
    }

    void release_stock(const std::string& why)
    {
        std::wstring br = x0::bridge_dir();
        std::string none;
        s2n_join_api_want(&br, &none);
        g_last = "stock: " + why;
        x0::logf("NOLA lshold: %s: the client's own save loads unchanged and the host's world follows in the world (sxjoin)", why.c_str());
        resume(g_h.raw, "own save");
    }

    void apply(const std::string& raw, const std::string& detail)
    {
        std::wstring br = x0::bridge_dir();
        void* p = reinterpret_cast<MallocFn>(exe() + kMalloc)((uint64_t)raw.size(), 0);
        if (!p) { release_stock("FMemory::Malloc of the spliced save failed"); return; }
        std::memcpy(p, raw.data(), raw.size());
        if (!s2m_wpkg::write_atomic(br + L"client_applied.txt", g_h.ap))
        {
            reinterpret_cast<FreeFn>(exe() + kFree)(p);
            release_stock("client_applied.txt not written");
            return;
        }
        reinterpret_cast<FreeFn>(exe() + kFree)(g_h.raw.data);
        Arr a{static_cast<uint8_t*>(p), (int32_t)raw.size(), (int32_t)raw.size()};
        g_last = "host world " + g_h.id;
        x0::logf("NOLA lshold: host world of save #%d (kh=%d, campaign %s) %s: %s", g_h.hs, g_h.kh, g_h.cid.c_str(),
                 g_h.fresh ? "loads with the host save's player (the client save is from another campaign)" : "loads with this client's own character", detail.c_str());
        resume(a, g_h.fresh ? "host world" : "host world + own character");
    }

    void work(std::string rc, std::string world, bool fresh, bool own, bool strip)
    {
        std::string raw, err, d;
        s2m_savefile::Opt o;
        o.keepPos = !own;
        o.vitals = true;
        o.strip = strip;
        s2m_wsplice::Out out;
        int st = 2;
        try
        {
            auto t0 = GetTickCount64();
            if (s2m_wsplice::run_raw(rc, world, s2m_oodle::codec(), o, fresh, out, raw, err))
            {
                st = 1;
                char b[400];
                std::snprintf(b, sizeof b, "world %zu B, client %zu B -> %zu B raw, %zu items, money %u, %s, %llu ms", out.worldRaw, out.clientRaw, out.outRaw, out.items, out.money,
                              out.strip.empty() ? "not stripped" : out.strip.c_str(), (unsigned long long)(GetTickCount64() - t0));
                d = b;
            }
            else d = err;
        }
        catch (const std::exception& x) { d = std::string("exception: ") + x.what(); }
        catch (...) { d = "exception"; }
        std::lock_guard<std::mutex> l(g_job.m);
        g_job.raw = std::move(raw);
        g_job.detail = d;
        g_job.st = st;
    }

    void take_pkg()
    {
        std::wstring br = x0::bridge_dir();
        std::string blob, e;
        s2m_wpkg::Pkg k;
        if (!s2m_wpkg::read_file(br + L"sx_in.pkg", blob) || !s2m_wpkg::parse(blob, k, e) || k.id != g_h.id)
        {
            release_stock("host world package unreadable (" + (e.empty() ? std::string("sx_in.pkg") : e) + ")");
            return;
        }
        DeleteFileW((br + L"sx_in.pkg").c_str());
        std::string none;
        s2n_join_api_want(&br, &none);
        std::string ape;
        bool apOk = x0::fi("lscrlf", 1) == 1 ? x0::read_text(br + L"client_applied.txt", ape) : s2m_wpkg::read_file(br + L"client_applied.txt", ape);
        if (!apOk) ape.clear();
        lsh::Ap a = lsh::parse_ap(ape);
        g_h.fresh = lsh::is_other(a, k.cid, x0::fi("cswc", 1)) && x0::fi("sxscope", 1) == 1;
        g_h.ap = k.ap;
        g_h.cid = k.cid.empty() ? "-" : k.cid;
        g_h.kh = k.kh;
        g_h.hs = k.hs;
        if (!s2m_oodle::ready(e)) { release_stock("Oodle not ready: " + e); return; }
        std::string rc(reinterpret_cast<const char*>(g_h.raw.data), (size_t)g_h.raw.num);
        {
            std::lock_guard<std::mutex> l(g_job.m);
            g_job.st = 0;
            g_job.raw.clear();
            g_job.detail.clear();
        }
        g_h.splicing = true;
        x0::logf("NOLA lshold: host world package %s received (save #%d, kh=%d, campaign %s, %zu B); client record: campaign %s, %d host steps: %s", k.id.c_str(), k.hs, k.kh,
                 g_h.cid.c_str(), k.data.size(), a.cid ? a.cid->c_str() : "-", a.n, g_h.fresh ? "other campaign, the host save's player is used" : "splice with this client's character");
        std::thread(work, std::move(rc), std::move(k.data), g_h.fresh, x0::fi("wspos", 1) == 1, x0::fi("wstrip", 1) == 1).detach();
    }

    void tick()
    {
        s2m_savefile::g_acceptU21 = x0::fi("savebe", 1) == 1;
        if (!g_h.on) { track_joined(); return; }
        double t = x0::now();
        if (g_h.splicing)
        {
            int st = 0;
            std::string raw, d;
            {
                std::lock_guard<std::mutex> l(g_job.m);
                st = g_job.st;
                if (st) { raw.swap(g_job.raw); d = g_job.detail; g_job.st = 0; }
            }
            if (!st) return;
            g_h.splicing = false;
            if (st == 1) apply(raw, d);
            else release_stock("host world not spliced: " + d);
            return;
        }
        uint64_t rx = 0;
        if (relay_rx(rx) && rx != g_h.rx) { g_h.rx = rx; g_h.rxAt = t; }
        lsh::Step s = lsh::step(t, g_h.t0, x0::fd("lsholdwait", 120), g_h.rxAt, 20);
        if (s == lsh::Step::timeout) { release_stock("no host world after " + std::to_string((int)(t - g_h.t0)) + " s"); return; }
        if (s == lsh::Step::silent) { release_stock("no data from the host for 20 s"); return; }
        if (t < g_h.pollAt) return;
        g_h.pollAt = t + 0.5;
        std::wstring br = x0::bridge_dir();
        std::string st, d;
        if (!s2n_join_api_in(&br, &g_h.id, &st, &d) && st.empty()) st = "none";
        if (st == "ready") { take_pkg(); return; }
        if (t >= g_h.logAt)
        {
            g_h.logAt = t + 10;
            x0::logf("NOLA lshold: load #%d held in the loading screen for the host world %s (%s %s, %.0f s)", g_n, g_h.id.c_str(), st.c_str(), d.c_str(), t - g_h.t0);
        }
    }

    void gate_detour(Del* del, uint64_t code, Arr* raw)
    {
        if (g_pass || g_h.on || !del || !raw) { reinterpret_cast<GateFn>(g_tramp)(del, code, raw); return; }
        lsh::Gate g;
        g.flag = F_lshold.i();
        g.sxjoin = x0::fi("sxjoin", 0);
        g.wpkg = x0::fi("wpkg", 1);
        g.client = x0::is_client();
        g.relay = s2r::x0_active();
        g.joined = g_joined;
        void* vt = nullptr;
        g.loadDelegate = del->size != 0 && del->data && rd_seh(del->data, &vt, 8) && (vt == exe() + kLoadDelegateVt || vt == exe() + kLoadDelegateVt2);
        int lb = s2m_load_begins(nullptr);
        g.loadBegan = lb > g_lbSeen;
        if (g.loadDelegate) g_lbSeen = lb;
        g.code = (uint8_t)(code & 0xff);
        g.rawNum = raw->data && raw->num > 0 ? (uint64_t)raw->num : 0;
        if (g.rawNum >= 4) rd_seh(raw->data, &g.rawHead, 4);
        if (lsh::strip_wanted(g, F_ldstrip.i()))
        {
            strip_load(raw);
            g.rawNum = raw->data && raw->num > 0 ? (uint64_t)raw->num : 0;
        }
        lsh::Why y = lsh::decide(g);
        if (y != lsh::Why::hold)
        {
            if (y != lsh::Why::off && y != lsh::Why::host && y != lsh::Why::notload)
                x0::logf("NOLA lshold: save load not held (%s, %llu B raw)", lsh::why_name(y), (unsigned long long)g.rawNum);
            reinterpret_cast<GateFn>(g_tramp)(del, code, raw);
            return;
        }
        ++g_n;
        g_h = Hold{};
        g_h.on = true;
        g_h.del = *del;
        del->data = nullptr;
        del->size = 0;
        g_h.code = code;
        g_h.raw = *raw;
        raw->data = nullptr;
        raw->num = 0;
        raw->max = 0;
        g_h.t0 = x0::now();
        g_h.rxAt = g_h.t0;
        relay_rx(g_h.rx);
        g_h.id = s2m_wpkg::world_id(lsh::epoch_ms());
        std::wstring br = x0::bridge_dir();
        bool ok = s2n_join_api_want(&br, &g_h.id);
        g_last = "holding " + g_h.id;
        x0::logf("NOLA lshold: load #%d: the client's save (%d B raw) waits in the loading screen for the host world (request %s, sx_want ok=%s)", g_n, g_h.raw.num, g_h.id.c_str(),
                 ok ? "true" : "false");
    }

    void install()
    {
        if (g_det || g_tried) return;
        if (!(x0::is_client() && F_lshold.on()) && !F_ldstrip.on()) return;
        g_tried = true;
        g_lbSeen = s2m_load_begins(nullptr);
        uint8_t* b = exe();
        if (!eq_seh(b + kGate, kGateA, sizeof kGateA) || !eq_seh(b + kGate + 15, kGateB, sizeof kGateB) ||
            !eq_seh(b + kMalloc, kMallocSig, sizeof kMallocSig) || !eq_seh(b + kFree, kFreeSig, sizeof kFreeSig))
        {
            x0::log("NOLA lshold: save load gate exe+0x389e3b8 / FMemory with unexpected bytes: load-screen hold NOT installed");
            return;
        }
        g_det = new x0::hot::Detour((uint64_t)(b + kGate), (uint64_t)&gate_detour, &g_tramp);
        if (!g_det->hook())
        {
            delete g_det;
            g_det = nullptr;
            g_tramp = 0;
            x0::log("NOLA lshold: detour of the save load gate exe+0x389e3b8 failed: load-screen hold NOT installed");
            return;
        }
        x0::logf("NOLA lshold: save load gate exe+0x389e3b8 hooked (client hold %s, co-op bodies stripped from loaded saves %s)", x0::is_client() && F_lshold.on() ? "on" : "off",
                 F_ldstrip.on() ? "on" : "off");
    }

    void init()
    {
        x0::every(x0::Mod::save, "lsholdInstall", x0::Rate::t1000, [] { install(); });
        x0::every(x0::Mod::save, "lshold", x0::Rate::t100, [] {
            if (g_det) tick();
        });
        x0::status(x0::Mod::save, [] {
            return std::string("lshold: ") + (g_det ? "hooked" : "off") + " joined=" + (g_joined ? "1" : "0") + " loads=" + std::to_string(g_n) + " last=" + g_last + " ldstrip=" + std::to_string(g_stripN) + "/" + std::to_string(g_stripBodies) + " " + g_stripLast;
        });
    }

    ::x0::ModReg g_reg(::x0::Mod::save, "lshold", &init);
}
