#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <Windows.h>

namespace s2rel
{
    using FnStorage = uint8_t* (*)();
    using FnHandle = uint64_t (*)(void*);
    using FnRel = int32_t (*)(uint64_t, uint64_t);
    using FnLevel = uint64_t (*)(int32_t*);
    using FnTime = float (*)(void*);
    using FnSet = void (*)(uint64_t, uint64_t, int32_t, float, uint8_t, uint8_t, uint8_t, uint8_t);

    struct Nat
    {
        bool tried = false, ok = false, canSet = false;
        char why[200] = {};
        FnStorage storage = nullptr; FnHandle handle = nullptr; FnRel rel = nullptr; FnLevel level = nullptr;
        FnTime time = nullptr; void** timeArg = nullptr; FnSet set = nullptr;
    };
    inline Nat N;

    struct FP { uint32_t off; uint8_t b[12]; uint32_t n; };
    const FP kGet[] = {
        {0x000, {0x56,0x57,0x53,0x48,0x83,0xEC,0x70}, 7},
        {0x016, {0x83,0xFA,0xEF,0x0F,0x87}, 5},
        {0x02e, {0xE8}, 1}, {0x047, {0x48,0x69,0xC9,0x00,0x07,0x00,0x00}, 7}, {0x052, {0x48,0x83,0xC7,0x10}, 4},
        {0x056, {0xE8}, 1}, {0x06f, {0x48,0x69,0xC9,0x00,0x07,0x00,0x00}, 7},
        {0x07d, {0xE8}, 1}, {0x088, {0xE8}, 1}, {0x093, {0xE8}, 1},
        {0x098, {0x48,0x8D,0x74,0x24,0x4C,0x89,0x06}, 7}, {0x0a2, {0xE8}, 1},
    };
    const FP kSet[] = {
        {0x000, {0x41,0x56,0x56,0x57,0x53,0x48,0x83,0xEC,0x58}, 9},
        {0x016, {0xE8}, 1}, {0x030, {0xE8}, 1},
        {0x068, {0x48,0x8B,0x05}, 3}, {0x072, {0xF2,0x0F,0x5A,0xB0,0xA0,0x00,0x00,0x00}, 8}, {0x07a, {0xE8}, 1}, {0x085, {0xE8}, 1},
        {0x08a, {0x31,0xC9,0x88,0x4C,0x24,0x38}, 6}, {0x098, {0xC6,0x44,0x24,0x28,0x01}, 5},
        {0x0a9, {0xE8}, 1},
    };
    inline float time_read(void* p) { return static_cast<float>(*reinterpret_cast<double*>(static_cast<uint8_t*>(p) + 0xa0)); }
    constexpr uint32_t kGetRva = 0x6bbac9a, kSetRva = 0x6b4fa86;

    template <size_t K> inline bool fp_ok(const uint8_t* p, const FP (&f)[K])
    {
        __try
        {
            for (size_t k = 0; k < K; ++k)
                for (uint32_t i = 0; i < f[k].n; ++i)
                    if (p[f[k].off + i] != f[k].b[i]) return false;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    inline uint8_t* call_at(uint8_t* p, uint32_t off) { return p + off + 5 + *reinterpret_cast<int32_t*>(p + off + 1); }

    inline bool text_range(uint8_t*& lo, size_t& n)
    {
        auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
        auto* s = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s)
            if (std::memcmp(s->Name, ".text", 5) == 0) { lo = base + s->VirtualAddress; n = s->Misc.VirtualSize; return true; }
        return false;
    }
    template <size_t K> inline uint8_t* locate(uint32_t rva, const FP (&f)[K], const char*& via)
    {
        auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        if (fp_ok(base + rva, f)) { via = "rva"; return base + rva; }
        uint8_t* lo; size_t n;
        if (text_range(lo, n))
            for (size_t i = 0; i + 0x100 < n; ++i)
                if (lo[i] == f[0].b[0] && lo[i + 1] == f[0].b[1] && lo[i + 2] == f[0].b[2] && fp_ok(lo + i, f)) { via = "aob"; return lo + i; }
        via = "nao achado";
        return nullptr;
    }

    inline bool resolve()
    {
        if (N.tried) return N.ok;
        N.tried = true;
        auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        const char *v1 = "", *v2 = "";
        uint8_t* g = locate(kGetRva, kGet, v1);
        if (!g) { strcpy_s(N.why, "GetRelationBetween impl nao achado (rva+aob)"); return false; }
        N.storage = reinterpret_cast<FnStorage>(call_at(g, 0x02e));
        N.handle = reinterpret_cast<FnHandle>(call_at(g, 0x07d));
        N.rel = reinterpret_cast<FnRel>(call_at(g, 0x093));
        N.level = reinterpret_cast<FnLevel>(call_at(g, 0x0a2));
        if (reinterpret_cast<uint8_t*>(N.storage) != call_at(g, 0x056) || reinterpret_cast<uint8_t*>(N.handle) != call_at(g, 0x088))
        { strcpy_s(N.why, "GetRelationBetween: chamadas nao conferem"); return false; }
        N.ok = true;
        uint8_t* s = locate(kSetRva, kSet, v2);
        if (s && reinterpret_cast<uint8_t*>(N.storage) == call_at(s, 0x016) && reinterpret_cast<uint8_t*>(N.handle) == call_at(s, 0x07a))
        {
            N.timeArg = reinterpret_cast<void**>(s + 0x068 + 7 + *reinterpret_cast<int32_t*>(s + 0x068 + 3));
            N.time = &time_read;
            N.set = reinterpret_cast<FnSet>(call_at(s, 0x0a9));
            N.canSet = true;
        }
        sprintf_s(N.why, "ler=%s(+%#llx) rel=+%#llx gravar=%s%s", v1, (unsigned long long)(g - base),
                  (unsigned long long)(reinterpret_cast<uint8_t*>(N.rel) - base), v2, N.canSet ? "" : " (desligado)");
        return true;
    }

    inline uint8_t* slot_of(uint32_t uid)
    {
        uint8_t* c = N.storage();
        uint32_t i = uid & 0x7ffffff;
        while (i >= 0x400) { c = *reinterpret_cast<uint8_t**>(c); if (!c) return nullptr; i -= 0x400; }
        return c + (size_t)i * 0x700;
    }

    inline int get(uint32_t a, uint32_t b, int32_t& value, int& level)
    {
        if (!resolve()) return 1;
        if (a >= 0xfffffff0u || b >= 0xfffffff0u) return 2;
        __try
        {
            uint8_t* sa = slot_of(a); uint8_t* sb = slot_of(b);
            if (!sa || !sb) return 2;
            uint64_t hb = N.handle(sb + 0x10), ha = N.handle(sa + 0x10);
            value = N.rel(ha, hb);
            int32_t v = value;
            level = static_cast<int>(N.level(&v) & 0xff);
            return 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 9; }
    }

    inline int set(uint32_t a, uint32_t b, int32_t value)
    {
        if (!resolve()) return 1;
        if (!N.canSet) return 3;
        if (a >= 0xfffffff0u || b >= 0xfffffff0u) return 2;
        __try
        {
            uint8_t* sa = slot_of(a); uint8_t* sb = slot_of(b);
            if (!sa || !sb) return 2;
            float t = N.time(*N.timeArg);
            uint64_t hb = N.handle(sb + 0x10), ha = N.handle(sa + 0x10);
            N.set(ha, hb, value, t, 0, 1, 0, 0);
            return 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 9; }
    }
}
