#include "s2m_oodle.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <mutex>

namespace s2m_oodle
{
    namespace
    {
        constexpr uintptr_t kDec = 0x20b8c87;
        constexpr uintptr_t kInit = 0x31be684;
        constexpr uintptr_t kEnc = 0x31be6ec;
        constexpr uintptr_t kBound = 0x2689f00;
        constexpr uint8_t kKraken = 8;
        constexpr uint8_t kCompressor = 3;
        constexpr int8_t kLevel = 3;

        struct Sig
        {
            uintptr_t rva;
            const char* name;
            uint8_t b[24];
            uint8_t m[24];
            size_t n;
        };

        const Sig kSigs[] = {
            {kDec, "Decompress", {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x78, 0x4c, 0x89, 0xcf, 0x4d, 0x89, 0xc6},
             {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 22},
            {kInit, "pool init", {0x48, 0x83, 0xec, 0x28, 0x8b, 0x05, 0, 0, 0, 0, 0x8b, 0x0d, 0, 0, 0, 0, 0x65, 0x48, 0x8b, 0x14, 0x25, 0x58, 0x00, 0x00},
             {1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1}, 24},
            {kEnc, "Compress", {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xec, 0xc8, 0x00, 0x00, 0x00, 0x4d, 0x89, 0xc4, 0x49, 0x89},
             {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 24},
            {kBound, "size bound", {0x4c, 0x8b, 0xc2, 0xb8, 0x00, 0x3b, 0x00, 0x00, 0xd3, 0xe8, 0xb9, 0x18, 0x00, 0x00, 0x00, 0xa8, 0x01},
             {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 17},
        };

        using DecFn = bool (*)(void*, int64_t, const void*, int64_t);
        using InitFn = void (*)();
        using EncFn = int64_t (*)(void*, int64_t, const void*, int64_t, uint8_t, int8_t, bool, int64_t);
        using BoundFn = int64_t (*)(uint8_t, int64_t);

        uint8_t* exe() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }

        bool rd_seh(const void* p, void* out, size_t n)
        {
            __try { std::memcpy(out, p, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool dec_seh(DecFn f, void* dst, int64_t rawLen, const void* src, int64_t srcLen, bool& ok)
        {
            __try { ok = f(dst, rawLen, src, srcLen); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool bound_seh(BoundFn f, int64_t len, int64_t& out)
        {
            __try { out = f(kKraken, len); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool enc_seh(InitFn init, EncFn f, void* dst, int64_t cap, const void* src, int64_t len, int64_t& n)
        {
            __try
            {
                init();
                n = f(dst, cap, src, len, kCompressor, kLevel, false, 0);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        int g_state = 0;
        std::string g_err;
        std::once_flag g_once;

        void probe()
        {
            uint8_t* b = exe();
            for (auto& s : kSigs)
            {
                uint8_t got[24] = {};
                if (!rd_seh(b + s.rva, got, s.n)) { g_err = std::string(s.name) + " not readable"; g_state = 2; return; }
                for (size_t i = 0; i < s.n; ++i)
                    if (s.m[i] && got[i] != s.b[i]) { g_err = std::string("Oodle ") + s.name + " bytes differ (new exe?)"; g_state = 2; return; }
            }
            g_state = 1;
        }

        bool check()
        {
            std::call_once(g_once, probe);
            return g_state == 1;
        }
    }

    auto ready(std::string& err) -> bool
    {
        bool ok = check();
        if (!ok) err = g_err;
        return ok;
    }

    auto codec() -> s2m_wsplice::Codec
    {
        s2m_wsplice::Codec c;
        c.dec = [](const std::string& st, uint32_t rawSize, std::string& raw, std::string& err) -> bool {
            if (!check()) { err = g_err; return false; }
            raw.assign((size_t)rawSize + 64, '\0');
            bool ok = false;
            if (!dec_seh(reinterpret_cast<DecFn>(exe() + kDec), raw.data(), (int64_t)rawSize, st.data(), (int64_t)st.size(), ok)) { err = "Oodle decode faulted"; return false; }
            if (!ok) { err = "Oodle decode returned false"; return false; }
            raw.resize(rawSize);
            return true;
        };
        c.enc = [](const std::string& raw, std::string& st, std::string& err) -> bool {
            if (!check()) { err = g_err; return false; }
            int64_t cap = 0;
            if (!bound_seh(reinterpret_cast<BoundFn>(exe() + kBound), (int64_t)raw.size(), cap) || cap <= 0) { err = "Oodle size bound failed"; return false; }
            st.assign((size_t)cap, '\0');
            int64_t n = 0;
            if (!enc_seh(reinterpret_cast<InitFn>(exe() + kInit), reinterpret_cast<EncFn>(exe() + kEnc), st.data(), cap, raw.data(), (int64_t)raw.size(), n))
            {
                err = "Oodle encode faulted";
                return false;
            }
            if (n <= 0 || n > cap) { err = "Oodle encode returned " + std::to_string(n); return false; }
            st.resize((size_t)n);
            return true;
        };
        return c;
    }
}
