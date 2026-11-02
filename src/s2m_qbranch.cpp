#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <polyhook2/Detour/x64Detour.hpp>
#include "s2x_hot.h"

#include "s2m_qbranch.h"
#include "s2m_quest.h"
#include "s2x.h"

#pragma intrinsic(_ReturnAddress)

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

    const uint32_t kEnd = 0x7a8b56;
    const uint32_t kPin = 0x7aa720;
    const uint32_t kAns = 0x2ef4ad6;
    const uint32_t kShow = 0x2f98a44;
    const uint32_t kMake = 0x235663c;
    const uint32_t kRel = 0x1ffefc6;
    const uint32_t kSm = 0xa2c8610;
    const uint32_t kDlg = 0xa2c85c8;
    const uint32_t kHash = 0x9ecfdc0;
    const uint32_t kPool = 0xb10f588;
    const uint32_t kPoolNeg = 0xb12f618;
    const uint32_t kCallFin = 0x7a9520;
    const uint32_t kCallDlg1 = 0x106184a;
    const uint32_t kCallDlg2 = 0x1061e53;
    const uint32_t kDlgCur = 0x350;
    const uint32_t kWSid = 0x300;

    using EndFn = void (*)(uint64_t, uint32_t*, uint32_t*);
    using PinFn = void (*)(uint64_t, uint32_t*, uint32_t*, uint32_t*);
    using AnsFn = void (*)(uint64_t, uint32_t*);
    using ShowFn = void (*)(uint64_t, uint64_t, int*, uint64_t);
    using MakeFn = uint32_t (*)(void*, void*, char, uint32_t);
    using HashFn = uint32_t (*)(const void*, uint64_t, uint32_t);
    using RelFn = void (*)(void*, uint32_t);

    struct Hook
    {
        const char* name;
        uint32_t rva;
        std::vector<uint8_t> pro;
        uint64_t* tramp;
        uint64_t fn;
        PLH::x64Detour* det = nullptr;
    };

    bool hook(Hook& h)
    {
        uint8_t* p = exe() + h.rva;
        if (!eq_seh(p, h.pro.data(), h.pro.size()))
        {
            x0::logf("NOLA qbranch: %s (exe+0x%x) com bytes diferentes do esperado: gancho NAO instalado", h.name, h.rva);
            return false;
        }
        h.det = new x0::hot::Detour((uint64_t)p, h.fn, h.tramp);
        if (!h.det->hook())
        {
            delete h.det;
            h.det = nullptr;
            *h.tramp = 0;
            x0::logf("NOLA qbranch: gancho %s (exe+0x%x) falhou", h.name, h.rva);
            return false;
        }
        return true;
    }

    bool calls(uint32_t at, uint32_t to)
    {
        uint8_t b[5] = {};
        if (!rd_seh(exe() + at, b, 5) || b[0] != 0xe8) return false;
        int32_t rel = 0;
        std::memcpy(&rel, b + 1, 4);
        return at + 5 + rel == to;
    }

    uint64_t glob(uint32_t rva)
    {
        uint64_t v = 0;
        rd_seh(exe() + rva, &v, 8);
        return v;
    }

    bool sid_str(uint32_t h, std::string& out)
    {
        int32_t s = (int32_t)h;
        if (s == 0) return false;
        uint32_t i = s < 0 ? (uint32_t)~s : (uint32_t)s;
        uint8_t* base = exe() + (s < 0 ? kPoolNeg : kPool);
        uint64_t chunk = 0, str = 0;
        int32_t len = 0;
        if (!rd_seh(base + 8ull * (i >> 10), &chunk, 8) || !chunk) return false;
        uint64_t e = chunk + (uint64_t(i & 0x3ff) << 5);
        if (!rd_seh(reinterpret_cast<void*>(e), &str, 8) || !rd_seh(reinterpret_cast<void*>(e + 8), &len, 4)) return false;
        if (!str || len <= 0 || len > 256) return false;
        wchar_t buf[257] = {};
        if (!rd_seh(reinterpret_cast<void*>(str), buf, size_t(len) * sizeof(wchar_t))) return false;
        out.clear();
        for (int k = 0; k < len && buf[k]; ++k)
        {
            if (buf[k] > 0x7e) return false;
            out += (char)buf[k];
        }
        return !out.empty();
    }

    void release(uint32_t* h)
    {
        if (!h) return;
        if (*h) reinterpret_cast<RelFn>(exe() + kRel)(exe() + kPool, *h);
        *h = 0;
    }

    uint32_t make(const std::string& s)
    {
        if (s.empty() || s.size() > 256) return 0;
        uint64_t hf = 0;
        if (!rd_seh(exe() + kHash, &hf, 8) || !hf) return 0;
        std::wstring w(s.begin(), s.end());
        struct
        {
            const wchar_t* p;
            int n;
        } sv{w.c_str(), (int)w.size()};
        uint32_t hs = reinterpret_cast<HashFn>(hf)(w.c_str(), w.size() * 2, 0);
        return reinterpret_cast<MakeFn>(exe() + kMake)(exe() + kPool, &sv, 0, hs);
    }

    std::string lc(std::string s)
    {
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }

    uint64_t cur_dlg()
    {
        uint64_t m = glob(kDlg), d = 0;
        if (m) rd_seh(reinterpret_cast<void*>(m + kDlgCur), &d, 8);
        return d;
    }

    struct Shown
    {
        uint64_t w = 0, dlg = 0;
        uint32_t h = 0;
        std::string s;
        double t = 0;
    };

    uint64_t g_endTr = 0, g_pinTr = 0, g_ansTr = 0, g_showTr = 0;
    std::map<std::string, Shown> g_shown;
    uint32_t g_forceH = 0;
    bool g_ended = false, g_ready = false, g_tried = false;

    bool live() { return x0::on_game_thread() && s2m_quest::qb_on(); }

    void swap_pin(uint32_t* p, const std::string& sub)
    {
        uint32_t h = sub.empty() ? 0 : make(sub);
        if (!sub.empty() && !h) return;
        release(p);
        *p = h;
    }

    thread_local int t_endDepth = 0;
    int g_endCut = 0;

    bool depth_cut(int d)
    {
        if (d < 64) return false;
        x0::Val v = x0::flag("qdepthguard");
        return v.k == x0::Val::nil || v.num() != 0;
    }

    struct DepthGuard
    {
        int& d;
        explicit DepthGuard(int& x) : d(x) { ++d; }
        ~DepthGuard() { --d; }
    };

    void end_detour(uint64_t sm, uint32_t* n, uint32_t* p)
    {
        if (depth_cut(t_endDepth))
        {
            if (++g_endCut <= 5) x0::log("NOLA qbranch: EndQuestNode recursivo cortado na profundidade 64");
            release(n);
            release(p);
            return;
        }
        DepthGuard dg(t_endDepth);
        int r = 0;
        std::string node, pin, sub;
        if (n && p && live() && sid_str(*n, node))
        {
            if (*p) sid_str(*p, pin);
            r = s2m_quest::qb_fire(node, pin, true, sub);
        }
        if (r == 2)
        {
            release(n);
            release(p);
            return;
        }
        if (r == 1) swap_pin(p, sub);
        reinterpret_cast<EndFn>(g_endTr)(sm, n, p);
    }

    thread_local int t_pinDepth = 0;
    int g_pinCut = 0;

    void pin_detour(uint64_t sm, uint32_t* q, uint32_t* n, uint32_t* p)
    {
        if (depth_cut(t_pinDepth))
        {
            if (++g_pinCut <= 5) x0::log("NOLA qbranch: FireQuestNodePin recursivo cortado na profundidade 64");
            release(q);
            release(n);
            release(p);
            return;
        }
        DepthGuard dg(t_pinDepth);
        uint32_t ra = (uint32_t)(reinterpret_cast<uint8_t*>(_ReturnAddress()) - exe());
        if (ra == kCallFin + 5)
        {
            if (g_forceH && n && *n == g_forceH) g_ended = true;
        }
        else if ((ra == kCallDlg1 + 5 || ra == kCallDlg2 + 5) && q && n && p)
        {
            int r = 0;
            std::string node, pin, sub;
            if (live() && sid_str(*n, node))
            {
                if (*p) sid_str(*p, pin);
                r = s2m_quest::qb_fire(node, pin, false, sub);
            }
            if (r == 2)
            {
                release(q);
                release(n);
                release(p);
                return;
            }
            if (r == 1) swap_pin(p, sub);
        }
        reinterpret_cast<PinFn>(g_pinTr)(sm, q, n, p);
    }

    void ans_detour(uint64_t mgr, uint32_t* h)
    {
        std::string ph;
        if (h && *h && live() && sid_str(*h, ph) && !s2m_quest::qb_answer(ph))
        {
            release(h);
            return;
        }
        reinterpret_cast<AnsFn>(g_ansTr)(mgr, h);
    }

    void show_detour(uint64_t w, uint64_t owner, int* e, uint64_t st)
    {
        reinterpret_cast<ShowFn>(g_showTr)(w, owner, e, st);
        uint32_t h = 0;
        std::string ph;
        if (!e || !live() || !rd_seh(e, &h, 4) || !sid_str(h, ph)) return;
        double t = x0::now();
        if (g_shown.size() > 64)
            for (auto it = g_shown.begin(); it != g_shown.end();)
                it = t - it->second.t > 600 ? g_shown.erase(it) : std::next(it);
        Shown& s = g_shown[lc(ph)];
        s.w = w;
        s.h = h;
        s.s = ph;
        s.dlg = cur_dlg();
        s.t = t;
    }

    Hook g_hEnd{"EndQuestNode", kEnd, {0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x40, 0x4c, 0x89, 0xc6, 0x48, 0x89, 0xd7, 0x48, 0x89, 0xcb, 0x48},
                &g_endTr, (uint64_t)&end_detour};
    Hook g_hPin{"FireQuestNodePin", kPin, {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x68, 0x4c, 0x89, 0x4c, 0x24, 0x38, 0x4c, 0x89, 0xc7},
                &g_pinTr, (uint64_t)&pin_detour};
    Hook g_hAns{"DialogAnswerCommit", kAns, {0x56, 0x57, 0x48, 0x83, 0xec, 0x38, 0x48, 0x89, 0xd6, 0x48, 0x8b, 0x05, 0xda, 0x4f, 0xf9, 0x06, 0x48, 0x31, 0xe0, 0x48, 0x89, 0x44, 0x24, 0x30},
                &g_ansTr, (uint64_t)&ans_detour};
    Hook g_hShow{"DialogAnswerShow", kShow, {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x89, 0xce, 0x4c, 0x89, 0x81, 0x68, 0x03, 0x00, 0x00, 0x4c, 0x89, 0x89, 0x70, 0x03, 0x00, 0x00},
                 &g_showTr, (uint64_t)&show_detour};

    const uint8_t kMakePro[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xec, 0xd8, 0x00, 0x00, 0x00, 0x44, 0x89, 0xcd, 0x49, 0x89};
    const uint8_t kRelPro[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x48, 0x85, 0xd2, 0x7e, 0x50};

    void install()
    {
        g_tried = true;
        bool base = eq_seh(exe() + kMake, kMakePro, sizeof kMakePro) && eq_seh(exe() + kRel, kRelPro, sizeof kRelPro) && glob(kHash) != 0;
        bool sites = calls(kCallFin, kPin) && calls(kCallDlg1, kPin) && calls(kCallDlg2, kPin);
        bool end = base && sites && hook(g_hEnd);
        bool pin = end && hook(g_hPin);
        bool ans = base && hook(g_hAns);
        bool show = ans && hook(g_hShow);
        g_ready = end && pin;
        x0::logf("NOLA qbranch: ganchos fim de no=%s pino=%s resposta=%s respostas na tela=%s%s%s", end ? "ok" : "FALHOU", pin ? "ok" : "FALHOU", ans ? "ok" : "FALHOU", show ? "ok" : "FALHOU",
                 base ? "" : "; criador/liberador de SID com bytes diferentes", sites ? "" : "; chamadas do pino com bytes diferentes");
    }

    void init()
    {
        if (!g_tried) install();
    }

    ::x0::ModReg g_qbranch_reg(::x0::Mod::quest, "qbranch", &init);

    const uint32_t kTrigMgr = 0xa2c8678;
    const uint32_t kTrigFind = 0xb92b86;
    const uint32_t kTrigUpd = 0x2220860;
    const uint32_t kPlayerId = 0x9eda140;
    const uint32_t kUpdPlayerCmp = 0x2220a2e;
    const uint32_t kUpdSimpleCmp = 0x2220942;
    const uint8_t kUpdPro[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xEC, 0x78};
    const uint8_t kFindPro[] = {0x56, 0x8B, 0x81, 0xB0, 0x00, 0x00, 0x00, 0x3B, 0x81, 0xDC, 0x00, 0x00, 0x00};
    const uint8_t kSimpleCmp[] = {0x80, 0xBE, 0xC4, 0x00, 0x00, 0x00, 0x00};
    const uint32_t kUnitTrig = 0x221fa8a;
    const uint32_t kTrigRel = 0x222053a;
    const uint32_t kUnitTrigOld = 0x221fb0a;
    const uint32_t kUnitTrigLock = 0x221fd87;
    const uint32_t kUnitTrigCall = 0x221febe;
    const uint32_t kTrigRelDec = 0x22206c6;
    const uint8_t kUnitTrigPro[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0xE8, 0x01, 0x00, 0x00};
    const uint8_t kTrigRelPro[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05};
    const uint8_t kUnitTrigOldB[] = {0xF3, 0x0F, 0x10, 0x82, 0x2C, 0x02, 0x00, 0x00};
    const uint8_t kUnitTrigLockB[] = {0xFF, 0x80, 0xD0, 0x00, 0x00, 0x00};
    const uint8_t kUnitTrigCallB[] = {0xE8, 0x9D, 0x09, 0x00, 0x00};
    const uint8_t kTrigRelDecB[] = {0x8B, 0x83, 0xD0, 0x00, 0x00, 0x00};

    using TrigFindFn = uint64_t (*)(uint64_t, const uint32_t*);
    using TrigUpdFn = void (*)(uint64_t, const uint32_t*, uint64_t, uint64_t);
    using TrigRelFn = void (*)(uint32_t*);

    bool trig_code_ok()
    {
        uint8_t* b = exe();
        uint8_t c[6] = {};
        if (!eq_seh(b + kTrigUpd, kUpdPro, sizeof kUpdPro) || !eq_seh(b + kTrigFind, kFindPro, sizeof kFindPro) ||
            !eq_seh(b + kUpdSimpleCmp, kSimpleCmp, sizeof kSimpleCmp) || !rd_seh(b + kUpdPlayerCmp, c, 6) || c[0] != 0x3B || c[1] != 0x05)
            return false;
        if (!eq_seh(b + kUnitTrig, kUnitTrigPro, sizeof kUnitTrigPro) || !eq_seh(b + kTrigRel, kTrigRelPro, sizeof kTrigRelPro) ||
            !eq_seh(b + kUnitTrigOld, kUnitTrigOldB, sizeof kUnitTrigOldB) || !eq_seh(b + kUnitTrigLock, kUnitTrigLockB, sizeof kUnitTrigLockB) ||
            !eq_seh(b + kUnitTrigCall, kUnitTrigCallB, sizeof kUnitTrigCallB) || !eq_seh(b + kTrigRelDec, kTrigRelDecB, sizeof kTrigRelDecB))
            return false;
        int32_t rel = 0;
        std::memcpy(&rel, c + 2, 4);
        return kUpdPlayerCmp + 6 + rel == kPlayerId;
    }

    struct TeRaw
    {
        uint32_t g[4] = {};
        uint64_t pawn = 0, mgr = 0, unit = 0, trig = 0;
        uint32_t uid = 0, pid = 0;
        int before = 0, mid = 0, after = 0;
        int n0 = 0, n1 = 0, n2 = 0;
        int mode = 0;
    };

    int te_listed(uint64_t trig, uint32_t uid)
    {
        int n = *reinterpret_cast<int*>(trig + 0xa8);
        uint64_t a = *reinterpret_cast<uint64_t*>(trig + 0xa0);
        if (n < 0 || n > 4096 || (n && !a)) return 0;
        for (int i = 0; i < n; ++i)
            if (*reinterpret_cast<uint32_t*>(a + uint64_t(i) * 0x10) == uid) return 1;
        return 0;
    }

    int te_count(uint64_t trig)
    {
        int n = *reinterpret_cast<int*>(trig + 0xa8);
        return n < 0 || n > 4096 ? -1 : n;
    }

    void te_release(const TeRaw& r)
    {
        uint64_t t2 = reinterpret_cast<TrigFindFn>(exe() + kTrigFind)(r.mgr, r.g);
        if (t2 == r.trig)
        {
            uint32_t g[4] = {r.g[0], r.g[1], r.g[2], r.g[3]};
            reinterpret_cast<TrigRelFn>(exe() + kTrigRel)(g);
            return;
        }
        __try
        {
            int* ref = reinterpret_cast<int*>(r.trig + 0xd0);
            if (*ref > 0) --*ref;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void te_call(TeRaw& r)
    {
        float* pos = reinterpret_cast<float*>(r.unit + 0x28);
        const float* c = reinterpret_cast<const float*>(r.trig + 0x10);
        uint8_t* simple = reinterpret_cast<uint8_t*>(r.trig + 0xc4);
        const float p0 = pos[0], p1 = pos[1], p2 = pos[2];
        const uint8_t s0 = *simple;
        ++*reinterpret_cast<int*>(r.trig + 0xd0);
        __try
        {
            __try
            {
                *simple = 1;
                pos[0] = c[0];
                pos[1] = c[1];
                pos[2] = c[2];
                reinterpret_cast<TrigUpdFn>(exe() + kTrigUpd)(r.mgr, r.g, r.trig, r.unit);
            }
            __finally
            {
                pos[0] = p0;
                pos[1] = p1;
                pos[2] = p2;
                *simple = s0;
                r.mid = te_listed(r.trig, r.uid);
                r.n1 = te_count(r.trig);
            }
        }
        __finally
        {
            te_release(r);
        }
    }

    void te_settle(TeRaw& r)
    {
        ++*reinterpret_cast<int*>(r.trig + 0xd0);
        __try
        {
            reinterpret_cast<TrigUpdFn>(exe() + kTrigUpd)(r.mgr, r.g, r.trig, r.unit);
            r.after = te_listed(r.trig, r.uid);
            r.n2 = te_count(r.trig);
        }
        __finally
        {
            te_release(r);
        }
    }

    int te_raw(TeRaw& r)
    {
        __try
        {
            uint8_t* b = exe();
            r.pid = *reinterpret_cast<uint32_t*>(b + kPlayerId);
            r.unit = *reinterpret_cast<uint64_t*>(r.pawn + 0x650);
            if (r.unit < 0x10000) return -3;
            r.uid = *reinterpret_cast<uint32_t*>(r.unit + 0x10);
            if (r.uid != r.pid) return -4;
            r.mgr = *reinterpret_cast<uint64_t*>(b + kTrigMgr);
            if (!r.mgr) return -5;
            r.trig = reinterpret_cast<TrigFindFn>(b + kTrigFind)(r.mgr, r.g);
            if (!r.trig) return -6;
            r.before = te_listed(r.trig, r.uid);
            r.n0 = te_count(r.trig);
            if (r.mode == 2)
            {
                if (!r.before) return 0;
                te_settle(r);
                return r.after ? 3 : 1;
            }
            if (r.before) return 0;
            te_call(r);
            const uint64_t t2 = reinterpret_cast<TrigFindFn>(b + kTrigFind)(r.mgr, r.g);
            if (t2 != r.trig) return 2;
            if (r.mode == 1) return r.mid ? 1 : -7;
            te_settle(r);
            if (!r.mid) return -7;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -8;
        }
    }

    bool parse_guid(const std::string& s, uint32_t g[4])
    {
        if (s.size() != 32) return false;
        for (int k = 0; k < 4; ++k)
        {
            uint32_t v = 0;
            for (int i = 0; i < 8; ++i)
            {
                char ch = s[size_t(k * 8 + i)];
                int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
                if (d < 0) return false;
                v = (v << 4) | uint32_t(d);
            }
            g[k] = v;
        }
        return true;
    }
}

namespace s2m_qbranch
{
    int force(const std::string& node, const std::string& q, const std::string& pin, bool fin, std::string& err)
    {
        if (!g_ready)
        {
            err = "ganchos de missao ausentes";
            return -1;
        }
        if (!x0::on_game_thread())
        {
            err = "fora da thread do jogo";
            return -1;
        }
        uint64_t sm = glob(kSm);
        if (!sm)
        {
            err = "gerenciador de missoes ausente";
            return -1;
        }
        uint32_t nh = make(node);
        uint32_t ph = pin.empty() ? 0 : make(pin);
        uint32_t qh = fin ? 0 : make(q);
        if (!nh || (!pin.empty() && !ph) || (!fin && !qh))
        {
            release(&nh);
            release(&ph);
            release(&qh);
            err = "SID nao criado";
            return -1;
        }
        if (fin)
        {
            g_forceH = nh;
            g_ended = false;
            reinterpret_cast<EndFn>(g_endTr)(sm, &nh, &ph);
            g_forceH = 0;
            return g_ended ? 1 : 0;
        }
        reinterpret_cast<PinFn>(g_pinTr)(sm, &qh, &nh, &ph);
        return 1;
    }

    bool has(const std::string& phrase)
    {
        if (!g_showTr || !g_ansTr) return false;
        auto it = g_shown.find(lc(phrase));
        if (it == g_shown.end()) return false;
        uint64_t d = cur_dlg();
        uint32_t h = 0;
        return d && d == it->second.dlg && rd_seh(reinterpret_cast<void*>(it->second.w + kWSid), &h, 4) && h == it->second.h;
    }

    bool pick(const std::string& phrase, std::string& err)
    {
        auto it = g_shown.find(lc(phrase));
        uint64_t m = glob(kDlg);
        if (!g_ansTr || it == g_shown.end() || !m)
        {
            err = "dialogo ausente";
            return false;
        }
        uint32_t h = make(it->second.s);
        if (!h)
        {
            err = "SID nao criado";
            return false;
        }
        g_shown.erase(it);
        reinterpret_cast<AnsFn>(g_ansTr)(m, &h);
        return true;
    }

    int trig_enter(const std::string& guid, uint64_t pawn, std::string& info, int mode)
    {
        static const char* why[] = {"", "guid invalido", "codigo do exe diferente do esperado (build)", "pawn sem unidade (+0x650)",
                                    "a unidade do pawn nao e o jogador local", "TriggerManager ausente", "gatilho nao carregado no TriggerManager",
                                    "o jogo nao pos o jogador no gatilho", "excecao"};
        TeRaw r;
        int rc;
        if (!x0::on_game_thread()) { info = "fora da thread do jogo"; return -9; }
        if (!pawn) { info = "sem pawn"; return -3; }
        if (!parse_guid(guid, r.g)) rc = -1;
        else if (!trig_code_ok()) rc = -2;
        else
        {
            r.pawn = pawn;
            r.mode = mode;
            rc = te_raw(r);
        }
        char b[240];
        std::snprintf(b, sizeof b, "uid=%u jogador=%u gatilho=0x%llx dentro antes=%d depois_entrar=%d fim=%d lista=%d/%d/%d%s%s", r.uid, r.pid,
                      static_cast<unsigned long long>(r.trig), r.before, r.mid, r.after, r.n0, r.n1, r.n2, rc < 0 ? " erro: " : "", rc < 0 ? why[-rc] : "");
        info = b;
        return rc;
    }
}
