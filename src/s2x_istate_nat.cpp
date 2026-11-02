#include <windows.h>

#include "s2x_istate_nat.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>

namespace s2x_isn
{
    namespace
    {
        constexpr uintptr_t kTab = 0xb10f588;
        constexpr uintptr_t kPool = 0xa74b120;
        constexpr uintptr_t kMgr = 0xa2c85f8;
        constexpr uintptr_t kPoster = 0xa2c85d8;
        constexpr uintptr_t kDtor = 0x8c95f90;
        constexpr uintptr_t kRot = 0x9fa52c0;
        constexpr uintptr_t kUpgMap = 0xadc5f08;
        constexpr int kMaxUpg = 48;
        constexpr int kMaxAtt = 24;
        constexpr int kMaxList = 1024;

        struct RmStr
        {
            const wchar_t* d;
            int32_t n;
            int32_t m;
        };

        struct RS
        {
            uint32_t uid;
            int sid;
            int count;
            uint8_t fl;
            uint8_t flag;
            uint8_t hasDur;
            uint8_t weapon;
            uint8_t hasAmmo;
            uint8_t type;
            uint8_t cal;
            uint8_t mode;
            float dur;
            int rounds;
            int nA, nB, nAtt;
            int A[kMaxUpg];
            int B[kMaxUpg];
            int att[kMaxAtt];
            int blk;
            uint8_t blk8[8];
            uint16_t blk2;
        };

        struct Blk
        {
            int sid;
            uint8_t b8[8];
            uint16_t b2;
            uint16_t pad;
        };

        using FnRec = char(__fastcall*)(uintptr_t);
        using FnPtr = uintptr_t(__fastcall*)(uintptr_t);
        using FnIsWpn = char(__fastcall*)(uintptr_t, int);
        using FnFlagSet = void(__fastcall*)(uintptr_t, char);
        using FnSidOk = char(__fastcall*)(uintptr_t, int);
        using FnMapFind = int(__fastcall*)(uintptr_t, int, int*);
        using FnUpgOwn = void(__fastcall*)(uintptr_t, int*, char);
        using FnUpgAct = void(__fastcall*)(uintptr_t, int*);
        using FnAttach = uint64_t(__fastcall*)(uintptr_t, uint32_t, int*);
        using FnBlkSet = void(__fastcall*)(uintptr_t, Blk*);
        using FnByte = void(__fastcall*)(uintptr_t, uint8_t);
        using FnVoid = void(__fastcall*)(uintptr_t);
        using FnInt = void(__fastcall*)(uintptr_t, int);
        using FnPost = void(__fastcall*)(uintptr_t, void*);
        using FnDtor = void(__fastcall*)(void*);
        using FnGround = uintptr_t(__fastcall*)(uintptr_t, double*, uintptr_t, int*);
        using FnSnapNew = uint32_t*(__fastcall*)(uintptr_t, uint32_t*, uint32_t*, uint32_t*);
        using FnRef = void(__fastcall*)(uintptr_t, int);
        using FnIntern = int(__fastcall*)(uintptr_t, RmStr*, bool);
        using FnDurGet = float(__fastcall*)(uintptr_t);
        using FnDurSet = void(__fastcall*)(uintptr_t, float);
        using FnSet = uintptr_t(__fastcall*)(uintptr_t);

