#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace s2m_cgc
{
    void feed(int mode, const std::vector<uint64_t>& keep);
    std::string stat();
}
