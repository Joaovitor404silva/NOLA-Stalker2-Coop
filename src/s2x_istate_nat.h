#pragma once
#include "s2x_istate.h"

#include <cstdint>
#include <string>
#include <vector>

namespace s2x_isn
{
    auto ready() -> bool;
    auto groundCtr(uintptr_t actor) -> uintptr_t;
    auto list(uintptr_t ctr, bool takeOnly, bool state, std::vector<s2x_is::Inst>& out) -> bool;
    auto capture(uint32_t uid, s2x_is::IState& out) -> bool;
    auto create(uintptr_t ctr, const s2x_is::IState& st, long long n, uint32_t& uid) -> bool;
    auto remove(uintptr_t ctr, uint32_t uid, long long n, long long& done) -> bool;
    auto apply(uint32_t uid, const s2x_is::IState& st, std::string& how) -> bool;
    auto spawn(const s2x_is::IState& st, double x, double y, double z, uint32_t& uid) -> bool;
}
