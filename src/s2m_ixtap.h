#pragma once
#include <string>

namespace s2m_ixtap
{
    using Probe = std::string (*)();
    using Gate = bool (*)();
    bool install(Probe p);
    void gate(Gate g);
    bool take(std::string& actor);
    std::string stat();
}
