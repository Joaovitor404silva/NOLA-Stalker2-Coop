#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "../../src/s2r_net.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace s2r;

static std::string slurp(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void spit(const char* path, const std::string& s)
{
    std::ofstream f(path, std::ios::binary);
    f.write(s.data(), (std::streamsize)s.size());
}

static std::string hex(std::string_view s)
{
    static const char* h = "0123456789abcdef";
    std::string o;
    o.reserve(s.size() * 2);
    for (unsigned char c : s)
    {
        o.push_back(h[c >> 4]);
        o.push_back(h[c & 15]);
    }
    return o;
}

static std::string unhex(std::string_view s)
{
    auto v = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    std::string o;
    for (size_t i = 0; i + 1 < s.size(); i += 2) o.push_back((char)(v(s[i]) * 16 + v(s[i + 1])));
    return o;
}

static std::vector<std::string> fields(const std::string& line)
{
    std::vector<std::string> f;
    size_t s = 0;
    while (true)
    {
        size_t t = line.find('\t', s);
        if (t == std::string::npos)
        {
            f.push_back(line.substr(s));
            return f;
        }
        f.push_back(line.substr(s, t - s));
        s = t + 1;
    }
}

static std::string dg_view(const std::string& dg)
{
    if (dg.size() >= 4 && dg[0] == 'S' && dg[1] == '2' && (uint8_t)dg[3] != proto::T_SXC)
    {
        std::string raw;
        if (z::decompress(std::string_view(dg).substr(4), raw)) return hex(dg.substr(0, 4)) + ":" + hex(raw);
    }
    return hex(dg);
}

static int mode_json(const char* in, const char* out)
{
    std::ifstream f(in, std::ios::binary);
    std::string o, line;
    while (std::getline(f, line))
    {
        std::string t = unhex(line);
        js::V v;
        if (!js::parse(t, v)) o += "ERR\n";
        else o += hex(js::dumps(v, true)) + "\t" + hex(js::dumps(v, false)) + "\t" + hex(js::py_repr(v)) + "\n";
    }
    spit(out, o);
    return 0;
}

static int mode_zc(const char* in, const char* out)
{
    spit(out, z::compress(slurp(in), 6));
    return 0;
}

static int mode_zd(const char* in, const char* out)
{
    std::string r, e;
    if (!z::decompress(slurp(in), r, &e))
    {
        std::printf("ERR %s\n", e.c_str());
        return 1;
    }
    spit(out, r);
    return 0;
}

static int mode_script(const char* in, const char* out)
{
    proto::Sender snd;
    proto::Assembler asmb;
    std::ifstream f(in, std::ios::binary);
    std::string o, line;
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto fl = fields(line);
        if (fl.empty() || fl[0].empty()) continue;
        double now = std::atof(fl[1].c_str());
        if (fl[0] == "S")
        {
            std::vector<std::string> dgs;
            std::string msg;
            auto e = snd.split(unhex(fl[2]), now, asmb.apps, dgs, msg);
            o += "S\t" + std::to_string((int)e) + "\t" + std::to_string(dgs.size()) + "\n";
            for (auto& d : dgs) o += "D\t" + dg_view(d) + "\t" + hex(d) + "\n";
        }
        else if (fl[0] == "F")
        {
            std::string msg;
            auto e = asmb.feed(unhex(fl[2]), now, msg);
            o += "F\t" + std::to_string((int)e) + "\t" + std::to_string(asmb.stale) + "\t" + std::to_string(asmb.apps.size()) + "\t" + std::to_string(asmb.npc_count()) + "\n";
        }
        else if (fl[0] == "R")
        {
            std::string r;
            double dx = fl.size() > 2 ? std::atof(fl[2].c_str()) : 0.0;
            if (asmb.render(snd.local_need(), now, dx, r)) o += "R\t" + hex(r) + "\n";
            else o += "R\tNone\n";
        }
    }
    spit(out, o);
    return 0;
}

