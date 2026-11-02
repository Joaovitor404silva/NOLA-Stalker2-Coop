#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace s2nmab
{
    constexpr uint32_t kMut = 1u << 10, kMon = 1u << 20, kInvOk = 1u << 21, kInv = 1u << 22, kGait = 0xFu << 16;

    inline uint32_t host_bits(int mab, bool mut, bool alive, int inv)
    {
        if (!mut || !alive) return 0;
        uint32_t f = 0;
        if (mab & 1) f |= kMon;
        if ((mab & 2) && inv >= 0) f |= kInvOk | (inv > 0 ? kInv : 0u);
        return f;
    }

    inline bool mon_on(uint32_t fl, int mab) { return (mab & 1) && (fl & kMut) && (fl & kMon); }

    inline int inv_of(uint32_t fl, int mab) { return (mab & 2) && (fl & kMut) && (fl & kInvOk) ? ((fl & kInv) ? 1 : 0) : -1; }

    inline int inv_state(float target) { return target >= 0.5f ? 1 : 0; }

    inline bool inv_valid(float v) { return v >= -0.001f && v <= 1.001f; }

    inline float inv_target(int st) { return st > 0 ? 1.f : 0.f; }

    constexpr uint32_t kCurOk = 1u << 28, kCurQ = 0xFu << 24;
    constexpr int kCurSteps = 15;

    inline uint32_t cur_bits(int cloak, float cur)
    {
        if (!cloak || !inv_valid(cur)) return 0;
        const int q = std::clamp((int)std::lround(cur * kCurSteps), 0, kCurSteps);
        return kCurOk | ((uint32_t)q << 24);
    }

    inline float cur_of(uint32_t fl, int cloak, int mab)
    {
        if (!cloak || inv_of(fl, mab) < 0 || !(fl & kCurOk)) return -1.f;
        return (float)((fl & kCurQ) >> 24) / kCurSteps;
    }

    inline float cloak_snap(float hc, int ht, float cc, float ct, double since, double every)
    {
        if (hc < 0 || ht < 0 || !inv_valid(cc) || !inv_valid(ct)) return -1.f;
        const float tgt = inv_target(ht);
        if (hc == tgt)
        {
            if (ct != tgt) return -1.f;
            return (std::fabs(cc - tgt) > 1e-3f || since >= every) ? tgt : -1.f;
        }
        return std::fabs(cc - hc) > 0.5f ? hc : -1.f;
    }

    enum Act { KEEP = 0, WAIT = 1, GATE = 2, DYN = 3, PLAY = 4, STOP = 5 };

    struct MIn
    {
        int hs = 0, done = 0, seq = 0;
        bool path = false, kin = false, gate = true, root = false, cur = false, mstop = true;
        double err = 0, merr = 150;
    };

    inline Act mon_act(const MIn& i)
    {
        if (i.hs != 0)
        {
            if (!i.kin && i.err > i.merr && (!i.gate || i.seq != i.hs || i.root)) return GATE;
            if (i.hs == i.done || i.seq != i.hs) return WAIT;
            return i.path ? PLAY : DYN;
        }
        return i.mstop && i.cur ? STOP : KEEP;
    }

    inline double play_at(double pos, double age) { return std::max(0.0, pos + std::clamp(age, 0.0, 2.0)); }

    inline bool root_lock(int mab, uint32_t fl, bool kin) { return kin && mon_on(fl, mab); }

    const std::string kPfx = "/Game/_STALKER2/";
    const std::string kPfx2 = "/Game/_STALKER2/SkeletalMeshes/characters/";

    inline std::string pack_path(std::string p)
    {
        if (p.compare(0, kPfx2.size(), kPfx2) == 0) p = "^" + p.substr(kPfx2.size());
        else if (p.compare(0, kPfx.size(), kPfx) == 0) p = "~" + p.substr(kPfx.size());
        size_t sl = p.rfind('/'), dt = p.rfind('.');
        if (sl != std::string::npos && dt != std::string::npos && dt > sl && p.compare(sl + 1, dt - sl - 1, p, dt + 1, std::string::npos) == 0) p.resize(dt);
        for (char& ch : p)
            if (ch == ',' || ch == ';' || ch == '#' || ch == '"' || ch == '\\' || ch == '|' || ch == '@' || ch == '+' || ch == '=' || ch == '`' || ch == ' ') ch = '_';
        return p;
    }

    inline std::string unpack_path(const std::string& p0)
    {
        std::string p = (!p0.empty() && p0[0] == '^') ? kPfx2 + p0.substr(1) : ((!p0.empty() && p0[0] == '~') ? kPfx + p0.substr(1) : p0);
        size_t sl = p.rfind('/');
        if (sl != std::string::npos && sl + 1 < p.size() && p.find('.', sl) == std::string::npos) p += "." + p.substr(sl + 1);
        return p;
    }
}