        uintptr_t base() { return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)); }
        template <class T> T fn(uintptr_t rva) { return reinterpret_cast<T>(base() + rva); }
        template <class T> T rd(uintptr_t a) { return *reinterpret_cast<volatile T*>(a); }

        bool eq(uintptr_t rva, std::initializer_list<uint8_t> b)
        {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(base() + rva);
            size_t i = 0;
            for (uint8_t v : b)
                if (p[i++] != v) return false;
            return true;
        }

        bool checkSeh(int& ok)
        {
            __try
            {
                ok = eq(0x650894, {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x30, 0x48, 0x8b, 0x05}) &&
                     eq(0x650922, {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x30, 0x48, 0x89, 0xce}) &&
                     eq(0xc5a3a4, {0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x68, 0x48, 0x89, 0xce}) &&
                     eq(0x22cc2f2, {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x30, 0x89, 0xd6}) &&
                     eq(0xf73f18, {0x8a, 0x41, 0x41, 0x24, 0x02, 0xd0, 0xe8, 0xc3}) &&
                     eq(0x3491a6a, {0x8a, 0x41, 0x41, 0x00, 0xd2, 0x24, 0xfd, 0x08, 0xd0, 0x88, 0x41, 0x41}) &&
                     eq(0x4da52e, {0x48, 0x8b, 0x89, 0x90, 0x00, 0x00, 0x00, 0xe9}) &&
                     eq(0x2ead6c6, {0x8a, 0x41, 0x20, 0xc3}) &&
                     eq(0x1170dc4, {0xe9, 0x8b, 0xde, 0x3c, 0xff}) &&
                     eq(0x25fac38, {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x20, 0x89, 0xd7, 0x48, 0x8d, 0x99, 0x90, 0x00, 0x02, 0x00}) &&
                     eq(0x52e700, {0x41, 0x57, 0x41, 0x56, 0x56, 0x57, 0x55, 0x53, 0x8b, 0x41, 0x08, 0x3b, 0x41, 0x34}) &&
                     eq(0x2fbf2e2, {0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x38, 0x44, 0x89, 0xc3}) &&
                     eq(0x2fbefca, {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xec, 0x98}) &&
                     eq(0x347d2b4, {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x58}) &&
                     eq(0x68e2a64, {0x48, 0x8d, 0x81, 0x38, 0x02, 0x00, 0x00, 0xc3, 0x56, 0x57, 0x48, 0x83, 0xec, 0x28, 0x48, 0x89}) &&
                     eq(0x1fd523e, {0x88, 0x91, 0xa8, 0x02, 0x00, 0x00, 0xc3}) &&
                     eq(0x1190294, {0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x38, 0x48, 0x89, 0xce}) &&
                     eq(0x1fd524e, {0x89, 0x91, 0xdc, 0x02, 0x00, 0x00, 0xc3}) &&
                     eq(0x2dfbe74, {0x88, 0x51, 0x20, 0xc3}) &&
                     eq(0xc5bd5e, {0x56, 0x48, 0x83, 0xec, 0x20, 0x48, 0x89, 0xce, 0x03, 0x51, 0x44, 0x89, 0x51, 0x44}) &&
                     eq(0x2222242, {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x58}) &&
                     eq(0x35f49d7, {0x89, 0x6c, 0x24, 0x40, 0x44, 0x89, 0x6c, 0x24, 0x44, 0xc6, 0x44, 0x24, 0x60, 0x12, 0xc7, 0x44, 0x24, 0x68, 0x2c, 0x00, 0x00}) &&
                     eq(0x2edf150, {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x58}) &&
                     eq(0x6b46058, {0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xec, 0xa8, 0x00, 0x00, 0x00, 0x4c, 0x89, 0xcb}) &&
                     eq(0x84bb22, {0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x20}) &&
                     eq(0xa6cd26, {0x56, 0x48, 0x83, 0xec, 0x20, 0x48, 0x89, 0xce, 0x80, 0xb9, 0x38, 0x01, 0x00, 0x00, 0x00}) &&
                     eq(0xec446e, {0x48, 0x8d, 0x41, 0x78, 0xc3}) &&
                     eq(0x35e314, {0x48, 0x89, 0xd0, 0x48, 0x8b, 0x49, 0x08, 0x8b, 0x49, 0x08, 0x89, 0x0a}) &&
                     eq(0x25a231a, {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x40, 0x48, 0x89, 0xce, 0x48, 0x8b, 0x05}) &&
                     eq(0x259c49c, {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x20, 0x85, 0xd2, 0x7e, 0x3b}) &&
                     eq(0x1ffefc6, {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x48})
                         ? 1
                         : 0;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        uintptr_t recRaw(uint32_t uid)
        {
            if (uid >= 0xfffffff0u) return 0;
            uint32_t u = uid & 0x7ffffff;
            uintptr_t blk = base() + kPool;
            while (u > 0xfff)
            {
                blk = rd<uintptr_t>(blk);
                if (!blk) return 0;
                u -= 0x1000;
            }
            uintptr_t r = blk + static_cast<uintptr_t>(u) * 0x88 + 0x10;
            return rd<uint32_t>(r + 8) == uid ? r : 0;
        }

        int readSet(uintptr_t set, int* out, int cap)
        {
            if (!set) return 0;
            uintptr_t data = rd<uintptr_t>(set);
            int num = rd<int>(set + 8);
            uintptr_t sec = rd<uintptr_t>(set + 0x20);
            uintptr_t bits = sec ? sec : set + 0x10;
            int k = 0;
            if (num < 0 || num > 4096 || (num > 0 && !data)) return 0;
            for (int i = 0; i < num && k < cap; ++i)
            {
                uint32_t w = rd<uint32_t>(bits + static_cast<uintptr_t>(i >> 5) * 4);
                if (!(w & (1u << (i & 31)))) continue;
                out[k++] = rd<int>(data + static_cast<uintptr_t>(i) * 12);
            }
            return k;
        }

        int readAtt(uintptr_t data, int* out, int cap)
        {
            uintptr_t a = rd<uintptr_t>(data + 0x78);
            int n = rd<int>(data + 0x80);
            if (n < 0 || n > 256 || (n > 0 && !a)) return 0;
            int k = 0;
            for (int i = 0; i < n && k < cap; ++i) out[k++] = rd<int>(a + static_cast<uintptr_t>(i) * 4);
            return k;
        }

        bool capRaw(uint32_t id, bool state, RS& o)
        {
            __try
            {
                std::memset(&o, 0, sizeof o);
                uintptr_t rec = recRaw(id);
                if (!rec) return false;
                o.uid = rd<uint32_t>(rec + 8);
                o.sid = rd<int>(rec + 0x38);
                o.count = rd<int>(rec + 0x44);
                o.fl = rd<uint8_t>(rec + 0x40);
                if (!state) return true;
                o.flag = (uint8_t)fn<FnRec>(0xf73f18)(rec);
                if (fn<FnRec>(0x650894)(rec))
                {
                    uintptr_t d = fn<FnPtr>(0x650922)(rec);
                    if (d)
                    {
                        o.hasDur = 1;
                        o.dur = (*reinterpret_cast<FnDurGet*>(rd<uintptr_t>(d) + 8))(d);
                    }
                    uintptr_t u = fn<FnPtr>(0xc5a3a4)(rec);
                    if (u)
                    {
                        o.nA = readSet((*reinterpret_cast<FnSet*>(rd<uintptr_t>(u) + 8))(u), o.A, kMaxUpg);
                        o.nB = readSet((*reinterpret_cast<FnSet*>(rd<uintptr_t>(u) + 0x10))(u), o.B, kMaxUpg);
                    }
                }
                if (fn<FnIsWpn>(0x22cc2f2)(rec, 0))
                {
                    uintptr_t data = rd<uintptr_t>(rec + 0x30);
                    if (!data) return false;
                    o.weapon = 1;
                    o.nAtt = readAtt(data, o.att, kMaxAtt);
                    uintptr_t pr = fn<FnPtr>(0x1170dc4)(data);
                    uintptr_t ws = fn<FnPtr>(0x4da52e)(data);
                    if (ws)
                    {
                        o.hasAmmo = 1;
                        o.type = pr ? rd<uint8_t>(pr + 0x155) : rd<uint8_t>(ws + 0x21);
                        o.rounds = rd<int>(ws + 0x2dc);
                        o.cal = rd<uint8_t>(ws + 0x2a8);
                        o.mode = (uint8_t)fn<FnRec>(0x2ead6c6)(ws);
                    }
                    o.blk = rd<int>(data + 0x238);
                    for (int i = 0; i < 8; ++i) o.blk8[i] = rd<uint8_t>(data + 0x23c + i);
                    o.blk2 = rd<uint16_t>(data + 0x244);
                }
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool listRaw(uintptr_t ctr, uint32_t* uids, int cap, int& n)
        {
            n = 0;
            __try
            {
                int cnt = rd<int>(ctr + 0x118);
                uintptr_t arr = rd<uintptr_t>(ctr + 0x110);
                if (cnt < 0 || cnt > cap || (cnt > 0 && !arr)) return false;
                for (int i = 0; i < cnt; ++i)
                {
                    uint32_t uid = rd<uint32_t>(arr + static_cast<uintptr_t>(i) * 16);
                    if (uid >= 0xfffffff0u) continue;
                    uids[n++] = uid;
                }
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool inCtr(uintptr_t ctr, uint32_t uid)
        {
            int cnt = rd<int>(ctr + 0x118);
            uintptr_t arr = rd<uintptr_t>(ctr + 0x110);
            if (cnt < 0 || cnt > kMaxList || (cnt > 0 && !arr)) return false;
            for (int i = 0; i < cnt; ++i)
                if (rd<uint32_t>(arr + static_cast<uintptr_t>(i) * 16) == uid) return true;
            return false;
        }

        int nameRaw(int h, wchar_t* buf, int cap)
        {
            __try
            {
                if (h <= 0) return 0;
                uintptr_t tab = base() + kTab;
                uintptr_t pg = rd<uintptr_t>(tab + static_cast<uintptr_t>(h >> 10) * 8);
                if (!pg) return 0;
                uintptr_t e = pg + static_cast<uintptr_t>(h & 0x3ff) * 0x20;
                const wchar_t* s = rd<const wchar_t*>(e);
                int len = rd<int>(e + 8);
                if (!s || len < 2 || len - 1 > cap) return 0;
                for (int k = 0; k < len - 1; ++k) buf[k] = s[k];
                return len - 1;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        }

        int internRaw(const wchar_t* w, int len)
        {
            __try
            {
                RmStr s{w, len, len};
                return fn<FnIntern>(0x25a231a)(base() + kTab, &s, true);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        }

        void releaseRaw(const int* h, int n)
        {
            __try
            {
                for (int i = 0; i < n; ++i)
                    if (h[i] > 0) fn<FnRef>(0x1ffefc6)(base() + kTab, h[i]);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        int own(int h)
        {
            fn<FnRef>(0x259c49c)(base() + kTab, h);
            return h;
        }

        bool has(const int* v, int n, int h)
        {
            for (int i = 0; i < n; ++i)
                if (v[i] == h) return true;
            return false;
        }

        bool applyRaw(uint32_t uid, const RS& w, bool setDur, int& errs)
        {
            errs = 0;
            __try
            {
                uintptr_t b = base();
                uintptr_t rec = recRaw(uid);
                if (!rec) return false;
                fn<FnFlagSet>(0x3491a6a)(rec, (char)w.flag);
                if (fn<FnRec>(0x650894)(rec))
                {
                    if (setDur && w.hasDur)
                    {
                        uintptr_t d = fn<FnPtr>(0x650922)(rec);
                        if (d) (*reinterpret_cast<FnDurSet*>(rd<uintptr_t>(d)))(d, w.dur);
                    }
                    uintptr_t u = fn<FnPtr>(0xc5a3a4)(rec);
                    if (u && (w.nA || w.nB))
                    {
                        int cur[kMaxUpg];
                        int nc = readSet((*reinterpret_cast<FnSet*>(rd<uintptr_t>(u) + 8))(u), cur, kMaxUpg);
                        for (int i = 0; i < w.nA; ++i)
                        {
                            int h = w.A[i];
                            if (has(cur, nc, h)) continue;
                            int k = h;
                            if (fn<FnMapFind>(0x52e700)(b + kUpgMap, h, &k) == -1) { ++errs; continue; }
                            int c = own(h);
                            fn<FnUpgOwn>(0x2fbf2e2)(u, &c, 0);
                        }
                        nc = readSet((*reinterpret_cast<FnSet*>(rd<uintptr_t>(u) + 0x10))(u), cur, kMaxUpg);
                        for (int i = 0; i < w.nB; ++i)
                        {
                            int h = w.B[i];
                            if (has(cur, nc, h)) continue;
                            int k = h;
                            if (fn<FnMapFind>(0x52e700)(b + kUpgMap, h, &k) == -1) { ++errs; continue; }
                            int c = own(h);
                            fn<FnUpgAct>(0x2fbefca)(u, &c);
                        }
                    }
                }
                if (w.weapon && fn<FnIsWpn>(0x22cc2f2)(rec, 0))
                {
                    uintptr_t mgr = rd<uintptr_t>(b + kMgr);
                    uintptr_t data = rd<uintptr_t>(rec + 0x30);
                    if (!mgr || !data) return false;
                    int cur[kMaxAtt];
                    int nc = readAtt(data, cur, kMaxAtt);
                    for (int i = 0; i < w.nAtt; ++i)
                    {
                        if (has(cur, nc, w.att[i])) continue;
                        int c = own(w.att[i]);
                        if (!fn<FnAttach>(0x347d2b4)(mgr, uid, &c)) ++errs;
                    }
                    data = rd<uintptr_t>(rec + 0x30);
                    if (!data) return false;
                    if (w.blk > 0 && fn<FnSidOk>(0x25fac38)(b + kTab, w.blk))
                    {
                        Blk s{};
                        s.sid = w.blk;
                        std::memcpy(s.b8, w.blk8, 8);
                        s.b2 = w.blk2;
                        fn<FnBlkSet>(0x68e2a6c)(data, &s);
                    }
                    if (w.hasAmmo)
                    {
                        uintptr_t ws = fn<FnPtr>(0x4da52e)(data);
                        if (!ws) return false;
                        *reinterpret_cast<volatile uint8_t*>(ws + 0x21) = w.type;
                        fn<FnByte>(0x1fd523e)(ws, w.cal);
                        fn<FnVoid>(0x1190294)(ws);
                        fn<FnInt>(0x1fd524e)(ws, w.rounds);
                        fn<FnByte>(0x2dfbe74)(ws, w.mode);
                        uintptr_t hp = rd<uintptr_t>(data + 0x90);
                        if (hp) *reinterpret_cast<volatile uint8_t*>(hp) = 1;
                    }
                }
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool createRaw(uintptr_t ctr, int sid, int n, float dur, uint32_t& uid)
        {
            uid = 0xffffffffu;
            __try
            {
                uintptr_t mgr = rd<uintptr_t>(base() + kMgr);
                if (!mgr || sid <= 0 || n <= 0) return false;
                uint32_t id = rd<uint32_t>(ctr + 8);
                alignas(16) uint32_t snap[0x1a] = {};
                snap[0] = (uint32_t)sid;
                snap[1] = (uint32_t)n;
                std::memcpy(&snap[2], &dur, 4);
                uint32_t out = 0xffffffffu;
                fn<FnSnapNew>(0x6b46058)(mgr, &out, &id, snap);
                uid = out;
                return out < 0xfffffff0u;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool dirtyRaw(uintptr_t ctr)
        {
            __try
            {
                fn<FnVoid>(0x84bb22)(ctr);
                if (rd<int>(ctr + 0xdc) != 6) fn<FnVoid>(0xa6cd26)(ctr);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool removeRaw(uintptr_t ctr, uint32_t uid, int n, int& done)
        {
            done = 0;
            __try
            {
                uintptr_t b = base();
                uintptr_t rec = recRaw(uid);
                if (!rec || !inCtr(ctr, uid)) return false;
                int have = rd<int>(rec + 0x44);
                int k = std::min(n, have);
                if (k <= 0) return false;
                int sid = rd<int>(rec + 0x38);
                own(sid);
                fn<FnInt>(0xc5bd5e)(rec, -k);
                alignas(16) uint8_t evt[0x30] = {};
                std::memcpy(evt, &sid, 4);
                std::memcpy(evt + 4, &k, 4);
                evt[0x20] = 0x12;
                uint32_t t = 0x2c;
                std::memcpy(evt + 0x28, &t, 4);
                fn<FnPost>(0x2222242)(rd<uintptr_t>(b + kPoster), evt);
                FnDtor dt = rd<FnDtor>(b + kDtor + static_cast<uintptr_t>(evt[0x20]) * 8);
                dt(evt);
                done = k;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool groundRaw(double x, double y, double z, uintptr_t& ctr)
        {
            ctr = 0;
            __try
            {
                uintptr_t b = base();
                uintptr_t mgr = rd<uintptr_t>(b + kMgr);
                if (!mgr) return false;
                double pos[3] = {x, y, z};
                int sid = 0;
                ctr = fn<FnGround>(0x2edf150)(mgr, pos, b + kRot, &sid);
                return ctr != 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool ctrInfo(uint32_t uid, uintptr_t& ctr, int& type, uint8_t& fl, int& count)
        {
            __try
            {
                uintptr_t rec = recRaw(uid);
                if (!rec) return false;
                ctr = rd<uintptr_t>(rec + 0x20);
                if (!ctr) return false;
                type = rd<int>(ctr + 0xdc);
                fl = rd<uint8_t>(rec + 0x40);
                count = rd<int>(rec + 0x44);
                return inCtr(ctr, uid);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool groundOf(uintptr_t actor, uintptr_t& ctr)
        {
            __try
            {
                ctr = rd<uintptr_t>(actor + 0x2b8);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        std::string nm(int h)
        {
            wchar_t buf[128];
            int n = nameRaw(h, buf, 128);
            std::string r;
            for (int i = 0; i < n; ++i) r += (char)(buf[i] < 128 ? buf[i] : '?');
            return r;
        }

        std::string hx(const uint8_t* p, int n)
        {
            std::string r;
            char b[4];
            for (int i = 0; i < n; ++i)
            {
                std::snprintf(b, sizeof b, "%02x", p[i]);
                r += b;
            }
            return r;
        }

        bool unhx(const std::string& s, uint8_t* p, int n)
        {
            if ((int)s.size() != n * 2) return false;
            for (int i = 0; i < n; ++i)
            {
                unsigned v = 0;
                if (std::sscanf(s.c_str() + i * 2, "%2x", &v) != 1) return false;
                p[i] = (uint8_t)v;
            }
            return true;
        }

        std::vector<std::string> split(const std::string& s, char c)
        {
            std::vector<std::string> r;
            size_t a = 0;
            for (;;)
            {
                size_t p = s.find(c, a);
                r.push_back(s.substr(a, p == std::string::npos ? std::string::npos : p - a));
                if (p == std::string::npos) break;
                a = p + 1;
            }
            return r;
        }

        void names(const int* v, int n, std::vector<std::string>& out)
        {
            for (int i = 0; i < n; ++i)
            {
                std::string s = nm(v[i]);
                if (s2x_is::sidOk(s)) out.push_back(s);
            }
        }

        s2x_is::IState toState(const RS& r)
        {
            s2x_is::IState s;
            s.sid = nm(r.sid);
            if (r.hasDur)
            {
                s.hasDur = true;
                s.dur = s2x_is::durOf(r.dur);
            }
            names(r.B, r.nB, s.upg);
            std::sort(s.upg.begin(), s.upg.end());
            std::vector<int> own;
            for (int i = 0; i < r.nA; ++i)
                if (!has(r.B, r.nB, r.A[i])) own.push_back(r.A[i]);
            std::vector<std::string> on;
            names(own.data(), (int)own.size(), on);
            std::sort(on.begin(), on.end());
            if (r.weapon)
            {
                names(r.att, r.nAtt, s.att);
                std::sort(s.att.begin(), s.att.end());
                if (r.hasAmmo)
                {
                    s.hasAmmo = true;
                    s.rounds = r.rounds;
                    s.ammo = "t" + std::to_string(r.type) + "c" + std::to_string(r.cal);
                }
            }
            if (r.flag) s.ext.push_back({'f', std::to_string(r.flag)});
            if (r.weapon && r.blk > 0)
            {
                std::string b = nm(r.blk);
                if (s2x_is::sidOk(b)) s.ext.push_back({'k', b + "." + hx(r.blk8, 8) + "." + hx(reinterpret_cast<const uint8_t*>(&r.blk2), 2)});
            }
            if (r.weapon && r.hasAmmo) s.ext.push_back({'m', std::to_string(r.mode)});
            if (!on.empty())
            {
                std::string j;
                for (auto& x : on) j += (j.empty() ? "" : ".") + x;
                s.ext.push_back({'o', j});
            }
            return s;
        }

        struct Want
        {
            RS r{};
            std::vector<int> held;
            ~Want() { releaseRaw(held.data(), (int)held.size()); }
            int in(const std::string& s)
            {
                if (!s2x_is::sidOk(s)) return 0;
                std::wstring w(s.begin(), s.end());
                int h = internRaw(w.c_str(), (int)w.size() + 1);
                if (h > 0) held.push_back(h);
                return h > 0 ? h : 0;
            }
        };

        bool fromState(const s2x_is::IState& s, Want& w)
        {
            RS& r = w.r;
            std::memset(&r, 0, sizeof r);
            r.sid = w.in(s.sid);
            if (r.sid <= 0) return false;
            r.dur = s.hasDur ? s2x_is::durF(s.dur) : 1.0f;
            r.hasDur = s.hasDur ? 1 : 0;
            if (r.dur < 0.f || r.dur > 1.f) r.dur = std::clamp(r.dur, 0.f, 1.f);
            std::vector<std::string> own;
            for (auto& e : s.ext)
            {
                if (e.first == 'f') r.flag = (uint8_t)std::atoi(e.second.c_str());
                else if (e.first == 'm') r.mode = (uint8_t)std::atoi(e.second.c_str());
                else if (e.first == 'o') own = split(e.second, '.');
                else if (e.first == 'k')
                {
                    auto p = split(e.second, '.');
                    if (p.size() == 3 && unhx(p[1], r.blk8, 8) && unhx(p[2], reinterpret_cast<uint8_t*>(&r.blk2), 2)) r.blk = w.in(p[0]);
                }
            }
            for (auto& u : s.upg)
            {
                if (r.nB >= kMaxUpg || r.nA >= kMaxUpg) break;
                int h = w.in(u);
                if (h <= 0) continue;
                r.B[r.nB++] = h;
                r.A[r.nA++] = h;
            }
            for (auto& u : own)
            {
                if (r.nA >= kMaxUpg) break;
                int h = w.in(u);
                if (h > 0 && !has(r.A, r.nA, h)) r.A[r.nA++] = h;
            }
            for (auto& a : s.att)
            {
                if (r.nAtt >= kMaxAtt) break;
                int h = w.in(a);
                if (h > 0) r.att[r.nAtt++] = h;
            }
            r.weapon = (s.hasAmmo || !s.att.empty() || r.blk > 0) ? 1 : 0;
            if (s.hasAmmo)
            {
                unsigned t = 0, c = 0;
                if (std::sscanf(s.ammo.c_str(), "t%uc%u", &t, &c) == 2 && t < 256 && c < 256)
                {
                    r.hasAmmo = 1;
                    r.type = (uint8_t)t;
                    r.cal = (uint8_t)c;
                    r.rounds = std::max(0, s.rounds);
                }
            }
            return true;
        }

        int g_ok = -1;
    }

    auto ready() -> bool
    {
        if (g_ok < 0)
        {
            int ok = 0;
            g_ok = checkSeh(ok) && ok ? 1 : 0;
        }
        return g_ok == 1;
    }

    auto groundCtr(uintptr_t actor) -> uintptr_t
    {
        uintptr_t c = 0;
        return actor && groundOf(actor, c) ? c : 0;
    }

    auto list(uintptr_t ctr, bool takeOnly, bool state, std::vector<s2x_is::Inst>& out) -> bool
    {
        out.clear();
        if (!ready() || !ctr) return false;
        std::vector<uint32_t> uids(kMaxList);
        int n = 0;
        if (!listRaw(ctr, uids.data(), kMaxList, n)) return false;
        auto r = std::make_unique<RS>();
        for (int i = 0; i < n; ++i)
        {
            if (!capRaw(uids[(size_t)i], state, *r)) return false;
            if (r->count <= 0) continue;
            if (takeOnly && !(r->fl & 1)) continue;
            s2x_is::Inst in;
            in.uid = r->uid;
            in.n = r->count;
            if (state) in.st = toState(*r);
            else in.st.sid = nm(r->sid);
            if (in.st.sid.empty()) return false;
            out.push_back(in);
        }
        return true;
    }

    auto capture(uint32_t uid, s2x_is::IState& out) -> bool
    {
        if (!ready()) return false;
        auto r = std::make_unique<RS>();
        if (!capRaw(uid, true, *r)) return false;
        out = toState(*r);
        return !out.sid.empty();
    }

    auto create(uintptr_t ctr, const s2x_is::IState& st, long long n, uint32_t& uid) -> bool
    {
        uid = 0;
        if (!ready() || !ctr || n <= 0) return false;
        auto w = std::make_unique<Want>();
        if (!fromState(st, *w)) return false;
        int k = (int)std::min<long long>(n, 1000000);
        if (!createRaw(ctr, w->r.sid, k, w->r.dur, uid))
        {
            uid = 0;
            return false;
        }
        int errs = 0;
        bool ok = st.plain() || applyRaw(uid, w->r, false, errs);
        dirtyRaw(ctr);
        return ok && errs == 0;
    }

    auto remove(uintptr_t ctr, uint32_t uid, long long n, long long& done) -> bool
    {
        done = 0;
        if (!ready() || !ctr || n <= 0) return false;
        int d = 0;
        bool ok = removeRaw(ctr, uid, (int)std::min<long long>(n, 1000000), d);
        done = d;
        return ok;
    }

    auto apply(uint32_t uid, const s2x_is::IState& st, std::string& how) -> bool
    {
        how.clear();
        if (!ready()) return false;
        uintptr_t ctr = 0;
        int type = 0, count = 0;
        uint8_t fl = 0;
        if (!ctrInfo(uid, ctr, type, fl, count)) return false;
        auto w = std::make_unique<Want>();
        if (!fromState(st, *w)) return false;
        if (type == 6 || (fl & 4))
        {
            how = "in place";
            int errs = 0;
            bool ok = applyRaw(uid, w->r, true, errs);
            dirtyRaw(ctr);
            return ok && errs == 0;
        }
        how = "replaced";
        uint32_t nu = 0;
        bool ok = create(ctr, st, 1, nu);
        if (!nu) return false;
        long long d = 0;
        return remove(ctr, uid, 1, d) && d == 1 && ok;

    }

    auto spawn(const s2x_is::IState& st, double x, double y, double z, uint32_t& uid) -> bool
    {
        uid = 0;
        if (!ready()) return false;
        uintptr_t ctr = 0;
        if (!groundRaw(x, y, z, ctr)) return false;
        return create(ctr, st, 1, uid);
    }
}
