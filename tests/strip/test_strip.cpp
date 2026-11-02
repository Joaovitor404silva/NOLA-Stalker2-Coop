#include "s2m_strip.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    auto slurp(const std::string& p, std::string& out) -> bool
    {
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        std::ostringstream o;
        o << f.rdbuf();
        out = o.str();
        return true;
    }

    struct Oracle
    {
        std::string file;
        int code = -1;
        size_t sizeIn = 0, sizeOut = 0;
        uint32_t removed[5] = {};
        std::map<std::tuple<std::string, std::string, std::string>, uint32_t> fixes;
    };

    auto read_log(const std::string& p, Oracle& o) -> bool
    {
        std::ifstream f(p);
        if (!f) return false;
        std::string line;
        static const char* names[5] = {"Unit", "Container", "Item", "Marker", "Squad"};
        while (std::getline(f, line))
        {
            std::istringstream s(line);
            std::string k;
            s >> k;
            if (k == "file") o.file = line.substr(5);
            else if (k == "exit") s >> o.code;
            else if (k == "size") s >> o.sizeIn >> o.sizeOut;
            else if (k == "removed")
            {
                std::string t;
                uint32_t n;
                s >> t >> n;
                for (int i = 0; i < 5; ++i)
                    if (t == names[i]) o.removed[i] = n;
            }
            else if (k == "fix")
            {
                std::string a, sec, kind;
                uint32_t n;
                s >> a >> sec >> kind >> n;
                o.fixes[{a, sec, kind}] = n;
            }
        }
        return o.code >= 0;
    }

    auto plant_refusal(const std::string& raw, const s2m_strip::Report& rep, std::string& out) -> bool
    {
        if (rep.puppets.empty()) return false;
        uint32_t nto;
        std::memcpy(&nto, raw.data() + 5, 4);
        const char hdr[] = "struct FUnitModel";
        size_t at = raw.find(hdr);
        if (at == std::string::npos) return false;
        size_t p = at + sizeof(hdr) - 1 + 4;
        uint16_t n;
        std::memcpy(&n, raw.data() + p, 2);
        p += 2 + (size_t)n * 8;
        while (p < nto && raw[p] == 0) ++p;
        if (p >= nto || raw[p] != 1) return false;
        out = raw;
        uint32_t id = rep.puppets.front();
        std::memcpy(&out[p + 1], &id, 4);
        return true;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::puts("usage: test_strip <oracle dir> [max files]");
        return 2;
    }
    std::string dir = argv[1];
    size_t maxFiles = argc > 2 ? (size_t)std::atoi(argv[2]) : 160;
    std::vector<std::string> files;
    {
        std::ifstream f(dir + "/files.txt");
        std::string l;
        while (std::getline(f, l))
            if (!l.empty()) files.push_back(l);
    }
    int total = 0, okBytes = 0, okCounts = 0, okIdem = 0, idemTried = 0, refuseOk = 0, refuseTried = 0;
    double maxMs = 0, sumMs = 0;
    size_t maxMsIdx = 0;
    for (size_t i = 0; i < files.size() && (size_t)total < maxFiles; ++i)
    {
        char base[16];
        std::snprintf(base, sizeof base, "%03zu", i);
        Oracle o;
        if (!read_log(dir + "/" + base + ".log", o)) continue;
        std::string raw;
        if (!slurp(files[i], raw))
        {
            std::printf("%s FAIL cannot read %s\n", base, files[i].c_str());
            ++total;
            continue;
        }
        ++total;
        s2m_strip::Report rep;
        std::string err;
        auto t0 = std::chrono::steady_clock::now();
        int rc = s2m_strip::strip(raw, rep, err);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        sumMs += ms;
        if (ms > maxMs)
        {
            maxMs = ms;
            maxMsIdx = i;
        }
        bool counts = rc == o.code;
        if (rc == 0)
        {
            for (int k = 0; k < 5; ++k)
                if (rep.removed[k] != o.removed[k]) counts = false;
            if (rep.fixes != o.fixes) counts = false;
        }
        bool bytes = false;
        if (rc == 0 && o.code == 0)
        {
            std::string exp;
            bytes = slurp(dir + "/" + base + ".bin", exp) && exp == rep.raw && rep.outSize == o.sizeOut;
        }
        else bytes = rc == o.code && rep.raw.empty();
        okBytes += bytes;
        okCounts += counts;
        bool pass = bytes && counts;
        if (rc == 0)
        {
            s2m_strip::Report r2;
            std::string e2;
            int rc2 = s2m_strip::strip(rep.raw, r2, e2);
            ++idemTried;
            bool idem = rc2 == 0 && r2.raw == rep.raw && r2.fixTotal == 0;
            for (int k = 0; k < 5; ++k)
                if (r2.removed[k]) idem = false;
            okIdem += idem;
            if (!idem)
            {
                pass = false;
                std::printf("%s idempotence FAIL rc=%d %s\n", base, rc2, e2.c_str());
            }
            if (refuseTried < 3)
            {
                std::string planted;
                if (plant_refusal(raw, rep, planted))
                {
                    s2m_strip::Report r3;
                    std::string e3;
                    int rc3 = s2m_strip::strip(planted, r3, e3);
                    ++refuseTried;
                    bool ok = rc3 == 2 && r3.raw.empty() && r3.outSize == 0;
                    refuseOk += ok;
                    std::printf("%s refusal rc=%d raw=%zu %s %s\n", base, rc3, r3.raw.size(), ok ? "ok" : "FAIL", e3.c_str());
                }
            }
        }
        if (!pass) std::printf("%s FAIL rc=%d oracle=%d bytes=%d counts=%d err=%s\n", base, rc, o.code, (int)bytes, (int)counts, err.c_str());
        else std::printf("%s ok %s %.0f ms\n", base, rep.summary().c_str(), ms);
        std::fflush(stdout);
    }
    int pass = 0;
    pass = okBytes < okCounts ? okBytes : okCounts;
    std::printf("bytes %d/%d counts %d/%d idempotent %d/%d refusal %d/%d\n", okBytes, total, okCounts, total, okIdem, idemTried, refuseOk, refuseTried);
    std::printf("time: max %.0f ms (file %zu), mean %.0f ms\n", maxMs, maxMsIdx, total ? sumMs / total : 0.0);
    bool all = total > 0 && okBytes == total && okCounts == total && okIdem == idemTried && refuseOk == refuseTried && refuseTried > 0;
    if (all) std::printf("PASS %d/%d\n", pass, total);
    else std::printf("FAIL %d/%d\n", pass, total);
    return all ? 0 : 1;
}
