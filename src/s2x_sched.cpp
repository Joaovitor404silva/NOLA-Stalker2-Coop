#include "s2x_int.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace x0
{
    namespace
    {
        const char* kNames[] = {"x0", "bodies", "damage", "npc", "items", "world", "pop", "quest", "lock", "save", "travel", "session"};
        DWORD g_gameThread = 0;
        double g_fStart = -1, g_prevMod = 0, g_prevTopMs = 0;
        std::string g_prevTop;
        struct PortFlags
        {
            Flag io{"cpp_port"};
            Flag m[11] = {Flag("cpp_bodies"), Flag("cpp_damage"), Flag("cpp_npc"), Flag("cpp_items"), Flag("cpp_world"), Flag("cpp_pop"),
                          Flag("cpp_quest"), Flag("cpp_lock"), Flag("cpp_save"), Flag("cpp_travel"), Flag("cpp_session")};
        };
        PortFlags& pf()
        {
            static PortFlags p;
            return p;
        }
        struct KeyEnt
        {
            Mod m;
            int vk;
            std::vector<int> mods;
            bool debug;
            double hold;
            std::function<void()> press, release;
            bool was = false, fired = false;
            double down_at = 0;
            int errs = 0;
            bool off = false;
        };
        std::deque<KeyEnt>& keys()
        {
            static std::deque<KeyEnt> k;
            return k;
        }
        struct StatusEnt { Mod m; std::function<std::string()> fn; };
        std::vector<StatusEnt>& statuses()
        {
            static std::vector<StatusEnt> v;
            return v;
        }
        struct CmdEnt { Mod m; std::string name; CmdFn fn; };
        std::vector<CmdEnt>& cmds()
        {
            static std::vector<CmdEnt> v;
            return v;
        }
        bool window_front()
        {
            HWND w = GetForegroundWindow();
            if (!w) return false;
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            return pid == GetCurrentProcessId();
        }
    }

    const char* mod_name(Mod m)
    {
        unsigned i = (unsigned)m;
        return i < sizeof kNames / sizeof kNames[0] ? kNames[i] : "?";
    }
    bool mod_from_name(std::string_view name, Mod& out)
    {
        for (unsigned i = 0; i < sizeof kNames / sizeof kNames[0]; ++i)
            if (name == kNames[i]) { out = (Mod)i; return true; }
        return false;
    }
    bool port(Mod m)
    {
        auto& p = pf();
        if (p.io.i() != 1) return false;
        if (m == Mod::x0) return true;
        unsigned i = (unsigned)m;
        if (i == 0 || i > 11) return false;
        return p.m[i - 1].i() == 1;
    }
    bool on_game_thread() { return g_gameThread != 0 && GetCurrentThreadId() == g_gameThread; }

    Iface& iface()
    {
        static Iface i;
        return i;
    }
    bool is_own_body(const Ref& r)
    {
        auto& f = iface().is_own_body;
        return f && f(r);
    }
    bool story_locked()
    {
        auto& f = iface().story_locked;
        return f && f();
    }

    ModReg::ModReg(Mod m, const char* id, void (*init)()) { in::mod_regs().push_back({m, id, init}); }

    int every(Mod m, const char* name, Rate r, std::function<void()> fn, int order)
    {
        auto& S = in::sched();
        in::Row row;
        row.m = m;
        row.name = name ? name : "?";
        row.r = r;
        row.fn = std::move(fn);
        row.order = order;
        S.rows.push_back(std::move(row));
        S.run.clear();
        return (int)S.rows.size() - 1;
    }
    int every(Mod m, const char* name, Rate r, std::function<void()> fn) { return every(m, name, r, std::move(fn), in::order_of(name ? name : "")); }
    void after(Mod m, double secs, std::function<void()> fn) { in::sched().afters.push_back({m, now() + secs, std::move(fn)}); }
    void post(std::function<void()> fn)
    {
        auto& S = in::sched();
        std::lock_guard<std::mutex> g(S.qmu);
        if (S.posted.size() >= 4096) { ++S.dropped; return; }
        S.posted.push_back(std::move(fn));
    }

    int key(Mod m, int vk, std::function<void()> on_press, std::initializer_list<int> mods, bool debug)
    {
        KeyEnt k;
        k.m = m;
        k.vk = vk;
        k.mods.assign(mods.begin(), mods.end());
        k.debug = debug;
        k.hold = 0;
        k.press = std::move(on_press);
        keys().push_back(std::move(k));
        return (int)keys().size() - 1;
    }
    int key_hold(Mod m, int vk, double secs, std::function<void()> on_hold, std::function<void()> on_release)
    {
        KeyEnt k;
        k.m = m;
        k.vk = vk;
        k.debug = false;
        k.hold = secs > 0 ? secs : 0.0001;
        k.press = std::move(on_hold);
        k.release = std::move(on_release);
        keys().push_back(std::move(k));
        return (int)keys().size() - 1;
    }
    bool key_down(int vk) { return window_front() && (GetAsyncKeyState(vk) & 0x8000) != 0; }

    void cmd(Mod m, const char* name, CmdFn fn) { cmds().push_back({m, name ? name : "", std::move(fn)}); }
    void status(Mod m, std::function<std::string()> line) { statuses().push_back({m, std::move(line)}); }
}

