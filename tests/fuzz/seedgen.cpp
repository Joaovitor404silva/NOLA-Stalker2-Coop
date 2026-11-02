#include "../../src/s2r_net.cpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sg
{
    using namespace s2r;
    using js::V;

    struct Rec
    {
        std::string b;
        void add(int route, int who, int dt, std::string_view dg)
        {
            if (dg.size() > 0xFFFF) dg = dg.substr(0, 0xFFFF);
            b.push_back((char)((route & 3) | ((who & 3) << 2) | ((dt & 15) << 4)));
            b.push_back((char)(dg.size() & 0xFF));
            b.push_back((char)(dg.size() >> 8));
            b.append(dg);
        }
    };

    static std::string slurp(const std::filesystem::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }

    static int written = 0;
    static std::filesystem::path outdir;

    static void put(const char* name, const Rec& r)
    {
        std::ofstream f(outdir / name, std::ios::binary);
        f.write(r.b.data(), (std::streamsize)r.b.size());
        ++written;
    }

    static std::vector<std::string> split(const std::string& txt, double now)
    {
        proto::Sender s;
        proto::AppCache have;
        std::vector<std::string> out;
        std::string msg;
        s.split(txt, now, have, out, msg);
        return out;
    }

    static std::string sxc(uint64_t wid, uint32_t i, std::string_view data)
    {
        std::string d = "S2";
        d.push_back((char)proto::PROTO);
        d.push_back((char)proto::T_SXC);
        for (int s = 0; s < 8; ++s) d.push_back((char)(wid >> (8 * s)));
        for (int s = 0; s < 4; ++s) d.push_back((char)(i >> (8 * s)));
        d.append(data);
        return d;
    }

    static uint32_t crc32(std::string_view s)
    {
        uint32_t c = 0xFFFFFFFFu;
        for (unsigned char ch : s)
        {
            c ^= ch;
            for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
        return ~c;
    }
}

int main(int argc, char** argv)
{
    using namespace sg;
    if (argc < 3)
    {
        std::printf("usage: seedgen <fixtures dir> <out dir>\n");
        return 2;
    }
    std::filesystem::path fx(argv[1]);
    outdir = argv[2];
    std::filesystem::create_directories(outdir);
    std::string a_out = slurp(fx / "A_out.json"), b_in = slurp(fx / "B_in.json"), a_in = slurp(fx / "A_in.json"), b_out = slurp(fx / "B_out.json");
    if (a_out.empty() || b_in.empty() || a_in.empty() || b_out.empty())
    {
        std::printf("FAIL fixtures missing in %s\n", argv[1]);
        return 1;
    }
    std::vector<std::string> da = split(a_out, 10.0), db = split(b_in, 10.0), da2 = split(a_out, 10.6);
    if (da.empty() || db.empty())
    {
        std::printf("FAIL Sender::split produced no datagrams\n");
        return 1;
    }

    {
        Rec r;
        for (auto& d : da) r.add(1, 0, 0, d);
        for (auto& d : da2) r.add(1, 0, 2, d);
        put("cl_a_stream", r);
    }
    {
        Rec r;
        for (auto& d : db) r.add(1, 0, 0, d);
        put("cl_b_stream", r);
    }

    Config cfg;
    cfg.role = "host";
    cfg.maxclients = proto::MAXV;
    cfg.slot_silent = 2;
    cfg.slot_left = 6;
    cfg.peer_gap = 0;
    Hub hub(cfg);
    std::vector<std::pair<int, std::string>> hub_out;
    hub.send = [&](const std::string& dg, const sockaddr_in& to) { hub_out.push_back({(int)(ntohs(to.sin_port) - 40000), dg}); return true; };
    hub.say = [](const std::string&) {};
    hub.warn = [](const char*, const std::string&) {};
    hub.view_bye = [](int, const std::string&) {};
    hub.publish = [](const Roster&) {};
    {
        Rec r;
        Stats st;
        uint64_t tot = 0;
        double now = 100.0;
        auto from = [](int i)
        {
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons((u_short)(40000 + i));
            a.sin_addr.s_addr = htonl(0x7F000001u + (uint32_t)i);
            return a;
        };
        for (auto& d : db)
        {
            now += 0.02;
            r.add(0, 1, 0, d);
            hub.rx(d, from(1), now, st, tot);
        }
        for (auto& d : da)
        {
            now += 0.02;
            r.add(0, 2, 0, d);
            hub.rx(d, from(2), now, st, tot);
        }
        hub.tick(now);
        for (auto& d : da2)
        {
            now += 0.27;
            r.add(0, 2, 1, d);
            hub.rx(d, from(2), now, st, tot);
        }
        hub.tick(now + 0.5);
        put("hub_two_clients", r);
    }
    {
        Rec r;
        for (auto& [who, d] : hub_out)
            if (who == 1) r.add(1, 0, 0, d);
        for (auto& d : da) r.add(1, 0, 0, d);
        put("cl_from_hub", r);
    }
    {
        Rec r;
        V ro = V::Object();
        ro.set("you", V::Int(1));
        ro.set("max", V::Int(3));
        ro.set("w", V::Str("w1"));
        V ps = V::Arr();
        for (int i = 0; i < 3; ++i)
        {
            V e = V::Object();
            e.set("pid", V::Int(i));
            e.set("sid", V::Str("s" + std::to_string(i)));
            e.set("st", V::Str("on"));
            ps.a.push_back(std::move(e));
        }
        ro.set("peers", std::move(ps));
        r.add(1, 0, 0, proto::pack(proto::T_ROSTER, ro));
        V pe;
        js::parse(a_out, pe);
        if (pe.t == V::object)
        {
            pe.set("pid", V::Int(2));
            pe.set("sid", V::Str("s2"));
            r.add(1, 0, 0, proto::pack(proto::T_PEER, pe));
            pe.set("pid", V::Int(0));
            pe.set("sid", V::Str("s0"));
            r.add(1, 0, 1, proto::pack(proto::T_PEER, pe));
        }
        V fu = V::Object();
        fu.set("max", V::Int(2));
        r.add(1, 0, 3, proto::pack(proto::T_FULL, fu));
        ro.set("you", V::Int(2));
        r.add(1, 0, 3, proto::pack(proto::T_ROSTER, ro));
        put("cl_roster_peer_full", r);
    }
    {
        Rec r;
        std::string pkg(3000, '\0');
        for (size_t i = 0; i < pkg.size(); ++i) pkg[i] = (char)(i * 7 + 3);
        V in = V::Object();
        in.set("id", V::Str("123"));
        in.set("st", V::Str("ok"));
        in.set("size", V::Int((int64_t)pkg.size()));
        in.set("crc", V::Int((int64_t)crc32(pkg)));
        in.set("ch", V::Int((int64_t)proto::SX_CH));
        r.add(2, 0, 1, proto::pack(proto::T_SXI, in));
        for (uint32_t i = 0; i * proto::SX_CH < pkg.size(); ++i) r.add(2, 0, 0, sxc(123, i, std::string_view(pkg).substr(i * proto::SX_CH, proto::SX_CH)));
        put("sx_full_transfer", r);
    }
    {
        Rec r;
        V w = V::Object();
        w.set("id", V::Str("123"));
        V rr = V::Arr();
        V pr = V::Arr();
        pr.a.push_back(V::Int(0));
        pr.a.push_back(V::Int(63));
        rr.a.push_back(std::move(pr));
        w.set("r", std::move(rr));
        r.add(2, 0, 0, proto::pack(proto::T_SXW, w));
        V wt = V::Object();
        wt.set("id", V::Str("123"));
        wt.set("st", V::Str("wait"));
        r.add(2, 0, 1, proto::pack(proto::T_SXI, wt));
        put("sx_want_wait", r);
    }
    {
        Rec r;
        r.add(2, 0, 0, a_in);
        r.add(2, 1, 1, b_out);
        r.add(2, 2, 1, b_in);
        put("bridge_views", r);
    }
    {
        Rec r;
        r.add(3, 0, 0, disc::probe(77));
        disc::Info inf;
        inf.name = "host";
        inf.id = "abc";
        inf.n = 2;
        inf.max = 4;
        inf.port = 27015;
        inf.hub = true;
        r.add(3, 0, 0, disc::reply(inf, 77));
        r.add(3, 0, 0, "10.0.0.5:27015");
        V pe;
        js::parse(a_out, pe);
        if (const V* x1 = pe.get("x1"))
            if (x1->t == V::string) r.add(3, 0, 0, x1->s);
        put("disc_x1", r);
    }
    std::printf("PASS wrote %d seeds to %s (A=%zu B=%zu hub_out=%zu datagrams)\n", written, argv[2], da.size(), db.size(), hub_out.size());
    return 0;
}
