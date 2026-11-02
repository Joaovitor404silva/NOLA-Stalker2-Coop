#include "s2m_save.h"
#include "s2x.h"
#include "s2m_items.h"
#include "s2m_quest.h"
#include "s2m_travel.h"
#include "s2m_damage.h"
#include "s2m_bodies.h"
#include "s2m_text.h"
#include "s2x_hot.h"

#include <Windows.h>
#include <clocale>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <string>

extern "C" bool s2m_solo_join_pass(int on, int* asked, int* allowed, unsigned* mask);
extern "C" bool s2m_ls_before_load();

extern "C" {
bool s2m_save_no_info(std::string*, std::string*, std::string*, std::string*) { return false; }
bool s2m_save_no_list(std::string* out) { out->clear(); return false; }
bool s2m_save_no_host(const std::wstring*, int, double, std::string* out) { *out = "s2n_join sem api"; return false; }
bool s2m_save_no_get(const std::wstring*, std::string* state, std::string* id, double* money, std::string* items, std::string* qs, std::string* hand, std::string* w)
{
    *state = "none"; id->clear(); *money = 0; items->clear(); qs->clear(); hand->clear(); w->clear();
    return false;
}
bool s2m_save_no_mark(const std::wstring*, const std::string*, const std::string*) { return false; }
bool s2m_save_no_want(const std::wstring*, const std::string*) { return false; }
bool s2m_save_no_in(const std::wstring*, const std::string*, std::string* state, std::string* detail) { *state = "none"; detail->clear(); return false; }
bool s2m_save_no_install(const std::wstring*, const std::string*, std::string*, int* kh, std::string*, std::string* err) { *kh = -1; *err = "s2n_join sem api"; return false; }
bool s2m_save_no_undo(const std::wstring*, const std::string*) { return false; }
bool s2m_save_no_snap(const std::wstring*, uint64_t, const std::string*, const std::string*, std::string*, int*, double*, std::string*, bool*, std::string* err) { *err = "s2n_join sem api"; return false; }
bool s2m_save_no_flag() { return false; }
bool s2m_save_no_wpfreeze(const std::wstring*, int, int, std::string*, std::string* err) { *err = "s2n_join without api"; return false; }
bool s2m_save_no_wptake(const std::wstring*, const std::string*, int* kh, int* hs, std::string*, long long*, std::string* err) { *kh = -1; *hs = -1; *err = "s2n_join without api"; return false; }
bool s2m_save_no_wplast(const std::wstring*, std::string*, std::string*, int* kh, int* hs) { *kh = -1; *hs = -1; return false; }
bool s2m_save_no_wspstart(const std::wstring*, const std::string*, const std::string*, const std::string*, bool, bool, std::string* err) { *err = "s2n_join without api"; return false; }
bool s2m_save_no_wsppoll(std::string* state, std::string* detail) { *state = "none"; detail->clear(); return false; }
bool s2m_save_no_folder(std::wstring*) { return false; }
bool s2m_save_no_snapworn(const std::wstring*, std::string* out) { out->clear(); return false; }
bool s2m_save_no_worn(uint64_t, std::string* out) { out->clear(); return false; }
bool s2m_save_no_unequip(uint64_t, int, std::string* err) { *err = "s2n_join without api"; return false; }
bool s2m_save_no_equip(uint64_t, const std::string*, int, std::string* err) { *err = "s2n_join without api"; return false; }
bool s2n_join_api_snapworn(const std::wstring* br, std::string* out);
bool s2n_join_api_worn(uint64_t pawn, std::string* out);
bool s2n_join_api_unequip(uint64_t pawn, int slot, std::string* err);
bool s2n_join_api_equip(uint64_t pawn, const std::string* sid, int slot, std::string* err);

bool s2n_save_api_info(std::string* dir, std::string* stamp, std::string* plat, std::string* newest);
bool s2n_save_api_list(std::string* out);
bool s2n_save_api_folder(std::wstring* out);
bool s2n_join_api_host(const std::wstring* br, int kh, double nowS, std::string* out);
bool s2n_join_api_get(const std::wstring* br, std::string* state, std::string* id, double* money, std::string* items, std::string* qs, std::string* hand, std::string* w);
bool s2n_join_api_mark(const std::wstring* br, const std::string* id, const std::string* st);
bool s2n_join_api_want(const std::wstring* br, const std::string* id);
bool s2n_join_api_in(const std::wstring* br, const std::string* id, std::string* state, std::string* detail);
bool s2n_join_api_install(const std::wstring* br, const std::string* id, std::string* guid, int* kh, std::string* cid, std::string* err);
bool s2n_join_api_undo(const std::wstring* br, const std::string* guid);
bool s2n_join_api_snap(const std::wstring* br, uint64_t addr, const std::string* sids, const std::string* wc, std::string* id, int* n, double* money, std::string* hand, bool* reused, std::string* err);
bool s2n_join_api_wpfreeze(const std::wstring* br, int hs, int kh, std::string* guid, std::string* err);
bool s2n_join_api_wptake(const std::wstring* br, const std::string* id, int* kh, int* hs, std::string* cid, long long* size, std::string* err);
bool s2n_join_api_wplast(const std::wstring* br, std::string* file, std::string* cid, int* kh, int* hs);
bool s2n_join_api_wspstart(const std::wstring* br, const std::string* dir, const std::string* guid, const std::string* file, bool own, bool strip, std::string* err);
bool s2n_join_api_wsppoll(std::string* state, std::string* detail);
bool s2m_session_em_nosave();
}
#pragma comment(linker, "/alternatename:s2n_save_api_info=s2m_save_no_info")
#pragma comment(linker, "/alternatename:s2n_save_api_list=s2m_save_no_list")
#pragma comment(linker, "/alternatename:s2n_save_api_folder=s2m_save_no_folder")
#pragma comment(linker, "/alternatename:s2n_join_api_host=s2m_save_no_host")
#pragma comment(linker, "/alternatename:s2n_join_api_get=s2m_save_no_get")
#pragma comment(linker, "/alternatename:s2n_join_api_mark=s2m_save_no_mark")
#pragma comment(linker, "/alternatename:s2n_join_api_want=s2m_save_no_want")
#pragma comment(linker, "/alternatename:s2n_join_api_in=s2m_save_no_in")
#pragma comment(linker, "/alternatename:s2n_join_api_install=s2m_save_no_install")
#pragma comment(linker, "/alternatename:s2n_join_api_undo=s2m_save_no_undo")
#pragma comment(linker, "/alternatename:s2n_join_api_snap=s2m_save_no_snap")
#pragma comment(linker, "/alternatename:s2m_session_em_nosave=s2m_save_no_flag")
#pragma comment(linker, "/alternatename:s2n_join_api_wpfreeze=s2m_save_no_wpfreeze")
#pragma comment(linker, "/alternatename:s2n_join_api_wptake=s2m_save_no_wptake")
#pragma comment(linker, "/alternatename:s2n_join_api_wplast=s2m_save_no_wplast")
#pragma comment(linker, "/alternatename:s2n_join_api_wspstart=s2m_save_no_wspstart")
#pragma comment(linker, "/alternatename:s2n_join_api_wsppoll=s2m_save_no_wsppoll")
#pragma comment(linker, "/alternatename:s2n_join_api_snapworn=s2m_save_no_snapworn")
#pragma comment(linker, "/alternatename:s2n_join_api_worn=s2m_save_no_worn")
#pragma comment(linker, "/alternatename:s2n_join_api_unequip=s2m_save_no_unequip")
#pragma comment(linker, "/alternatename:s2n_join_api_equip=s2m_save_no_equip")

