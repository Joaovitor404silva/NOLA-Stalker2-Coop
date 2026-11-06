#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "s2x.h"
#include "s2m_items.h"
#include "s2m_bodies.h"
#include "s2m_damage.h"
#include "s2m_lock.h"
#include "s2m_quest.h"
#include "s2m_save.h"
#include "s2n_nat_api.h"
#include "s2m_cgc.h"
#include "s2m_inter.h"
#include "s2x_istate_nat.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <tuple>
#include <vector>

namespace
{
using namespace s2m_items;

std::wstring W(const std::string& s)
{
    std::wstring r;
    r.reserve(s.size());
    for (unsigned char c : s) r += (wchar_t)c;
    return r;
}

std::string N(const std::wstring& s)
{
    std::string r;
    r.reserve(s.size());
    for (wchar_t c : s) r += (char)(c < 256 ? c : '?');
    return r;
}

bool mem_ok(uintptr_t p, size_t n)
{
    if (!p) return false;
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery(reinterpret_cast<const void*>(p), &mi, sizeof mi)) return false;
    if (mi.State != MEM_COMMIT) return false;
    if (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    const DWORD rd = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(mi.Protect & rd)) return false;
    uintptr_t end = reinterpret_cast<uintptr_t>(mi.BaseAddress) + mi.RegionSize;
    return p + n <= end;
}

uint8_t* exeBase() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }

