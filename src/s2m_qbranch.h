#pragma once
#include <cstdint>
#include <string>

namespace s2m_qbranch
{
    int force(const std::string& node, const std::string& q, const std::string& pin, bool fin, std::string& err);
    bool has(const std::string& phrase);
    bool pick(const std::string& phrase, std::string& err);
    int trig_enter(const std::string& guid, uint64_t pawn, std::string& info, int mode = 0);
}
