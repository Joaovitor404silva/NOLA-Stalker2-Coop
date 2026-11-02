#pragma once
#include "s2m_wsplice.h"

#include <string>

namespace s2m_oodle
{
    auto ready(std::string& err) -> bool;
    auto codec() -> s2m_wsplice::Codec;
}