namespace x0::in
{
    std::vector<ModEnt>& mod_regs()
    {
        static std::vector<ModEnt> v;
        return v;
    }
    int order_of(std::string_view row)
    {
        static const std::pair<const char*, int> t[] = {
            {"io", 0},
            {"session100", ord::t100_noPause},
            {"bodies100", ord::t100_bodyGate},
            {"travelA", ord::t100_cineTick},
            {"save100", ord::t100_joinTick},
            {"joinTick", ord::t100_joinTick},
            {"lifeGuard", ord::t100_lifeGuard},
            {"quest100", ord::t100_rbTick},
            {"rbTick", ord::t100_rbTick},
            {"saveIn", ord::t100_questIn},
            {"itemsT100", ord::t100_catchupIn},
            {"dmgIn", ord::t100_dmgIn},
            {"shotsOut", ord::t100_dmgIn + 1},
            {"shotsIn", ord::t100_dmgIn + 2},
            {"cadeia", ord::t100_dmgIn + 3},
            {"worldT100", ord::t100_doorTick},
            {"questA", ord::t100_trigTick},
            {"kitTick", ord::t100_kitTick},
            {"vendTrade", ord::t100_vendTradeTick},
            {"ldTick", ord::t100_ldTick},
            {"questB", ord::t100_qpTick},
            {"travelB", ord::t100_cfollowTick},
            {"quest1000", ord::t1000_questRollback},
            {"bodies1000", ord::t1000_noInteract},
            {"chUse", ord::t1000_chUseTick},
            {"ldBlk", ord::t1000_ldBlkTick},
            {"ldSave", ord::t1000_lgTick},
            {"pop", ord::t1000_alifeTick},
            {"relMir", ord::t1000_relMirTick},
        };
        for (auto& [n, o] : t)
            if (row == n) return o;
        return ord::none;
    }

    void run_mod_inits()
    {
        std::stable_sort(mod_regs().begin(), mod_regs().end(), [](const ModEnt& a, const ModEnt& b) { return (unsigned)a.m < (unsigned)b.m; });
        for (auto& r : mod_regs())
        {
            int errs = 0;
            bool off = false;
            sched().guard("init", r.m, errs, off, [&] { r.init(); });
            logf("NOLA x0 module %s registered (port=%d)", r.id, (int)port(r.m));
        }
    }

    Sched& sched()
    {
        static Sched s;
        return s;
    }

    bool Sched::guard(const char* what, Mod m, int& errs, bool& off, const std::function<void()>& fn)
    {
        if (off) return false;
        try
        {
            fn();
            return true;
        }
        catch (const std::exception& e)
        {
            ++errs;
            char b[400];
            std::snprintf(b, sizeof b, "NOLA x0 error %s/%s #%d: %s", mod_name(m), what, errs, e.what());
            if (errs <= errmax) log(b);
        }
        catch (...)
        {
            ++errs;
            char b[200];
            std::snprintf(b, sizeof b, "NOLA x0 error %s/%s #%d: exception", mod_name(m), what, errs);
            if (errs <= errmax) log(b);
        }
        if (errs >= errmax)
        {
            off = true;
            char b[200];
            std::snprintf(b, sizeof b, "NOLA x0 %s/%s switched OFF after %d errors", mod_name(m), what, errs);
            log(b);
        }
        return false;
    }

