#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "s2x_fr_core.h"

static int g_fail = 0;
static int g_pass = 0;

static void check(bool ok, const char* name, const std::string& detail = {})
{
    if (ok) { ++g_pass; std::printf("PASS %s\n", name); }
    else { ++g_fail; std::printf("FAIL %s %s\n", name, detail.c_str()); }
}

static bool write_file(const char* path, const std::vector<uint8_t>& b)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) return false;
    bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
    std::fclose(f);
    return ok;
}

int main(int argc, char** argv)
{
    const char* wrap = argc > 1 ? argv[1] : "out\\wrap.frd";
    using namespace frc;
    {
        const uint32_t cap = 1024;
        std::vector<uint8_t> mem(bytes_for(cap));
        Ring r;
        check(r.attach(mem.data(), mem.size(), cap, true, 10000000, 5000, 1700000000000000LL, 4242), "attach ring");
        for (uint32_t i = 0; i < 2600; ++i) r.put(5000 + i * 100, 1700000000000000LL + i * 10, k_log, (uint8_t)(i % f_count), -1, "w", i, i * 7, i, 0, "rec " + std::to_string(i));
        check(r.count() == 2600, "count after wrap", std::to_string(r.count()));
        const Rec* recs = reinterpret_cast<const Rec*>(mem.data() + kHdrSize);
        bool slots = true;
        uint64_t lo = 2600 - cap, minn = ~0ULL, maxn = 0;
        for (uint32_t s = 0; s < cap; ++s)
        {
            const Rec& x = recs[s];
            if (x.n == 0 || (x.n - 1) % cap != s || x.n - 1 < lo) slots = false;
            if (x.seq != (uint32_t)(x.n - 1)) slots = false;
            minn = x.n < minn ? x.n : minn;
            maxn = x.n > maxn ? x.n : maxn;
        }
        check(slots, "every slot holds a record of the last cap writes");
        check(minn == lo + 1 && maxn == 2600, "oldest and newest record after wrap", std::to_string(minn) + ".." + std::to_string(maxn));
        const Rec& last = recs[(2600 - 1) % cap];
        check(std::string(last.text) == "rec 2599" && std::string(last.tag) == "w", "newest record text and tag");
        std::vector<uint8_t> copy(r.bytes());
        check(r.copy_to(copy.data(), copy.size()), "snapshot copy");
        Hdr* h = reinterpret_cast<Hdr*>(copy.data());
        put_str(h->role, sizeof h->role, "host");
        put_str(h->sid, sizeof h->sid, "1729500000-77");
        put_str(h->host, sizeof h->host, "TESTPC");
        put_str(h->reason, sizeof h->reason, "wrap_test");
        put_str(h->tracked, sizeof h->tracked, "QB");
        check(write_file(wrap, copy), "write the wrap.frd fixture", wrap);
    }
    {
        std::string longt(200, 'x');
        char buf[16];
        put_str(buf, sizeof buf, std::string("ab\x01\x7f" "cd"));
        check(std::string(buf) == "ab..cd", "non-printable bytes become dots", buf);
        put_str(buf, sizeof buf, longt);
        check(std::string(buf).size() == 15, "text is cut to the field");
    }
    {
        int n = 0;
        std::string tags;
        each_tag("lead#QB:1;a|b;c#HC:x#bad#Q9:z", [&](std::string_view t, std::string_view p) { ++n; tags += std::string(t) + "=" + std::string(p) + " "; });
        check(n == 3 && tags == "QB=1;a|b;c HC=x Q9=z ", "x1 tag split matches the bridge rules", tags);
    }
    {
        const uint32_t cap = 256;
        std::vector<uint8_t> mem(bytes_for(cap));
        Ring r;
        r.attach(mem.data(), mem.size(), cap, true, 1000, 0, 0, 1);
        TagTab tab;
        Clock c{100, 1, 10.0};
        int a = tag_diff(r, tab, c, k_tx_tag, 1, 8, "#QB:1;x;y#H:1", 0.25);
        c.t = 10.05;
        int b = tag_diff(r, tab, c, k_tx_tag, 1, 9, "#QB:1;x;y#H:1", 0.25);
        c.t = 10.1;
        int d = tag_diff(r, tab, c, k_tx_tag, 1, 10, "#QB:2;x;y#H:1", 0.25);
        c.t = 10.2;
        int e = tag_diff(r, tab, c, k_tx_tag, 1, 11, "#QB:3;x;y#H:1", 0.25);
        c.t = 10.6;
        int f = tag_diff(r, tab, c, k_tx_tag, 1, 12, "#QB:3;x;y#H:1", 0.25);
        check(a == 2 && b == 0, "unchanged tags are not recorded again", std::to_string(a) + "," + std::to_string(b));
        check(d == 0 && e == 0, "changes inside 0.25 s are coalesced", std::to_string(d) + "," + std::to_string(e));
        const Rec* recs = reinterpret_cast<const Rec*>(mem.data() + kHdrSize);
        bool last_ok = f == 1 && r.count() == 3 && std::string(recs[2].tag) == "QB" && recs[2].aux == 2 && recs[2].hash == fnv("3;x;y") && recs[2].seq == 12;
        check(last_ok, "the settled value is recorded later with the coalesced count", std::to_string(f) + " aux=" + std::to_string(recs[2].aux));
    }
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
