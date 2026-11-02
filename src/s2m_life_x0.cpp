#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include <polyhook2/Detour/x64Detour.hpp>
#include "s2x_hot.h"

#include "s2x.h"
#include "s2m_damage.h"

namespace
{
    uint8_t* exe() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }

    bool eq_seh(const uint8_t* p, const uint8_t* b, size_t n)
    {
        __try { return std::memcmp(p, b, n) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool cp_seh(void* d, const void* s, size_t n)
    {
        __try { std::memcpy(d, s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    const uint8_t kGm[91] = {0x48, 0x83, 0xec, 0x28, 0x83, 0x3d, 0xb5, 0xb0, 0x7a, 0x06, 0xff, 0x74, 0x49, 0xe8, 0x28, 0x3e, 0xed, 0xfe, 0xb9, 0xff, 0xff, 0xff, 0x07,
                             0x23, 0x0d, 0xa3, 0xb0, 0x7a, 0x06, 0x81, 0xf9, 0x00, 0x04, 0x00, 0x00, 0x73, 0x1e, 0x89, 0xc9, 0x48, 0x69, 0xc9, 0x00, 0x07, 0x00, 0x00,
                             0x48, 0x01, 0xc1, 0x48, 0x83, 0xc1, 0x10, 0xba, 0x06, 0x00, 0x00, 0x00, 0x48, 0x83, 0xc4, 0x28, 0xe9, 0x13, 0xf7, 0xa4, 0xff, 0x48, 0x8b,
                             0x00, 0x81, 0xc1, 0x00, 0xfc, 0xff, 0xff, 0x81, 0xf9, 0xff, 0x03, 0x00, 0x00, 0x76, 0xd1, 0xeb, 0xed, 0x48, 0x83, 0xc4, 0x28, 0xc3};
    const size_t kGmMask = 54;
    bool g_gmBad = false;

    bool gm_mask(uint8_t v)
    {
        if (g_gmBad) return false;
        uint8_t* p = exe() + 0x372f080;
        uint8_t k[sizeof kGm];
        std::memcpy(k, kGm, sizeof k);
        k[kGmMask] = 0x06;
        bool a = eq_seh(p, k, sizeof k);
        k[kGmMask] = 0x04;
        bool b = eq_seh(p, k, sizeof k);
        if (!a && !b)
        {
            g_gmBad = true;
            x0::log("NOLA LIFE: GameManager god off (exe+0x372f080) com bytes diferentes do esperado: o jogo pode limpar o bit 0x2 do jogador (nao corrigido nesta versao do jogo)");
            return false;
        }
        if (p[kGmMask] == v) return true;
        DWORD old = 0;
        if (!VirtualProtect(p + kGmMask, 1, PAGE_EXECUTE_READWRITE, &old)) return false;
        p[kGmMask] = v;
        VirtualProtect(p + kGmMask, 1, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p + kGmMask, 1);
        return p[kGmMask] == v;
    }

    using LoadUiFn = void (*)(int64_t, uint8_t, uint8_t, uint8_t, uint8_t);
    uint64_t g_luTramp = 0;
    PLH::x64Detour* g_luDet = nullptr;
    bool g_luTried = false;
    volatile LONG g_luBlock = 0;
    volatile LONG g_luTries = 0;

    void lu_detour(int64_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e)
    {
        if (g_luBlock) { InterlockedIncrement(&g_luTries); return; }
        reinterpret_cast<LoadUiFn>(g_luTramp)(a, b, c, d, e);
    }

    bool lu_install()
    {
        if (g_luDet) return true;
        if (g_luTried) return false;
        g_luTried = true;
        static const uint8_t k[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xec, 0xd8, 0x00, 0x00, 0x00, 0x44, 0x88, 0x4c, 0x24, 0x26};
        uint8_t* p = exe() + 0x38ae099;
        if (!eq_seh(p, k, sizeof k))
        {
            x0::log("NOLA LIFE: carga de save pela UI (exe+0x38ae099) com bytes diferentes do esperado: bloqueio da tela de morte NAO instalado");
            return false;
        }
        g_luDet = new x0::hot::Detour((uint64_t)p, (uint64_t)&lu_detour, &g_luTramp);
        if (!g_luDet->hook())
        {
            delete g_luDet;
            g_luDet = nullptr;
            g_luTramp = 0;
            x0::log("NOLA LIFE: gancho da carga de save pela UI falhou: bloqueio da tela de morte NAO instalado");
            return false;
        }
        x0::log("NOLA LIFE: gancho da carga de save pela UI instalado (exe+0x38ae099; XLoadLastSave do mod nao passa por ele)");
        return true;
    }

    bool rd_seh(const void* p, void* out, size_t n)
    {
        __try { std::memcpy(out, p, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool wr_seh(void* p, const void* in, size_t n)
    {
        __try { std::memcpy(p, in, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    struct TArr
    {
        uint8_t* d;
        int32_t n;
        int32_t m;
    };

    const size_t kBtnSize = 0x60;
    const size_t kBtnSid = 0x28;
    const size_t kBtnAct = 0x38;
    const size_t kSvModel = 0x2e0;
    const size_t kModelList = 0x30;
    const wchar_t* const kHideModel = L"DeathScreenButtonData";
    const wchar_t* const kHideSid[] = {L"sid_misc_pausemenu_loadlastsave", L"sid_misc_pausemenu_save_load"};
    const char* const kHideAct[] = {"LoadLastSaveAction", "LoadSaveAction"};

    using BuildFn = void (*)(int64_t);
    uint64_t g_bvTramp = 0;
    PLH::x64Detour* g_bvDet = nullptr;
    bool g_bvTried = false;
    volatile LONG g_bvHidden = 0;
    volatile LONG g_bvBuilds = 0;

    bool sid_hidden(const uint8_t* e)
    {
        uint64_t s = 0;
        int32_t n = 0;
        if (!rd_seh(e + kBtnSid, &s, 8) || !rd_seh(e + kBtnSid + 8, &n, 4) || !s || n <= 1 || n > 128) return false;
        wchar_t buf[129] = {};
        if (!rd_seh(reinterpret_cast<const void*>(s), buf, size_t(n) * sizeof(wchar_t))) return false;
        buf[n - 1] = 0;
        for (const wchar_t* h : kHideSid)
            if (_wcsicmp(buf, h) == 0) return true;
        return false;
    }

    bool act_hidden(const uint8_t* e)
    {
        uint64_t a = 0;
        if (!rd_seh(e + kBtnAct, &a, 8) || !a) return false;
        std::string c = x0::class_name(x0::Ref::addr(a));
        for (const char* h : kHideAct)
            if (c == h) return true;
        return false;
    }

    bool death_model(uint64_t model)
    {
        if (!model) return false;
        std::string n = x0::obj_name(x0::Ref::addr(model));
        std::wstring w(n.begin(), n.end());
        return w == kHideModel;
    }

    void bv_detour(int64_t sv)
    {
        BuildFn orig = reinterpret_cast<BuildFn>(g_bvTramp);
        uint64_t model = 0;
        if (!g_luBlock || !sv || !rd_seh(reinterpret_cast<const void*>(sv + kSvModel), &model, 8) || !death_model(model)) { orig(sv); return; }
        uint8_t* lp = reinterpret_cast<uint8_t*>(model + kModelList);
        TArr a{};
        if (!rd_seh(lp, &a, sizeof a) || !a.d || a.n <= 0 || a.n > 64) { orig(sv); return; }
        std::vector<uint8_t> keep;
        keep.reserve(size_t(a.n) * kBtnSize);
        int hid = 0;
        for (int i = 0; i < a.n; ++i)
        {
            const uint8_t* e = a.d + size_t(i) * kBtnSize;
            if (sid_hidden(e) || act_hidden(e)) { ++hid; continue; }
            size_t at = keep.size();
            keep.resize(at + kBtnSize);
            if (!rd_seh(e, keep.data() + at, kBtnSize)) { orig(sv); return; }
        }
        if (!hid) { orig(sv); return; }
        int32_t kept = a.n - hid;
        TArr t{kept ? keep.data() : nullptr, kept, kept};
        if (!wr_seh(lp, &t, sizeof t)) { orig(sv); return; }
        orig(sv);
        wr_seh(lp, &a, sizeof a);
        InterlockedExchangeAdd(&g_bvHidden, hid);
        InterlockedIncrement(&g_bvBuilds);
    }

    bool bv_install()
    {
        if (g_bvDet) return true;
        if (g_bvTried) return false;
        g_bvTried = true;
        static const uint8_t k[] = {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x50, 0x48, 0x89, 0xce, 0x48, 0x8b, 0x05};
        uint8_t* p = exe() + 0x3330b02;
        if (!eq_seh(p, k, sizeof k))
        {
            x0::log("NOLA LIFE: montagem dos botoes de menu (exe+0x3330b02) com bytes diferentes do esperado: botoes de carregar da tela de morte NAO escondidos");
            return false;
        }
        g_bvDet = new x0::hot::Detour((uint64_t)p, (uint64_t)&bv_detour, &g_bvTramp);
        if (!g_bvDet->hook())
        {
            delete g_bvDet;
            g_bvDet = nullptr;
            g_bvTramp = 0;
            x0::log("NOLA LIFE: gancho da montagem dos botoes de menu falhou: botoes de carregar da tela de morte NAO escondidos");
            return false;
        }
        x0::log("NOLA LIFE: gancho da montagem dos botoes de menu instalado (exe+0x3330b02 MenuSubViewBase::vf_159; so DeathScreenButtonData, so cliente morto)");
        return true;
    }

    using LoadBeginFn = void (*)(int64_t, void*, void*);
    uint64_t g_lbTramp = 0;
    PLH::x64Detour* g_lbDet = nullptr;
    bool g_lbTried = false;
    volatile LONG g_lbN = 0;
    uint32_t g_lbGuid[4] = {};
    bool g_lbHas = false;

    void lb_detour(int64_t gm, void* guid, void* extra)
    {
        uint32_t g[4] = {};
        if (guid && cp_seh(g, guid, sizeof g))
        {
            std::memcpy(g_lbGuid, g, sizeof g);
            g_lbHas = true;
        }
        else g_lbHas = false;
        InterlockedIncrement(&g_lbN);
        reinterpret_cast<LoadBeginFn>(g_lbTramp)(gm, guid, extra);
    }

    bool lb_install()
    {
        if (g_lbDet) return true;
        if (g_lbTried) return false;
        g_lbTried = true;
        static const uint8_t kA[] = {0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x48, 0x4c, 0x89, 0xc6, 0x48, 0x89, 0xcf, 0x48, 0x8b, 0x05};
        static const uint8_t kB[] = {0x48, 0x31, 0xe0, 0x48, 0x89, 0x44, 0x24, 0x40, 0x0f, 0x10, 0x02, 0x0f, 0x11, 0x81, 0xb0, 0x01, 0x00, 0x00};
        uint8_t* p = exe() + 0x38ae8bc;
        if (!eq_seh(p, kA, sizeof kA) || !eq_seh(p + 22, kB, sizeof kB))
        {
            x0::log("NOLA n7: inicio da carga de save (exe+0x38ae8bc) com bytes diferentes do esperado: o host NAO avisa o inicio da carga (o cliente espera o host entrar no mundo)");
            return false;
        }
        g_lbDet = new x0::hot::Detour((uint64_t)p, (uint64_t)&lb_detour, &g_lbTramp);
        if (!g_lbDet->hook())
        {
            delete g_lbDet;
            g_lbDet = nullptr;
            g_lbTramp = 0;
            x0::log("NOLA n7: gancho do inicio da carga de save falhou: o host NAO avisa o inicio da carga");
            return false;
        }
        x0::log("NOLA n7: gancho do inicio da carga de save instalado (exe+0x38ae8bc: tela de morte, menus e XLoadLastSave passam por ele)");
        return true;
    }
}

namespace
{
    const uint8_t kKd[] = {0x53, 0x48, 0x83, 0xec, 0x20, 0x89, 0xd3, 0x48, 0x8b, 0x41, 0x08, 0x88, 0x50, 0x2d, 0x48, 0x8b, 0x09, 0xe8};
    const uint8_t kWk[] = {0x8b, 0x91, 0x1c, 0x04, 0x00, 0x00, 0x85, 0xd2, 0x74, 0x4e, 0x8b, 0x89, 0x18, 0x04, 0x00, 0x00};
    int g_kdOk = 0;

    using KdFn = void (*)(uint64_t, uint32_t);
    using WkFn = uint64_t (*)(uint64_t);

    bool kd_code()
    {
        if (g_kdOk == 0)
        {
            g_kdOk = eq_seh(exe() + 0x3307254, kKd, sizeof kKd) && eq_seh(exe() + 0x2602220, kWk, sizeof kWk) ? 1 : -1;
            if (g_kdOk < 0) x0::log("NOLA LIFE: estado KnockDown do jogador (exe+0x3307254) com bytes diferentes do esperado: camera caida NAO disponivel nesta versao do jogo");
        }
        return g_kdOk > 0;
    }

    uint64_t wk_seh(uint64_t o)
    {
        __try { return reinterpret_cast<WkFn>(exe() + 0x2602220)(o); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    bool kd_holder(uint64_t pawn, uint64_t& h, uint64_t& hit)
    {
        uint64_t a = 0, o = 0, rec = 0;
        if (!pawn || !rd_seh(reinterpret_cast<void*>(pawn + 0x9e8), &a, 8) || !a) return false;
        h = a + 0x9e0;
        if (!rd_seh(reinterpret_cast<void*>(h), &o, 8) || !o || !rd_seh(reinterpret_cast<void*>(h + 8), &hit, 8) || !hit) return false;
        uint64_t r = wk_seh(o);
        return r && rd_seh(reinterpret_cast<void*>(r + 0x650), &rec, 8) && rec;
    }

    int kd_call(uint64_t h, bool on)
    {
        __try { reinterpret_cast<KdFn>(exe() + 0x3307254)(h, on ? 1u : 0u); return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
    }
}

extern "C" int s2m_life_knock(uint64_t pawn, int on)
{
    if (!kd_code()) return -1;
    uint64_t h = 0, hit = 0;
    if (!kd_holder(pawn, h, hit)) return 0;
    uint8_t b = 0;
    if (on < 0) return rd_seh(reinterpret_cast<void*>(hit + 0x2d), &b, 1) ? (b ? 2 : 1) : 0;
    int r = kd_call(h, on != 0);
    if (r < 1) return r;
    return rd_seh(reinterpret_cast<void*>(hit + 0x2d), &b, 1) && (b != 0) == (on != 0) ? 1 : -3;
}

namespace
{
    struct RArr
    {
        uint64_t p = 0;
        int32_t n = 0, m = 0;
    };

    bool arr_at(uint64_t a, RArr& t) { return rd_seh(reinterpret_cast<void*>(a), &t, 16) && t.n >= 0 && t.n <= t.m && t.m < 1000000 && (t.n == 0 || t.p > 0x10000); }

    int32_t rd_i(uint64_t a)
    {
        int32_t v = INT32_MIN;
        rd_seh(reinterpret_cast<void*>(a), &v, 4);
        return v;
    }

    struct Lay
    {
        bool ok = false;
        int seg = 0, segName = 0, path = 0, pathStart = 0, pathCount = 0;
        int copy = 0, copyAcc = 0, copyDs = 0, copyDe = 0, batch = 0, batchCopies = 0;
        int chain = 0, chainS = 0, chainE = 0, ind = 0, indOff = 0, indType = 0;
        int libSegs = 0, libSrc = 0, libDst = 0, libBatch = 0;
        int sm = 0, smName = 0, smStates = 0, st = 0, stName = 0;
    };

    bool lay_get(Lay& L)
    {
        if (L.ok) return true;
        auto S = [](const wchar_t* n) { return x0::find_path((std::wstring(L"/Script/Engine.") + n).c_str()); };
        x0::Ref seg = S(L"PropertyAccessSegment"), path = S(L"PropertyAccessPath"), copy = S(L"PropertyAccessCopy"), batch = S(L"PropertyAccessCopyBatch");
        x0::Ref chain = S(L"PropertyAccessIndirectionChain"), ind = S(L"PropertyAccessIndirection"), lib = S(L"PropertyAccessLibrary");
        x0::Ref sm = S(L"BakedAnimationStateMachine"), st = S(L"BakedAnimationState"), abc = S(L"AnimBlueprintGeneratedClass");
        int64_t v[] = {
            x0::array_stride(lib, "PathSegments"), x0::field_off(seg, "Name"), x0::array_stride(lib, "SrcPaths"), x0::field_off(path, "PathSegmentStartIndex"), x0::field_off(path, "PathSegmentCount"),
            x0::array_stride(batch, "Copies"), x0::field_off(copy, "AccessIndex"), x0::field_off(copy, "DestAccessStartIndex"), x0::field_off(copy, "DestAccessEndIndex"),
            x0::array_stride(lib, "CopyBatchArray"), x0::field_off(batch, "Copies"),
            x0::struct_size(chain), x0::field_off(chain, "IndirectionStartIndex"), x0::field_off(chain, "IndirectionEndIndex"),
            x0::struct_size(ind), x0::field_off(ind, "Offset"), x0::field_off(ind, "Type"),
            x0::field_off(lib, "PathSegments"), x0::field_off(lib, "SrcPaths"), x0::field_off(lib, "DestPaths"), x0::field_off(lib, "CopyBatchArray"),
            x0::array_stride(abc, "BakedStateMachines"), x0::field_off(sm, "MachineName"), x0::field_off(sm, "States"), x0::array_stride(sm, "States"), x0::field_off(st, "StateName")};
        for (int64_t x : v)
            if (x < 0 || x > 0x1000) return false;
        int* d[] = {&L.seg, &L.segName, &L.path, &L.pathStart, &L.pathCount, &L.copy, &L.copyAcc, &L.copyDs, &L.copyDe, &L.batch, &L.batchCopies,
                    &L.chain, &L.chainS, &L.chainE, &L.ind, &L.indOff, &L.indType, &L.libSegs, &L.libSrc, &L.libDst, &L.libBatch,
                    &L.sm, &L.smName, &L.smStates, &L.st, &L.stName};
        for (size_t k = 0; k < sizeof d / sizeof d[0]; ++k) *d[k] = (int)v[k];
        L.ok = L.seg > 0 && L.path > 0 && L.copy > 0 && L.batch > 0 && L.chain > 0 && L.ind > 0 && L.sm > 0 && L.st > 0;
        return L.ok;
    }

    Lay g_lay;

    struct KdHold
    {
        bool on = false;
        x0::Ref cls;
        uint64_t at = 0;
        uint32_t orig = 0, now = 0;
        int machine = -1, outState = -1, node = -1;
        std::string fail;
    } KH;

    struct Lib
    {
        RArr segs, src, dst, batches, srcAcc, dstAcc, inds;
    };

    std::vector<std::string> names_of(const Lib& b, const RArr& paths, int i)
    {
        std::vector<std::string> out;
        const Lay& L = g_lay;
        if (i < 0 || i >= paths.n) return out;
        uint64_t p = paths.p + (uint64_t)i * L.path;
        int32_t s = rd_i(p + L.pathStart), c = rd_i(p + L.pathCount);
        if (s < 0 || c <= 0 || c > 16 || s + c > b.segs.n) return out;
        for (int k = 0; k < c; ++k)
        {
            uint64_t raw = 0;
            if (!rd_seh(reinterpret_cast<void*>(b.segs.p + (uint64_t)(s + k) * L.seg + L.segName), &raw, 8)) return {};
            out.push_back(x0::fname_str(raw));
        }
        return out;
    }

    bool name_eq(const std::string& a, const char* b)
    {
        size_t n = std::strlen(b);
        if (a.size() != n) return false;
        for (size_t i = 0; i < n; ++i)
            if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
        return true;
    }

    int path_find(const Lib& b, const RArr& paths, std::initializer_list<const char*> want)
    {
        int hit = -1;
        for (int i = 0; i < paths.n; ++i)
        {
            std::vector<std::string> n = names_of(b, paths, i);
            if (n.size() != want.size()) continue;
            bool eq = true;
            size_t k = 0;
            for (const char* w : want) eq = eq && name_eq(n[k++], w);
            if (!eq) continue;
            if (hit >= 0) return -2;
            hit = i;
        }
        return hit;
    }

    bool chain_one(const RArr& acc, int i, int& ind)
    {
        const Lay& L = g_lay;
        if (i < 0 || i >= acc.n) return false;
        uint64_t p = acc.p + (uint64_t)i * L.chain;
        int32_t s = rd_i(p + L.chainS), e = rd_i(p + L.chainE);
        if (s < 0 || e != s + 1) return false;
        ind = s;
        return true;
    }

    int ind_users(const RArr& acc, int ind)
    {
        const Lay& L = g_lay;
        int n = 0;
        for (int i = 0; i < acc.n; ++i)
        {
            uint64_t p = acc.p + (uint64_t)i * L.chain;
            if (rd_i(p + L.chainS) <= ind && ind < rd_i(p + L.chainE)) n++;
        }
        return n;
    }

    bool ind_offset(const Lib& b, int ind, uint32_t& off)
    {
        const Lay& L = g_lay;
        if (ind < 0 || ind >= b.inds.n) return false;
        uint64_t p = b.inds.p + (uint64_t)ind * L.ind;
        uint8_t ty = 0xff;
        return rd_seh(reinterpret_cast<void*>(p + L.indType), &ty, 1) && ty == 0 && rd_seh(reinterpret_cast<void*>(p + L.indOff), &off, 4);
    }

    bool kh_machine(const x0::Ref& cls, int& machine, int& outState, std::string* diag = nullptr)
    {
        const Lay& L = g_lay;
        char b[160];
        auto add = [&](const std::string& t) { if (diag) *diag += t; };
        int64_t o = x0::prop_off(cls, "BakedStateMachines");
        RArr ms;
        machine = outState = -1;
        bool okArr = o >= 0 && arr_at(cls.address() + o, ms);
        snprintf(b, sizeof b, "BakedStateMachines +0x%llx n=%d passo %d, estado passo %d nome +%d; maquinas:", (long long)o, okArr ? ms.n : -1, L.sm, L.st, L.stName);
        add(b);
        if (!okArr) return false;
        for (int i = 0; i < ms.n; ++i)
        {
            uint64_t m = ms.p + (uint64_t)i * L.sm, raw = 0;
            std::string nm = rd_seh(reinterpret_cast<void*>(m + L.smName), &raw, 8) ? x0::fname_str(raw) : "?";
            add(" [" + std::to_string(i) + "] " + nm);
            if (!name_eq(nm, "KnockDown")) continue;
            RArr st;
            if (machine >= 0 || !arr_at(m + L.smStates, st))
            {
                add(machine >= 0 ? " (repetida)" : " (estados ilegiveis)");
                return false;
            }
            machine = i;
            add(" {");
            for (int k = 0; k < st.n; ++k)
            {
                std::string sn = rd_seh(reinterpret_cast<void*>(st.p + (uint64_t)k * L.st + L.stName), &raw, 8) ? x0::fname_str(raw) : "?";
                add((k ? "," : "") + sn);
                if (name_eq(sn, "Out")) outState = k;
            }
            add("}");
        }
        return machine >= 0 && outState >= 0;
    }

    int kh_node(const x0::Ref& cls, uint64_t inst, int machine, std::string& diag)
    {
        char b[200];
        int64_t fi = x0::field_off(x0::find_path(L"/Script/Engine.AnimNode_StateMachine"), "StateMachineIndexInClass");
        int csz = x0::struct_size(x0::find_path(L"/Script/Engine.AnimBlueprintGeneratedClass"));
        if (fi < 0 || csz < 0x40 || csz > 0x4000)
        {
            diag = "AnimNode_StateMachine/AnimBlueprintGeneratedClass sem reflexao";
            return -1;
        }
        uint64_t P = 0;
        int nsm = 0;
        for (const auto& pr : x0::struct_props(cls, L"AnimNode_StateMachine"))
        {
            nsm++;
            if (rd_i(inst + (uint64_t)pr.second + (uint64_t)fi) != machine) continue;
            if (P)
            {
                diag = "dois nos AnimNode_StateMachine com StateMachineIndexInClass " + std::to_string(machine);
                return -1;
            }
            P = pr.first;
        }
        if (!P)
        {
            diag = "no AnimNode_StateMachine com StateMachineIndexInClass " + std::to_string(machine) + " nao achado (" + std::to_string(nsm) + " nos)";
            return -1;
        }
        int best = -1, bestN = 0, bestOff = 0, ties = 0;
        for (int o = 0x30; o + 16 <= csz; o += 8)
        {
            RArr a;
            if (!arr_at(cls.address() + (uint64_t)o, a) || a.n <= nsm || a.n > 20000) continue;
            int at = -1, hits = 0;
            for (int k = 0; k < a.n && hits >= 0; ++k)
            {
                uint64_t e = 0;
                if (!rd_seh(reinterpret_cast<void*>(a.p + (uint64_t)k * 8), &e, 8)) hits = -1;
                else if (e == P) at = k, hits++;
            }
            if (hits != 1) continue;
            if (a.n > bestN) bestN = a.n, best = a.n - 1 - at, bestOff = o, ties = 0;
            else if (a.n == bestN) ties++;
        }
        if (best < 0 || ties)
        {
            snprintf(b, sizeof b, "lista AnimNodeProperties da classe nao achada (candidatas iguais=%d)", ties);
            diag = b;
            return -1;
        }
        snprintf(b, sizeof b, "no da maquina: AnimNodeProperties +0x%x n=%d, %d nos de maquina, MachineIndex %d", bestOff, bestN, nsm, best);
        diag = b;
        return best;
    }

    std::string kh_resolve(uint64_t inst, KdHold& r)
    {
        if (!lay_get(g_lay)) return "estruturas PropertyAccess/BakedAnimationStateMachine sem reflexao";
        const Lay& L = g_lay;
        x0::Ref ai = x0::Ref::addr(inst);
        uint64_t c = 0;
        if (!ai || !rd_seh(reinterpret_cast<void*>(inst + 0x10), &c, 8)) return "AnimInstance do jogador invalido";
        x0::Ref cls = x0::Ref::addr(c);
        if (!cls || x0::obj_name(cls) != "AnimBP_Player_C" || !x0::is_a(cls, L"AnimBlueprintGeneratedClass")) return "classe da AnimInstance nao e AnimBP_Player_C";
        uint64_t data = 0;
        x0::Ref st;
        if (!x0::sparse_of(cls, data, st) || x0::obj_name(st) != "AnimBlueprintGeneratedConstantData") return "sem AnimBlueprintGeneratedConstantData";
        int64_t ext = x0::field_off(st, "AnimBlueprintExtension_PropertyAccess");
        int64_t libo = x0::field_off(x0::find_path(L"/Script/Engine.AnimSubsystem_PropertyAccess"), "Library");
        if (ext < 0 || libo < 0) return "sem AnimBlueprintExtension_PropertyAccess.Library";
        uint64_t lib = data + ext + libo;
        Lib b;
        uint64_t tail = lib + L.libBatch + 0x10;
        if (!arr_at(lib + L.libSegs, b.segs) || !arr_at(lib + L.libSrc, b.src) || !arr_at(lib + L.libDst, b.dst) || !arr_at(lib + L.libBatch, b.batches) ||
            !arr_at(tail, b.srcAcc) || !arr_at(tail + 0x10, b.dstAcc) || !arr_at(tail + 0x20, b.inds))
            return "biblioteca PropertyAccess ilegivel";
        if (b.srcAcc.n != b.src.n || b.dstAcc.n != b.dst.n || b.inds.n == 0) return "biblioteca PropertyAccess sem os acessos resolvidos";
        int d = path_find(b, b.dst, {"AnimGraphNode_TransitionResult_32", "bCanEnterTransition"});
        if (d < 0) return "destino AnimGraphNode_TransitionResult_32.bCanEnterTransition nao achado (ou repetido)";
        int a = -1;
        for (int k = 0; k < b.batches.n; ++k)
        {
            RArr cp;
            if (!arr_at(b.batches.p + (uint64_t)k * L.batch + L.batchCopies, cp)) return "lote de copias ilegivel";
            for (int j = 0; j < cp.n; ++j)
            {
                uint64_t p = cp.p + (uint64_t)j * L.copy;
                if (rd_i(p + L.copyDs) <= d && d < rd_i(p + L.copyDe))
                {
                    if (a >= 0) return "duas copias para TransitionResult_32";
                    a = rd_i(p + L.copyAcc);
                }
            }
        }
        std::vector<std::string> sn = names_of(b, b.src, a);
        if (sn.size() != 2 || sn[0] != "StateData" || sn[1] != "bInAir") return "a regra de TransitionResult_32 nao le StateData.bInAir";
        int iFrom = -1, iTo = -1;
        uint32_t offFrom = 0, offTo = 0;
        int64_t want = x0::prop_off(ai, "StateData.bInAir"), to = x0::prop_off(ai, "HitData.bKnockedDown");
        if (!chain_one(b.srcAcc, a, iFrom) || !ind_offset(b, iFrom, offFrom) || want < 0 || offFrom != (uint32_t)want) return "acesso de StateData.bInAir com indirecao inesperada";
        int kd = path_find(b, b.src, {"HitData", "bKnockedDown"});
        if (kd < 0 || !chain_one(b.srcAcc, kd, iTo) || !ind_offset(b, iTo, offTo) || to < 0 || offTo != (uint32_t)to) return "acesso de HitData.bKnockedDown com indirecao inesperada";
        if (ind_users(b.srcAcc, iFrom) != 1 || ind_users(b.dstAcc, iFrom) != 0) return "indirecao de TransitionResult_32 compartilhada";
        int m = -1, s = -1;
        std::string diag;
        bool okM = kh_machine(cls, m, s, &diag);
        x0::logf("NOLA LIFE: regra de levantar: %s", diag.c_str());
        if (!okM) return "maquina KnockDown/estado Out nao achados";
        std::string nd;
        r.node = kh_node(cls, inst, m, nd);
        x0::logf("NOLA LIFE: regra de levantar: %s", nd.c_str());
        r.cls = cls;
        r.at = b.inds.p + (uint64_t)iFrom * L.ind + L.indOff;
        r.orig = offFrom;
        r.now = offTo;
        r.machine = m;
        r.outState = s;
        return {};
    }

    bool wr_u32(uint64_t a, uint32_t v)
    {
        uint32_t cur = 0;
        return cp_seh(reinterpret_cast<void*>(a), &v, 4) && rd_seh(reinterpret_cast<void*>(a), &cur, 4) && cur == v;
    }

    uint64_t anim_of(uint64_t pawn)
    {
        uint64_t h = 0, hit = 0, ai = 0;
        if (!kd_code() || !kd_holder(pawn, h, hit) || !rd_seh(reinterpret_cast<void*>(h), &ai, 8)) return 0;
        return ai;
    }
}

extern "C" int s2m_life_kdhold(uint64_t pawn, int on)
{
    if (on < 0) return KH.on ? 1 : 0;
    if (!on)
    {
        if (!KH.on) return 0;
        KH.on = false;
        uint32_t cur = 0;
        if (!KH.cls.valid() || !rd_seh(reinterpret_cast<void*>(KH.at), &cur, 4) || cur != KH.now)
        {
            x0::log("NOLA LIFE: regra de levantar: classe AnimBP_Player_C trocada ou ja limpa, nada a restaurar");
            return 2;
        }
        bool ok = wr_u32(KH.at, KH.orig);
        x0::logf("NOLA LIFE: regra de levantar restaurada: TransitionResult_32 volta a ler StateData.bInAir (+0x%x) ok=%s", KH.orig, ok ? "true" : "false");
        return ok ? 1 : -4;
    }
    if (KH.on) return 1;
    uint64_t ai = anim_of(pawn);
    if (!ai) return -2;
    KdHold r;
    std::string why = kh_resolve(ai, r);
    if (!why.empty())
    {
        if (why != KH.fail) x0::logf("NOLA LIFE: regra de levantar NAO trocada: %s", why.c_str());
        KH.fail = why;
        return -3;
    }
    if (!wr_u32(r.at, r.now)) return -4;
    r.on = true;
    KH = r;
    x0::logf("NOLA LIFE: regra de levantar trocada: TransitionResult_32 (KnockDown Falling->Out) le HitData.bKnockedDown (+0x%x) em vez de StateData.bInAir (+0x%x)", r.now, r.orig);
    return 1;
}

extern "C" double s2m_life_kdout_left(uint64_t pawn)
{
    uint64_t ai = anim_of(pawn), c = 0;
    if (!ai || KH.node < 0 || KH.outState < 0 || !KH.cls.valid() || !rd_seh(reinterpret_cast<void*>(ai + 0x10), &c, 8) || c != KH.cls.address()) return -1;
    x0::Call k(x0::Ref::addr(ai), L"GetRelevantAnimTimeRemaining");
    k.i(L"MachineIndex", KH.node).i(L"StateIndex", KH.outState);
    return k.run() ? (double)k.rf() : -1;
}

extern "C" bool s2m_life_gm_keep(bool on) { return gm_mask(on ? 0x04 : 0x06); }

extern "C" bool s2m_life_load_block(bool on)
{
    if (on) bv_install();
    if (on && !lu_install()) return false;
    InterlockedExchange(&g_luBlock, on ? 1 : 0);
    return true;
}

extern "C" bool s2m_life_load_hook() { return bv_install(); }

extern "C" int s2m_life_load_tries() { return (int)g_luTries; }

extern "C" int s2m_life_load_hidden(int* builds)
{
    if (builds) *builds = (int)g_bvBuilds;
    return (int)g_bvHidden;
}

extern "C" int s2m_load_begins(char* guid33)
{
    if (guid33) std::snprintf(guid33, 33, "none");
    if (!lb_install()) return 0;
    int n = (int)g_lbN;
    if (guid33 && n > 0 && g_lbHas) std::snprintf(guid33, 33, "%08X%08X%08X%08X", g_lbGuid[0], g_lbGuid[1], g_lbGuid[2], g_lbGuid[3]);
    return n;
}

extern "C" double s2m_life_lowhp()
{
    double best = -1;
    int n = 0;
    for (const x0::Ref& r : x0::all_of(L"MaterialInstanceDynamic"))
    {
        if (x0::obj_name(r).rfind("MID_PPI_LowHP", 0) != 0) continue;
        ++n;
        x0::Call c(r, L"K2_GetScalarParameterValue");
        c.n(L"ParameterName", L"LowHPIntensity");
        if (c.run() && c.rf() > best) best = c.rf();
    }
    static int lastN = -1;
    if (n != lastN) x0::logf("NOLA LIFE: efeito de vida baixa: %d MID_PPI_LowHP no jogo", n);
    lastN = n;
    return n ? best : -1;
}