    void Sched::tick(double t)
    {
        if (last < 0) last = t;
        double dt = t - last;
        if (dt < 0) dt = 0;
        if (dt > 1) dt = 1;
        last = t;
        errmax = (int)std::max<int64_t>(1, fi("x0errmax", 5));
        a100 += dt;
        a1000 += dt;
        bool do100 = a100 >= 0.1, do1000 = a1000 >= 1.0;
        if (do100) a100 -= 0.1 * (int)(a100 / 0.1);
        if (do1000) a1000 -= 1.0 * (int)(a1000 / 1.0);
        std::deque<std::function<void()>> q;
        {
            std::lock_guard<std::mutex> g(qmu);
            q.swap(posted);
        }
        topName.clear();
        topMs = 0;
        auto top = [this](const char* n, std::chrono::steady_clock::time_point c0) {
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count();
            if (ms > topMs) { topMs = ms; topName = n; }
            return ms;
        };
        for (auto& f : q)
        {
            int e = 0;
            bool o = false;
            auto c0 = std::chrono::steady_clock::now();
            guard("post", Mod::x0, e, o, f);
            top("x0/post", c0);
        }
        if (!afters.empty())
        {
            std::vector<After> due;
            for (auto it = afters.begin(); it != afters.end();)
                if (it->at <= t) { due.push_back(std::move(*it)); it = afters.erase(it); }
                else ++it;
            for (auto& a : due)
            {
                if (a.m != Mod::x0 && !port(a.m)) continue;
                int e = 0;
                bool o = false;
                auto c0 = std::chrono::steady_clock::now();
                guard("after", a.m, e, o, a.fn);
                top((std::string(mod_name(a.m)) + "/after").c_str(), c0);
            }
        }
        if (run.size() != rows.size())
        {
            run.resize(rows.size());
            for (size_t i = 0; i < rows.size(); ++i) run[i] = i;
            std::stable_sort(run.begin(), run.end(), [this](size_t a, size_t b) { return rows[a].order < rows[b].order; });
        }
        for (size_t k = 0; k < run.size(); ++k)
        {
            size_t i = run[k];
            Row& r = rows[i];
            if (r.off) continue;
            if (r.r == Rate::t100 && !do100) continue;
            if (r.r == Rate::t1000 && !do1000) continue;
            if (r.m != Mod::x0 && !(active ? active(r.m) : port(r.m))) continue;
            auto c0 = std::chrono::steady_clock::now();
            guard(r.name.c_str(), r.m, r.errs, r.off, r.fn);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count();
            Row& rr = rows[i];
            if (ms > topMs) { topMs = ms; topName = std::string(mod_name(rr.m)) + "/" + rr.name; }
            ++rr.n;
            rr.ms += ms;
            if (ms > rr.mx) rr.mx = ms;
            if (ms > 8) ++rr.over;
        }
    }

    std::string status_text(std::string_view mod)
    {
        std::string o;
        Mod want = Mod::count;
        if (!mod.empty() && !mod_from_name(mod, want)) return "unknown module " + std::string(mod);
        for (auto& r : sched().rows)
        {
            if (want != Mod::count && r.m != want) continue;
            char b[200];
            std::snprintf(b, sizeof b, "%s/%s n=%lld ms=%.1f max=%.2f over8=%d errs=%d%s; ", mod_name(r.m), r.name.c_str(), (long long)r.n, r.ms, r.mx, r.over, r.errs, r.off ? " OFF" : "");
            o += b;
        }
        for (auto& s : statuses())
        {
            if (want != Mod::count && s.m != want) continue;
            if (s.m != Mod::x0 && !port(s.m)) continue;
            std::string l;
            int e = 0;
            bool off = false;
            sched().guard("status", s.m, e, off, [&] { l = s.fn(); });
            if (!l.empty()) o += std::string(mod_name(s.m)) + ": " + l + "; ";
        }
        return o;
    }

    void keys_tick()
    {
        bool front = window_front();
        double t = now();
        bool dbg = fi("debugkeys", 0) == 1;
        for (auto& k : keys())
        {
            if (k.off) continue;
            bool act = (k.m == Mod::x0 || port(k.m)) && (!k.debug || dbg);
            bool down = act && front && (GetAsyncKeyState(k.vk) & 0x8000) != 0;
            if (down && !k.mods.empty())
            {
                bool any = false;
                for (int mv : k.mods) if (GetAsyncKeyState(mv) & 0x8000) { any = true; break; }
                down = any;
            }
            if (k.hold <= 0)
            {
                if (down && !k.was && k.press) sched().guard("key", k.m, k.errs, k.off, k.press);
            }
            else
            {
                if (down && !k.was) { k.down_at = t; k.fired = false; }
                if (down && !k.fired && t - k.down_at >= k.hold)
                {
                    k.fired = true;
                    if (k.press) sched().guard("key_hold", k.m, k.errs, k.off, k.press);
                }
                if (!down && k.was && k.fired)
                {
                    k.fired = false;
                    if (k.release) sched().guard("key_release", k.m, k.errs, k.off, k.release);
                }
            }
            k.was = down;
        }
    }

