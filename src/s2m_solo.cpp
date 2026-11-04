#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <polyhook2/Detour/x64Detour.hpp>
#include "s2x_hot.h"

#include "s2m_bodies.h"
#include "s2m_solo_logic.h"
#include "s2x.h"

extern "C" bool s2m_lock_dialog_shown();
extern "C" unsigned long long s2m_lock_dialog_view();
extern "C" void s2m_pcompass_before_save();
extern "C" bool s2m_lockbehind_hold(const wchar_t* nodeLower);
extern "C" bool s2m_lockbehind_on();
extern "C" void s2m_lockbehind_before_save();
bool s2m_travel_cine_on();

namespace
{
    uint8_t* exe() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }

    bool rd_seh(const void* p, void* out, size_t n)
    {
        __try { std::memcpy(out, p, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool wr_seh(void* p, const void* in, size_t n)
    {
        __try { std::memcpy(p, in, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool eq_seh(const uint8_t* p, const uint8_t* b, size_t n)
    {
        __try { return std::memcmp(p, b, n) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    const uint32_t kSlmGlobal = 0xa2c8698;
    const uint32_t kAsmGlobal = 0xa2c86a0;
    const uint32_t kMaskOff = 0x20c;
    const uint32_t kIvOff = 0x80;
    const uint32_t kSidPool = 0xb10f588;
    const uint32_t kSidPoolNeg = 0xb12f618;
    const uint32_t kSidRelease = 0x1ffefc6;

    using solo::polStock;
    using solo::polHost;
    using solo::polClient;
    volatile LONG g_pol = polStock;
    volatile LONG g_mute = 0;
    volatile LONG g_joinPass = 0;
    solo::PassCount g_passCount;

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
            x0::logf("NOLA solo: %s (exe+0x%x) com bytes diferentes do esperado: gancho NAO instalado", h.name, h.rva);
            return false;
        }
        h.det = new x0::hot::Detour((uint64_t)p, h.fn, h.tramp);
        if (!h.det->hook())
        {
            delete h.det;
            h.det = nullptr;
            *h.tramp = 0;
            x0::logf("NOLA solo: gancho %s (exe+0x%x) falhou", h.name, h.rva);
            return false;
        }
        return true;
    }

    SRWLOCK g_mk = SRWLOCK_INIT;
    uint64_t g_slm = 0;
    uint32_t g_stock = 0, g_written = 0;
    bool g_haveStock = false, g_haveWritten = false;

    uint32_t eff_mask(uint32_t st, LONG pol) { return solo::eff_mask(st, pol, g_joinPass != 0); }

    void mask_apply(uint64_t slm, bool fresh)
    {
        if (!slm) return;
        AcquireSRWLockExclusive(&g_mk);
        uint32_t cur = 0;
        if (rd_seh(reinterpret_cast<void*>(slm + kMaskOff), &cur, 4))
        {
            if (slm != g_slm) { g_slm = slm; g_haveWritten = false; }
            if (fresh || !g_haveWritten || cur != g_written) { g_stock = cur; g_haveStock = true; }
            uint32_t e = eff_mask(g_stock, g_pol);
            if (cur == e || wr_seh(reinterpret_cast<void*>(slm + kMaskOff), &e, 4))
            {
                g_written = e;
                g_haveWritten = true;
            }
        }
        ReleaseSRWLockExclusive(&g_mk);
    }

    void mask_stock(uint64_t slm)
    {
        if (!slm) return;
        AcquireSRWLockExclusive(&g_mk);
        uint32_t cur = 0;
        if (slm == g_slm && g_haveStock && g_haveWritten && rd_seh(reinterpret_cast<void*>(slm + kMaskOff), &cur, 4) && cur == g_written && cur != g_stock)
            wr_seh(reinterpret_cast<void*>(slm + kMaskOff), &g_stock, 4);
        ReleaseSRWLockExclusive(&g_mk);
    }

    uint64_t g_ivAsm = 0;
    float g_ivStock = 0, g_ivWritten = 0;
    bool g_ivHaveStock = false, g_ivHaveWritten = false;

    void iv_apply(uint64_t a)
    {
        if (!a) return;
        AcquireSRWLockExclusive(&g_mk);
        float cur = 0;
        if (rd_seh(reinterpret_cast<void*>(a + kIvOff), &cur, 4))
        {
            if (a != g_ivAsm) { g_ivAsm = a; g_ivHaveWritten = false; }
            if (!g_ivHaveWritten || cur != g_ivWritten) { g_ivStock = cur; g_ivHaveStock = true; }
            float w = g_pol == polClient ? 0.f : g_ivStock;
            if (cur == w || wr_seh(reinterpret_cast<void*>(a + kIvOff), &w, 4))
            {
                g_ivWritten = w;
                g_ivHaveWritten = true;
            }
        }
        ReleaseSRWLockExclusive(&g_mk);
    }

    uint64_t global(uint32_t rva)
    {
        uint64_t v = 0;
        rd_seh(exe() + rva, &v, 8);
        return v;
    }

    using CanSaveFn = bool (*)(uint64_t, uint32_t);
    using Pass4Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t);
    using StartNodeFn = void (*)(int64_t, uint32_t*, uint32_t*);
    using ReleaseFn = void (*)(void*, uint32_t);

    uint64_t g_csTr = 0, g_w1Tr = 0, g_w2Tr = 0, g_ivTr = 0, g_snTr = 0;
    volatile LONG g_csN = 0, g_csNo = 0, g_w1N = 0, g_w2N = 0, g_ivN = 0;

    bool cs_detour(uint64_t slm, uint32_t type)
    {
        InterlockedIncrement(&g_csN);
        mask_apply(slm, false);
        bool r = reinterpret_cast<CanSaveFn>(g_csTr)(slm, type);
        if (r)
        {
            s2m_pcompass_before_save();
            s2m_lockbehind_before_save();
        }
        if (!r && g_pol != polStock) InterlockedIncrement(&g_csNo);
        solo::count_pass(g_passCount, g_joinPass != 0, type, r);
        return r;
    }

    uint64_t w1_detour(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
    {
        InterlockedIncrement(&g_w1N);
        mask_stock(a);
        uint64_t r = reinterpret_cast<Pass4Fn>(g_w1Tr)(a, b, c, d);
        mask_apply(a, true);
        return r;
    }

    uint64_t w2_detour(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
    {
        InterlockedIncrement(&g_w2N);
        mask_stock(a);
        uint64_t r = reinterpret_cast<Pass4Fn>(g_w2Tr)(a, b, c, d);
        mask_apply(a, true);
        return r;
    }

    uint64_t iv_detour(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
    {
        InterlockedIncrement(&g_ivN);
        iv_apply(a);
        return reinterpret_cast<Pass4Fn>(g_ivTr)(a, b, c, d);
    }

    std::unordered_map<std::wstring, uint16_t> g_mq, g_mn;
    std::vector<std::wstring> g_mp;
    std::vector<std::string> g_why;
    volatile LONG g_muteQ = 0;
    struct MLog
    {
        std::wstring sid, quest;
        uint16_t why = 0;
        bool byQuest = false;
    };
    SRWLOCK g_mlk = SRWLOCK_INIT;
    std::vector<MLog> g_mlog;
    volatile LONG g_muteN = 0, g_passN = 0, g_mlogLost = 0, g_swOn = 0, g_swStarts = 0;
    std::unordered_set<std::wstring> g_mdSet;
    struct ScnRow
    {
        std::wstring name;
        char kind = 'S';
    };
    std::unordered_map<std::wstring, ScnRow> g_scn;
    std::wstring g_scAllow;
    std::vector<std::wstring> g_scEv;
    bool g_snLogged = false;
    volatile LONG g_scHost = 0, g_scActive = 0, g_scHostOpen = 0, g_snSeen = 0;

    bool report_node(const std::wstring& s)
    {
        for (auto& x : g_mp)
            if (s.size() > x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0) return true;
        return false;
    }

    bool sid_str(uint32_t h, std::wstring& out)
    {
        int32_t s = (int32_t)h;
        if (s == 0) return false;
        uint32_t i = s < 0 ? (uint32_t)~s : (uint32_t)s;
        uint8_t* base = exe() + (s < 0 ? kSidPoolNeg : kSidPool);
        uint64_t chunk = 0, str = 0;
        int32_t len = 0;
        if (!rd_seh(base + 8ull * (i >> 10), &chunk, 8) || !chunk) return false;
        uint64_t e = chunk + (uint64_t(i & 0x3ff) << 5);
        if (!rd_seh(reinterpret_cast<void*>(e), &str, 8) || !rd_seh(reinterpret_cast<void*>(e + 8), &len, 4)) return false;
        if (!str || len <= 0 || len > 256) return false;
        wchar_t buf[257] = {};
        if (!rd_seh(reinterpret_cast<void*>(str), buf, size_t(len) * sizeof(wchar_t))) return false;
        buf[len] = 0;
        out.assign(buf);
        for (auto& c : out) c = (wchar_t)std::towlower(c);
        return !out.empty();
    }

    void sid_release(uint32_t* h)
    {
        if (!h) return;
        reinterpret_cast<ReleaseFn>(exe() + kSidRelease)(exe() + kSidPool, *h);
        *h = 0;
    }

    thread_local int t_snDepth = 0;
    int g_snCut = 0;

    bool sn_depth_cut(int d)
    {
        if (d < 64) return false;
        x0::Val v = x0::flag("qdepthguard");
        return v.k == x0::Val::nil || v.num() != 0;
    }

    struct SnDepth
    {
        int& d;
        explicit SnDepth(int& x) : d(x) { ++d; }
        ~SnDepth() { --d; }
    };

    void sn_detour(int64_t sm, uint32_t* q, uint32_t* n)
    {
        if (sn_depth_cut(t_snDepth))
        {
            if (++g_snCut <= 5) x0::log("NOLA solo: StartQuestNode recursivo cortado na profundidade 64");
            sid_release(q);
            sid_release(n);
            return;
        }
        SnDepth sd(t_snDepth);
        InterlockedIncrement(&g_snSeen);
        if (n && !g_mute && s2m_lockbehind_on())
        {
            std::wstring lb;
            if (sid_str(*n, lb) && s2m_lockbehind_hold(lb.c_str()))
            {
                sid_release(q);
                sid_release(n);
                return;
            }
        }
        if ((g_mute || g_scHost) && n)
        {
            std::wstring s, qs;
            bool hit = false, byQ = false;
            uint16_t why = 0;
            bool sidOk = sid_str(*n, s);
            if (sidOk && g_scHost)
            {
                auto sc = g_scn.find(s);
                if (sc != g_scn.end())
                {
                    AcquireSRWLockExclusive(&g_mlk);
                    if (g_scEv.size() < 16) g_scEv.push_back(sc->second.name);
                    ReleaseSRWLockExclusive(&g_mlk);
                }
            }
            if (!g_mute)
            {
                reinterpret_cast<StartNodeFn>(g_snTr)(sm, q, n);
                return;
            }
            if (sidOk && g_swOn && g_mdSet.count(s)) InterlockedIncrement(&g_swStarts);
            if (sidOk && !report_node(s))
            {
                auto it = g_mn.find(s);
                if (it != g_mn.end())
                {
                    hit = true;
                    why = it->second;
                    if (q) sid_str(*q, qs);
                }
                else if (g_muteQ && q && sid_str(*q, qs))
                {
                    auto jt = g_mq.find(qs);
                    if (jt != g_mq.end())
                    {
                        hit = byQ = true;
                        why = jt->second;
                    }
                }
            }
            if (hit && sidOk && g_scActive)
            {
                AcquireSRWLockShared(&g_mlk);
                bool al = s == g_scAllow;
                ReleaseSRWLockShared(&g_mlk);
                if (al) hit = false;
            }
            if (hit)
            {
                InterlockedIncrement(&g_muteN);
                AcquireSRWLockExclusive(&g_mlk);
                if (g_mlog.size() < 512) g_mlog.push_back({s, qs, why, byQ});
                else InterlockedIncrement(&g_mlogLost);
                ReleaseSRWLockExclusive(&g_mlk);
                sid_release(q);
                sid_release(n);
                return;
            }
            InterlockedIncrement(&g_passN);
        }
        reinterpret_cast<StartNodeFn>(g_snTr)(sm, q, n);
    }

    Hook g_hCs{"CanSaveNow", 0xe44e3c, {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xec, 0xd8, 0x00, 0x00, 0x00, 0x44, 0x0f, 0x29, 0x84, 0x24, 0xc0, 0x00},
               &g_csTr, (uint64_t)&cs_detour};
    Hook g_hW1{"SaveTypesFromDifficulty", 0x361bea0, {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x30, 0x48, 0x89, 0xd6, 0x48, 0x89, 0xcf, 0x48, 0x8b, 0x05, 0x0c, 0xdc, 0x86, 0x06},
               &g_w1Tr, (uint64_t)&w1_detour};
    Hook g_hW2{"SaveTypesQuickFix", 0x6be8e5c, {0x56, 0x48, 0x83, 0xec, 0x20, 0x48, 0x89, 0xce, 0x48, 0x8b, 0x0d, 0x25, 0xf8, 0x6d, 0x03, 0xe8, 0x1c, 0x68, 0x33, 0xfc},
               &g_w2Tr, (uint64_t)&w2_detour};
    Hook g_hIv{"AutoSaveArm", 0x3554b0a, {0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x81, 0xec, 0x88, 0x00, 0x00, 0x00, 0x0f, 0x29, 0x74, 0x24, 0x70, 0x48, 0x89, 0xce},
               &g_ivTr, (uint64_t)&iv_detour};
    Hook g_hSn{"StartQuestNode", 0x500f70, {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xec, 0x98, 0x00, 0x00, 0x00, 0x4c, 0x89, 0xc6, 0x48, 0x89, 0xd7, 0x49, 0x89, 0xcf},
               &g_snTr, (uint64_t)&sn_detour};

    const uint8_t kRelPro[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x48, 0x85, 0xd2, 0x7e, 0x50};

    struct St
    {
        bool tried = false;
        bool saveOk = false, muteOk = false;
        LONG pol = -1;
        LONG mute = -1;
        LONG muteQ = -1;
        struct Rate
        {
            double at = -1e9;
            int held = 0;
        };
        std::unordered_map<std::wstring, Rate> rate;
        LONG muteSeen = 0;
        double sumAt = 0;
    } g;

    std::wstring lower_w(const std::string& s)
    {
        std::wstring w(s.begin(), s.end());
        for (auto& c : w) c = (wchar_t)std::towlower(c);
        return w;
    }

    std::unordered_map<std::string, uint16_t> g_whyIx;
    std::vector<std::wstring> g_md;

    struct Sweep
    {
        uint64_t pawn = 0;
        bool held = false, heldDone = false, playDone = false;
        size_t cursor = 0;
        int passes = 0;
        ULONGLONG passAt = 0;
        ULONGLONG passStart = 0;
        int ok = 0, bad = 0;
    } g_sw;

    bool sweep_chunk(const char* when, int pend, size_t budget)
    {
        x0::Ref aa = x0::first_of(L"CustomConsoleManagerAA");
        if (!aa)
        {
            x0::logf("NOLA solo: qmuteact %s: CustomConsoleManagerAA ausente, nada encerrado", when);
            return true;
        }
        if (g_sw.cursor == 0)
        {
            g_sw.ok = 0;
            g_sw.bad = 0;
            g_sw.passStart = GetTickCount64();
            InterlockedExchange(&g_swStarts, 0);
            InterlockedExchange(&g_swOn, 1);
        }
        LARGE_INTEGER f{}, a{}, b{};
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&a);
        size_t end = std::min(g_md.size(), g_sw.cursor + budget);
        for (size_t i = g_sw.cursor; i < end; ++i)
        {
            x0::Call c(aa, L"XEndQuestNodeBySID");
            c.s(L"SID", g_md[i]);
            if (c.run()) ++g_sw.ok;
            else ++g_sw.bad;
        }
        QueryPerformanceCounter(&b);
        double ms = f.QuadPart ? 1000.0 * double(b.QuadPart - a.QuadPart) / double(f.QuadPart) : -1;
        g_sw.cursor = end;
        if (g_sw.cursor < g_md.size())
        {
            x0::logf("NOLA solo: qmuteact %s: lote %d-%d de %d nos SetDialog em %.1f ms", when, (int)(end - budget), (int)end, (int)g_md.size(), ms);
            return false;
        }
        g_sw.cursor = 0;
        InterlockedExchange(&g_swOn, 0);
        x0::logf("NOLA solo: qmuteact %s: XEndQuestNodeBySID em %d nos SetDialog de missoes so do host (%d ok, %d falharam) em %llu ms, inicios de missao pendentes %d, inicios dos nos varridos durante a varredura %d", when,
                 (int)g_md.size(), g_sw.ok, g_sw.bad, (unsigned long long)(GetTickCount64() - g_sw.passStart), pend, (int)g_swStarts);
        return true;
    }

    void sweep_tick()
    {
        if (!g.muteOk || g_md.empty() || !g_mute || !g_muteQ || x0::fi("qmuteact", 1) != 1) return;
        x0::Ref pw = x0::first_of(L"BP_Stalker2Character_C");
        uint64_t a = pw.valid() ? pw.address() : 0;
        if (!a) return;
        if (a != g_sw.pawn)
        {
            g_sw = Sweep{};
            g_sw.pawn = a;
        }
        if (g_sw.playDone) return;
        int pend = m01::quests_held();
        if (pend < 0) return;
        const bool spread = x0::fi("qmutespread", 1) == 1;
        const size_t budget = spread ? static_cast<size_t>(std::max(1, static_cast<int>(x0::fi("qmutebudget", 150)))) : g_md.size();
        if (pend > 0)
        {
            g_sw.held = true;
            if (!g_sw.heldDone && sweep_chunk("carga nova, tela de carga segurando as missoes", pend, budget)) g_sw.heldDone = true;
            return;
        }
        const int passes = spread ? std::max(1, static_cast<int>(x0::fi("qmutepasses", 3))) : 1;
        if (g_sw.passes >= passes)
        {
            g_sw.playDone = true;
            return;
        }
        if (g_sw.cursor == 0 && GetTickCount64() < g_sw.passAt) return;
        if (!sweep_chunk(g_sw.held ? "missoes liberadas depois da carga" : "carga nova", pend, budget)) return;
        g_sw.passes += 1;
        g_sw.passAt = GetTickCount64() + static_cast<ULONGLONG>(std::max(0, static_cast<int>(x0::fi("qmutegap", 2000))));
        if (g_sw.passes >= passes) g_sw.playDone = true;
    }

    uint16_t why_id(const std::string& w)
    {
        auto it = g_whyIx.find(w);
        if (it != g_whyIx.end()) return it->second;
        if (g_why.size() >= 0xffff) return 0;
        g_why.push_back(w);
        uint16_t i = (uint16_t)(g_why.size() - 1);
        g_whyIx[w] = i;
        return i;
    }

    bool load_muted()
    {
        std::string text;
        if (!x0::data("qmute_client.txt", text)) return false;
        size_t p = 0;
        while (p < text.size())
        {
            size_t e = text.find('\n', p);
            if (e == std::string::npos) e = text.size();
            std::string ln = text.substr(p, e - p);
            p = e + 1;
            while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.pop_back();
            if (ln.empty()) continue;
            std::vector<std::string> f;
            size_t a = 0;
            while (true)
            {
                size_t t = ln.find('\t', a);
                f.push_back(ln.substr(a, t == std::string::npos ? std::string::npos : t - a));
                if (t == std::string::npos) break;
                a = t + 1;
            }
            if (f.size() == 1)
            {
                g_mn[lower_w(f[0])] = why_id("lista");
                continue;
            }
            std::string why = f.size() >= 3 ? f[2] : std::string("-");
            if (f[0] == "Q") g_mq[lower_w(f[1])] = why_id("a missao " + f[1] + " so roda no host (" + why + ")");
            else if (f[0] == "N") g_mn[lower_w(f[1])] = why_id(why);
            else if (f[0] == "P") g_mp.push_back(lower_w(f[1]));
            else if (f[0] == "D")
            {
                g_md.push_back(std::wstring(f[1].begin(), f[1].end()));
                g_mdSet.insert(lower_w(f[1]));
            }
            else if (f[0] == "S" || f[0] == "T" || f[0] == "V") g_scn[lower_w(f[1])] = ScnRow{std::wstring(f[1].begin(), f[1].end()), f[0][0]};
        }
        return !g_mn.empty() || !g_mq.empty();
    }

    void install()
    {
        g.tried = true;
        bool cs = hook(g_hCs);
        bool w1 = cs && hook(g_hW1);
        bool w2 = cs && hook(g_hW2);
        bool iv = hook(g_hIv);
        g.saveOk = cs;
        x0::logf("NOLA solo: ganchos de save CanSaveNow=%s tipos=%s/%s intervalo do autosave=%s", cs ? "ok" : "FALHOU", w1 ? "ok" : "FALHOU", w2 ? "ok" : "FALHOU", iv ? "ok" : "FALHOU");
        bool data = load_muted();
        bool rel = eq_seh(exe() + kSidRelease, kRelPro, sizeof kRelPro);
        bool sn = data && rel && hook(g_hSn);
        g.muteOk = sn;
        x0::logf("NOLA solo: mute de missoes do cliente %s (qmute_client.txt: %d missoes so do host, %d nos, %d sufixos de relato, %d SetDialog encerrados a cada carga%s)", sn ? "pronto" : "NAO instalado",
                 (int)g_mq.size(), (int)g_mn.size(), (int)g_mp.size(), (int)g_md.size(), rel ? "" : ", liberador de SID com bytes diferentes");
    }

    struct Scene
    {
        int hSeq = 0;
        std::string hNode;
        int hSt = 0;
        double hAt = 0;
        bool hClosed = false;
        bool hPrev = false;
        unsigned long long hView = 0;
        unsigned long long hPrevView = 0;
        bool vid = false;
        long long epoch = (long long)(GetTickCount64() / 1000);
        bool seen = false;
        long long cEp = 0;
        int cSeq = 0;
        std::wstring node;
        double at = 0;
        bool sawOpen = false;
        double t0 = 0;
        long long rxEp = -1;
        int rxSeq = -1, rxOpen = -1, rxAlive = -2;
        int logSt = 0;
    } g_sc;

    std::string g_scWhy;

    void sc_why(const char* s)
    {
        if (g_scWhy == s) return;
        g_scWhy = s;
        x0::logf("NOLA cena: %s", s);
    }

    void scene_end(const char* why)
    {
        if (!g_sc.node.empty())
        {
            x0::Ref aa = x0::first_of(L"CustomConsoleManagerAA");
            bool ok = false;
            if (aa)
            {
                x0::Call c(aa, L"XEndQuestNodeBySID");
                c.s(L"SID", g_sc.node);
                ok = c.run();
            }
            x0::logf("NOLA cena: cliente encerrou a cena %ls (%s) ok=%d", g_sc.node.c_str(), why, ok ? 1 : 0);
        }
        g_sc.node.clear();
        g_sc.sawOpen = false;
        InterlockedExchange(&g_scActive, 0);
    }

    void scene_tick()
    {
        const bool on = x0::coop() && x0::fi("qscene", 1) == 1 && g.muteOk;
        const bool host = on && x0::is_host();
        InterlockedExchange(&g_scHost, host && !g_scn.empty() ? 1 : 0);
        std::vector<std::wstring> ev;
        AcquireSRWLockExclusive(&g_mlk);
        ev.swap(g_scEv);
        ReleaseSRWLockExclusive(&g_mlk);
        double now = x0::now();
        if (host)
        {
            const bool shown = !g_scn.empty() && s2m_lock_dialog_shown();
            const unsigned long long view = shown ? s2m_lock_dialog_view() : 0ull;
            for (auto& e : ev)
            {
                std::string n(e.begin(), e.end());
                auto it = g_scn.find(lower_w(n));
                char k = it == g_scn.end() ? 'S' : it->second.kind;
                if ((k == 'T' && x0::fi("qscenenpc", 1) != 1) || (k == 'V' && x0::fi("qscenevid", 1) != 1)) continue;
                ++g_sc.hSeq;
                g_sc.hNode = n;
                g_sc.hSt = k == 'V' ? 0 : 1;
                g_sc.hAt = now;
                g_sc.hClosed = !g_sc.hPrev;
                g_sc.hView = g_sc.hPrevView;
                x0::logf("NOLA cena: host abriu a cena %s #%d, os clientes entram", g_sc.hNode.c_str(), g_sc.hSeq);
            }
            g_sc.hPrev = shown;
            g_sc.hPrevView = view;
            const bool cine = x0::fi("qscenecine", 1) == 1 && s2m_travel_cine_on();
            if (g_sc.hSt != 0)
            {
                if (g_sc.hSt == 1)
                {
                    if (!shown) g_sc.hClosed = true;
                    else if (g_sc.hClosed || (x0::fi("qsceneview", 1) == 1 && view != g_sc.hView)) g_sc.hSt = 2;
                    if (g_sc.hSt == 1 && !cine && now - g_sc.hAt > x0::fd("qscenewait", 8)) g_sc.hSt = 0;
                }
                else if (!shown && !cine) g_sc.hSt = 0;
                if (g_sc.hSt != 0 && now - g_sc.hAt > x0::fd("qscenecap", 600)) g_sc.hSt = 0;
            }
            InterlockedExchange(&g_scHostOpen, g_sc.hSt == 2 ? 1 : 0);
            if (g_sc.hSt != g_sc.logSt)
            {
                x0::logf("NOLA cena: host cena #%d %s: estado %d -> %d (0 fechada, 1 esperando dialogo, 2 dialogo aberto) widget de dialogo visivel=%d, aberto enviado aos clientes=%d, cinematica do host=%d, %.1f s desde o inicio", g_sc.hSeq,
                         g_sc.hNode.c_str(), g_sc.logSt, g_sc.hSt, shown ? 1 : 0, g_sc.hSt == 2 ? 1 : 0, cine ? 1 : 0, now - g_sc.hAt);
                g_sc.logSt = g_sc.hSt;
            }
            return;
        }
        const bool fresh = x0::fi("qscenefresh", 1) == 1;
        if (!on || !x0::is_client())
        {
            if (!g_sc.node.empty()) scene_end("fora de co-op ou sem papel de cliente");
            g_sc.seen = false;
            g_sc.t0 = 0;
            if (x0::coop() && x0::is_client())
                sc_why(x0::fi("qscene", 1) != 1 ? "cliente nao segue cenas: flag qscene desligada" : "cliente nao segue cenas: mute de missoes nao instalado (muteOk=0)");
            return;
        }
        if (g_sc.t0 <= 0) g_sc.t0 = now;
        std::string t;
        if (!x0::rpart("QS", t) || t.empty())
        {
            if (!g_sc.node.empty()) scene_end("host sem cena");
            if (x0::peer().state != x0::Peer::up) g_sc.t0 = now;
            else if (fresh && now - g_sc.t0 >= 3)
            {
                g_sc.seen = true;
                g_sc.cEp = 0;
                g_sc.cSeq = 0;
            }
            else g_sc.seen = false;
            sc_why("cliente espera: o host ainda nao publicou cena (tag QS vazia)");
            return;
        }
        long long ep = 0;
        int seq = 0, open = 0, alive = -1;
        char nd[160] = {};
        if (std::sscanf(t.c_str(), "%lld,%d,%159[^,],%d,%d", &ep, &seq, nd, &open, &alive) < 4)
        {
            sc_why("tag QS recebida mas ilegivel");
            return;
        }
        if (ep != g_sc.rxEp || seq != g_sc.rxSeq || open != g_sc.rxOpen || alive != g_sc.rxAlive)
        {
            g_sc.rxEp = ep;
            g_sc.rxSeq = seq;
            g_sc.rxOpen = open;
            g_sc.rxAlive = alive;
            x0::logf("NOLA cena: cliente recebeu a tag QS do host: epoca %lld cena #%d no %s dialogo aberto=%d cena viva no host=%d (ultima seq vista %d)", ep, seq, nd, open, alive, g_sc.cSeq);
        }
        if (!g_sc.seen)
        {
            g_sc.seen = true;
            g_sc.cEp = ep;
            g_sc.cSeq = (open || alive == 1) ? 0 : seq;
            if (!open && alive != 1) x0::logf("NOLA cena: cliente viu a cena #%d do host ja em andamento ao entrar (dialogo fechado): tratada como antiga, nao segue", seq);
        }
        else if (ep != g_sc.cEp)
        {
            const bool wasEmpty = fresh && g_sc.cEp == 0;
            g_sc.cEp = ep;
            g_sc.cSeq = wasEmpty ? 0 : ((open || alive == 1) ? 0 : seq);
            if (wasEmpty) x0::logf("NOLA cena: cliente viu a primeira cena #%d do host nascer (a tag estava vazia antes): vai seguir", seq);
        }
        if (seq != g_sc.cSeq)
        {
            if (!g_sc.node.empty()) scene_end("outra cena do host");
            g_sc.cSeq = seq;
            std::string a(nd);
            std::wstring w(a.begin(), a.end());
            AcquireSRWLockExclusive(&g_mlk);
            g_scAllow = lower_w(a);
            ReleaseSRWLockExclusive(&g_mlk);
            g_sc.node = w;
            g_sc.at = now;
            g_sc.sawOpen = false;
            auto kr = g_scn.find(lower_w(a));
            g_sc.vid = kr != g_scn.end() && kr->second.kind == 'V';
            const bool allowed = kr != g_scn.end() && (kr->second.kind != 'T' || x0::fi("qscenenpc", 1) == 1) && (!g_sc.vid || x0::fi("qscenevid", 1) == 1);
            if (!allowed)
            {
                x0::logf("NOLA cena: cliente NAO segue a cena #%d %s: %s", seq, nd,
                         kr == g_scn.end() ? "no fora da lista de cenas (qmute_client.txt S/T/V)" : kr->second.kind == 'T' ? "cena de NPC com qscenenpc desligada" : "video com qscenevid desligada");
                g_sc.node.clear();
                return;
            }
            InterlockedExchange(&g_scActive, 1);
            x0::Ref aa = x0::first_of(L"CustomConsoleManagerAA");
            bool ok = false;
            if (aa)
            {
                x0::Call c(aa, L"XStartQuestNodeBySID");
                c.s(L"SID", w);
                ok = c.run();
            }
            x0::logf("NOLA cena: cliente segue a cena do host %s #%d ok=%d", nd, seq, ok ? 1 : 0);
            if (!ok) scene_end("start falhou");
            else if (g_sc.vid) InterlockedExchange(&g_scActive, 0);
            return;
        }
        if (g_sc.node.empty()) return;
        if (g_sc.vid)
        {
            if (now - g_sc.at > x0::fd("qscenemax", 180)) scene_end("limite de tempo");
            return;
        }
        if (open) g_sc.sawOpen = true;
        if (alive >= 0 && x0::fi("qscenecine", 1) == 1)
        {
            if (!alive) scene_end(g_sc.sawOpen ? "o host fechou o dialogo e a cena" : "o host encerrou a cena sem abrir dialogo");
            return;
        }
        if (g_sc.sawOpen && !open) scene_end("o host fechou o dialogo");
        else if (!g_sc.sawOpen && now - g_sc.at > x0::fd("qscenewait", 8)) scene_end("o host nao abriu dialogo");
        else if (now - g_sc.at > x0::fd("qscenemax", 180)) scene_end("limite de tempo");
    }

    std::string scene_out()
    {
        if (!g_scHost || g_sc.hSeq <= 0) return {};
        char b[256];
        std::snprintf(b, sizeof b, "%lld,%d,%s,%d,%d", g_sc.epoch, g_sc.hSeq, g_sc.hNode.c_str(), g_scHostOpen ? 1 : 0, g_sc.hSt != 0 ? 1 : 0);
        static std::string last;
        if (last != b)
        {
            last = b;
            x0::logf("NOLA cena: host publica a tag QS aos clientes: %s", b);
        }
        return b;
    }

    void tick()
    {
        if (!g.tried) install();
        scene_tick();
        if (g_snSeen > 0 && !g_snLogged)
        {
            g_snLogged = true;
            x0::log("NOLA solo: StartQuestNode: primeiro inicio de no visto pelo gancho (exe+0x500f70)");
        }
        bool co = x0::coop();
        LONG pol = polStock;
        if (co && x0::fi("rolesave", 1) == 1) pol = x0::is_host() ? polHost : polClient;
        LONG mute = (co && x0::is_client() && x0::fi("rolemute", 1) == 1 && g.muteOk) ? 1 : 0;
        LONG muteQ = x0::fi("qmuteq", 1) == 1 ? 1 : 0;
        InterlockedExchange(&g_pol, pol);
        InterlockedExchange(&g_mute, mute);
        InterlockedExchange(&g_muteQ, muteQ);
        if (muteQ != g.muteQ)
        {
            if (g.muteQ >= 0 || !muteQ) x0::logf("NOLA solo: qmuteq=%d: %s", (int)muteQ, muteQ ? "missoes de historia e secundarias inteiras so rodam no host" : "so os nos da lista (comportamento antigo)");
            g.muteQ = muteQ;
        }
        sweep_tick();
        if (g.saveOk) mask_apply(global(kSlmGlobal), false);
        if (g_hIv.det) iv_apply(global(kAsmGlobal));
        if (pol != g.pol || mute != g.mute)
        {
            const char* pn = pol == polHost ? "host em co-op (sem saves Auto)" : pol == polClient ? "cliente em co-op (so Auto, sem autosave por tempo)" : "jogo solo (saves do jogo)";
            AcquireSRWLockShared(&g_mk);
            uint32_t st = g_stock, wr = g_written;
            float ivs = g_ivStock, ivw = g_ivWritten;
            ReleaseSRWLockShared(&g_mk);
            x0::logf("NOLA solo: %s -> %s; tipos de save 0x%x (jogo 0x%x), intervalo do autosave %.0f s (jogo %.0f s), mute de missoes %s", g.pol < 0 ? "inicio" : "papel mudou", pn, wr, st,
                     ivw, ivs, mute ? "LIGADO" : "desligado");
            if (g.pol == polClient && pol != polClient) x0::log("NOLA solo: o autosave por tempo volta na proxima carga de mundo; save manual ja liberado");
            g.pol = pol;
            g.mute = mute;
        }
        std::vector<MLog> ml;
        AcquireSRWLockExclusive(&g_mlk);
        ml.swap(g_mlog);
        ReleaseSRWLockExclusive(&g_mlk);
        double now = x0::now();
        auto narrow = [](const std::wstring& s) {
            std::string a;
            for (wchar_t c : s) a += (c > 0 && c < 0x80) ? (char)c : '?';
            return a;
        };
        for (auto& m : ml)
        {
            auto& r = g.rate[m.sid];
            if (now - r.at < 30)
            {
                ++r.held;
                continue;
            }
            std::string extra = r.held > 0 ? " (+" + std::to_string(r.held) + " vezes nos ultimos 30 s)" : std::string();
            r.at = now;
            r.held = 0;
            const std::string& why = m.why < g_why.size() ? g_why[m.why] : std::string("-");
            x0::log("NOLA solo: inicio de no calado no cliente: " + narrow(m.sid) + " (missao " + (m.quest.empty() ? std::string("?") : narrow(m.quest)) + ") motivo: " +
                    (m.byQuest ? why : "no da lista: " + why) + extra);
        }
        LONG lost = InterlockedExchange(&g_mlogLost, 0);
        if (lost > 0) x0::logf("NOLA solo: %d inicios calados sem linha propria (fila cheia)", (int)lost);
        LONG mn = g_muteN;
        if (mn != g.muteSeen && now >= g.sumAt)
        {
            g.sumAt = now + 30;
            g.muteSeen = mn;
            x0::logf("NOLA solo: %d inicios de no calados, %d passaram; CanSaveNow %d chamadas (%d recusadas em co-op)", (int)mn, (int)g_passN, (int)g_csN, (int)g_csNo);
        }
    }

    void solo_init()
    {
        x0::every(x0::Mod::session, "solo", x0::Rate::t100, [] { tick(); });
        x0::own_tag(x0::Mod::session, "QS", [] { return scene_out(); });
    }
    ::x0::ModReg g_solo_reg(::x0::Mod::session, "solo", &solo_init);
}

extern "C" bool s2m_solo_quest_muted(const char* quest)
{
    if (!quest || !g_mute || !g_muteQ) return false;
    return g_mq.count(lower_w(quest)) > 0;
}

extern "C" bool s2m_solo_quest_hostonly(const char* quest)
{
    if (!quest) return false;
    return g_mq.count(lower_w(quest)) > 0;
}

extern "C" bool s2m_solo_scene_take()
{
    return g_scActive != 0;
}

extern "C" bool s2m_solo_join_pass(int on, int* asked, int* allowed, unsigned* mask)
{
    if (!g.saveOk) return false;
    if (on)
    {
        g_passCount = solo::PassCount{};
        InterlockedExchange(&g_joinPass, 1);
    }
    else InterlockedExchange(&g_joinPass, 0);
    uint64_t slm = global(kSlmGlobal);
    mask_apply(slm, false);
    uint32_t m = 0;
    if (slm) rd_seh(reinterpret_cast<void*>(slm + kMaskOff), &m, 4);
    if (asked) *asked = g_passCount.asked;
    if (allowed) *allowed = g_passCount.allowed;
    if (mask) *mask = m;
    return true;
}
