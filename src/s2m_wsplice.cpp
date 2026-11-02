#include "s2m_wsplice.h"

#include <algorithm>
#include <exception>

#include "s2m_strip.h"

namespace s2m_wsplice
{
    using namespace s2m_savefile;

    auto decode(const std::string& file, const Codec& c, std::string& raw, std::string& err) -> bool
    {
        uint32_t rs = 0;
        std::string st;
        if (!file_split(file, rs, st, err)) return false;
        if (rs < 16) { err = "raw size " + std::to_string(rs); return false; }
        if (!c.dec || !c.dec(st, rs, raw, err)) { if (err.empty()) err = "decode failed"; return false; }
        if (raw.size() != rs) { err = "decoded " + std::to_string(raw.size()) + " B, header says " + std::to_string(rs); return false; }
        if (!known_version(u32at(raw, 0))) { err = "raw does not start with the save version"; return false; }
        return true;
    }

    auto encode(const std::string& raw, const Codec& c, std::string& file, std::string& err) -> bool
    {
        std::string st;
        if (!c.enc || !c.enc(raw, st, err)) { if (err.empty()) err = "encode failed"; return false; }
        if (st.empty()) { err = "encoder returned nothing"; return false; }
        std::string back;
        if (!c.dec(st, (uint32_t)raw.size(), back, err)) { err = "read-back decode: " + err; return false; }
        if (back != raw) { err = "read-back differs from the spliced raw"; return false; }
        file = file_wrap(st, (uint32_t)raw.size());
        return true;
    }

    auto run_raw(const std::string& rc, const std::string& worldFile, const Codec& c, const Opt& o, bool fresh, Out& out, std::string& raw, std::string& err) -> bool
    {
        std::string rw, e;
        if (!decode(worldFile, c, rw, e)) { err = "host world: " + e; return false; }
        out.clientRaw = rc.size();
        out.worldRaw = rw.size();
        if (o.strip)
        {
            s2m_strip::Report sr;
            int sc = s2m_strip::strip(rw, sr, e);
            if (sc == s2m_strip::kParse && u32at(rw, 0) == kVersionU21)
                out.strip = "not stripped (save version be, " + e + ")";
            else if (sc != s2m_strip::kOk)
            {
                err = "puppet strip refused the host world (" + std::to_string(sc) + "): " + e;
                return false;
            }
            else
            {
                out.strip = sr.summary();
                out.stripIn = sr.inSize;
                out.stripOut = sr.outSize;
                rw = std::move(sr.raw);
            }
        }
        try
        {
            if (fresh)
            {
                Parsed wP = parse(rw);
                Char wc = character(rw, wP);
                out.items = wc.items.size();
                out.money = wc.money;
                raw = std::move(rw);
            }
            else
            {
                raw = splice(rw, rc, o, out.rep);
                Parsed oP = parse(raw), dP = parse(rc);
                Char oc = character(raw, oP), dc = character(rc, dP);
                if (!o.vitals) std::copy(std::begin(dc.u148), std::end(dc.u148), oc.u148);
                std::string why;
                if (!same_char(oc, dc, why)) { err = "spliced character differs from the client's: " + why; return false; }
                out.items = oc.items.size();
                out.money = oc.money;
            }
        }
        catch (const std::exception& x)
        {
            err = std::string("splice: ") + x.what();
            return false;
        }
        out.outRaw = raw.size();
        return true;
    }

    auto run(const std::string& clientFile, const std::string& worldFile, const Codec& c, const Opt& o, Out& out, std::string& err) -> bool
    {
        std::string rc, raw, e;
        if (!decode(clientFile, c, rc, e)) { err = "client save: " + e; return false; }
        if (!run_raw(rc, worldFile, c, o, false, out, raw, err)) return false;
        if (!encode(raw, c, out.file, e)) { err = "encode: " + e; return false; }
        return true;
    }
}
