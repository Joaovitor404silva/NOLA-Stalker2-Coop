#include "../../src/s2r_net.cpp"
#include "s2x_int.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fz
{
    using namespace s2r;

    struct Env
    {
        Config cfg;
        std::wstring dir;
        Env()
        {
            wchar_t tmp[MAX_PATH];
            GetTempPathW(MAX_PATH, tmp);
            dir = std::wstring(tmp) + L"nola_fuzz_" + std::to_wstring(GetCurrentProcessId());
            cfg.role = "host";
            cfg.maxclients = proto::MAXV;
            cfg.dir = dir + L"\\hub";
            cfg.slot_silent = 2;
            cfg.slot_left = 6;
            cfg.peer_gap = 0;
            cfg.relay_tags = {"@W", "H", "D", "L", "K", "J", "RS", "AS", "AK"};
            x0::in::load_default_tables(x0::in::flags(), nullptr);
            x0::in::flags().set("netrel", x0::Val::I(1), nullptr, "fuzz");
            base = x0::in::flags();
        }
        x0::in::Flags base;
        void reset()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
            std::filesystem::create_directories(dir + L"\\hub", ec);
            std::filesystem::create_directories(dir + L"\\cl", ec);
            std::FILE* f = nullptr;
            _wfopen_s(&f, (dir + L"\\cl\\sx_want.txt").c_str(), L"wb");
            if (f)
            {
                std::fputs("123\n", f);
                std::fclose(f);
            }
            x0::in::flags() = base;
        }
    };

    static Env& env()
    {
        static Env e;
        return e;
    }

    static sockaddr_in addr_of(int i)
    {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons((u_short)(40000 + i));
        a.sin_addr.s_addr = htonl(0x7F000001u + (uint32_t)i);
        return a;
    }

    static void bridge_in(x0::in::Bridge& b, int v, const std::string& text, double now)
    {
        bool ch = false;
        b.take_in_view(v, text, now, ch);
        if (b.logs.size() > 64) b.logs.clear();
    }

    static void run(const uint8_t* data, size_t size)
    {
        Env& e = env();
        e.reset();
        Hub hub(e.cfg);
        uint64_t sent = 0;
        hub.send = [&](const std::string& dg, const sockaddr_in&) { sent += dg.size(); return true; };
        hub.say = [](const std::string&) {};
        hub.warn = [](const char*, const std::string&) {};
        hub.view_bye = [](int, const std::string&) {};
        hub.publish = [](const Roster&) {};
        Roster ros;
        CRx crx;
        crx.say = [](const std::string&) {};
        crx.warn = [](const char*, const std::string&) {};
        crx.view_bye = [](int, const std::string&) {};
        crx.publish = [&](const Roster& r) { ros = r; };
        crx.roster = [&] { return ros; };
        x0::in::Bridge br;
        crx.put_view = [&](int v, std::string t, double now) { bridge_in(br, v, t, now); };
        proto::Assembler asmb;
        proto::SaveXfer sx(e.dir + L"\\cl", [](const std::string&) {});
        auto sxs = [&](const std::string& dg) { sent += dg.size(); };
        Stats st;
        uint64_t tot = 0;
        double now = 100.0;
        std::set<std::string> need = {"a1", "b2"};
        try
        {
            sx.tick(now, sxs);
        }
        catch (const std::exception&)
        {
        }
        size_t p = 0;
        int n = 0;
        while (p < size && n < 256)
        {
            uint8_t ctl = data[p++];
            size_t len = 0;
            if (p + 2 <= size)
            {
                len = (size_t)data[p] | ((size_t)data[p + 1] << 8);
                p += 2;
            }
            else p = size;
            if (len > size - p) len = size - p;
            std::string_view dg(reinterpret_cast<const char*>(data + p), len);
            p += len;
            ++n;
            now += 0.02 + 0.25 * (ctl >> 4);
            int route = ctl & 3, who = (ctl >> 2) & 3;
            if (route == 0)
            {
                hub.rx(dg, addr_of(who), now, st, tot);
                hub.tick(now);
                hub.render(need, now, 0, [&](int v, std::string t) { bridge_in(br, v, t, now); });
            }
            else if (route == 1)
            {
                bool s2 = dg.size() >= 4 && dg[0] == 'S' && dg[1] == '2' && (uint8_t)dg[2] == proto::PROTO;
                uint8_t typ = s2 ? (uint8_t)dg[3] : 0;
                if (s2 && (typ == proto::T_PEER || typ == proto::T_ROSTER || typ == proto::T_FULL)) crx.rx(dg, typ, now, st);
                else
                {
                    std::string msg;
                    asmb.feed(dg, now, msg);
                    std::string out;
                    if (asmb.render(need, now, (ctl & 0x40) ? 1.5 : 0, out, -1)) bridge_in(br, 0, out, now);
                }
            }
            else if (route == 2)
            {
                bool s2 = dg.size() >= 4 && dg[0] == 'S' && dg[1] == '2' && (uint8_t)dg[2] == proto::PROTO;
                if (s2 && ((uint8_t)dg[3] == proto::T_SXW || (uint8_t)dg[3] == proto::T_SXI || (uint8_t)dg[3] == proto::T_SXC))
                {
                    try
                    {
                        sx.feed(dg, sxs);
                        sx.tick(now, sxs);
                    }
                    catch (const std::exception&)
                    {
                    }
                }
                else bridge_in(br, who, std::string(dg), now);
            }
            else
            {
                int64_t nonce = 0;
                disc::is_probe(dg, nonce);
                disc::Host h;
                if (disc::parse_reply(dg, h, nonce))
                {
                    std::vector<disc::Host> v;
                    disc::add_host(v, h);
                    disc::pick(v, {h.id});
                }
                std::string ip;
                int port = 0;
                disc::parse_addr(dg, ip, port);
                x1_relay(dg, e.cfg.relay_tags);
            }
        }
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    static const char* cur = std::getenv("NOLA_FUZZ_CUR");
    if (cur)
    {
        std::string p = std::string(cur) + "\\cur_" + std::to_string(GetCurrentProcessId()) + ".bin";
        std::FILE* f = nullptr;
        fopen_s(&f, p.c_str(), "wb");
        if (f)
        {
            std::fwrite(data, 1, size, f);
            std::fclose(f);
        }
    }
    fz::run(data, size);
    return 0;
}

