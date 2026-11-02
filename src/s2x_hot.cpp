#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include <DynamicOutput/DynamicOutput.hpp>
#include <LuaMadeSimple/LuaMadeSimple.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <atomic>
#include <set>

#include "s2r_steam.h"
#include "s2r_x0.h"
#include "s2x.h"
#include "s2x_hot.h"
#include "s2x_hot_logic.h"

using namespace RC;
using namespace RC::Unreal;

extern "C" void s2m_downview_unload();

namespace x0::hot
{
    namespace
    {
        bool g_down = false;

        void say(const std::wstring& w) { Output::send<LogLevel::Normal>(STR("{}\n"), w); }
        std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

        bool alive(const Reg::Ufn& u)
        {
            if (!u.f || u.idx < 0) return false;
            try
            {
                FUObjectItem* it = FUObjectArray::IndexToObject(u.idx);
                return it && x0::hot::logic::slot_matches(it->GetUObject(), static_cast<UObject*>(u.f), u.idx);
            }
            catch (...)
            {
                return false;
            }
        }
    }

    std::pair<int, int> ufn(UFunction* f, std::pair<int, int> ids)
    {
        int32_t idx = f ? f->GetInternalIndex() : -1;
        std::lock_guard<std::mutex> g(reg().mu);
        reg().ufns.push_back({f, ids, idx});
        return ids;
    }

    int shutdown_all()
    {
        if (g_down) return 0;
        g_down = true;
        int n = 0;
        try { s2r::x0_stop(); ++n; } catch (...) {}
        try { s2r::steam_stop_exit(); ++n; } catch (...) {}
        try { s2m_downview_unload(); ++n; } catch (...) {}
        std::vector<std::function<void()>> d;
        std::vector<uint64_t> c;
        std::vector<Reg::Ufn> u;
        std::vector<Detour*> h;
        {
            std::lock_guard<std::mutex> g(reg().mu);
            d = reg().downs;
            c = reg().cbs;
            u = reg().ufns;
            h = reg().dets;
        }
        for (auto it = d.rbegin(); it != d.rend(); ++it)
        {
            try { (*it)(); ++n; } catch (...) {}
        }
        for (uint64_t id : c)
            if (Unreal::Hook::UnregisterCallback(id)) ++n;
        int gone = 0;
        for (auto& r : u)
        {
            if (!alive(r))
            {
                ++gone;
                continue;
            }
            try { UObjectGlobals::UnregisterHook(r.f, r.ids); ++n; } catch (...) {}
        }
        if (gone) say(L"NOLA hot reload: " + std::to_wstring(gone) + L" hooked UFunctions were already gone (GC): not unregistered");
        for (auto it = h.rbegin(); it != h.rend(); ++it)
            if ((*it)->isHooked() && (*it)->unHook()) ++n;
        return n;
    }

    namespace
    {
        using StartFn = RC::CppUserModBase* (*)();
        using UninstFn = void (*)(RC::CppUserModBase*);
        using DownFn = int (*)();

        struct LuaRec
        {
            std::wstring name;
            LuaMadeSimple::Lua* lua;
            LuaMadeSimple::Lua* main;
            LuaMadeSimple::Lua* async;
            std::vector<LuaMadeSimple::Lua*> hooks;
        };

        std::atomic<bool> g_hold{false};
        std::mutex g_pm;
        std::set<lua_State*> g_parked;

        void park_hook(lua_State* L, lua_Debug*)
        {
            if (!g_hold.load()) return;
            {
                std::lock_guard<std::mutex> g(g_pm);
                g_parked.insert(L);
            }
            for (int i = 0; i < 3000 && g_hold.load(); ++i) Sleep(1);
            std::lock_guard<std::mutex> g(g_pm);
            g_parked.erase(L);
        }

        bool async_quiet(lua_State* L)
        {
            {
                std::lock_guard<std::mutex> g(g_pm);
                if (g_parked.count(L)) return true;
            }
            lua_Debug ar{};
            return lua_getstack(L, 0, &ar) == 0;
        }

        void uninst_self(RC::CppUserModBase* m) { delete m; }
        int down_self() { return shutdown_all(); }