bool bytesEq(const uint8_t* p, const uint8_t* b, size_t n)
{
    __try { return std::memcmp(p, b, n) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

struct RmStr
{
    const wchar_t* d;
    int32_t n;
    int32_t m;
};
using RmInternFn = int(__fastcall*)(void*, RmStr*, bool);
using RmRefFn = void(__fastcall*)(void*, int);
using RmRemoveFn = int(__fastcall*)(void*, int*, int, char);

int rmNative()
{
    static int ok = -1;
    if (ok >= 0) return ok;
    const uint8_t* b = exeBase();
    static const uint8_t kRemove[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x78, 0x44, 0x88, 0x4c, 0x24, 0x2b};
    static const uint8_t kRel[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x48, 0x85, 0xd2, 0x7e, 0x50};
    static const uint8_t kIntern[] = {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x40, 0x48, 0x89, 0xce, 0x48, 0x8b, 0x05};
    static const uint8_t kRef[] = {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x20, 0x85, 0xd2, 0x7e, 0x3b, 0x89, 0xd6, 0x48, 0x89, 0xcf};
    ok = b && bytesEq(b + 0x35f488c, kRemove, sizeof kRemove) && bytesEq(b + 0x1ffefc6, kRel, sizeof kRel) && bytesEq(b + 0x25a231a, kIntern, sizeof kIntern) &&
                 bytesEq(b + 0x259c49c, kRef, sizeof kRef)
             ? 1
             : 0;
    return ok;
}

bool rmSeh(uintptr_t pawn, const wchar_t* w, int n, int cnt, int& out)
{
    __try
    {
        uint8_t* b = exeBase();
        uintptr_t a = *reinterpret_cast<volatile uintptr_t*>(pawn + 0x650);
        if (!a) return false;
        uintptr_t c = *reinterpret_cast<volatile uintptr_t*>(a + 0x678);
        if (!c) return false;
        void* ctr = *reinterpret_cast<void* volatile*>(c + 0x1c0);
        if (!ctr) return false;
        void* tab = b + 0xb10f588;
        RmStr s{w, n, n};
        int h = reinterpret_cast<RmInternFn>(b + 0x25a231a)(tab, &s, true);
        if (h <= 0) { out = 0; return true; }
        int cp = h;
        reinterpret_cast<RmRefFn>(b + 0x259c49c)(tab, cp);
        out = reinterpret_cast<RmRemoveFn>(b + 0x35f488c)(ctr, &cp, cnt, 1);
        if (cp > 0) reinterpret_cast<RmRefFn>(b + 0x1ffefc6)(tab, cp);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

struct NatRaw
{
    wchar_t s[96];
    int len;
    int n;
    uint8_t fl;
};
using CtCreateFn = void(__fastcall*)(void*, uint32_t*, uint32_t, int*, int, float);
using CtPostFn = void(__fastcall*)(void*);
using WRefreshFn = void(__fastcall*)(void*);

int ctNative()
{
    static int ok = -1;
    if (ok >= 0) return ok;
    const uint8_t* b = exeBase();
    static const uint8_t kList[] = {0x48, 0x63, 0xa9, 0x18, 0x01, 0x00, 0x00, 0x48, 0x85, 0xed, 0x0f, 0x84, 0x8a, 0x03, 0x00, 0x00, 0x4c, 0x8b, 0xa9, 0x10, 0x01, 0x00, 0x00, 0x48, 0xc1, 0xe5, 0x04};
    static const uint8_t kPool[] = {0x4c, 0x8d, 0x3d, 0x13, 0xb2, 0x25, 0x08, 0x81, 0xfe, 0x00, 0x10, 0x00, 0x00};
    static const uint8_t kRec[] = {0x48, 0xc1, 0xe1, 0x07, 0x48, 0x8d, 0x04, 0xc1};
    static const uint8_t kSid[] = {0x44, 0x8b, 0x72, 0x38};
    static const uint8_t kTake[] = {0x8a, 0x41, 0x40, 0x24, 0x01, 0xc3};
    static const uint8_t kCnt[] = {0x03, 0x7c, 0x11, 0x54};
    static const uint8_t kTab[] = {0x48, 0x8d, 0x05, 0x6c, 0x82, 0xc5, 0x0a};
    static const uint8_t kPage[] = {0x89, 0xf0, 0x25, 0xff, 0x03, 0x00, 0x00, 0xc1, 0xee, 0x0a, 0x48, 0x8b, 0x0c, 0xf7, 0xc1, 0xe0, 0x05};
    static const uint8_t kStore[] = {0x4c, 0x8b, 0x35, 0xe9, 0x3c, 0x70, 0x03};
    static const uint8_t kMk[] = {0xe8, 0x2d, 0x0c, 0x37, 0xfc, 0x48, 0x89, 0xd9, 0xe8, 0xc3, 0x71, 0xc8, 0xf9, 0x48, 0x89, 0xd9, 0xe8, 0xbf, 0x83, 0xea, 0xf9};
    static const uint8_t kId[] = {0x48, 0x89, 0xd0, 0x8b, 0x49, 0x08, 0x89, 0x0a, 0xc3};
    static const uint8_t kWin[] = {0x48, 0x8b, 0x8e, 0xe0, 0x06, 0x00, 0x00};
    static const uint8_t kRef[] = {0xff, 0x90, 0x70, 0x05, 0x00, 0x00};
    ok = b && rmNative() == 1 && bytesEq(b + 0x24efea0, kList, sizeof kList) && bytesEq(b + 0x24eff06, kPool, sizeof kPool) && bytesEq(b + 0x24eff56, kRec, sizeof kRec) &&
                 bytesEq(b + 0x68f5304, kSid, sizeof kSid) && bytesEq(b + 0x1fb59ea, kTake, sizeof kTake) && bytesEq(b + 0x4b74ea, kCnt, sizeof kCnt) && bytesEq(b + 0x4b7315, kTab, sizeof kTab) &&
                 bytesEq(b + 0x259c4bc, kPage, sizeof kPage) && bytesEq(b + 0x6bc4908, kStore, sizeof kStore) && bytesEq(b + 0x6bc4952, kMk, sizeof kMk) && bytesEq(b + 0xec4464, kId, sizeof kId) &&
                 bytesEq(b + 0x2ef5135, kWin, sizeof kWin) && bytesEq(b + 0x2ef51a2, kRef, sizeof kRef)
             ? 1
             : 0;
    return ok;
}

uintptr_t ctOf(uintptr_t a, int where)
{
    __try
    {
        if (where == 1) return *reinterpret_cast<volatile uintptr_t*>(a + 0x6e0);
        uintptr_t p = *reinterpret_cast<volatile uintptr_t*>(a + 0x650);
        if (!p) return 0;
        uintptr_t c = *reinterpret_cast<volatile uintptr_t*>(p + 0x678);
        if (!c) return 0;
        return *reinterpret_cast<volatile uintptr_t*>(c + 0x1c0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

bool ctListSeh(uintptr_t ctr, NatRaw* out, int cap, int& n)
{
    n = 0;
    __try
    {
        uint8_t* b = exeBase();
        int cnt = *reinterpret_cast<volatile int*>(ctr + 0x118);
        uintptr_t arr = *reinterpret_cast<volatile uintptr_t*>(ctr + 0x110);
        if (cnt < 0 || cnt > cap) return false;
        if (cnt > 0 && !arr) return false;
        uintptr_t tab = reinterpret_cast<uintptr_t>(b + 0xb10f588);
        for (int i = 0; i < cnt; ++i)
        {
            uint32_t uid = *reinterpret_cast<volatile uint32_t*>(arr + static_cast<uintptr_t>(i) * 16);
            if (uid >= 0xfffffff0u) continue;
            uint32_t u = uid & 0x7ffffff;
            uintptr_t blk = reinterpret_cast<uintptr_t>(b + 0xa74b120);
            while (u > 0xfff)
            {
                blk = *reinterpret_cast<volatile uintptr_t*>(blk);
                if (!blk) return false;
                u -= 0x1000;
            }
            uintptr_t r = blk + static_cast<uintptr_t>(u) * 0x88 + 0x10;
            int h = *reinterpret_cast<volatile int*>(r + 0x38);
            if (h <= 0) return false;
            uintptr_t pg = *reinterpret_cast<volatile uintptr_t*>(tab + static_cast<uintptr_t>(h >> 10) * 8);
            if (!pg) return false;
            uintptr_t e = pg + static_cast<uintptr_t>(h & 0x3ff) * 0x20;
            const wchar_t* s = *reinterpret_cast<const wchar_t* volatile*>(e);
            int len = *reinterpret_cast<volatile int*>(e + 8);
            if (!s || len < 2 || len > 96) return false;
            NatRaw& o = out[n++];
            for (int k = 0; k < len - 1; ++k) o.s[k] = s[k];
            o.len = len - 1;
            o.n = *reinterpret_cast<volatile int*>(r + 0x44);
            o.fl = *reinterpret_cast<volatile uint8_t*>(r + 0x40);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ctPutSeh(uintptr_t w, const wchar_t* s, int len, int d, int& done)
{
    done = 0;
    __try
    {
        uint8_t* b = exeBase();
        void* ctr = *reinterpret_cast<void* volatile*>(w + 0x6e0);
        if (!ctr) return false;
        void* tab = b + 0xb10f588;
        RmStr st{s, len, len};
        int h = reinterpret_cast<RmInternFn>(b + 0x25a231a)(tab, &st, true);
        if (h <= 0) return false;
        if (d > 0)
        {
            void* store = *reinterpret_cast<void* volatile*>(b + 0xa2c85f8);
            if (!store) return false;
            uint32_t id = *reinterpret_cast<volatile uint32_t*>(reinterpret_cast<uintptr_t>(ctr) + 8);
            uint32_t made = 0;
            int hh = h;
            reinterpret_cast<CtCreateFn>(b + 0x2f35584)(store, &made, id, &hh, d, 1.0f);
            reinterpret_cast<CtPostFn>(b + 0x84bb22)(ctr);
            reinterpret_cast<CtPostFn>(b + 0xa6cd26)(ctr);
            done = d;
            return true;
        }
        int cp = h;
        reinterpret_cast<RmRefFn>(b + 0x259c49c)(tab, cp);
        done = reinterpret_cast<RmRemoveFn>(b + 0x35f488c)(ctr, &cp, -d, 1);
        if (cp > 0) reinterpret_cast<RmRefFn>(b + 0x1ffefc6)(tab, cp);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ctRefreshSeh(uintptr_t w)
{
    __try
    {
        uintptr_t vt = *reinterpret_cast<volatile uintptr_t*>(w);
        if (!vt) return false;
        WRefreshFn f = *reinterpret_cast<WRefreshFn volatile*>(vt + 0x570);
        if (!f) return false;
        f(reinterpret_cast<void*>(w));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

class XEnv : public Env
{
  public:
    struct Slot
    {
        x0::Ref r;
        uint32_t gen = 1;
        bool used = false;
    };
    std::vector<Slot> slots_;
    std::vector<uint32_t> free_;
    std::map<std::tuple<int32_t, void*, int32_t>, uint32_t> index_;
    std::optional<double> latch_;
    std::mt19937 rng_{(unsigned)std::time(nullptr)};
    int meshNull_ = 0;

    Obj to_s(const x0::Ref& r)
    {
        if (r.idx < 0 || !r.raw || !r.valid()) return 0;
        auto k = std::make_tuple(r.idx, r.raw, r.serial);
        auto it = index_.find(k);
        if (it != index_.end() && slots_[it->second].used) return ((uint64_t)slots_[it->second].gen << 32) | (uint64_t)(it->second + 1);
        uint32_t at;
        if (!free_.empty()) { at = free_.back(); free_.pop_back(); }
        else { at = (uint32_t)slots_.size(); slots_.emplace_back(); }
        Slot& s = slots_[at];
        s.r = r;
        s.used = true;
        index_[k] = at;
        return ((uint64_t)s.gen << 32) | (uint64_t)(at + 1);
    }
    x0::Ref to_x(Obj o) const
    {
        uint32_t lo = (uint32_t)(o & 0xffffffffu);
        if (lo == 0 || lo > slots_.size()) return x0::Ref{};
        const Slot& s = slots_[lo - 1];
        if (!s.used || s.gen != (uint32_t)(o >> 32)) return x0::Ref{};
        return s.r;
    }
    void prune()
    {
        for (size_t i = 0; i < slots_.size(); ++i)
        {
            Slot& s = slots_[i];
            if (!s.used || s.r.valid()) continue;
            index_.erase(std::make_tuple(s.r.idx, s.r.raw, s.r.serial));
            s.r.reset();
            s.used = false;
            ++s.gen;
            free_.push_back((uint32_t)i);
        }
    }
    void latch() { latch_ = x0::now(); }

    x0::Ref med() { return x0::find_path(L"/Script/Stalker2.Default__CppMediator"); }
    x0::Ref pawnRef()
    {
        x0::Ref pc = x0::first_of(L"PlayerController");
        x0::Ref pw;
        if (pc.valid() && x0::prop<x0::Ref>(pc, "Pawn", pw) && pw.valid()) return pw;
        return x0::first_of(L"BP_Stalker2Character_C");
    }
    bool callB(Obj o, const wchar_t* fn, const wchar_t* p, bool v)
    {
        x0::Ref x = to_x(o);
        return x.valid() && x0::Call(x, fn).b(p, v).run();
    }
    x0::Ref meshComp(Obj o)
    {
        x0::Ref a = to_x(o), m;
        if (!a.valid() || !x0::prop<x0::Ref>(a, "Mesh", m) || !m.valid()) return x0::Ref{};
        return m;
    }

    double now() override { return x0::now(); }
    bool host() override { return x0::is_host(); }
    double fnum(const char* key, double def) override
    {
        x0::Val v = x0::flag(key);
        return v.k == x0::Val::i || v.k == x0::Val::f ? v.num() : def;
    }
    bool fhas(const char* key) override { return x0::flag(key).k != x0::Val::nil; }
    std::string fstr(const char* key, const std::string& def) override
    {
        x0::Val v = x0::flag(key);
        return v.k == x0::Val::nil ? def : v.str();
    }
    void log(const std::string& line) override { x0::log(line); }

    bool part(const char* tag, std::string& out) override { return x0::rpart(tag, out); }
    bool peerLoopback() override { return x0::peer().loopback; }
    int peerSessions() override { return x0::peer().sessions; }
    bool peerSilent() override { return x0::peer().state == x0::Peer::silent; }
    bool remote(double& x, double& y, double& recv) override
    {
        const x0::Snap& s = x0::remote();
        if (!s.ok || !s.has_pos || s.recv < 0) return false;
        x = s.x;
        y = s.y;
        recv = s.recv;
        return true;
    }
    bool lookOn() override { return true; }
    std::string lookPut(const std::string& text) override { return x0::look_put(text); }
    bool lookGet(const std::string& hash, std::string& text) override { return x0::look_get(hash, text); }
    std::optional<double> gameSecs() override
    {
        x0::Ref tm = x0::first_of(L"TimeManager");
        double v = 0;
        if (!tm.valid() || !x0::prop<double>(tm, "GameTimeSeconds", v)) return std::nullopt;
        return v;
    }
    double wallSecs() override { return (double)std::time(nullptr); }
    int random(int lo, int hi) override { return std::uniform_int_distribution<int>(lo, hi)(rng_); }
    std::optional<double> keyLatch() override { return latch_; }
    bool keyDownG() override { return x0::key_down('G'); }

    bool readText(const std::string& name, std::string& out) override { return x0::table(name, out); }
    bool appendText(const std::string& name, const std::string& text) override { return x0::append_text(x0::bridge_file(W(name)), text); }
    bool writeText(const std::string& name, const std::string& text) override { return x0::write_text(x0::bridge_file(W(name)), text); }
    bool dataText(const std::string& name, std::string& out) override { return x0::data(name, out); }

    bool valid(Obj o) override { return o != 0 && to_x(o).valid(); }
    Obj pawn() override { return to_s(pawnRef()); }
    Obj controller() override { return to_s(x0::first_of(L"PlayerController")); }
    Obj first(const char* cls) override { return to_s(x0::first_of(W(cls).c_str())); }
    std::vector<Obj> all(const char* cls) override
    {
        std::vector<Obj> r;
        for (const x0::Ref& x : x0::all_of(W(cls).c_str()))
        {
            Obj o = to_s(x);
            if (o) r.push_back(o);
        }
        return r;
    }
    uint64_t addr(Obj o) override
    {
        x0::Ref x = to_x(o);
        return x.valid() ? x.address() : 0;
    }
    std::string name(Obj o) override
    {
        x0::Ref x = to_x(o);
        return x.valid() ? x0::obj_name(x) : std::string();
    }
    std::string cls(Obj o) override
    {
        x0::Ref x = to_x(o);
        return x.valid() ? x0::class_name(x) : std::string();
    }
    std::string full(Obj o) override
    {
        x0::Ref x = to_x(o);
        return x.valid() ? x0::full_name(x) : std::string();
    }
    bool loc(Obj o, V3& out) override
    {
        x0::Vec3 v;
        x0::Ref x = to_x(o);
        if (!x.valid() || !x0::call_v(x, L"K2_GetActorLocation", v)) return false;
        out = {v.x, v.y, v.z};
        return true;
    }
    bool setLoc(Obj o, const V3& v) override
    {
        x0::Ref x = to_x(o);
        return x.valid() && x0::Call(x, L"K2_SetActorLocation").v(L"NewLocation", x0::Vec3{v.x, v.y, v.z}).b(L"bSweep", false).b(L"bTeleport", true).run();
    }
    bool alive(Obj o, bool& out) override
    {
        x0::Ref x = to_x(o);
        if (!x.valid() || !x0::is_a(x, L"Obj")) return false;
        x0::Call c(x, L"IsAlive");
        if (!c.run()) return false;
        out = c.rb();
        return true;
    }
    bool maxHp(Obj o, double& out) override
    {
        x0::Ref x = to_x(o);
        if (!x.valid()) return false;
        x0::Call c(x, L"GetMaxHP");
        if (!c.run()) return false;
        out = c.rd();
        return true;
    }
    bool hidden(Obj o, bool& out) override
    {
        x0::Ref x = to_x(o);
        return x.valid() && x0::prop<bool>(x, "bHidden", out);
    }
    bool setHidden(Obj o, bool on) override { return callB(o, L"SetActorHiddenInGame", L"bNewHidden", on); }
    bool setCollision(Obj o, bool on) override { return callB(o, L"SetActorEnableCollision", L"bNewActorEnableCollision", on); }
    bool setActorTick(Obj o, bool on) override { return callB(o, L"SetActorTickEnabled", L"bEnabled", on); }
    Obj interacted(Obj pawnOrPc) override
    {
        x0::Ref x = to_x(pawnOrPc);
        if (!x.valid()) return 0;
        return to_s(x0::call_o(x, L"GetInteractedActor"));
    }
    bool closeLoot(Obj pawnOrPc) override
    {
        x0::Ref x = to_x(pawnOrPc);
        return x.valid() && x0::call0(x, L"CloseLootMenu");
    }
    bool visible(Obj widget, bool& out) override
    {
        x0::Ref x = to_x(widget);
        if (!x.valid()) return false;
        x0::Call c(x, L"IsVisible");
        if (!c.run()) return false;
        out = c.rb();
        return true;
    }
    bool natItems(Obj o, int where, bool all, Counts& out) override
    {
        out.clear();
        if (ctNative() != 1) return false;
        x0::Ref x = to_x(o);
        if (!x.valid()) return false;
        uintptr_t ctr = ctOf(static_cast<uintptr_t>(x.address()), where);
        if (!ctr) return false;
        std::vector<NatRaw> buf(1024);
        int n = 0;
        if (!ctListSeh(ctr, buf.data(), (int)buf.size(), n)) return false;
        for (int i = 0; i < n; ++i)
        {
            const NatRaw& r = buf[(size_t)i];
            if ((!all && !(r.fl & 1)) || r.n <= 0) continue;
            std::string sid;
            for (int k = 0; k < r.len; ++k) sid += (char)(r.s[k] < 128 ? r.s[k] : '?');
            out[sid] += r.n;
        }
        return true;
    }
    bool natPut(Obj widget, const std::string& sid, long long d, long long& done) override
    {
        done = 0;
        if (d == 0) return true;
        if (ctNative() != 1) return false;
        x0::Ref x = to_x(widget);
        if (!x.valid()) return false;
        std::wstring w = W(sid);
        int k = 0;
        int dd = (int)std::max<long long>(-1000000, std::min<long long>(1000000, d));
        if (!ctPutSeh(static_cast<uintptr_t>(x.address()), w.c_str(), (int)w.size() + 1, dd, k)) return false;
        done = k;
        return true;
    }
    bool natRefresh(Obj widget) override
    {
        if (ctNative() != 1) return false;
        x0::Ref x = to_x(widget);
        return x.valid() && ctRefreshSeh(static_cast<uintptr_t>(x.address()));
    }
    uintptr_t isCtr(Obj o, int where)
    {
        if (ctNative() != 1 || !s2x_isn::ready()) return 0;
        x0::Ref x = to_x(o);
        if (!x.valid()) return 0;
        uintptr_t a = static_cast<uintptr_t>(x.address());
        return where == 2 ? s2x_isn::groundCtr(a) : ctOf(a, where);
    }
    bool natInst(Obj o, int where, bool state, std::vector<s2x_is::Inst>& out) override
    {
        out.clear();
        uintptr_t ctr = isCtr(o, where);
        return ctr && s2x_isn::list(ctr, where == 1, state, out);
    }
    bool natPutI(Obj o, int where, const s2x_is::IState& st, long long n, uint32_t& uid) override
    {
        uid = 0;
        uintptr_t ctr = isCtr(o, where);
        if (!ctr || n <= 0) return false;
        if (st.plain()) return s2x_isn::create(ctr, st, n, uid);
        bool ok = true;
        for (long long i = 0; i < n; ++i) ok = s2x_isn::create(ctr, st, 1, uid) && ok;
        return ok;
    }
    bool natRmI(Obj o, int where, uint32_t uid, long long n, long long& done) override
    {
        done = 0;
        uintptr_t ctr = isCtr(o, where);
        return ctr && s2x_isn::remove(ctr, uid, n, done);
    }
    bool natApplyI(uint32_t uid, const s2x_is::IState& st) override
    {
        if (ctNative() != 1) return false;
        std::string how;
        bool r = s2x_isn::apply(uid, st, how);
        x0::log("NOLA istate: item " + std::to_string(uid) + " state " + (how.empty() ? "not set" : how) + (r ? "" : " (erro)"));
        return r;
    }
    bool natSpawnI(const s2x_is::IState& st, double x, double y, double z) override
    {
        if (ctNative() != 1) return false;
        uint32_t uid = 0;
        return s2x_isn::spawn(st, x, y, z, uid);
    }
    bool invReadable(Obj o) override
    {

        x0::Ref x = to_x(o);
        if (!x.valid()) return false;
        uintptr_t a = (uintptr_t)x.address();
        if (!mem_ok(a + 0x650, sizeof(uintptr_t))) return false;
        uintptr_t p = *reinterpret_cast<const uintptr_t*>(a + 0x650);
        if (!p) return false;
        return mem_ok(p + 0x678, sizeof(uintptr_t));
    }
    bool count(Obj pawn, const std::string& sid, long long& out) override
    {
        x0::Ref x = to_x(pawn), m = med();
        if (!x.valid() || !m.valid()) return false;
        x0::Call c(m, L"GetItemsCountInInventory");
        c.o(L"Obj", x).s(L"ItemName", W(sid));
        if (!c.run()) return false;
        out = (long long)c.ri();
        return true;
    }
    std::optional<long long> guid(Obj pawn) override
    {
        x0::Ref x = to_x(pawn), m = med();
        if (!x.valid() || !m.valid()) return std::nullopt;
        x0::Call c(m, L"GetGUID");
        c.o(L"Actor", x);
        if (!c.run()) return std::nullopt;
        return (long long)c.ri();
    }
    bool money(Obj pawn, long long& out) override
    {
        x0::Ref x = to_x(pawn);
        if (!x.valid()) return false;
        x0::Call c(x, L"GetMoney");
        if (!c.run()) return false;
        out = (long long)c.rf();
        return true;
    }
    bool guidHex(Obj o, std::string& out) override
    {
        x0::Ref x = to_x(o);
        int32_t a = 0, b = 0, c = 0, d = 0;
        if (!x.valid() || !x0::prop<int32_t>(x, "Guid.A", a) || !x0::prop<int32_t>(x, "Guid.B", b) || !x0::prop<int32_t>(x, "Guid.C", c) || !x0::prop<int32_t>(x, "Guid.D", d)) return false;
        char buf[40];
        snprintf(buf, sizeof buf, "%08X%08X%08X%08X", (uint32_t)a, (uint32_t)b, (uint32_t)c, (uint32_t)d);
        out = buf;
        return true;
    }
    std::optional<std::string> meshName(Obj o, const char* comp) override
    {
        x0::Ref a = to_x(o), c, sm;
        if (!a.valid()) return std::nullopt;
        if (!x0::prop<x0::Ref>(a, comp, c) || !c.valid()) return std::nullopt;
        if (!x0::prop<x0::Ref>(c, "StaticMesh", sm) || !sm.valid())
        {
            meshNull_ += 1;
            if (meshNull_ <= 10 || meshNull_ % 100 == 0)
            {
                std::string f = x0::full_name(a);
                size_t p = f.find_last_of(".:");
                std::string tail = p == std::string::npos ? f : f.substr(p + 1);
                char buf[512];
                snprintf(buf, sizeof buf, "NOLA meshsafe: malha nula (%s.%s) n=%d %s", comp, "StaticMesh", meshNull_, tail.empty() ? "?" : tail.c_str());
                x0::log(buf);
            }
            return std::nullopt;
        }
        return x0::obj_name(sm);
    }
    bool meshVisible(Obj o, bool& out) override
    {
        x0::Ref m = meshComp(o);
        if (!m.valid()) return false;
        x0::Call c(m, L"IsVisible");
        if (!c.run()) return false;
        out = c.rb();
        return true;
    }
    bool setMeshVisible(Obj o, bool on) override
    {
        x0::Ref m = meshComp(o);
        return m.valid() && x0::Call(m, L"SetVisibility").b(L"bNewVisibility", on).b(L"bPropagateToChildren", true).run();
    }
    int setInteraction(Obj o, bool on) override
    {
        x0::Ref a = to_x(o);
        if (!a.valid()) return 0;
        std::vector<x0::Ref> list = compsX(a, L"/Script/Stalker2.InteractionComponent");
        if (list.empty())
        {
            x0::Ref c;
            if (x0::prop<x0::Ref>(a, "NPCComponent", c) && c.valid()) list.push_back(c);
        }
        int n = 0;
        for (const x0::Ref& c : list)
        {
            if (!c.valid()) continue;
            bool act = false;
            bool has = x0::prop<bool>(c, "bIsInteractionActive", act);
            bool owned = s2inter::stockOwned([&](const char* k) { return x0::is_a(c, W(k).c_str()); });
            if (s2inter::flip(owned, has, act, on))
            {
                if (x0::Call(c, L"SetInteractionActive").b(L"bActive", on).run()) n += 1;
            }
        }
        return n;
    }
    std::vector<x0::Ref> compsX(const x0::Ref& a, const wchar_t* path)
    {
        x0::Ref k = x0::find_path(path);
        if (!k.valid() || !a.valid()) return {};
        x0::Call c(a, L"K2_GetComponentsByClass");
        c.o(L"ComponentClass", k);
        if (!c.run()) return {};
        return c.rarr();
    }
    std::vector<Obj> comps(Obj o, const char* clsPath) override
    {
        std::vector<Obj> r;
        for (const x0::Ref& c : compsX(to_x(o), W(clsPath).c_str()))
        {
            Obj s = to_s(c);
            if (s) r.push_back(s);
        }
        return r;
    }
    bool compActive(Obj c, bool& out) override
    {
        x0::Ref x = to_x(c);
        if (!x.valid()) return false;
        x0::Call k(x, L"IsActive");
        if (!k.run()) return false;
        out = k.rb();
        return true;
    }
    bool compQuiet(Obj c, bool stopAudio) override
    {
        x0::Ref x = to_x(c);
        if (!x.valid()) return false;
        if (stopAudio && !x0::call0(x, L"Stop")) return false;
        if (!x0::call0(x, L"Deactivate")) return false;
        if (!x0::Call(x, L"SetActive").b(L"bNewActive", false).b(L"bReset", false).run()) return false;
        return x0::Call(x, L"SetComponentTickEnabled").b(L"bEnabled", false).run();
    }
    bool compWake(Obj c, bool activate) override
    {
        x0::Ref x = to_x(c);
        if (!x.valid()) return false;
        if (!x0::Call(x, L"SetComponentTickEnabled").b(L"bEnabled", true).run()) return false;
        if (activate) return x0::Call(x, L"SetActive").b(L"bNewActive", true).b(L"bReset", false).run();
        return true;
    }

    bool startNode(const std::string& sid, std::string& err) override
    {
        x0::Ref aa = x0::first_of(L"CustomConsoleManagerAA");
        if (!aa.valid())
        {
            err = "CustomConsoleManagerAA ausente";
            return false;
        }
        if (!x0::Call(aa, L"XStartQuestNodeBySID").s(L"SID", W(sid)).run())
        {
            err = "XStartQuestNodeBySID falhou";
            return false;
        }
        return true;
    }
    bool createItem(const std::string& sid, long long uid, long long n) override
    {
        x0::Ref rk = x0::first_of(L"CustomConsoleManagerRK");
        return rk.valid() && x0::Call(rk, L"XCreateItemInInventoryByID").s(L"PrototypeID", W(sid)).i(L"ObjUID", uid).i(L"Count", n).f(L"Durability", 1.0f).run();
    }
    bool spawnObj(const std::string& sid, double x, double y, double z, std::string& err) override
    {
        x0::Ref rk = x0::first_of(L"CustomConsoleManagerRK");
        if (!rk.valid())
        {
            err = "CustomConsoleManagerRK ausente";
            return false;
        }
        bool ok = x0::Call(rk, L"XSpawnObjBySID").s(L"PrototypeSID", W(sid)).i(L"Rank", 1).b(L"bDestroyOnOffline", false)
                      .f(L"Px", (float)x).f(L"Py", (float)y).f(L"Pz", (float)z).f(L"Pitch", 0.f).f(L"Yaw", 0.f).f(L"Roll", 0.f).run();
        if (!ok) err = "XSpawnObjBySID falhou";
        return ok;
    }
    bool uiDrop(uint32_t uid, long long n) override
    {
        for (const x0::Ref& w : x0::all_of(L"InventoryNew"))
        {
            if (!w.valid() || x0::obj_name(w).rfind("Default__", 0) == 0) continue;
            return x0::Call(w, L"OnSplitConfirmed").i(L"ItemAmountSplitForDrop", n).raw(L"ItemUID", &uid, 4).run();
        }
        return false;
    }
    bool spawnItem(const std::string& sid, double x, double y, double z) override
    {
        x0::Ref m = x0::first_of(L"CustomConsoleManagerBuild");
        return m.valid() && x0::Call(m, L"XSpawnItemInCoordinatesBySID").s(L"ItemPrototypeSID", W(sid)).f(L"Px", (float)x).f(L"Py", (float)y).f(L"Pz", (float)z).run();
    }
    bool killUid(long long uid) override
    {
        x0::Ref m = x0::first_of(L"CustomConsoleManagerDC");
        return m.valid() && x0::Call(m, L"XKillNpcByUID").i(L"NpcUID", uid).run();
    }
    bool setUnkillable(long long uid, bool on) override
    {
        x0::Ref m = x0::first_of(L"CustomConsoleManagerBuild");
        return m.valid() && x0::Call(m, L"XSetEntityUnkillableByUID").i(L"EntityUID", uid).b(L"bGodModeState", on).run();
    }
    bool destroyModel(long long uid) override
    {
        x0::Ref m = x0::first_of(L"CustomConsoleManagerAI2");
        return m.valid() && x0::Call(m, L"XDestroyModel").i(L"UID", uid).run();
    }

    bool isOwnBody(Obj o) override
    {
        x0::Ref x = to_x(o);
        return x.valid() && m01::is_own_body(x);
    }
    Obj targetPawn() override { return to_s(m01::target()); }
    Obj puppetPawn() override { return to_s(m01::puppet()); }
    bool bodySpawning() override { return m01::spawn_pending(); }
    std::string worldKey() override
    {
        x0::Ref pw = pawnRef();
        if (!pw.valid()) return std::string();
        return std::to_string(pw.address()) + "|" + x0::full_name(pw);
    }
    bool worldOpen() override { return m01::body_gate(); }
    bool storyNpc(Obj o) override
    {
        x0::Ref x = to_x(o);
        if (!x.valid()) return false;
        if (!x0::port(x0::Mod::lock)) return true;
        return !s2m_lock::story_guid(x).empty();
    }
    std::optional<long long> npcUid(uint64_t, const std::string&) override { return std::nullopt; }
    bool natOn() override { return true; }
    bool natDead(const std::string& hid) override
    {
        try { return s2nat::dead(hid); } catch (...) { return false; }
    }
    bool natCorpse(const std::string& hid, V3& out) override
    {
        try
        {
            double x = 0, y = 0, z = 0;
            if (!s2nat::corpse(hid, x, y, z)) return false;
            out = V3{x, y, z};
            return true;
        }
        catch (...) { return false; }
    }
    std::string lkHold(const std::string& slot, const std::string& key, double age, double lease, double hmax) override
    {
        try { return s2nat::lk_hold(slot, key, age, lease, hmax); } catch (...) { return key; }
    }
    int lkOff(uint64_t addr, int on, double lease) override
    {
        try { return s2nat::lk_off(addr, on, lease); } catch (...) { return 0; }
    }
    std::optional<double> clSwap(const std::string& hid, uint64_t loot, uint64_t src) override
    {
        try
        {
            int mism = -1;
            if (!s2nat::cl_swap(hid, loot, src, mism)) return std::nullopt;
            return (double)mism;
        }
        catch (...) { return std::nullopt; }
    }
    void clSwapTick() override
    {
        try { s2nat::cl_swap_tick(); } catch (...) {}
    }
    bool removeItem(Obj o, const std::string& sid, long long n, long long& removed) override
    {
        removed = 0;
        if (n <= 0) return true;
        if (rmNative() != 1) return false;
        x0::Ref x = to_x(o);
        if (!x.valid()) return false;
        std::wstring w = W(sid);
        int out = 0;
        if (!rmSeh(static_cast<uintptr_t>(x.address()), w.c_str(), (int)w.size() + 1, (int)std::min<long long>(n, 1000000), out)) return false;
        removed = out;
        return true;
    }
    uint64_t natBody(const std::string& hid, int& st) override
    {
        st = 0;
        try { return s2nat::body(hid, st); } catch (...) { return 0; }
    }
    void natClOwn(uint64_t a, bool on) override
    {
        try { s2nat::cl_own(a, on ? 1 : 0); } catch (...) {}
    }
    void gcFeed(int mode, const std::vector<uint64_t>& keep) override { s2m_cgc::feed(mode, keep); }
    void natGcKeep(std::vector<uint64_t>& keep) override
    {
        try { s2nat::gc_keep(keep); } catch (...) {}
    }
    std::string gcStat() override { return s2m_cgc::stat(); }
    bool qiCorpse(Obj corpse, const std::string& id) override
    {
        if (!x0::port(x0::Mod::quest)) return false;
        x0::Ref x = to_x(corpse);
        return x.valid() && s2m_quest::qi_corpse(x.address(), id);
    }
    bool qiHostKeep(const std::string& id, Obj pawn) override
    {
        if (!x0::port(x0::Mod::quest)) return false;
        x0::Ref x = to_x(pawn);
        return x.valid() && s2m_quest::qi_host_keep(id, x.address());
    }

    bool mc() override { return x0::max_clients() > 1 && (x0::is_host() ? x0::hub() : x0::roster().on); }
    int views() override { return x0::views(); }
    int viewPid(int v) override { return x0::view_pid(v); }
    int myPid() override { return x0::my_pid(); }
    bool partV(const char* tag, std::string& out, int v) override { return x0::rpart(tag, out, v); }
    bool remoteV(int v, double& x, double& y, double& recv) override
    {
        const x0::Snap& s = x0::remote(v);
        if (!s.ok || !s.has_pos || s.recv < 0) return false;
        x = s.x;
        y = s.y;
        recv = s.recv;
        return true;
    }
    int viewSessions(int v) override { return x0::peer(v).sessions; }
    std::string peerId() override { return x0::peer().id; }
    std::string campaign() override { return x0::port(x0::Mod::quest) ? s2m_quest::camp_id() : std::string(); }
    bool cuOn() override { return x0::port(x0::Mod::save) && x0::port(x0::Mod::quest); }
    s2m_save::QState* cuQ() override { return &s2m_save::qs(); }
    std::map<std::string, int>* cuApplied() override { return &s2m_save::applied(); }
    int cuApply(const std::string& ev) override
    {
        s2m_quest::Applied a = s2m_quest::quest_apply_ev(ev, false);
        if (!a.ok) return -2;
        return a.r;
    }
    bool cuRbReady() override { return s2m_quest::rb_ready(); }
    void cuRbDone(const std::string& why) override { s2m_quest::rb_done(why); }
};

XEnv g_env;

bool on() { return x0::port(x0::Mod::items); }
}

X0_MODULE(items)
{
    s2m_items::bind(&g_env);
    x0::every(x0::Mod::items, "itemsT100", x0::Rate::t100, [] { if (on()) s2m_items::tick100(); });
    x0::every(x0::Mod::items, "itemsT1000", x0::Rate::t1000, [] {
        g_env.prune();
        if (on() && s2m_save::lootSids().empty() && !s2m_items::lootSids().empty()) s2m_save::setLootSids(s2m_items::lootSids());
    });
    x0::key(x0::Mod::items, 'G', [] { g_env.latch(); });
    s2m::damage::links().item_take = [](const std::string& sid) {
        // A queued removal is not a completed debit. Do not leave a delayed
        // removal in the generic queue after a revive has been cancelled.
        std::string err;
        return on() && g_env.startNode("S2U_P_" + sid + "_1", err);
    };
    s2m::damage::links().item_refund = [](const std::string& sid) {
        return on() && g_env.createItem(sid, 0, 1);
    };
    s2m_quest::hooks().campaign_rotated = [] { if (on()) s2m_items::campaignRotated(); };
    x0::on_role(x0::Mod::items, [](const std::string&, const std::string&) { if (on()) s2m_items::roleChanged(); });
    x0::own_tag(x0::Mod::items, "K", [] { return s2m_items::outK(); });
    x0::own_tag(x0::Mod::items, "X", [] { return s2m_items::outLoot(); });
    for (const char* t : {"C", "R", "Q", "T"}) x0::own_tag(x0::Mod::items, t, [] { return std::string(); });
    x0::own_tag(x0::Mod::items, "G", [] { return s2m_items::outGround(); });
    x0::own_tag(x0::Mod::items, "U", [] { return s2m_items::outU(); });
    x0::own_tag(x0::Mod::items, "Y", [] { return s2m_items::outStash(); });
    x0::own_tag(x0::Mod::items, "YL", [] { return std::string(); });
    x0::own_tag(x0::Mod::items, "AL", [] { return s2m_items::outArt(); });
    x0::own_tag(x0::Mod::items, "CL", [] { return s2m_items::outCl(); });
    for (const char* t : {"CO", "CLT", "CLX"}) x0::own_tag(x0::Mod::items, t, [] { return std::string(); });
    x0::own_tag(x0::Mod::items, "KH", [] { return s2m_items::outKH(); });
    x0::own_tag(x0::Mod::items, "GH", [] { return s2m_items::outGH(); });
    x0::own_tag(x0::Mod::items, "GC", [] { return s2m_items::outGC(); });
    x0::own_tag(x0::Mod::items, "GR", [] { return s2m_items::outGR(); });
    x0::own_tag(x0::Mod::items, "UR", [] { return s2m_items::outUR(); });
    x0::own_tag(x0::Mod::items, "IDR", [] { return s2m_items::outIDR(); });
    x0::own_tag(x0::Mod::items, "IDS", [] { return s2m_items::outIDS(); });
    x0::own_tag(x0::Mod::items, "IDU", [] { return s2m_items::outIDU(); });
    x0::own_tag(x0::Mod::items, "YR", [] { return s2m_items::outYR(); });
    x0::own_tag(x0::Mod::items, "CLH", [] { return s2m_items::outCLH(); });
    m01::add_own_check(x0::Mod::items, [](const x0::Ref& r) { return on() && s2m_items::isLootBody(g_env.to_s(r)); });
    m01::Ext& ex = m01::ext();
    ex.loot_pending = [] { return on() && s2m_items::lootPending(); };
    ex.loot_body = [](const x0::Ref& r) { return on() && s2m_items::isLootBody(g_env.to_s(r)); };
    ex.loot_reshow = [](const x0::Ref& r) { if (on()) s2m_items::lootReshow(g_env.to_s(r)); };
    x0::status(x0::Mod::items, [] { return s2m_items::status(); });
    x0::cmd(x0::Mod::items, "idropnow", [](const std::vector<std::string>& a) { return a.size() >= 3 ? s2m_items::idropNow(a[1], std::atoll(a[2].c_str())) : std::string("idropnow <sid> <n>"); });
    x0::cmd(x0::Mod::items, "items", [](const std::vector<std::string>&) { return s2m_items::status(); });
}