    std::string cmd_run(const std::string& line)
    {
        auto a = split(line, ' ', false);
        if (a.empty()) return "";
        const std::string& c = a[0];
        if (c == "cfg" && a.size() >= 3)
        {
            Val v;
            if (!lua_tonumber(a[2], v)) v = Val::S(a[2]);
            Val before = flag(a[1]);
            fset(a[1], v, "devcmd");
            return "cfg " + a[1] + " " + before.str() + " -> " + flag(a[1]).str();
        }
        if (c == "flags")
        {
            if (a.size() >= 2) return a[1] + "=" + flag(a[1]).str();
            std::vector<std::string> ks;
            for (auto& [k, e] : flags().m) ks.push_back(k + "=" + e.v.str());
            std::sort(ks.begin(), ks.end());
            std::string o;
            for (auto& k : ks) o += k + " ";
            return o;
        }
        if (c == "status") return status_text(a.size() >= 2 ? a[1] : std::string());
        if (c == "port")
        {
            std::string o = "cpp_port=" + flag("cpp_port").str();
            for (unsigned i = 1; i < (unsigned)Mod::count; ++i) o += std::string(" ") + mod_name((Mod)i) + "=" + (port((Mod)i) ? "1" : "0");
            return o;
        }
        std::string name = c, target;
        if (c == "dump" && a.size() >= 2) target = a[1];
        for (auto& e : cmds())
        {
            if (e.name != name) continue;
            if (!target.empty() && target != mod_name(e.m)) continue;
            std::string r;
            int er = 0;
            bool off = false;
            sched().guard(("cmd " + name).c_str(), e.m, er, off, [&] { r = e.fn(a); });
            return r;
        }
        return "unknown command " + c;
    }

    void cmd_poll()
    {
#ifdef S2X_DEVCMD
        static double at = -1;
        double t = now();
        if (t - at < 0.5) return;
        at = t;
        if (fi("devcmd", 0) != 1) return;
        std::wstring in = bridge_file(L"s2cmd.txt");
        std::string text;
        if (!read_text(in, text)) return;
        std::wstring taken = in + L".run";
        if (!MoveFileExW(in.c_str(), taken.c_str(), MOVEFILE_REPLACE_EXISTING)) return;
        DeleteFileW(taken.c_str());
        std::string out;
        for (auto& l : split(text, '\n', false))
        {
            std::string r = cmd_run(l);
            out += "> " + l + "\n" + r + "\n";
            log("NOLA x0 devcmd: " + l);
        }
        append_text(bridge_file(L"s2cmd_out.txt"), out);
#endif
    }

    void x0_tick_frame()
    {
        if (g_gameThread == 0) g_gameThread = GetCurrentThreadId();
        static bool inited = false;
        double t = now();
        g_fStart = t;
        {
            static double fl_last = -1, fl_max = 0, fl_stat = -1;
            static int64_t fl_n = 0;
            if (fl_last > 0) fl_max = std::max(fl_max, t - fl_last);
            if (fl_last > 0 && (t - fl_last) * 1000 >= fd("stallms", 250))
                logf("NOLA stall: gap=%.0f ms mod_prev=%.0f ms top=%s %.0f ms", (t - fl_last) * 1000, g_prevMod, g_prevTop.empty() ? "-" : g_prevTop.c_str(), g_prevTopMs);
            fl_last = t;
            ++fl_n;
            if (fl_stat < 0) fl_stat = t;
            else if (t - fl_stat >= 10)
            {
                logf("NOLA framegap 10 s: frames=%lld frame_gap_max=%.0f ms", (long long)fl_n, fl_max * 1000);
                fl_stat = t; fl_max = 0; fl_n = 0;
            }
        }
        flags_poll(t, false);
        if (!inited)
        {
            inited = true;
            run_mod_inits();
            every(Mod::x0, "io", Rate::t100, [] { io_tick(); });
            every(Mod::x0, "state", Rate::t1000, [] { state_tick(); });
            every(Mod::x0, "status60", Rate::t1000, [] {
                static double at = now();
                if (!port(Mod::x0) || now() - at < 60) return;
                at = now();
                auto& B = bridge();
                char b[300];
                std::snprintf(b, sizeof b, "NOLA x0 60 s: out=%lld in=%lld peer=%d live=%d nonce=%s ", (long long)B.outs, (long long)B.ins, (int)B.peer.p.state, (int)B.peer.p.live,
                              B.nonce.c_str());
                log(std::string(b) + status_text(""));
            });
        }
        bridge().dispatch();
        {
            auto& B = bridge();
            std::vector<std::string> l;
            {
                std::lock_guard<std::recursive_mutex> g(B.mu);
                l.swap(B.logs);
            }
            for (auto& s : l) log(s);
        }
        keys_tick();
        cmd_poll();
        sched().tick(t);
    }

    void x0_tick_end()
    {
        g_prevMod = g_fStart > 0 ? (now() - g_fStart) * 1000 : 0;
        g_prevTop = sched().topName;
        g_prevTopMs = sched().topMs;
    }
}
