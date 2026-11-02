#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace s2m_strip
{
    enum Exit : int { kOk = 0, kParse = 1, kUnhandled = 2, kResidual = 3 };

    struct Report
    {
        uint32_t removed[5] = {};
        std::map<std::tuple<std::string, std::string, std::string>, uint32_t> fixes;
        uint32_t fixTotal = 0;
        size_t inSize = 0, outSize = 0;
        uint32_t ntoIn = 0, ntoOut = 0;
        std::vector<std::string> unhandled;
        std::vector<std::string> residual;
        std::string raw;
        std::vector<uint32_t> puppets;
        auto summary() const -> std::string;
    };

    auto strip(const std::string& raw, Report& rep, std::string& err) -> int;
}