#ifdef FUZZ_REPLAY
static bool replay_file(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::string b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    fz::run(reinterpret_cast<const uint8_t*>(b.data()), b.size());
    return true;
}

static int selftest()
{
    using namespace s2r;
    using js::V;
    struct Case
    {
        int64_t size, ch;
        bool ok;
    };
    const Case cases[] = {{3000, 1100, true}, {1ll << 40, 1100, false}, {1000, 1 << 20, false}, {proto::SX_MAX_SIZE, 1, false}, {64ll << 20, 1100, true}};
    int bad = 0;
    for (auto& c : cases)
    {
        fz::Env& e = fz::env();
        e.reset();
        std::wstring part = e.dir + L"\\cl\\sx_in.part";
        bool threw = false;
        {
            proto::SaveXfer sx(e.dir + L"\\cl", [](const std::string&) {});
            auto snd = [](const std::string&) {};
            sx.tick(100.0, snd);
            V in = V::Object();
            in.set("id", V::Str("123"));
            in.set("st", V::Str("ok"));
            in.set("size", V::Int(c.size));
            in.set("crc", V::Int(0));
            in.set("ch", V::Int(c.ch));
            try
            {
                sx.feed(proto::pack(proto::T_SXI, in), snd);
            }
            catch (const std::exception&)
            {
                threw = true;
            }
        }
        std::error_code ec;
        bool has_part = std::filesystem::exists(part, ec);
        uint64_t psz = has_part ? (uint64_t)std::filesystem::file_size(part, ec) : 0;
        bool pass = c.ok ? (!threw && has_part && psz == (uint64_t)c.size) : (threw && !has_part);
        std::printf("%s sx info size=%lld ch=%lld threw=%d part=%d size_on_disk=%llu\n", pass ? "PASS" : "FAIL", (long long)c.size, (long long)c.ch, threw ? 1 : 0, has_part ? 1 : 0,
                    (unsigned long long)psz);
        if (!pass) ++bad;
    }
    fz::env().reset();
    return bad ? 1 : 0;
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--selftest") return selftest();
    int n = 0;
    for (int i = 1; i < argc; ++i)
    {
        std::filesystem::path a(argv[i]);
        std::error_code ec;
        if (std::filesystem::is_directory(a, ec))
        {
            for (auto& e : std::filesystem::directory_iterator(a))
                if (e.is_regular_file())
                {
                    if (!replay_file(e.path()))
                    {
                        std::printf("FAIL cannot read %s\n", e.path().string().c_str());
                        return 1;
                    }
                    ++n;
                }
        }
        else if (!replay_file(a))
        {
            std::printf("FAIL cannot read %s\n", argv[i]);
            return 1;
        }
        else ++n;
    }
    if (n == 0)
    {
        std::printf("FAIL no inputs\n");
        return 1;
    }
    std::printf("PASS replayed %d inputs without a crash\n", n);
    return 0;
}
#endif