static int mode_run(int argc, char** argv)
{
    Config c;
    c.listen = 7777;
    std::string peer = "127.0.0.1:7777";
    double seconds = 0;
    std::string dir, world;
    for (int i = 2; i + 1 < argc; i += 2)
    {
        std::string k = argv[i], v = argv[i + 1];
        if (k == "--listen") c.listen = std::atoi(v.c_str());
        else if (k == "--peer") peer = v;
        else if (k == "--dx") c.dx = std::atof(v.c_str());
        else if (k == "--dir") dir = v;
        else if (k == "--record") c.record = widen(v);
        else if (k == "--loss") c.loss = std::atof(v.c_str());
        else if (k == "--seconds") seconds = std::atof(v.c_str());
        else if (k == "--loop") c.loop_ms = std::atoi(v.c_str());
        else if (k == "--role") c.role = v;
        else if (k == "--max") c.maxclients = std::atoi(v.c_str());
        else if (k == "--name") c.disc_name = v;
        else if (k == "--silent") c.slot_silent = std::atof(v.c_str());
        else if (k == "--left") c.slot_left = std::atof(v.c_str());
        else if (k == "--tags") c.relay_tags = relay_tags_d1(slurp(v.c_str()));
        else if (k == "--world") world = v;
    }
    size_t colon = peer.rfind(':');
    c.peer_host = peer.substr(0, colon);
    c.peer_port = std::atoi(peer.c_str() + colon + 1);
    c.dir = widen(dir);
    c.files = true;
    Net n;
    if (!n.start(c, [](const std::string& s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); })) return 2;
    double t0 = clock_s();
    while (seconds <= 0 || clock_s() - t0 < seconds)
    {
        if (!world.empty()) n.put_world(world);
        Sleep(50);
    }
    Stats st = n.stats();
    n.stop();
    std::printf("net_tool totals: sent=%llu recv=%llu written=%llu disc_rx=%llu disc_tx=%llu\n", (unsigned long long)st.tot_sent, (unsigned long long)st.tot_recv,
                (unsigned long long)st.tot_written, (unsigned long long)st.disc_rx, (unsigned long long)st.disc_tx);
    if (c.maxclients > 1 || st.peer_rx || st.roster_rx || st.full_rx)
        std::printf("net_tool hub: conns=%d peer_tx=%llu roster_tx=%llu full_tx=%llu app_fwd=%llu peer_rx=%llu roster_rx=%llu full_rx=%llu bye_rx=%llu\n", st.conns,
                    (unsigned long long)st.peer_tx, (unsigned long long)st.roster_tx, (unsigned long long)st.full_tx, (unsigned long long)st.app_fwd, (unsigned long long)st.peer_rx,
                    (unsigned long long)st.roster_rx, (unsigned long long)st.full_rx, (unsigned long long)st.byes_rx);
    return 0;
}

static int mode_scan(int argc, char** argv)
{
    int port = std::atoi(argv[2]);
    int wait = 1500;
    bool bc = true;
    std::vector<disc::Target> extra;
    for (int i = 3; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--nobc") bc = false;
        else if (a.rfind("--wait=", 0) == 0) wait = std::atoi(a.c_str() + 7);
        else
        {
            std::string ip;
            int p = 0;
            if (disc::parse_addr(a, ip, p)) extra.push_back({ip, p});
            else extra.push_back({a, port});
        }
    }
    auto t = disc::targets({port}, extra, bc);
    std::string err;
    auto hs = disc::run(t, wait, &err);
    std::printf("scan port=%d targets=%d hosts=%d%s\n", port, (int)t.size(), (int)hs.size(), err.empty() ? "" : (" err=" + err).c_str());
    for (auto& h : hs)
        std::printf("host %s name=%s id=%s players=%d/%d hub=%d full=%d ms=%.0f\n", h.addr().c_str(), h.name.c_str(), h.id.c_str(), h.n, h.max, (int)h.hub, (int)h.full(), h.ms);
    std::fflush(stdout);
    return hs.empty() ? 1 : 0;
}

static int mode_tags(const char* tsv, const char* x1)
{
    std::set<std::string> t = relay_tags_d1(slurp(tsv));
    std::string l;
    for (auto& s : t) l += (l.empty() ? "" : ",") + s;
    std::printf("%s\n", l.c_str());
    if (x1) std::printf("%s\n", x1_relay(slurp(x1), t).c_str());
    return 0;
}

static int mode_bench(const char* in)
{
    std::string t = slurp(in);
    js::V v;
    double t0 = clock_s();
    for (int i = 0; i < 200; ++i) js::parse(t, v);
    double t1 = clock_s();
    std::string d;
    for (int i = 0; i < 200; ++i) d = js::dumps(v, true);
    double t2 = clock_s();
    std::string c;
    for (int i = 0; i < 200; ++i) c = z::compress(d, 6);
    double t3 = clock_s();
    const js::V* n = v.get("npcs");
    std::string sm = n && !n->a.empty() ? js::dumps(n->a[0], true) : d.substr(0, 300);
    for (int i = 0; i < 2000; ++i) c = z::compress(sm, 6);
    double t4 = clock_s();
    std::string r;
    for (int i = 0; i < 200; ++i) z::decompress(z::compress(d, 6), r);
    double t5 = clock_s();
    std::printf("per op us: parse %.1f dumps %.1f compress21k %.1f compress_small(%d B) %.1f compress+inflate21k %.1f\n",(t1 - t0) * 5000, (t2 - t1) * 5000, (t3 - t2) * 5000, (int)sm.size(), (t4 - t3) * 500, (t5 - t4) * 5000);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    std::string m = argv[1];
    if (m == "json" && argc >= 4) return mode_json(argv[2], argv[3]);
    if (m == "zc" && argc >= 4) return mode_zc(argv[2], argv[3]);
    if (m == "zd" && argc >= 4) return mode_zd(argv[2], argv[3]);
    if (m == "script" && argc >= 4) return mode_script(argv[2], argv[3]);
    if (m == "run") return mode_run(argc, argv);
    if (m == "scan" && argc >= 3) return mode_scan(argc, argv);
    if (m == "bench" && argc >= 3) return mode_bench(argv[2]);
    if (m == "tags" && argc >= 3) return mode_tags(argv[2], argc >= 4 ? argv[3] : nullptr);
    return 2;
}
