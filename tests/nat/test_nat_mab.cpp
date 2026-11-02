#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "../../src/s2n_gait.h"
#include "../../src/s2n_mab.h"

static int g_fail = 0, g_ok = 0;
#define CHECK(c, ...) do { if (c) ++g_ok; else { ++g_fail; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace legacy
{
    struct In { int hs, done, seq, gateSeq; bool path, gate, root, cur, mstop; double err, merr; };
    enum { KEEP, WAIT, GATE_STOP, GATE_KEEP, DYN, PLAY, STOP };
    int body(const In& i)
    {
        if (i.hs != 0 && i.err > i.merr && (!i.gate || i.seq != i.hs || i.root)) return (!i.gate || i.gateSeq != i.hs) ? GATE_STOP : GATE_KEEP;
        if (i.hs != 0)
        {
            if (i.hs == i.done || i.seq != i.hs) return WAIT;
            if (!i.path) return DYN;
            return PLAY;
        }
        if (i.mstop && i.cur) return STOP;
        return KEEP;
    }
}

static void t_bits()
{
    for (int mab = 0; mab <= 3; ++mab)
        for (int mut = 0; mut <= 1; ++mut)
            for (int al = 0; al <= 1; ++al)
                for (int inv = -1; inv <= 1; ++inv)
                {
                    uint32_t b = s2nmab::host_bits(mab, mut != 0, al != 0, inv);
                    CHECK((b & ~(s2nmab::kMon | s2nmab::kInvOk | s2nmab::kInv)) == 0, "mab=%d bits outside 20-22: %08x", mab, b);
                    if (!mut || !al) { CHECK(b == 0, "dead/human sends bits mab=%d mut=%d al=%d", mab, mut, al); continue; }
                    uint32_t fl = 1u | 1024u | b;
                    CHECK(s2nmab::mon_on(fl, 3) == ((mab & 1) != 0), "mon round trip mab=%d", mab);
                    int want = (mab & 2) ? inv : -1;
                    CHECK(s2nmab::inv_of(fl, 3) == want, "inv round trip mab=%d inv=%d got=%d", mab, inv, s2nmab::inv_of(fl, 3));
                    CHECK(s2nmab::inv_of(fl, 1) == -1 && !s2nmab::mon_on(fl, 2), "client flag gates mab=%d", mab);
                    CHECK(s2nmab::inv_of(b, 3) == -1 && !s2nmab::mon_on(b, 3), "no mutant bit means ignore");
                }
    CHECK(s2nmab::inv_of(1u | 1024u, 3) == -1 && !s2nmab::mon_on(1u | 1024u, 3), "old host (no bits) leaves the client path off");
}

