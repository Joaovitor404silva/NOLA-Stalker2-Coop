#include "s2x.h"

#include <cstdio>
#include <optional>
#include <string>

std::string s2m_travel_ft_lock();

namespace s2m_aiwatch
{
    namespace
    {
        struct W
        {
            int n = 0;
            std::optional<uint64_t> pawnAddr;
            std::optional<std::string> pend, why, st;
            std::optional<double> at;
        };
        std::optional<W> g_w;
    }

    void tick()
    {
        if (x0::fi("aiwatch", 0) != 1 || x0::fi("alifeoff", 0) == 1 || !x0::is_host()) return;
        if (!g_w) g_w.emplace();
        W& w = *g_w;
        x0::Ref pw = x0::first_of(L"BP_Stalker2Character_C");
        if (!pw.valid()) return;
        double now = x0::now();
        uint64_t ad = pw.address();
        if (w.pawnAddr != ad)
        {
            w.pend = w.pawnAddr ? "pawn novo" : "primeira vez";
            w.pawnAddr = ad;
        }
        if (!w.pend && now - w.at.value_or(-1000) < x0::fd("aiwatchs", 10)) return;
        std::string why = s2m_travel_ft_lock();
        if (why.empty() && x0::call_b(pw, L"IsSleeping", false)) why = "dormindo";
        if (!why.empty())
        {
            if (w.why != why)
            {
                w.why = why;
                x0::log("NOLA aiwatch: espero (" + why + ")" + (w.pend ? (" com " + *w.pend + " pendente") : std::string()));
            }
            return;
        }
        long long m = (long long)x0::fi("aiwatchm", 4194303);
        x0::Ref am = x0::first_of(L"CustomConsoleManagerAM"), ss = x0::first_of(L"CustomConsoleManagerSS");
        bool ok1 = am.valid() && x0::Call(am, L"XActivateAIFeature").i(L"Feature", m).run();
        bool ok2 = ss.valid() && x0::call0(ss, L"XALifeEnable");
        w.at = now;
        w.n++;
        std::string st = std::string(ok1 ? "ok" : "ERRO") + "," + (ok2 ? "ok" : "ERRO");
        std::optional<std::string> reason;
        if (w.pend) reason = w.pend;
        else if (w.why) reason = "livre de novo depois de " + *w.why;
        else if (st != w.st) reason = "resultado mudou";
        if (reason)
        {
            char b[512];
            std::snprintf(b, sizeof b, "NOLA aiwatch: XActivateAIFeature(%lld) + XALifeEnable -> %s (%s) #%d", m, st.c_str(), reason->c_str(), w.n);
            x0::log(b);
        }
        w.st = st;
        w.pend.reset();
        w.why.reset();
    }

    void reset() { g_w.reset(); }
}

namespace
{
    void aiwatch_init() { x0::every(x0::Mod::pop, "aiwatch", x0::Rate::t1000, [] { s2m_aiwatch::tick(); }, x0::ord::t1000_aiwTick); }
    ::x0::ModReg g_aiwatch_reg(::x0::Mod::pop, "aiwatch", &aiwatch_init);
}
