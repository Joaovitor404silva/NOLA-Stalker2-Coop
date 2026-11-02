#pragma once
#include "s2m_savefile.h"

#include <cstdint>
#include <functional>
#include <string>

namespace s2m_wsplice
{
    struct Codec
    {
        std::function<bool(const std::string& stream, uint32_t rawSize, std::string& raw, std::string& err)> dec;
        std::function<bool(const std::string& raw, std::string& stream, std::string& err)> enc;
    };

    struct Out
    {
        std::string file;
        s2m_savefile::Report rep;
        size_t worldRaw = 0, clientRaw = 0, outRaw = 0;
        size_t items = 0;
        uint32_t money = 0;
        std::string strip;
        size_t stripIn = 0, stripOut = 0;
    };

    auto decode(const std::string& file, const Codec& c, std::string& raw, std::string& err) -> bool;
    auto encode(const std::string& raw, const Codec& c, std::string& file, std::string& err) -> bool;
    auto run_raw(const std::string& clientRaw, const std::string& worldFile, const Codec& c, const s2m_savefile::Opt& o, bool fresh, Out& out, std::string& raw, std::string& err) -> bool;
    auto run(const std::string& clientFile, const std::string& worldFile, const Codec& c, const s2m_savefile::Opt& o, Out& out, std::string& err) -> bool;
}