static void t_cloak()
{
    using namespace s2nmab;
    CHECK(((kCurOk | kCurQ) & 0x00FFFFFFu) == 0, "cloak bits stay above bit 23 (hid, mab, gait, pid, vend, fixed, mut untouched)");
    CHECK(cur_bits(0, 0.7f) == 0 && cur_bits(1, -1.f) == 0 && cur_bits(1, 2.f) == 0, "flag off or invalid current sends nothing");
    for (int st = 0; st <= 1; ++st)
        for (int i = 0; i <= 100; ++i)
        {
            float c = i / 100.f;
            uint32_t fl = 1u | 1024u | host_bits(3, true, true, st) | cur_bits(1, c);
            float d = cur_of(fl, 1, 3);
            CHECK(d >= 0 && std::fabs(d - c) <= 0.5f / kCurSteps + 1e-6f, "cur round trip %.2f -> %.4f", c, d);
            CHECK(inv_of(fl, 3) == st, "cur bits keep inv state st=%d c=%.2f", st, c);
            CHECK(cur_of(fl, 0, 3) == -1.f && cur_of(fl, 1, 1) == -1.f, "client natcloak=0 or natmab bit2 off ignores the current");
            char row[64];
            std::snprintf(row, sizeof row, "%u", fl);
            unsigned back = 0;
            CHECK(std::sscanf(row, "%u", &back) == 1 && back == fl, "fl survives the %%u wire field %08x", fl);
        }
    CHECK(cur_of(1u | 1024u | host_bits(3, true, true, 1), 1, 3) == -1.f, "old host without cloak bits: client keeps the old path");
    CHECK(cur_of(1u | 1024u | cur_bits(1, 1.f), 1, 3) == -1.f, "current without inv state is ignored");
    CHECK(cur_bits(1, 0.f) == kCurOk && cur_of(cur_bits(1, 1.f) | 1024u | host_bits(3, true, true, 1), 1, 3) == 1.f, "ends exact: 0 and 1");

    CHECK(cloak_snap(0.f, 0, 1.f, 0.f, 0.0, 1.0) == 0.f, "copy stuck cloaked while host settled visible: snap to visible");
    CHECK(cloak_snap(1.f, 1, 0.f, 1.f, 0.0, 1.0) == 1.f, "copy stuck visible while host settled cloaked: snap to cloaked");
    CHECK(cloak_snap(0.f, 0, 0.f, 0.f, 0.2, 1.0) == -1.f, "settled and equal: no call before the re-apply period");
    CHECK(cloak_snap(0.f, 0, 0.f, 0.f, 1.0, 1.0) == 0.f, "settled and equal: material re-applied once per period");
    CHECK(cloak_snap(0.f, 0, 1.f, 1.f, 5.0, 1.0) == -1.f, "copy target not forced yet: wait for the manager detour");
    CHECK(cloak_snap(0.4f, 0, 0.55f, 0.f, 5.0, 1.0) == -1.f, "both easing to visible, small gap: let the stock tick ease");
    CHECK(cloak_snap(0.6f, 1, 0.45f, 1.f, 5.0, 1.0) == -1.f, "both easing to cloaked, small gap: let the stock tick ease");
    CHECK(cloak_snap(0.2f, 0, 0.9f, 0.f, 0.0, 1.0) == 0.2f, "host easing, copy far behind: snap to the host current");
    CHECK(cloak_snap(-1.f, 0, 1.f, 0.f, 9.0, 1.0) == -1.f && cloak_snap(0.f, -1, 1.f, 0.f, 9.0, 1.0) == -1.f, "no host data: no call");
    CHECK(cloak_snap(0.f, 0, std::nanf(""), 0.f, 9.0, 1.0) == -1.f && cloak_snap(0.f, 0, 5.f, 0.f, 9.0, 1.0) == -1.f, "garbage copy read: no call");

    const float rate[2] = {1.f / 0.4f, 1.f / 1.8f};
    for (int ht = 0; ht <= 1; ++ht)
        for (int lagMs = 0; lagMs <= 200; lagMs += 10)
        {
            float tgt = inv_target(ht), r = rate[ht ? 1 : 0], host = 1.f - tgt, copy = 1.f - tgt;
            int snaps = 0;
            for (int f = 0; f < 240; ++f)
            {
                float dt = 1.f / 60.f;
                host += std::clamp(tgt - host, -r * dt, r * dt);
                if (f * dt * 1000.f >= lagMs) copy += std::clamp(tgt - copy, -r * dt, r * dt);
                float hq = cur_of(cur_bits(1, host) | 1024u | host_bits(3, true, true, ht), 1, 3);
                float v = cloak_snap(hq, ht, copy, tgt, 0.1, 1.0);
                if (v >= 0) { copy = v; ++snaps; }
            }
            CHECK(copy == tgt, "ht=%d lag=%d ms: copy ends on the host state (%.3f)", ht, lagMs, copy);
            CHECK(lagMs > 150 || snaps <= 1, "ht=%d lag=%d ms: normal easing lag does not fight the tick (snaps=%d)", ht, lagMs, snaps);
        }
}

static void t_old_client()
{
    uint32_t all = s2nmab::host_bits(3, true, true, 1);
    for (int mb = 0; mb <= 5; ++mb)
    {
        uint32_t base = 1u | 128u | 1024u | (1u << 19) | ((uint32_t)mb << 16);
        uint32_t fl = base | all;
        CHECK((fl & 0xFFFFFu) == base, "bits 0-19 unchanged mb=%d", mb);
        int hg0 = (base & (1u << 19)) ? (int)((base >> 16) & 7) : -1, hg1 = (fl & (1u << 19)) ? (int)((fl >> 16) & 7) : -1;
        CHECK(hg0 == hg1 && hg1 == mb, "MS1 gait decode unchanged mb=%d", mb);
        CHECK(s2ngait::host_gait(hg1, 4) == s2ngait::host_gait(hg0, 4), "host gait unchanged mb=%d", mb);
        CHECK((fl & 1024) && !(fl & 16) && !(fl & 2) && !(fl & 4) && !(fl & 8), "old client: mutant flag set, no human combat bits");
    }
    CHECK((all & s2nmab::kGait) == 0 && (all & (7u << 11)) == 0 && (all & 0x7FFu) == 0, "no overlap with pid, gait or human bits");
}