namespace s2m_save {
namespace {

std::wstring wide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

bool invUnsafe(uint64_t a)
{
    if (!a) return true;
    __try
    {
        uintptr_t p = *reinterpret_cast<volatile uintptr_t*>(static_cast<uintptr_t>(a) + 0x650);
        if (!p) return true;
        (void)*reinterpret_cast<volatile uintptr_t*>(p + 0x678);
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
}

struct NStr
{
    const wchar_t* d;
    int32_t n;
    int32_t m;
};
using AmHandleFn = int(__fastcall*)(void*, NStr*, bool);
using AmRefFn = void(__fastcall*)(void*, int);
using AmSumFn = int(__fastcall*)(void*, int*);

bool amEq(const uint8_t* p, const uint8_t* b, size_t n)
{
    __try { return std::memcmp(p, b, n) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

int amNative()
{
    static int ok = -1;
    if (ok >= 0) return ok;
    auto* b = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    static const uint8_t kCount[] = {0x48, 0x8b, 0x81, 0x50, 0x06, 0x00, 0x00, 0x48, 0x8b, 0x88, 0x78, 0x06, 0x00, 0x00, 0x48, 0x85, 0xc9, 0x74, 0x2e, 0xe8};
    static const uint8_t kTab[] = {0x48, 0x8d, 0x0d, 0x67, 0x76, 0x60, 0x04};
    static const uint8_t kCtr[] = {0x48, 0x8b, 0x81, 0xc0, 0x01, 0x00, 0x00, 0xc3};
    static const uint8_t kHandle[] = {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x40, 0x48, 0x89, 0xce, 0x48, 0x8b, 0x05};
    static const uint8_t kRef[] = {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x20, 0x85, 0xd2, 0x7e, 0x3b, 0x89, 0xd6, 0x48, 0x89, 0xcf};
    static const uint8_t kSum[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x78};
    ok = b && amEq(b + 0x6b07eff, kCount, sizeof kCount) && amEq(b + 0x6b07f1a, kTab, sizeof kTab) && amEq(b + 0xff1646, kCtr, sizeof kCtr) &&
                 amEq(b + 0x25a231a, kHandle, sizeof kHandle) && amEq(b + 0x259c49c, kRef, sizeof kRef) && amEq(b + 0x68eb5ae, kSum, sizeof kSum)
             ? 1
             : 0;
    return ok;
}

bool amLoadedSeh(uintptr_t pawn, const wchar_t* w, int n, int& out)
{
    __try
    {
        auto* b = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        uintptr_t a = *reinterpret_cast<volatile uintptr_t*>(pawn + 0x650);
        if (!a) return false;
        uintptr_t c = *reinterpret_cast<volatile uintptr_t*>(a + 0x678);
        if (!c) return false;
        void* ctr = *reinterpret_cast<void* volatile*>(c + 0x1c0);
        if (!ctr) return false;
        void* tab = b + 0xb10f588;
        NStr s{w, n, n};
        int h = reinterpret_cast<AmHandleFn>(b + 0x25a231a)(tab, &s, true);
        if (h <= 0) { out = 0; return true; }
        int cp = h;
        reinterpret_cast<AmRefFn>(b + 0x259c49c)(tab, cp);
        out = reinterpret_cast<AmSumFn>(b + 0x68eb5ae)(ctr, &cp);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool depsOk() { return x0::port(x0::Mod::quest) && x0::port(x0::Mod::travel) && x0::port(x0::Mod::items); }

class XEnv : public Env {
public:
    int invGuardN = 0;
    bool amWarn = false;

    x0::Ref pawnRef(Id a)
    {
        x0::Ref r = x0::Ref::addr(a);
        return r.valid() ? r : x0::Ref();
    }
    x0::Ref moveComp(Id a)
    {
        x0::Ref p = pawnRef(a), cm;
        if (!p || !x0::prop<x0::Ref>(p, "CharacterMovement", cm) || !cm.valid()) return x0::Ref();
        return cm;
    }
    std::wstring br() { return x0::bridge_dir(); }

    double now() override { return x0::now(); }
    double flag(const char* key, double def) override
    {
        x0::Val v = x0::flag(key);
        return v.k == x0::Val::nil ? def : v.num();
    }
    std::string flagText(const char* key, const char* def) override
    {
        x0::Val v = x0::flag(key);
        return v.k == x0::Val::nil ? std::string(def) : v.str();
    }
    bool isHost() override { return x0::is_host(); }
    bool coop() override { return x0::coop(); }
    void log(const std::string& line) override { x0::log(line); }
    void screen(const char* id, const std::string& t, double s) override { x0::ui_msg(id, t, s); }
    void screenClear(const char* id) override { x0::screen_text_clear(id); }
    bool readText(const std::string& name, std::string& out) override { return x0::table(name, out); }
    bool writeText(const std::string& name, const std::string& text) override { return x0::write_text(x0::bridge_file(wide(name)), text); }
    bool writeBin(const std::string& name, const std::string& bytes) override { return x0::write_bin(x0::bridge_file(wide(name)), bytes); }
    bool readAbs(const std::string& path, std::string& out, bool bin) override
    {
        return bin ? x0::read_bin(wide(path), out) : x0::read_text(wide(path), out);
    }
    bool writeAbs(const std::string& path, const std::string& bytes) override
    {
        std::wstring w = wide(path);
        if (!x0::write_bin(w, bytes, false)) return false;
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (!GetFileAttributesExW(w.c_str(), GetFileExInfoStandard, &a)) return false;
        unsigned long long n = ((unsigned long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
        return n == bytes.size();
    }
    bool statTail(const std::string& path, long long& size, std::string& tail16) override
    {
        HANDLE h = CreateFileW(wide(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER sz;
        bool ok = GetFileSizeEx(h, &sz) != 0;
        tail16.clear();
        if (ok)
        {
            size = sz.QuadPart;
            if (size >= 16)
            {
                LARGE_INTEGER off;
                off.QuadPart = size - 16;
                char buf[16];
                DWORD got = 0;
                ok = SetFilePointerEx(h, off, nullptr, FILE_BEGIN) && ReadFile(h, buf, 16, &got, nullptr) && got == 16;
                if (ok) tail16.assign(buf, 16);
            }
        }
        CloseHandle(h);
        return ok;
    }
    bool saveInfo(std::string& dir, std::string& stamp, std::string& plat, std::string& newest) override
    {
        return s2n_save_api_info(&dir, &stamp, &plat, &newest);
    }
    std::string saveList() override
    {
        std::string o;
        s2n_save_api_list(&o);
        return o;
    }
    std::string saveFolder() override
    {
        std::wstring f;
        if (!s2n_save_api_folder(&f) || f.empty()) return "SaveGames";
        std::string o;
        for (wchar_t c : f) o += (c > 0 && c < 0x80) ? (char)c : '_';
        return o;
    }
    std::string dateText() override
    {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &t);
        char buf[64] = {0};
        _locale_t c = _create_locale(LC_ALL, "C");
        size_t n = c ? _strftime_l(buf, sizeof buf, "%c", &tm, c) : std::strftime(buf, sizeof buf, "%c", &tm);
        if (c) _free_locale(c);
        return std::string(buf, n);
    }

    std::string sxHost(int kh) override
    {
        std::wstring b = br();
        std::string o;
        s2n_join_api_host(&b, kh, x0::now(), &o);
        return o;
    }
    bool sxSnap(Id pawn, const std::string& sids, const std::string& wc, std::string& id, int& n, double& money, std::string& hand, bool& reused, std::string& err) override
    {
        std::wstring b = br();
        if (pawn && !pawnRef(pawn)) { err = "pawn invalido"; return false; }
        return s2n_join_api_snap(&b, pawn, &sids, &wc, &id, &n, &money, &hand, &reused, &err);
    }
    std::string sxGet(std::string& id, double& money, std::string& items, std::string& qs, std::string& hand, std::string& w) override
    {
        std::wstring b = br();
        std::string st;
        if (!s2n_join_api_get(&b, &st, &id, &money, &items, &qs, &hand, &w) && st.empty()) st = "none";
        return st;
    }
    bool sxWorn(std::string& out) override
    {
        std::wstring b = br();
        return s2n_join_api_snapworn(&b, &out);
    }
    bool worn(Id pawn, std::string& out) override
    {
        out.clear();
        if (!pawn || !pawnRef(pawn)) return false;
        return s2n_join_api_worn(pawn, &out);
    }
    bool unequip(Id pawn, int slot, std::string& err) override
    {
        if (!pawn || !pawnRef(pawn)) { err = "pawn invalido"; return false; }
        return s2n_join_api_unequip(pawn, slot, &err);
    }
    bool equip(Id pawn, const std::string& sid, int slot, std::string& err) override
    {
        if (!pawn || !pawnRef(pawn)) { err = "pawn invalido"; return false; }
        return s2n_join_api_equip(pawn, &sid, slot, &err);
    }
    bool sxMark(const std::string& id, const std::string& state) override
    {
        std::wstring b = br();
        return s2n_join_api_mark(&b, &id, &state);
    }
    bool sxWant(const std::string& id) override
    {
        std::wstring b = br();
        return s2n_join_api_want(&b, &id);
    }
    std::string sxIn(const std::string& id, std::string& detail) override
    {
        std::wstring b = br();
        std::string st;
        if (!s2n_join_api_in(&b, &id, &st, &detail) && st.empty()) st = "none";
        return st;
    }
    bool sxInstall(const std::string& id, std::string& guid, int& kh, std::string& cid, std::string& err) override
    {
        std::wstring b = br();
        return s2n_join_api_install(&b, &id, &guid, &kh, &cid, &err);
    }
    bool sxUndo(const std::string& guid) override
    {
        std::wstring b = br();
        return s2n_join_api_undo(&b, &guid);
    }
    bool wpFreeze(int hs, int kh, std::string& guid, std::string& err) override
    {
        std::wstring b = br();
        return s2n_join_api_wpfreeze(&b, hs, kh, &guid, &err);
    }
    bool wpTake(const std::string& id, int& kh, int& hs, std::string& cid, long long& size, std::string& err) override
    {
        std::wstring b = br();
        return s2n_join_api_wptake(&b, &id, &kh, &hs, &cid, &size, &err);
    }
    bool wpLast(std::string& file, std::string& cid, int& kh, int& hs) override
    {
        std::wstring b = br();
        return s2n_join_api_wplast(&b, &file, &cid, &kh, &hs);
    }
    bool wspStart(const std::string& dir, const std::string& guid, const std::string& file, bool own, bool strip, std::string& err) override
    {
        std::wstring b = br();
        return s2n_join_api_wspstart(&b, &dir, &guid, &file, own, strip, &err);
    }
    std::string wspPoll(std::string& detail) override
    {
        std::string st;
        if (!s2n_join_api_wsppoll(&st, &detail) && st.empty()) st = "none";
        return st;
    }

    Id localPawn() override
    {
        x0::Ref pc = x0::first_of(L"PC");
        if (!pc) pc = x0::first_of(L"PlayerController");
        x0::Ref pw;
        if (pc && x0::prop<x0::Ref>(pc, "Pawn", pw) && pw.valid()) return pw.address();
        pw = x0::first_of(L"BP_Stalker2Character_C");
        return pw.valid() ? pw.address() : 0;
    }
    bool hp(Id pawn, double& out) override
    {
        x0::Ref p = pawnRef(pawn);
        if (!p) return false;
        x0::Call c(p, L"GetHP");
        if (!c.run()) return false;
        out = c.rf();
        return true;
    }
    bool movementMode(Id pawn, int& out) override
    {
        x0::Ref cm = moveComp(pawn);
        uint8_t m = 0;
        if (!cm || !x0::prop<uint8_t>(cm, "MovementMode", m)) return false;
        out = m;
        return true;
    }
    bool setMovementMode(Id pawn, int mode) override
    {
        x0::Ref cm = moveComp(pawn);
        if (!cm) return false;
        x0::Call c(cm, L"SetMovementMode");
        c.i(L"NewMovementMode", mode).i(L"NewCustomMode", 0);
        return c.run();
    }
    bool location(Id pawn, V3& out) override
    {
        x0::Ref p = pawnRef(pawn);
        x0::Vec3 v;
        if (!p || !x0::call_v(p, L"K2_GetActorLocation", v)) return false;
        out = {v.x, v.y, v.z};
        return true;
    }
    bool localPos(V3& out) override
    {
        Id p = localPawn();
        return p && location(p, out);
    }
    bool money(Id pawn, double& out) override
    {
        x0::Ref p = pawnRef(pawn);
        if (!p) return false;
        x0::Call c(p, L"GetMoney");
        if (!c.run()) return false;
        out = c.rf();
        return true;
    }
    bool modifyMoney(Id pawn, long long delta) override
    {
        x0::Ref p = pawnRef(pawn);
        if (!p) return false;
        x0::Call c(p, L"ModifyMoney");
        c.i(L"Delta", delta);
        return c.run();
    }
    bool itemCount(Id pawn, const std::string& sid, int& out) override
    {
        x0::Ref p = pawnRef(pawn);
        if (!p) return false;
        if (invUnsafe(p.address()))
        {
            if (++invGuardN <= 5) x0::log("NOLA save: GetItemsCountInInventory recusado: +0x650 nulo no pawn (b192)");
            return false;
        }
        x0::Ref med = x0::find_path(L"/Script/Stalker2.Default__CppMediator");
        if (!med) return false;
        x0::Call c(med, L"GetItemsCountInInventory");
        c.o(L"Obj", p).s(L"ItemName", wide(sid));
        if (!c.run()) return false;
        out = static_cast<int>(c.ri());
        return true;
    }
    bool ammoLoaded(Id pawn, const std::string& sid, int& out) override
    {
        if (amNative() != 1)
        {
            if (!amWarn) { amWarn = true; x0::log("NOLA n7: contagem de municao carregada indisponivel (bytes do exe diferentes): conta antiga"); }
            return false;
        }
        x0::Ref p = pawnRef(pawn);
        if (!p || invUnsafe(p.address())) return false;
        std::wstring w = wide(sid);
        return amLoadedSeh(static_cast<uintptr_t>(p.address()), w.c_str(), (int)w.size() + 1, out);
    }
    bool createItem(const std::string& sid, int n, std::string& err) override
    {
        x0::Ref rk = x0::first_of(L"CustomConsoleManagerRK");
        if (!rk) { err = "CustomConsoleManagerRK ausente"; return false; }
        x0::Call c(rk, L"XCreateItemInInventoryByID");
        c.s(L"PrototypeID", wide(sid)).i(L"ObjUID", 0).i(L"Count", n).f(L"Durability", 1.0f);
        if (!c.run()) { err = "XCreateItemInInventoryByID falhou"; return false; }
        return true;
    }
    bool forcedAutoSave(std::string& err) override
    {
        x0::Ref rk = x0::first_of(L"CustomConsoleManagerRK");
        if (!rk) { err = "CustomConsoleManagerRK ausente"; return false; }
        if (!x0::call0(rk, L"XTriggerForcedAutoSave")) { err = "XTriggerForcedAutoSave falhou"; return false; }
        return true;
    }
    bool joinAutoSave(std::string& err) override
    {
        x0::Ref rk = x0::first_of(L"CustomConsoleManagerRK");
        if (!rk) { err = "CustomConsoleManagerRK ausente"; return false; }
        int asked = 0, allowed = 0;
        unsigned mOn = 0, mOff = 0;
        bool pass = s2m_solo_join_pass(1, nullptr, nullptr, &mOn);
        bool called = x0::call0(rk, L"XTriggerForcedAutoSave");
        if (pass) s2m_solo_join_pass(0, &asked, &allowed, &mOff);
        char b[160];
        if (!pass) std::snprintf(b, sizeof b, "save mask not managed (CanSaveNow hook off)");
        else std::snprintf(b, sizeof b, "CanSaveNow(Auto) asked %d, allowed %d, mask 0x%x during, 0x%x after", asked, allowed, mOn, mOff);
        err = b;
        if (!called) { err = "XTriggerForcedAutoSave falhou; " + err; return false; }
        return !pass || allowed > 0;
    }
    bool loadLastSave(std::string& err) override
    {
        x0::Ref mp = x0::first_of(L"CustomConsoleManagerMP");
        if (!mp) { err = "CustomConsoleManagerMP ausente"; return false; }
        s2m_ls_before_load();
        if (!x0::call0(mp, L"XLoadLastSave")) { err = "XLoadLastSave falhou"; return false; }
        return true;
    }
    int loadBegins(std::string& guid) override
    {
        char g[40] = {};
        int n = s2m_load_begins(g);
        guid = g;
        return n;
    }

    bool part(const char* tag, std::string& out) override { return x0::rpart(tag, out); }
    int peerState() override
    {
        switch (x0::peer().state)
        {
        case x0::Peer::up: return peerUp;
        case x0::Peer::silent: return peerSilent;
        default: return peerNone;
        }
    }
    bool peerLiveFalse() override { return x0::peer().state != x0::Peer::none && !x0::peer().live; }
    int peerSessions() override { return x0::peer().sessions; }
    bool remotePos(V3& out) override
    {
        const x0::Snap& r = x0::remote();
        if (!r.ok || !r.has_pos) return false;
        if (x0::fd("kzsafe", 0) == 1 && !x0::in_world(r.x, r.y, r.z)) return false;
        out = {r.x, r.y, r.z};
        return true;
    }

    QuestResult questApply(const std::string& kind, const std::optional<std::string>& a, const std::optional<std::string>& b,
                           const std::optional<std::string>& c, const std::optional<std::string>& sid, bool live) override
    {
        s2m_quest::Applied q = s2m_quest::quest_apply(kind, a, b, c, sid, live);
        QuestResult r;
        r.ok = q.ok;
        r.r = q.r;
        r.hasWhy = q.why.has_value();
        if (q.why) r.why = *q.why;
        return r;
    }
    void rbDone(const std::string& why) override { s2m_quest::rb_done(why); }
    int hostLogSize() override { return static_cast<int>(s2m_quest::host_log_size()); }
    std::optional<long long> hostGen() override
    {
        auto g = s2m_quest::host_gen();
        if (!g) return std::nullopt;
        return static_cast<long long>(*g);
    }
    void qiReset() override { s2m_quest::qi_reset(); }
    bool questTableOn() override { return s2m_quest::qi_table_on(); }
    bool isQuestItem(const std::string& sid) override { return s2m_quest::is_quest_item(sid); }
    void itemRemove(const std::string& sid, int n) override { s2m_items::itemRemove("P", sid, std::to_string(n), ""); }
    bool itemsBusy() override { return s2m_items::runBusy(); }
    int questsHeld() override { return m01::quests_held(); }
    bool cineFrozen() override { return s2m_travel_frozen(); }
    bool cineJoinBusy() override { return s2m_travel_frozen() || s2m_travel_remote_cine(); }
    bool downed() override
    {
        auto& f = x0::iface().local_downed;
        return f && f();
    }
    bool emNoSave() override { return s2m_session_em_nosave(); }
    bool leaveStoreOk() override { return !x0::save_store_switching(); }
    void leashSet(const std::string& name, double m, double secs, const std::string& why, bool hard) override { s2m_travel_lr_set(name, m, secs, why, hard); }
    void leashDrop(const std::string& name, const std::string& why) override { s2m_travel_lr_drop(name, why); }
    bool leashPull(const std::string& name, double m, double secs, const std::string& why) override { return s2m_travel_lr_pull(name, m, secs, why); }
};

XEnv g_xenv;

}
}

namespace
{
    using ActFn = void (*)(uint64_t);

    const uintptr_t kQuitMenu = 0x6cad650;
    const uintptr_t kQuitGame = 0x6cadd26;
    const uint8_t kQuitMenuSig[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x81, 0xec, 0x98, 0x00, 0x00, 0x00, 0x48, 0x89, 0xce};
    const uint8_t kQuitGameSig[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00, 0x48, 0x89, 0xce};

    struct QuitHook
    {
        const char* name;
        uintptr_t rva;
        const uint8_t* sig;
        size_t n;
        uint64_t tramp;
        x0::hot::Detour* det;
    };

    QuitHook g_qh[2] = {{"quit to main menu", kQuitMenu, kQuitMenuSig, sizeof kQuitMenuSig, 0, nullptr},
                        {"quit game", kQuitGame, kQuitGameSig, sizeof kQuitGameSig, 0, nullptr}};
    bool g_qhTried = false;

    struct Held
    {
        int which = -1;
        uint64_t self = 0;
        double at = 0;
    };
    Held g_held;

    bool qh_eq(const uint8_t* p, const uint8_t* b, size_t n)
    {
        __try { return std::memcmp(p, b, n) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void qh_run(int i, uint64_t self) { reinterpret_cast<ActFn>(g_qh[i].tramp)(self); }

    void qh_act(int i, uint64_t self)
    {
        if (!x0::on_game_thread()) { qh_run(i, self); return; }
        if (g_held.self)
        {
            x0::logf("NOLA clq: %s clicked while the leave save of %s runs: ignored", g_qh[i].name, g_qh[g_held.which].name);
            return;
        }
        std::string why;
        int r = 0;
        try
        {
            if (s2m_save::depsOk()) r = s2m_save::leaveAsk(why);
            else why = "save module stopped";
        }
        catch (...)
        {
            r = 0;
            why = "exception";
        }
        x0::logf("NOLA clq: %s: %s (%s)", g_qh[i].name, r ? "held until the client save is written" : "goes on now", why.c_str());
        if (!r) { qh_run(i, self); return; }
        g_held.which = i;
        g_held.self = self;
        g_held.at = x0::now();
        x0::ui_msg("clq", nola_text::tx(nola_text::T::say_leave_save), x0::fd("clqwait", 12) + 2);
    }

    void qh_menu(uint64_t self) { qh_act(0, self); }
    void qh_game(uint64_t self) { qh_act(1, self); }

    void qh_install()
    {
        if (g_qhTried) return;
        g_qhTried = true;
        uint8_t* b = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        uint64_t cbs[2] = {(uint64_t)&qh_menu, (uint64_t)&qh_game};
        for (int i = 0; i < 2; ++i)
        {
            QuitHook& q = g_qh[i];
            if (!qh_eq(b + q.rva, q.sig, q.n))
            {
                x0::logf("NOLA clq: %s exe+0x%llx bytes differ (new exe?): no client save on this leave", q.name, (unsigned long long)q.rva);
                continue;
            }
            q.det = new x0::hot::Detour((uint64_t)(b + q.rva), cbs[i], &q.tramp);
            if (!q.det->hook())
            {
                delete q.det;
                q.det = nullptr;
                q.tramp = 0;
                x0::logf("NOLA clq: %s hook failed: no client save on this leave", q.name);
                continue;
            }
            x0::logf("NOLA clq: %s hook on (exe+0x%llx, clqsave=%lld)", q.name, (unsigned long long)q.rva, (long long)x0::fi("clqsave", 1));
        }
    }

    void qh_tick()
    {
        qh_install();
        if (!g_held.self) return;
        if (s2m_save::depsOk() && s2m_save::leaveWait()) return;
        Held h = g_held;
        g_held = Held{};
        x0::screen_text_clear("clq");
        bool live = x0::Ref::addr(h.self).valid();
        x0::logf("NOLA clq: %s goes on %.1f s after the click%s", g_qh[h.which].name, x0::now() - h.at, live ? "" : " (the menu action is gone: nothing to resume)");
        if (live) qh_run(h.which, h.self);
    }
}

X0_MODULE(save)
{
    s2m_save::bind(&s2m_save::g_xenv);
    s2m::damage::links().die_follow = [] { if (s2m_save::depsOk()) s2m_save::n7DieFollow(); };
    x0::on_tag(x0::Mod::save, "GO", [](std::string_view v, const x0::Snap&) { if (s2m_save::depsOk()) s2m_save::n7Go(std::string(v)); });
    x0::every(x0::Mod::save, "saveJoin", x0::Rate::t100, [] {
        if (!s2m_save::depsOk())
        {
            x0::log_every("save_deps", 30, "NOLA save: modulo parado: cpp_save=1 precisa de cpp_quest, cpp_travel e cpp_items ligados (o #N e o b165/n7 nao rodam)");
            return;
        }
        if (s2m_save::lootSids().empty() && !s2m_items::lootSids().empty()) s2m_save::setLootSids(s2m_items::lootSids());
        s2m_save::tick100_join();
    }, x0::ord::t100_joinTick);
    x0::every(x0::Mod::save, "saveIn", x0::Rate::t100, [] { if (s2m_save::depsOk()) s2m_save::tick100_mid(); }, x0::ord::t100_questIn);
    x0::every(x0::Mod::save, "saveKit", x0::Rate::t100, [] { if (s2m_save::depsOk()) s2m_save::tick100_kit(); }, x0::ord::t100_kitTick);
    x0::every(x0::Mod::save, "saveLeave", x0::Rate::t100, [] { qh_tick(); });
    x0::own_tag(x0::Mod::save, "N", [] { return s2m_save::depsOk() ? s2m_save::csOut() : std::string(); });
    x0::own_tag(x0::Mod::save, "HLD", [] { return s2m_save::depsOk() ? s2m_save::n7HL() : std::string(); });
    x0::status(x0::Mod::save, [] { return std::string(s2m_save::depsOk() ? "" : "PARADO (faltam cpp_quest/cpp_travel/cpp_items) ") + s2m_save::status(); });
}

extern "C" bool s2m_save_join_busy() { return s2m_save::depsOk() && s2m_save::joinBusy(); }
