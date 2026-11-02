#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace s2x_is
{
    struct IState
    {
        std::string sid;
        bool hasDur = false;
        uint32_t dur = 0;
        std::vector<std::string> upg;
        std::vector<std::string> att;
        bool hasAmmo = false;
        int32_t rounds = 0;
        std::string ammo;
        std::vector<std::pair<char, std::string>> ext;
        auto plain() const -> bool;
    };

    struct Inst
    {
        uint32_t uid = 0;
        long long n = 0;
        IState st;
    };

    constexpr size_t kMaxText = 600;

    auto durOf(float f) -> uint32_t;
    auto durF(uint32_t bits) -> float;
    auto sidOk(const std::string& s) -> bool;
    auto textOk(const std::string& s) -> bool;
    auto enc(const IState& s) -> std::string;
    auto dec(const std::string& text, IState& out) -> bool;
    auto encSid(const IState& s) -> std::string;
    auto decSid(const std::string& text, IState& out) -> bool;
    auto encList(const std::vector<IState>& v) -> std::string;
    auto decList(const std::string& text, std::vector<IState>& out) -> bool;
    auto same(const IState& a, const IState& b) -> bool;
    auto score(const IState& want, const IState& have) -> int;
    auto pick(const IState& want, const std::vector<Inst>& cands, const std::vector<bool>& used) -> int;
    auto show(const IState& s) -> std::string;

    struct Plan
    {
        std::vector<int> keep;
        std::vector<std::pair<int, IState>> apply;
        std::vector<IState> create;
        std::vector<long long> left;
    };
    auto expand(const std::vector<Inst>& v, bool statefulOnly) -> std::vector<IState>;
    auto ofSid(const std::vector<IState>& v, const std::string& sid) -> std::vector<IState>;
    auto minus(const std::vector<IState>& a, const std::vector<IState>& b) -> std::vector<IState>;
    auto common(const std::vector<IState>& a, const std::vector<IState>& b) -> std::vector<IState>;
    auto plan(const std::vector<IState>& want, const std::vector<Inst>& have) -> Plan;
    auto pickRm(const IState& want, const std::vector<Inst>& have, std::vector<long long>& left) -> int;
    auto pickSpare(const std::string& sid, const std::vector<Inst>& have, std::vector<long long>& left) -> int;
}