static void t_decide()
{
    std::mt19937 rng(77);
    std::uniform_int_distribution<int> S(0, 3);
    std::uniform_real_distribution<double> E(0, 400);
    int n = 0, same = 0;
    for (int r = 0; r < 20000; ++r)
    {
        s2nmab::MIn i;
        i.hs = S(rng); i.done = S(rng); i.seq = S(rng); i.path = S(rng) > 0; i.kin = false; i.gate = S(rng) > 0; i.root = S(rng) == 0; i.cur = S(rng) > 1; i.mstop = S(rng) > 0;
        i.err = E(rng); i.merr = 150;
        legacy::In l{i.hs, i.done, i.seq, S(rng), i.path, i.gate, i.root, i.cur, i.mstop, i.err, i.merr};
        int lg = legacy::body(l);
        s2nmab::Act a = s2nmab::mon_act(i);
        int mapped = a == s2nmab::GATE ? ((lg == legacy::GATE_STOP || lg == legacy::GATE_KEEP) ? lg : -1)
                   : a == s2nmab::WAIT ? legacy::WAIT : a == s2nmab::DYN ? legacy::DYN : a == s2nmab::PLAY ? legacy::PLAY : a == s2nmab::STOP ? legacy::STOP : legacy::KEEP;
        ++n; same += mapped == lg;
        s2nmab::MIn k = i; k.kin = true;
        s2nmab::Act ak = s2nmab::mon_act(k);
        CHECK(ak != s2nmab::GATE, "kin never gates");
        if (i.hs != 0 && i.hs != i.done && i.seq == i.hs) CHECK(ak == (i.path ? s2nmab::PLAY : s2nmab::DYN), "kin plays the host montage at once");
    }
    CHECK(same == n, "without kin the mutant decision equals the human body-montage rule (%d/%d)", same, n);
    s2nmab::MIn i; i.hs = 5; i.seq = 5; i.path = true; i.done = 0;
    CHECK(s2nmab::mon_act(i) == s2nmab::PLAY, "new seq plays");
    i.done = 5; CHECK(s2nmab::mon_act(i) == s2nmab::WAIT, "played seq waits");
    i.done = 0; i.seq = 4; CHECK(s2nmab::mon_act(i) == s2nmab::WAIT, "m record not yet in waits");
    i.hs = 0; i.cur = true; CHECK(s2nmab::mon_act(i) == s2nmab::STOP, "host idle stops a local montage");
    i.mstop = false; CHECK(s2nmab::mon_act(i) == s2nmab::KEEP, "natmstop 0 keeps it");
    CHECK(std::fabs(s2nmab::play_at(0.3, 0.2) - 0.5) < 1e-9 && s2nmab::play_at(-1, -5) == 0 && std::fabs(s2nmab::play_at(0, 9) - 2) < 1e-9, "play_at clamps");
    uint32_t fl = 1024u | s2nmab::kMon;
    CHECK(s2nmab::root_lock(3, fl, true) && !s2nmab::root_lock(3, fl, false) && !s2nmab::root_lock(2, fl, true) && !s2nmab::root_lock(3, 1024u, true), "root lock only under kin with host montage data");
    CHECK(s2nmab::inv_state(1.f) == 1 && s2nmab::inv_state(0.f) == 0 && s2nmab::inv_state(0.49f) == 0 && s2nmab::inv_state(0.5f) == 1, "inv threshold");
    CHECK(s2nmab::inv_valid(0) && s2nmab::inv_valid(1) && !s2nmab::inv_valid(-1) && !s2nmab::inv_valid(7), "inv range");
    CHECK(s2nmab::inv_target(1) == 1.f && s2nmab::inv_target(0) == 0.f, "inv target");
}

struct Cls { std::string kind, sid; double invis = 0, tovis = 0; };

