#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <polyhook2/Detour/x64Detour.hpp>
#include "s2x_hot.h"

#include "s2x.h"
#include "s2m_bodies.h"
#include "s2m_loadscreen_logic.h"
#include "s2m_text.h"
#include "s2r_net.h"
#include "s2r_x0.h"

extern "C" int s2m_load_begins(char* guid33);
extern "C" bool s2m_save_join_busy();
bool s2m_travel_tpf_busy();

namespace
{
    uint8_t* exe() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }

    bool eq_seh(const uint8_t* p, const uint8_t* b, size_t n)
    {
        __try { return std::memcmp(p, b, n) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool rd_seh(const void* p, void* out, size_t n)
    {
        __try { std::memcpy(out, p, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    const uintptr_t kVf154 = 0x3679f5c;
    const uintptr_t kPress = 0x3551ba6;
    const uintptr_t kFlow = 0xff7408;
    const uint8_t kVf154Sig[] = {0x56, 0x48, 0x83, 0xec, 0x20, 0x48, 0x89, 0xce, 0xe8, 0x9f, 0xd4, 0x97, 0xfd, 0x48, 0x89,
                                 0xc1, 0xe8, 0xab, 0x00, 0x00, 0x00, 0x84, 0xc0, 0x0f, 0x84, 0x88, 0x00, 0x00, 0x00};
    const uint8_t kPressSig[] = {0x56, 0x48, 0x83, 0xec, 0x20, 0x48, 0x89, 0xce, 0xe8, 0x55, 0x58, 0xaa, 0xfd, 0x48, 0x89, 0xc1, 0xb2, 0x01, 0xe8, 0xcd,
                                 0x06, 0x00, 0x00, 0x48, 0x8b, 0x8e, 0x00, 0x03, 0x00, 0x00, 0x48, 0x8b, 0x01, 0xb2, 0x01, 0xff, 0x90, 0xd8, 0x02, 0x00};
    const uint8_t kFlowSig[] = {0x48, 0x8b, 0x05, 0x21, 0xf1, 0x0f, 0x0a, 0x48, 0x8b, 0x88, 0xd8, 0x01, 0x00, 0x00, 0xe9};

    using WidgetFn = void (*)(int64_t);
    using FlowFn = int64_t (*)();

    x0::Flag F_lsskip("lsskip");
    x0::Flag F_lsgate("lsgate");
    x0::Flag F_lshostskip("lshostskip");
    uint64_t g_tramp = 0, g_pressTramp = 0;
    PLH::x64Detour* g_det = nullptr;
    PLH::x64Detour* g_pressDet = nullptr;
    bool g_tried = false;
    bool g_pressTried = false;
    bool g_pass = false;
    int g_n = 0;

    struct Hold
    {
        bool on = false;
        int64_t w = 0;
        x0::Ref wr, tb;
        x0::TextKeep keep;
        bool kept = false;
        double t0 = 0, hostAt = 0, logAt = 0, goneAt = -1, rxAt = 0;
        uint64_t rx = 0;
        lsx::Hw host = lsx::Hw::none;
        lsx::Up up;
        lsx::Hold last = lsx::Hold::off;
        std::string shown;
        int keys = 0;
        bool joinSeen = false;
    };

    Hold g_h;
    lsx::HostW g_hw;
    std::string g_hwOut;
    std::string g_last = "none";

    int64_t flow_seh()
    {
        __try { return reinterpret_cast<FlowFn>(exe() + kFlow)(); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    bool press_seh(int64_t w)
    {
        __try
        {
            reinterpret_cast<WidgetFn>(g_pressTramp ? (void*)g_pressTramp : (void*)(exe() + kPress))(w);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool press(int64_t w)
    {
        g_pass = true;
        bool ok = press_seh(w);
        g_pass = false;
        return ok;
    }

    lsx::Why decide_now(int64_t gf)
    {
        uint8_t b[2] = {};
        bool wait = gf && rd_seh(reinterpret_cast<const void*>(gf + 0x31), b, 2) && lsx::waits(b[0], b[1]);
        const x0::Peer& p = x0::peer();
        return lsx::decide(F_lsskip.i(), x0::is_client(), p.state == x0::Peer::up, p.state == x0::Peer::silent, p.left, wait, x0::is_host() && F_lshostskip.i() == 1 && s2r::x0_active());
    }

    bool relay_rx(uint64_t& rx)
    {
        s2r::Stats st;
        if (!s2r::x0_stats(st)) return false;
        rx = st.tot_recv;
        return true;
    }

    lsx::Hw host_world(uint32_t* epoch)
    {
        s2r::Roster r;
        if (!s2r::x0_roster(r)) return lsx::Hw::none;
        return lsx::hw_parse(r.world, epoch);
    }

    std::string fmt_tx(nola_text::T id, double secs)
    {
        char b[400];
        std::snprintf(b, sizeof b, nola_text::tx(id).c_str(), secs);
        return b;
    }

    void show(const std::string& txt)
    {
        if (!g_h.tb.valid() || txt == g_h.shown) return;
        if (!g_h.kept) g_h.kept = x0::text_keep(g_h.tb, g_h.keep);
        x0::text_set(g_h.tb, txt);
        x0::Call(g_h.tb, L"SetVisibility").i(L"InVisibility", 3).run();
        g_h.shown = txt;
    }

    void restore()
    {
        if (g_h.kept && g_h.tb.valid()) x0::text_put(g_h.tb, g_h.keep);
    }

    void release(const char* why)
    {
        if (!g_h.on) return;
        Hold h = g_h;
        restore();
        g_h = Hold{};
        if (!h.wr.valid())
        {
            g_last = std::string("dropped: ") + why;
            x0::logf("NOLA LS gate: carga #%d: tela de carga sumiu antes da liberacao (%s, %.1f s)", g_n, why, x0::now() - h.t0);
            return;
        }
        bool ok = press(h.w);
        g_last = why;
        x0::logf("NOLA LS gate: carga #%d liberada apos %.1f s (%s; teclas engolidas %d; exe+0x3551ba6 ok=%s, t=%lld ms)", g_n, x0::now() - h.t0, why, h.keys, ok ? "true" : "false",
                 (long long)x0::wall_ms());
    }

    void main_menu()
    {
        x0::Ref ps = x0::first_of(L"PresentationSubsystem");
        bool ok = ps && x0::call0(ps, L"GoToMainMenu");
        x0::logf("NOLA LS gate: host sem resposta: volta ao menu principal (PresentationSubsystem::GoToMainMenu ok=%s)", ok ? "true" : "false");
    }

    bool pawn_now() { return x0::first_of(L"BP_Stalker2Character_C").valid(); }

    bool arm_now() { return lsx::arm(F_lsgate.i(), x0::is_client(), s2r::x0_active(), pawn_now()); }

    void arm(int64_t w)
    {
        g_h = Hold{};
        g_h.on = true;
        g_h.w = w;
        g_h.wr = x0::Ref::addr((uint64_t)w);
        uint64_t pab = 0;
        if (w && rd_seh(reinterpret_cast<const void*>(w + 0x300), &pab, 8) && pab) g_h.tb = x0::Ref::addr(pab);
        g_h.t0 = x0::now();
        g_h.rxAt = g_h.t0;
        relay_rx(g_h.rx);
        g_h.host = host_world(nullptr);
        g_h.hostAt = g_h.t0;
    }

    nola_text::T text_of(lsx::Hold h)
    {
        switch (h)
        {
        case lsx::Hold::host: return nola_text::T::ls_wait_host;
        case lsx::Hold::enter: return nola_text::T::ls_wait_enter;
        case lsx::Hold::sync: return nola_text::T::ls_wait_sync;
        case lsx::Hold::join: return nola_text::T::ls_wait_join;
        default: return nola_text::T::ls_wait_peer;
        }
    }

    void client_tick()
    {
        if (!g_h.on) return;
        double t = x0::now();
        if (!g_h.wr.valid()) { release("widget gone"); return; }
        uint64_t rx = 0;
        if (relay_rx(rx) && rx != g_h.rx) { g_h.rx = rx; g_h.rxAt = t; }
        uint32_t ep = 0;
        lsx::Hw hw = host_world(&ep);
        if (hw != g_h.host)
        {
            x0::logf("NOLA LS gate: carga #%d: mundo do host %s -> %s (epoca %u, %.1f s)", g_n, lsx::hw_name(g_h.host), lsx::hw_name(hw), ep, t - g_h.t0);
            g_h.host = hw;
            g_h.hostAt = t;
        }
        const x0::Peer& p = x0::peer();
        bool up = p.state == x0::Peer::up;
        lsx::up_step(g_h.up, t, up && lsx::host_ok(hw), x0::remote().seq);
        lsx::In in;
        in.flag = F_lsgate.i();
        in.client = x0::is_client();
        in.relay = s2r::x0_active();
        in.host = hw;
        in.up = up;
        in.t = t;
        in.t0 = g_h.t0;
        in.hostAt = g_h.hostAt;
        in.upAt = g_h.up.at;
        in.rxAt = g_h.rxAt;
        in.frames = g_h.up.frames;
        std::string s;
        in.partB = x0::rpart("B", s);
        in.natnpc = x0::fi("natnpc", 0) == 1;
        in.partNN = in.natnpc && x0::rpart("NN", s);
        bool jb = s2m_save_join_busy(), tb = s2m_travel_tpf_busy();
        in.joinBusy = jb || tb;
        in.wait = x0::fd("lsholdwait", 120);
        in.tpcap = x0::fd("lstpcap", 20);
        in.sync = x0::fd("lssync", 10);
        in.silent = x0::fd("lssilent", 20);
        lsx::Hold r = lsx::gate(in);
        if (r == lsx::Hold::join) g_h.joinSeen = true;
        if (r != g_h.last)
        {
            x0::logf("NOLA LS gate: carga #%d: %s -> %s (host %s, peer %s, quadros %d, B=%d NN=%d, join=%d tpf=%d, %.1f s)", g_n, lsx::hold_name(g_h.last), lsx::hold_name(r), lsx::hw_name(hw),
                     up ? "up" : "ausente", g_h.up.frames, in.partB ? 1 : 0, in.partNN ? 1 : 0, jb ? 1 : 0, tb ? 1 : 0, t - g_h.t0);
            g_h.last = r;
        }
        if (r == lsx::Hold::go)
        {
            release(g_h.joinSeen && in.joinBusy ? "host up + synced, join teleport capped" : "host up + synced");
            return;
        }
        if (r == lsx::Hold::off) { release("gate off"); return; }
        if (r == lsx::Hold::gone)
        {
            if (g_h.goneAt < 0)
            {
                g_h.goneAt = t;
                show(nola_text::tx(nola_text::T::ls_gone));
                x0::logf("NOLA LS gate: carga #%d: host sem resposta (relay mudo %.0f s, mundo do host %s ha %.0f s): o cliente NAO entra no mundo; menu principal em 3 s", g_n, t - g_h.rxAt,
                         lsx::hw_name(hw), t - g_h.hostAt);
            }
            else if (t - g_h.goneAt >= 3)
            {
                release("host gone");
                main_menu();
            }
            return;
        }
        g_h.goneAt = -1;
        show(fmt_tx(text_of(r), t - g_h.t0));
        if (t >= g_h.logAt)
        {
            g_h.logAt = t + 10;
            x0::logf("NOLA LS gate: carga #%d segura na tela de carga (%s, %.0f s)", g_n, lsx::hold_name(r), t - g_h.t0);
        }
    }

    void host_tick()
    {
        if (!x0::is_host() || !s2r::x0_active())
        {
            g_hwOut.clear();
            return;
        }
        x0::Ref pw = x0::first_of(L"BP_Stalker2Character_C");
        x0::Ref sm = x0::first_of(L"ScenarioManager");
        int tick = -1;
        uint8_t b = 0;
        if (sm.valid() && rd_seh(reinterpret_cast<const uint8_t*>(sm.get()) + 0x83, &b, 1)) tick = b;
        int pend = m01::quests_held();
        lsx::Hw was = g_hw.st;
        if (lsx::host_step(g_hw, x0::now(), pw.valid() ? pw.address() : 0, s2m_load_begins(nullptr), tick, pend, 10, 30, 30, x0::fd("lshostcap", 90)))
            x0::logf("NOLA LS host: mundo %s -> %s (epoca %u, tick do ScenarioManager %d, inicios de missao pendentes %d)", lsx::hw_name(was), lsx::hw_name(g_hw.st), g_hw.epoch, tick, pend);
        std::string o = lsx::hw_str(g_hw);
        s2r::x0_world(o);
        g_hwOut = o;
    }

    void press_detour(int64_t w)
    {
        if (g_pass || !g_h.on || !x0::is_client())
        {
            reinterpret_cast<WidgetFn>(g_pressTramp)(w);
            return;
        }
        if (++g_h.keys <= 3) x0::logf("NOLA LS gate: carga #%d: tecla/fim da tela engolido enquanto o mundo do host nao esta pronto (%s)", g_n, lsx::hold_name(g_h.last));
    }

    void vf154_detour(int64_t w)
    {
        lsx::Why y = decide_now(flow_seh());
        bool gate = g_pressDet && w && x0::Ref::addr((uint64_t)w).valid() && arm_now();
        if (gate)
        {
            if (g_h.on)
            {
                restore();
                x0::logf("NOLA LS gate: carga #%d ainda segura quando outra carga terminou: segue a nova", g_n);
            }
            ++g_n;
            arm(w);
        }
        reinterpret_cast<WidgetFn>(g_tramp)(w);
        if (gate)
        {
            x0::logf("NOLA LS gate: carga #%d completa: o cliente espera na tela de carga ate o mundo do host estar de pe e sincronizado (mundo do host %s, peer %s, t=%lld ms)", g_n,
                     lsx::hw_name(g_h.host), x0::peer().state == x0::Peer::up ? "up" : "ausente", (long long)x0::wall_ms());
            show(fmt_tx(nola_text::T::ls_wait_peer, 0));
            client_tick();
            return;
        }
        if (y == lsx::Why::nowait || y == lsx::Why::off) return;
        ++g_n;
        if (y != lsx::Why::skip)
        {
            x0::logf("NOLA LS: carga #%d completa, tela 'aperte qualquer botao' mantida (%s)", g_n, lsx::why_name(y));
            return;
        }
        uint64_t pab = 0;
        if (!w || !rd_seh(reinterpret_cast<const void*>(w + 0x300), &pab, 8) || !pab)
        {
            x0::logf("NOLA LS: carga #%d completa, widget sem PressAnyButtonText: tela mantida", g_n);
            return;
        }
        bool ok = press(w);
        x0::logf("NOLA LS: carga #%d completa, %s em sessao: tela de carga encerrada sem tecla (exe+0x3551ba6 ok=%s, t=%lld ms)", g_n, x0::is_client() ? "cliente" : "host",
                 ok ? "true" : "false", (long long)x0::wall_ms());
    }

    bool hook(PLH::x64Detour*& d, uintptr_t at, void* fn, uint64_t& tramp)
    {
        d = new x0::hot::Detour((uint64_t)(exe() + at), (uint64_t)fn, &tramp);
        if (d->hook()) return true;
        delete d;
        d = nullptr;
        tramp = 0;
        return false;
    }

    void install()
    {
        if (x0::is_client() && F_lsgate.on() && g_det && !g_pressDet && !g_pressTried)
        {
            g_pressTried = true;
            if (eq_seh(exe() + kPress, kPressSig, sizeof kPressSig) && hook(g_pressDet, kPress, (void*)&press_detour, g_pressTramp))
                x0::log("NOLA LS gate: gancho de FUN_1435fa938 instalado depois (o jogo era host antes): a tela de carga espera o mundo do host");
            else
                x0::log("NOLA LS gate: gancho de FUN_1435fa938 falhou: a tela de carga NAO espera o mundo do host");
        }
        if (g_det || g_tried) return;
        if (x0::is_client() ? (!F_lsskip.on() && !F_lsgate.on()) : !(x0::is_host() && F_lsskip.on() && F_lshostskip.on())) return;
        g_tried = true;
        uint8_t* b = exe();
        if (!eq_seh(b + kVf154, kVf154Sig, sizeof kVf154Sig) || !eq_seh(b + kPress, kPressSig, sizeof kPressSig) || !eq_seh(b + kFlow, kFlowSig, sizeof kFlowSig))
        {
            x0::log("NOLA LS: LoadingScreenWidget::vf_154 / FUN_1435fa938 / FUN_14108a334 com bytes diferentes do esperado: fim automatico da tela de carga NAO instalado");
            return;
        }
        if (x0::is_client() && F_lsgate.on()) g_pressTried = true;
        if (x0::is_client() && F_lsgate.on() && !hook(g_pressDet, kPress, (void*)&press_detour, g_pressTramp))
            x0::log("NOLA LS gate: gancho de FUN_1435fa938 falhou: a tela de carga NAO espera o mundo do host");
        if (!hook(g_det, kVf154, (void*)&vf154_detour, g_tramp))
        {
            x0::log("NOLA LS: gancho de LoadingScreenWidget::vf_154 falhou: fim automatico da tela de carga NAO instalado");
            return;
        }
        x0::logf("NOLA LS: gancho de LoadingScreenWidget::vf_154 instalado (exe+0x3679f5c; %s em sessao: fim da carga chama exe+0x3551ba6 como a tecla; portao do mundo do host %s)",
                 x0::is_client() ? "cliente" : "host (lshostskip)", g_pressDet ? "ligado (exe+0x3551ba6 segura a tecla)" : "desligado");
    }

    void ls_init()
    {
        x0::every(x0::Mod::session, "loadscreen1000", x0::Rate::t1000, [] { install(); });
        x0::every(x0::Mod::session, "loadscreen100", x0::Rate::t100, [] {
            if (x0::is_client()) client_tick();
            else host_tick();
        });
        x0::status(x0::Mod::session, [] {
            return std::string("lsgate: ") + (g_pressDet ? "hooked" : "off") + " loads=" + std::to_string(g_n) + " holding=" + (g_h.on ? lsx::hold_name(g_h.last) : "no") + " last=" + g_last +
                   " hostworld=" + (g_hwOut.empty() ? "-" : g_hwOut);
        });
    }

    ::x0::ModReg g_ls_reg(::x0::Mod::session, "loadscreen", &ls_init);
}

extern "C" bool s2m_ls_before_load()
{
    if (!g_h.on) return false;
    release("new save load");
    return true;
}
