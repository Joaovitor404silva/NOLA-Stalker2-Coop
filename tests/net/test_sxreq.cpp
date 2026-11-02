#include "s2r_net.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace s2r;

static int fails = 0;
static void check(bool ok, const char* what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++fails;
}
static void put(const fs::path& p, const std::string& d)
{
    std::ofstream f(p, std::ios::binary);
    f << d;
}
static std::string get(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

int main()
{
    fs::path d = fs::temp_directory_path() / "s2r_sxreq_test";
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    fs::path req = d / "sx_req.txt";
    put(req, "91760000000001\n");
    std::vector<std::string> said;
    std::vector<std::string> sent;
    proto::SaveXfer::Send send = [&](const std::string& b) { sent.push_back(b); };
    {
        proto::SaveXfer sx(d.wstring() + L"\\", [&](const std::string& s) { said.push_back(s); });
        check(!fs::exists(req), "relay start removes a request left by an earlier run");
        check(!said.empty() && said.back().find("91760000000001") != std::string::npos, "relay start says which stale request it removed");

        sx.tick(100.0, send);
        js::V o = js::V::Object();
        o.set("id", js::V::Str("91792417733373"));
        js::V r = js::V::Arr();
        js::V pr = js::V::Arr();
        pr.a.push_back(js::V::Int(0));
        pr.a.push_back(js::V::Int(63));
        r.a.push_back(pr);
        o.set("r", r);
        std::string w = proto::pack(proto::T_SXW, o);
        sx.feed(w, send);
        check(get(req).rfind("91792417733373", 0) == 0, "a client want writes sx_req.txt");
        sx.tick(105.0, send);
        sx.feed(w, send);
        sx.tick(109.0, send);
        check(fs::exists(req), "client still asking (want 4 s ago): request kept");
        sx.tick(116.0, send);
        check(!fs::exists(req), "no want for 11 s: request dropped from sx_req.txt");
        check(said.back().find("dropped") != std::string::npos, "drop is logged");

        sx.feed(w, send);
        put(req, "91799999999999\n");
        sx.tick(130.0, send);
        check(get(req) == "91799999999999\n", "a newer request written by someone else is not removed");
    }
    {
        fs::remove(req, ec);
        size_t n = said.size();
        proto::SaveXfer sx(d.wstring() + L"\\", [&](const std::string& s) { said.push_back(s); });
        check(said.size() == n, "relay start with no request file: nothing said");
    }
    fs::remove_all(d, ec);
    std::printf("%s (%d failed)\n", fails ? "FAIL" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
