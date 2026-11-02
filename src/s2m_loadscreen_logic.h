#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>

namespace lsx
{
    enum class Why : uint8_t { skip = 0, off, host, solo, nowait };

    inline const char* why_name(Why w)
    {
        switch (w)
        {
        case Why::skip: return "skip";
        case Why::off: return "off";
        case Why::host: return "host";
        case Why::solo: return "solo";
        case Why::nowait: return "nowait";
        }
        return "?";
    }

    inline bool peer_in(bool up, bool silent, bool left) { return up || (silent && !left); }

    inline bool waits(uint8_t gf31, uint8_t gf32) { return gf31 != 0 || gf32 != 0; }

    inline Why decide(int64_t flag, bool client, bool up, bool silent, bool left, bool wait, bool hostskip = false)
    {
        if (flag != 1) return Why::off;
        if (!client && !hostskip) return Why::host;
        if (client && !peer_in(up, silent, left)) return Why::solo;
        if (!wait) return Why::nowait;
        return Why::skip;
    }

    enum class Hw : uint8_t { none = 0, loading, held, up };

    inline const char* hw_name(Hw w)
    {
        switch (w)
        {
        case Hw::none: return "none";
        case Hw::loading: return "loading";
        case Hw::held: return "held";
        case Hw::up: return "up";
        }
        return "?";
    }

    struct HostW
    {
        uint32_t epoch = 0;
        Hw st = Hw::loading;
        uint64_t pawn = 0;
        int lb = -1;
        double pawnAt = 0, loadAt = 0, tickAt = -1;
    };

    inline bool host_step(HostW& h, double t, uint64_t pawn, int lb, int tick, int pending, double noSm = 10, double drain = 30, double stuck = 30, double cap = 90)
    {
        Hw was = h.st;
        if (!pawn)
        {
            h.pawn = 0;
            h.st = Hw::loading;
        }
        else if (pawn != h.pawn)
        {
            h.pawn = pawn;
            h.pawnAt = t;
            h.tickAt = -1;
            h.lb = lb;
            h.st = Hw::held;
        }
        else if (lb > h.lb)
        {
            h.lb = lb;
            h.loadAt = t;
            h.st = Hw::loading;
        }
        else if (h.st == Hw::loading && t - h.loadAt > stuck)
        {
            h.pawnAt = t;
            h.tickAt = -1;
            h.st = Hw::held;
        }
        if (h.st == Hw::held)
        {
            if (tick > 0 && h.tickAt < 0) h.tickAt = t;
            bool loose = tick < 0 ? t - h.pawnAt >= noSm : tick > 0 && (pending <= 0 || t - h.tickAt >= drain || t - h.pawnAt >= cap);
            if (loose)
            {
                h.st = Hw::up;
                ++h.epoch;
            }
        }
        return h.st != was;
    }

    inline std::string hw_str(const HostW& h) { return std::to_string(h.epoch) + "," + std::to_string((int)h.st); }

    inline Hw hw_parse(const std::string& s, uint32_t* epoch = nullptr)
    {
        size_t c = s.find(',');
        if (c == std::string::npos || c == 0 || c + 2 != s.size()) return Hw::none;
        for (size_t i = 0; i < c; ++i)
            if (s[i] < '0' || s[i] > '9') return Hw::none;
        int v = s[c + 1] - '0';
        if (v < 1 || v > 3) return Hw::none;
        if (epoch) *epoch = (uint32_t)std::strtoul(s.substr(0, c).c_str(), nullptr, 10);
        return (Hw)v;
    }

    enum class Hold : uint8_t { go = 0, off, peer, host, enter, sync, join, gone };

    inline const char* hold_name(Hold h)
    {
        switch (h)
        {
        case Hold::go: return "go";
        case Hold::off: return "off";
        case Hold::peer: return "peer";
        case Hold::host: return "host";
        case Hold::enter: return "enter";
        case Hold::sync: return "sync";
        case Hold::join: return "join";
        case Hold::gone: return "gone";
        }
        return "?";
    }

    inline bool arm(int64_t flag, bool client, bool relay, bool pawn) { return flag == 1 && client && relay && pawn; }

    struct In
    {
        int64_t flag = 1;
        bool client = true, relay = true;
        Hw host = Hw::none;
        bool up = false;
        double t = 0, t0 = 0, hostAt = 0, upAt = -1, rxAt = 0;
        int frames = 0;
        bool partB = false, partNN = false, natnpc = false;
        bool joinBusy = false;
        double wait = 120, tpcap = 20, sync = 10, silent = 20;
    };

    inline bool fresh(const In& i) { return i.frames >= 2 && i.partB && (i.partNN || !i.natnpc); }

    inline bool host_ok(Hw h) { return h == Hw::up || h == Hw::none; }

    inline Hold gate(const In& i)
    {
        if (i.flag != 1 || !i.client || !i.relay) return Hold::off;
        if (i.t - i.rxAt > i.silent) return Hold::gone;
        if (i.host == Hw::loading) return i.t - (i.hostAt > i.t0 ? i.hostAt : i.t0) > i.wait ? Hold::gone : Hold::host;
        if (i.host == Hw::held) return Hold::enter;
        if (!i.up) return i.t - (i.hostAt > i.t0 ? i.hostAt : i.t0) > i.wait ? Hold::gone : Hold::peer;
        if (i.upAt < 0) return Hold::sync;
        if (!fresh(i) && i.t - i.upAt < i.sync) return Hold::sync;
        if (i.joinBusy && i.t - i.upAt < i.tpcap) return Hold::join;
        return Hold::go;
    }

    struct Up
    {
        double at = -1;
        uint64_t seq = 0;
        int frames = 0;
    };

    inline void up_step(Up& u, double t, bool ready, uint64_t seq)
    {
        if (!ready)
        {
            u = Up{};
            return;
        }
        if (u.at < 0)
        {
            u.at = t;
            u.seq = seq;
            u.frames = 0;
            return;
        }
        if (seq != u.seq)
        {
            u.seq = seq;
            ++u.frames;
        }
    }
}