        std::wstring self_dir()
        {
            HMODULE m = nullptr;
            wchar_t b[2048] = {};
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&self_dir), &m) || !m) return {};
            DWORD n = GetModuleFileNameW(m, b, 2048);
            if (n == 0 || n >= 2048) return {};
            std::wstring p(b, n);
            size_t s = p.find_last_of(L"\\/");
            return s == std::wstring::npos ? std::wstring() : p.substr(0, s + 1);
        }

        std::string read_small(const std::wstring& p)
        {
            HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) return {};
            char b[512];
            DWORD r = 0;
            std::string o;
            if (ReadFile(h, b, sizeof b, &r, nullptr) && r > 0) o.assign(b, r);
            CloseHandle(h);
            return o;
        }

        std::vector<std::wstring> stale_copies(const std::wstring& dir)
        {
            std::vector<std::wstring> o;
            WIN32_FIND_DATAW fd{};
            HANDLE h = FindFirstFileW((dir + L"hot_*.dll").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return o;
            do
                if (logic::is_copy_name(fd.cFileName)) o.push_back(fd.cFileName);
            while (FindNextFileW(h, &fd));
            FindClose(h);
            return o;
        }

        class Stub : public RC::CppUserModBase
        {
          public:
            explicit Stub(RC::CppUserModBase* inner) : cur_(inner)
            {
                ModName = inner->ModName;
                ModVersion = inner->ModVersion;
                ModDescription = inner->ModDescription;
                ModAuthors = inner->ModAuthors;
                dir_ = self_dir();
                hot_ = x0::bridge_dir() + L"hot\\";
                token_ = logic::trim(read_small(hot_ + L"go"));
                std::vector<std::wstring> old = stale_copies(dir_);
                int del = 0;
                for (auto& f : old)
                    if (DeleteFileW((dir_ + f).c_str())) ++del;
                say(L"NOLA hot reload: stub on (gen 0 = main.dll), trigger " + hot_ + L"go, payload " + hot_ + L"next.dll, " + std::to_wstring(del) + L" stale copies removed");
                g_stub = this;
            }

            ~Stub() override
            {
                g_stub = nullptr;
                if (cur_ && uninst_) uninst_(cur_);
                cur_ = nullptr;
            }

            auto on_update() -> void override
            {
                if (cur_) cur_->on_update();
            }

            auto on_unreal_init() -> void override
            {
                unreal_ = true;
                if (cur_) cur_->on_unreal_init();
                Unreal::Hook::RegisterEngineTickPreCallback(
                    [](Unreal::Hook::TCallbackIterationData<void>&, Unreal::UEngine*, float, bool) {
                        if (g_stub) g_stub->poll();
                    },
                    {false, false, STR("NolaNative"), STR("hotstub")});
            }

            auto on_program_start() -> void override
            {
                if (cur_) cur_->on_program_start();
            }

            auto on_dll_load(StringViewType dll_name) -> void override
            {
                if (cur_) cur_->on_dll_load(dll_name);
            }

            auto on_cpp_mods_loaded() -> void override
            {
                if (cur_) cur_->on_cpp_mods_loaded();
            }

#pragma warning(push)
#pragma warning(disable : 4996)
            auto on_lua_start(StringViewType mod_name, LuaMadeSimple::Lua& lua, LuaMadeSimple::Lua& main_lua, LuaMadeSimple::Lua& async_lua,
                              std::vector<LuaMadeSimple::Lua*>& hook_luas) -> void override
            {
                {
                    std::lock_guard<std::mutex> g(lmu_);
                    luas_.push_back({std::wstring(mod_name), &lua, &main_lua, &async_lua, hook_luas});
                }
                if (cur_) cur_->on_lua_start(mod_name, lua, main_lua, async_lua, hook_luas);
            }

            auto on_lua_stop(StringViewType mod_name, LuaMadeSimple::Lua& lua, LuaMadeSimple::Lua& main_lua, LuaMadeSimple::Lua& async_lua,
                             std::vector<LuaMadeSimple::Lua*>& hook_luas) -> void override
            {
                {
                    std::lock_guard<std::mutex> g(lmu_);
                    luas_.erase(std::remove_if(luas_.begin(), luas_.end(), [&](const LuaRec& r) { return r.lua == &lua; }), luas_.end());
                }
                if (cur_) cur_->on_lua_stop(mod_name, lua, main_lua, async_lua, hook_luas);
            }
#pragma warning(pop)

            void poll()
            {
                ULONGLONG t = GetTickCount64();
                if (t < next_) return;
                next_ = t + 500;
                if (luaPend_ && cur_) replay_lua();
                std::string tok = logic::trim(read_small(hot_ + L"go"));
                if (tok.empty() || tok == token_) return;
                token_ = tok;
                reload(tok);
            }

          private:
            static inline Stub* g_stub = nullptr;

            void reload(const std::string& tok)
            {
                auto t0 = std::chrono::steady_clock::now();
                int ng = gen_ + 1;
                std::wstring dst = dir_ + logic::copy_name(GetCurrentProcessId(), ng);
                std::wstring src = hot_ + L"next.dll";
                std::wstring wt = wide(tok);
                if (!CopyFileW(src.c_str(), dst.c_str(), FALSE))
                {
                    say(L"NOLA hot reload: FAILED token=" + wt + L": copy " + src + L" -> " + dst + L" error " + std::to_wstring(GetLastError()) + L"; gen " + std::to_wstring(gen_) + L" keeps running");
                    return;
                }
                HMODULE m = LoadLibraryW(dst.c_str());
                if (!m)
                {
                    say(L"NOLA hot reload: FAILED token=" + wt + L": LoadLibrary error " + std::to_wstring(GetLastError()) + L"; gen " + std::to_wstring(gen_) + L" keeps running");
                    DeleteFileW(dst.c_str());
                    return;
                }
                auto start = reinterpret_cast<StartFn>(GetProcAddress(m, "nola_hot_start"));
                auto uninst = reinterpret_cast<UninstFn>(GetProcAddress(m, "uninstall_mod"));
                auto down = reinterpret_cast<DownFn>(GetProcAddress(m, "nola_hot_shutdown"));
                if (!start || !uninst || !down)
                {
                    say(L"NOLA hot reload: FAILED token=" + wt + L": next.dll lacks nola_hot_start/uninstall_mod/nola_hot_shutdown (release DLL?); gen " + std::to_wstring(gen_) + L" keeps running");
                    FreeLibrary(m);
                    DeleteFileW(dst.c_str());
                    return;
                }
                int undone = down_ ? down_() : 0;
                if (cur_ && uninst_) uninst_(cur_);
                cur_ = nullptr;
                auto t1 = std::chrono::steady_clock::now();
                SetEnvironmentVariableW(L"NOLA_HOT_GEN", std::to_wstring(ng).c_str());
                cur_ = start();
                uninst_ = uninst;
                down_ = down;
                mods_.push_back(m);
                gen_ = ng;
                if (unreal_ && cur_) cur_->on_unreal_init();
                luaPend_ = true;
                luaTries_ = 0;
                int nl = cur_ ? replay_lua() : -1;
                auto t2 = std::chrono::steady_clock::now();
                double ms_down = std::chrono::duration<double, std::milli>(t1 - t0).count();
                double ms_all = std::chrono::duration<double, std::milli>(t2 - t0).count();
                wchar_t b[256];
                std::swprintf(b, 256, L" ok: %d hooks/callbacks/threads undone, %d Lua mods replayed%s, %.0f ms total (%.0f ms load+shutdown), %d old images stay mapped", undone,
                              nl < 0 ? 0 : nl, nl < 0 ? L" (deferred: a Lua async thread is busy)" : L"", ms_all, ms_down, ng);
                say(L"NOLA hot reload: gen " + std::to_wstring(ng - 1) + L" -> " + std::to_wstring(ng) + L" token=" + wt + b);
            }

            int replay_lua()
            {
                std::vector<LuaRec> recs;
                {
                    std::lock_guard<std::mutex> g(lmu_);
                    recs = luas_;
                }
                struct Prev
                {
                    lua_State* L;
                    lua_Hook h;
                    int mask, count;
                };
                std::vector<Prev> prev;
                g_hold = true;
                for (auto& r : recs)
                {
                    lua_State* L = r.async->get_lua_state();
                    prev.push_back({L, lua_gethook(L), lua_gethookmask(L), lua_gethookcount(L)});
                    lua_sethook(L, &park_hook, LUA_MASKCOUNT, 1);
                }
                bool quiet = false;
                for (int i = 0; i < 50 && !quiet; ++i)
                {
                    quiet = true;
                    for (auto& p : prev)
                        if (!async_quiet(p.L)) quiet = false;
                    if (!quiet) Sleep(1);
                }
                if (quiet)
                {
#pragma warning(push)
#pragma warning(disable : 4996)
                    for (auto& r : recs)
                    {
                        std::vector<LuaMadeSimple::Lua*> hooks = r.hooks;
                        cur_->on_lua_start(r.name, *r.lua, *r.main, *r.main, hooks);
                    }
#pragma warning(pop)
                }
                for (auto& p : prev) lua_sethook(p.L, p.h, p.mask, p.count);
                g_hold = false;
                if (!quiet)
                {
                    if (++luaTries_ % 10 == 1) say(L"NOLA hot reload: Lua replay waits, an async Lua thread is busy (try " + std::to_wstring(luaTries_) + L")");
                    return -1;
                }
                luaPend_ = false;
                if (luaTries_ > 0) say(L"NOLA hot reload: Lua replay done after " + std::to_wstring(luaTries_) + L" waits, " + std::to_wstring(recs.size()) + L" Lua mods");
                return (int)recs.size();
            }

            RC::CppUserModBase* cur_ = nullptr;
            UninstFn uninst_ = &uninst_self;
            DownFn down_ = &down_self;
            std::vector<HMODULE> mods_;
            std::vector<LuaRec> luas_;
            std::mutex lmu_;
            bool luaPend_ = false;
            int luaTries_ = 0;
            std::wstring dir_, hot_;
            std::string token_;
            ULONGLONG next_ = 0;
            int gen_ = 0;
            bool unreal_ = false;
        };
    }

    RC::CppUserModBase* wrap(RC::CppUserModBase* m)
    {
        std::string flags;
        if (x0::read_text(x0::bridge_file(L"flags.txt"), flags) && !logic::enabled(flags))
        {
            say(L"NOLA hot reload: off (hotreload=0 in flags.txt)");
            return m;
        }
        return new Stub(m);
    }
}

extern "C" __declspec(dllexport) int nola_hot_shutdown() { return x0::hot::shutdown_all(); }
