#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace s2m_savefile
{
    constexpr uint32_t kVersion = 0xBB;
    constexpr uint32_t kVersionU21 = 0xBE;
    constexpr uint32_t kVersion6b8Arr = 0xBC;
    inline bool g_acceptU21 = true;
    inline bool known_version(uint32_t v) { return v == kVersion || (g_acceptU21 && v == kVersionU21); }
    constexpr uint32_t kNone = 0xffffffffu;
    constexpr uint32_t kItem = 0x30000000u;
    constexpr uint32_t kCont = 0x38000000u;
    constexpr uint32_t kIdx = 0x07ffffffu;

    struct Unk : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    enum class Tk : uint8_t { ref = 0, sid = 1, fname = 2 };

    struct Tape
    {
        size_t p = 0;
        Tk k = Tk::ref;
    };

    struct Names
    {
        uint32_t nto = 0;
        std::vector<std::vector<std::vector<std::string>>> groups;
        std::vector<std::vector<std::unordered_map<std::string, uint16_t>>> index;
        std::vector<std::string> added;
        auto parse(const std::string& b) -> void;
        auto blob() const -> std::string;
        auto name(int g, uint8_t t, uint16_t i) const -> const std::string&;
        auto get(int g, uint8_t t, const std::string& n) -> uint16_t;
    };

    struct Item
    {
        uint32_t idx = 0;
        size_t at = 0, end = 0;
        uint32_t owner = kNone;
        uint8_t sidT = 0;
        uint16_t sidI = 0;
        uint32_t count = 0;
        uint8_t b40 = 0, b60 = 0;
        int dtype = -1;
        std::vector<Tape> tape;
    };

    struct Cont
    {
        uint32_t idx = 0;
        size_t at = 0, end = 0, atD0 = 0, at110 = 0, endGrid = 0;
        uint32_t money = 0;
        uint8_t sidT = 0;
        uint16_t sidI = 0;
        std::vector<uint32_t> arr110;
        uint16_t gw = 0, gh = 0;
        std::vector<Tape> tape;
    };

    struct Unit
    {
        size_t at = 0, end = 0, atPos = 0, atU148 = 0;
        size_t s668 = 0, e668 = 0, s678 = 0, e678 = 0, s6a8 = 0, e6a8 = 0;
        size_t atHand = 0, atInv = 0, atSlots = 0;
        uint8_t sidT = 0;
        uint16_t sidI = 0;
        uint32_t u148[8] = {};
        uint32_t hand[4] = {};
        uint32_t inv = kNone;
        uint8_t slotK[17] = {};
        uint32_t slotV[17] = {};
        std::vector<Tape> tape;
    };

    struct Parsed
    {
        Names names;
        std::vector<Item> items;
        std::unordered_map<uint32_t, size_t> itemAt;
        uint32_t icnt = 0;
        size_t isec = 0, iend = 0;
        std::vector<Cont> conts;
        uint32_t ccnt = 0;
        size_t csec = 0, cend = 0;
        Unit unit;
        uint32_t nextUnitSidT = 0, nextUnitSidI = 0;
    };

    struct ItemView
    {
        std::string sid;
        uint32_t count = 0;
        uint16_t gx = 0, gy = 0;
        uint8_t b40 = 0, b60 = 0;
        uint32_t weight = 0;
        int dtype = -1;
        size_t bytes = 0;
        bool inArr = false;
        std::vector<std::string> attach, upgA, upgB, set48, set98;
        uint32_t u98 = 0;
        uint32_t cond = 0;
        bool hasState = false;
        uint32_t rounds = 0, mag = 0;
        std::string ammo, cart;
        auto key() const -> std::string;
    };

    struct Char
    {
        float pos[3] = {};
        uint32_t u148[8] = {};
        uint32_t inv = kNone;
        uint32_t money = 0;
        uint32_t hand = kNone;
        std::vector<std::pair<uint8_t, uint32_t>> slots;
        std::vector<std::string> slotSids;
        std::string quick[4];
        uint16_t gw = 0, gh = 0;
        size_t nArr = 0;
        std::vector<uint32_t> ids;
        std::vector<ItemView> items;
    };

    struct Opt
    {
        bool keepPos = true;
        bool vitals = true;
        bool strip = true;
    };

    struct Report
    {
        std::vector<std::pair<uint32_t, uint32_t>> idmap;
        std::vector<uint32_t> hostIds, donorIds, dropped;
        uint32_t icntOld = 0, icntNew = 0;
        std::vector<std::string> namesAdded;
        std::vector<std::string> log;
    };

    auto u32at(const std::string& b, size_t p) -> uint32_t;
    auto crc32(const char* d, size_t n, uint32_t seed = 0) -> uint32_t;
    auto find_section(const std::string& b, const std::string& name) -> size_t;
    auto parse(const std::string& raw) -> Parsed;
    auto character(const std::string& raw, const Parsed& P) -> Char;
    auto splice(const std::string& host, const std::string& donor, const Opt& o, Report& rep) -> std::string;
    auto same_char(const Char& a, const Char& b, std::string& why) -> bool;

    auto file_split(const std::string& file, uint32_t& rawSize, std::string& stream, std::string& err) -> bool;
    auto file_wrap(const std::string& stream, uint32_t rawSize) -> std::string;
}