static std::map<std::string, Cls> load_cls(const char* path)
{
    std::map<std::string, Cls> m;
    std::ifstream f(path);
    std::string ln;
    while (std::getline(f, ln))
    {
        std::stringstream ss(ln);
        std::string k1, k2, t, v;
        if (!std::getline(ss, k1, '\t') || !std::getline(ss, k2, '\t') || !std::getline(ss, t, '\t') || !std::getline(ss, v, '\t')) continue;
        if (k1.size() < 2 || k1[0] != 's' || k2.size() < 2) continue;
        Cls& c = m[k1.substr(1)];
        std::string k = k2.substr(1);
        if (k == "kind") c.kind = v;
        else if (k == "sid") c.sid = v;
        else if (k == "invis") c.invis = std::atof(v.c_str());
        else if (k == "tovis") c.tovis = std::atof(v.c_str());
    }
    return m;
}

static void t_classes(const std::map<std::string, Cls>& m)
{
    CHECK(m.size() > 30, "nat_classes.tsv rows %zu", m.size());
    int inv = 0;
    for (auto& [k, c] : m)
    {
        if (c.invis <= 0) continue;
        ++inv;
        std::string l = k;
        for (char& ch : l) ch = (char)std::tolower((unsigned char)ch);
        CHECK(c.kind == "mutant", "%s has invisibility but kind %s", k.c_str(), c.kind.c_str());
        CHECK(l.find("bloodsucker") != std::string::npos, "%s invisibility outside the bloodsucker family", k.c_str());
        CHECK(c.tovis > 0 && c.tovis <= 5 && c.invis <= 5, "%s rates %g/%g", k.c_str(), c.invis, c.tovis);
        std::printf("invisibility class %-30s sid=%-22s to_invisible=%gs to_visible=%gs\n", k.c_str(), c.sid.c_str(), c.invis, c.tovis);
    }
    CHECK(inv >= 2, "invisibility classes %d", inv);
    auto b = m.find("BP_AI_Bloodsucker_C");
    CHECK(b != m.end() && std::fabs(b->second.invis - 1.8) < 1e-6 && std::fabs(b->second.tovis - 0.4) < 1e-6, "Bloodsucker 1.8/0.4");
}

static void t_montages(const char* path)
{
    std::ifstream f(path);
    std::string ln;
    std::getline(f, ln);
    int n = 0, gate = 0;
    std::set<std::string> files;
    while (std::getline(f, ln))
    {
        size_t tb = ln.find('\t');
        if (tb == std::string::npos) continue;
        std::string fn = ln.substr(0, tb), p = ln.substr(tb + 1);
        ++n; files.insert(fn);
        CHECK(p.rfind("/Game/", 0) == 0, "%s path %s", fn.c_str(), p.c_str());
        std::string pk = s2nmab::pack_path(p);
        CHECK(pk.find_first_of(",;#|@ ") == std::string::npos, "packed has separators: %s", pk.c_str());
        CHECK(s2nmab::unpack_path(pk) == p, "round trip %s -> %s -> %s", p.c_str(), pk.c_str(), s2nmab::unpack_path(pk).c_str());
        CHECK(pk.size() < p.size(), "packing shortens %s", p.c_str());
        char mb[48];
        std::snprintf(mb, sizeof mb, "m%d,b,%d,%d,", 123456, 999999, 2500);
        std::string rec = mb + pk;
        int mu = 0, sq = 0, pm = 0, off = 0;
        char kc = 0;
        int r = sscanf_s(rec.c_str() + 1, "%d,%c,%d,%d,%n", &mu, &kc, 1, &sq, &pm, &off);
        CHECK(r >= 4 && off > 0 && mu == 123456 && kc == 'b' && sq == 999999 && pm == 2500 && rec.substr(1 + off) == pk, "m record parse %s", rec.c_str());
        std::string l = pk;
        for (char& ch : l) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
        gate += l.find("cov") != std::string::npos || l.find("contextual") != std::string::npos || l.find("_ca_") != std::string::npos;
    }
    CHECK(n >= 150 && files.size() >= 20, "ability montages %d in %zu files", n, files.size());
    std::printf("ability montages: %d in %zu ability files, root-gated names (only off kin): %d\n", n, files.size(), gate);
}

int main()
{
    t_bits();
    t_cloak();
    t_old_client();
    t_decide();
    t_classes(load_cls("../../data/nat_classes.tsv"));
    t_montages("mab_montages.tsv");
    std::printf("test_nat_mab: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
