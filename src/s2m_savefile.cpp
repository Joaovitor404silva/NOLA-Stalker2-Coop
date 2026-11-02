#include "s2m_savefile.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <set>
#include <unordered_set>

namespace s2m_savefile
{
    namespace
    {
        auto hx(uint32_t v) -> std::string
        {
            char b[16];
            std::snprintf(b, sizeof b, "%08x", v);
            return b;
        }
        auto put32(std::string& b, size_t p, uint32_t v) -> void
        {
            std::memcpy(&b[p], &v, 4);
        }
        auto put16(std::string& b, size_t p, uint16_t v) -> void
        {
            std::memcpy(&b[p], &v, 2);
        }
        auto app32(std::string& b, uint32_t v) -> void
        {
            b.append(reinterpret_cast<const char*>(&v), 4);
        }
        auto app16(std::string& b, uint16_t v) -> void
        {
            b.append(reinterpret_cast<const char*>(&v), 2);
        }

        struct Rd
        {
            const std::string& b;
            const Names& nm;
            size_t p = 0;
            std::vector<Tape>* tape = nullptr;

            Rd(const std::string& buf, const Names& n, size_t pos = 0) : b(buf), nm(n), p(pos) {}

            auto need(size_t n) -> void
            {
                if (p + n > b.size()) throw Unk("read past end at " + hx((uint32_t)p));
            }
            auto u8() -> uint8_t
            {
                need(1);
                return (uint8_t)b[p++];
            }
            auto u16() -> uint16_t
            {
                need(2);
                uint16_t v;
                std::memcpy(&v, &b[p], 2);
                p += 2;
                return v;
            }
            auto u32() -> uint32_t
            {
                need(4);
                uint32_t v;
                std::memcpy(&v, &b[p], 4);
                p += 4;
                return v;
            }
            auto u64() -> uint64_t
            {
                need(8);
                uint64_t v;
                std::memcpy(&v, &b[p], 8);
                p += 8;
                return v;
            }
            auto skip(size_t n) -> void
            {
                need(n);
                p += n;
            }
            auto mark(Tk k) -> void
            {
                if (tape) tape->push_back({p, k});
            }
            auto nref(int g, const char* what) -> std::pair<uint8_t, uint16_t>
            {
                size_t at = p;
                uint8_t t = u8();
                uint16_t i = u16();
                if (t >= nm.groups[g].size() || i >= nm.groups[g][t].size())
                    throw Unk(std::string("bad ") + what + " ref t=" + std::to_string(t) + " i=" + std::to_string(i) + " at " + hx((uint32_t)at));
                return {t, i};
            }
            auto sid() -> std::pair<uint8_t, uint16_t>
            {
                mark(Tk::sid);
                return nref(1, "sid");
            }
            auto sidName() -> std::string
            {
                auto [t, i] = sid();
                return nm.groups[1][t][i];
            }
            auto fname() -> void
            {
                mark(Tk::fname);
                nref(0, "fname");
            }
            auto ref() -> uint32_t
            {
                mark(Tk::ref);
                return u32();
            }
            auto opt32() -> void
            {
                if (u8()) u32();
            }
            template <class F> auto arr(F f) -> uint16_t
            {
                uint16_t n = u16();
                for (uint16_t k = 0; k < n; ++k) f();
                return n;
            }
        };

        auto e80(Rd& r) -> void
        {
            r.sid();
            r.ref();
            r.u8();
            r.opt32();
            r.u32();
            r.u8();
            for (int k = 0; k < 5; ++k) r.u32();
            r.opt32();
            r.u8();
        }
        auto e38(Rd& r) -> void
        {
            r.sid();
            r.ref();
            r.u8();
            r.opt32();
        }
        auto effects(Rd& r) -> void
        {
            r.arr([&] { e80(r); });
            r.arr([&] { r.u32(); r.u32(); });
            r.arr([&] { e80(r); });
            r.arr([&] { e38(r); });
            r.arr([&] { r.u32(); r.arr([&] { r.u8(); r.u64(); }); });
            r.arr([&] { e38(r); });
            r.arr([&] { e38(r); });
        }
        auto stats26(Rd& r, int extra) -> void
        {
            for (int k = 0; k < 19; ++k) r.u32();
            r.u8();
            r.u8();
            r.u32();
            for (int k = 0; k < extra; ++k) r.u32();
        }
        auto weapon_state(Rd& r, ItemView* v) -> void
        {
            r.arr([&] { r.u8(); });
            r.arr([&] { r.u8(); r.sid(); });
            r.u8();
            r.u8();
            r.u8();
            r.u32();
            std::string cart = r.sidName();
            uint32_t rounds = r.u32();
            r.u32();
            for (int k = 0; k < 7; ++k) r.u32();
            r.arr([&] { r.u8(); r.u32(); });
            r.u32();
            stats26(r, 2);
            stats26(r, 5);
            r.u32();
            r.u8();
            r.u32();
            r.u32();
            r.u32();
            r.u8();
            std::string ammo = r.sidName();
            r.u32();
            r.u32();
            r.u32();
            r.u8();
            r.u32();
            if (r.u8()) throw Unk("weapon state strings opt at " + hx((uint32_t)r.p));
            r.u8();
            r.arr([&] { r.u32(); });
            uint32_t mag = r.u32();
            r.u32();
            r.u32();
            for (int k = 0; k < 5; ++k) r.u8();
            r.fname();
            r.u8();
            if (v)
            {
                v->hasState = true;
                v->cart = cart;
                v->rounds = rounds;
                v->ammo = ammo;
                v->mag = mag;
            }
        }
        auto sids(Rd& r, std::vector<std::string>* out) -> void
        {
            r.arr([&] {
                std::string n = r.sidName();
                if (out) out->push_back(n);
            });
        }
        auto data_weapon(Rd& r, ItemView* v) -> void
        {
            r.ref();
            if (r.u8())
            {
                r.u8();
                weapon_state(r, v);
            }
            sids(r, v ? &v->attach : nullptr);
            uint32_t u98 = r.u32();
            if (v) v->u98 = u98;
            sids(r, v ? &v->upgA : nullptr);
            sids(r, v ? &v->upgB : nullptr);
            r.sid();
            r.sid();
            r.u8();
            r.u32();
            r.u8();
            r.u8();
            r.arr([&] { r.sid(); r.sid(); });
            r.u8();
        }
        auto data_type1(Rd& r, ItemView* v) -> void
        {
            r.ref();
            uint32_t c = r.u32();
            if (v) v->cond = c;
            sids(r, v ? &v->set48 : nullptr);
            sids(r, v ? &v->set98 : nullptr);
            r.sid();
        }
        auto poly(Rd& r, uint32_t pt) -> void
        {
            r.ref();
            switch (pt)
            {
            case 0: case 2: case 5: case 6: case 7: case 9:
                return;
            case 1:
                r.u32();
                r.u32();
                return;
            case 3:
                r.u32();
                return;
            case 4:
                for (int k = 0; k < 3; ++k) r.u32();
                r.skip(24);
                r.u32();
                r.u32();
                r.u8();
                r.u8();
                return;
            case 8:
                r.skip(24);
                r.skip(64);
                r.u8();
                r.u32();
                r.u32();
                return;
            default:
                throw Unk("poly type " + std::to_string(pt) + " at " + hx((uint32_t)r.p));
            }
        }
        auto data_type2(Rd& r) -> void
        {
            r.ref();
            r.arr([&] { r.u32(); });
            r.ref();
            r.ref();
            for (int k = 0; k < 4; ++k) r.u8();
            r.u8();
            r.u32();
            if (r.u8())
            {
                uint32_t pt = r.u32();
                poly(r, pt);
            }
            if (r.u8()) r.skip(32);
            r.u8();
        }
        auto item_data(Rd& r, uint32_t t, ItemView* v) -> void
        {
            switch (t)
            {
            case 0:
                data_weapon(r, v);
                return;
            case 1:
                data_type1(r, v);
                return;
            case 2:
                data_type2(r);
                return;
            case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 11: case 12:
                r.ref();
                return;
            case 10:
                r.ref();
                r.u8();
                return;
            default:
                throw Unk("item data type " + std::to_string(t) + " at " + hx((uint32_t)r.p));
            }
        }
        auto item(Rd& r, Item& d, ItemView* v) -> void
        {
            d.at = r.p;
            if (r.u8())
            {
                effects(r);
                r.ref();
                r.u32();
            }
            auto s = r.sid();
            d.sidT = s.first;
            d.sidI = s.second;
            uint16_t gx = r.u16(), gy = r.u16();
            d.owner = r.ref();
            d.count = r.u32();
            d.b60 = r.u8();
            uint32_t w = r.u32();
            r.u8();
            d.b40 = r.u8();
            if (v)
            {
                v->sid = r.nm.groups[1][s.first][s.second];
                v->count = d.count;
                v->gx = gx;
                v->gy = gy;
                v->b40 = d.b40;
                v->b60 = d.b60;
                v->weight = w;
            }
            if (r.u8())
            {
                uint32_t t = r.u32();
                d.dtype = (int)t;
                if (v) v->dtype = (int)t;
                item_data(r, t, v);
            }
            r.u32();
            r.u32();
            d.end = r.p;
            if (v) v->bytes = d.end - d.at;
        }
        auto container(Rd& r, Cont& d) -> void
        {
            d.at = r.p;
            r.u8();
            r.skip(12);
            r.u32();
            auto s = r.sid();
            d.sidT = s.first;
            d.sidI = s.second;
            r.arr([&] { r.sid(); });
            r.sid();
            d.atD0 = r.p;
            d.money = r.u32();
            r.u32();
            d.at110 = r.p;
            r.arr([&] { d.arr110.push_back(r.ref()); });
            r.arr([&] { r.ref(); r.u16(); r.u16(); });
            d.gw = r.u16();
            d.gh = r.u16();
            d.endGrid = r.p;
            r.u8();
            r.u32();
            r.arr([&] { r.sid(); });
            r.skip(12);
            r.u32();
            r.u8();
            if (r.u8())
            {
                r.arr([&] { r.u32(); });
                r.u8();
                r.u32();
                r.u64();
                r.sid();
            }
            r.ref();
            r.u32();
            r.skip(16);
            r.u32();
            r.u8();
            r.u8();
            d.end = r.p;
        }

        auto stats_2c8(Rd& r) -> void
        {
            for (int k = 0; k < 23; ++k) r.u32();
            for (int k = 0; k < 3; ++k) r.opt32();
        }
        auto m8341a6(Rd& r) -> void
        {
            r.u32();
            r.u32();
            r.u8();
        }
        auto unit_head(Rd& r, Unit* u) -> std::pair<uint8_t, uint16_t>
        {
            if (u) u->at = r.p;
            r.u32();
            r.u8();
            r.u8();
            if (u) u->atPos = r.p;
            r.skip(12);
            r.u32();
            r.u32();
            r.u64();
            r.u32();
            r.skip(16);
            r.u32();
            r.u32();
            r.u64();
            r.u64();
            auto s = r.sid();
            r.u64();
            if (u) u->atU148 = r.p;
            for (int k = 0; k < 8; ++k)
            {
                uint32_t v = r.u32();
                if (u) u->u148[k] = v;
            }
            r.arr([&] { r.u32(); });
            r.u32();
            for (int k = 0; k < 14; ++k) r.u32();
            r.u8();
            r.u8();
            r.u8();
            r.u32();
            r.skip(12);
            r.u32();
            r.u8();
            r.u64();
            r.u32();
            r.u32();
            r.arr([&] { r.skip(16); });
            for (int k = 0; k < 6; ++k) r.u32();
            stats_2c8(r);
            if (r.u8())
            {
                m8341a6(r);
                uint32_t n = r.u32();
                for (uint32_t k = 0; k < n; ++k)
                {
                    r.u32();
                    m8341a6(r);
                }
                r.u32();
                r.u32();
            }
            r.arr([&] {
                r.sid();
                r.u32();
                r.skip(24);
                r.fname();
                r.skip(12);
            });
            r.sid();
            r.arr([&] { r.skip(0x60); });
            r.arr([&] { r.fname(); });
            r.skip(0x60);
            r.u8();
            r.u32();
            r.u32();
            r.sid();
            if (u)
            {
                u->sidT = s.first;
                u->sidI = s.second;
            }
            return s;
        }
        auto comp668(Rd& r) -> void
        {
            effects(r);
            r.u32();
            r.u8();
            for (int k = 0; k < 20; ++k) r.u32();
            r.arr([&] { r.u32(); r.u32(); });
            r.u32();
            r.u32();
            r.u8();
            r.u64();
            r.u64();
            r.u8();
            r.u32();
            r.u8();
            r.u32();
            r.arr([&] {
                r.sid();
                r.arr([&] { r.sid(); r.u32(); r.u32(); });
            });
        }
        auto comp678(Rd& r, Unit* u) -> void
        {
            r.u32();
            for (int k = 0; k < 5; ++k) r.u8();
            if (r.u8()) r.u32();
            r.u32();
            r.u32();
            r.u8();
            r.u8();
            if (u) u->atHand = r.p;
            for (int k = 0; k < 4; ++k)
            {
                uint32_t v = r.u32();
                if (u) u->hand[k] = v;
            }
            if (u) u->atInv = r.p;
            uint32_t inv = r.u32();
            if (u) u->inv = inv;
            if (u) u->atSlots = r.p;
            for (int k = 0; k < 17; ++k)
            {
                uint8_t s = r.u8();
                uint32_t v = r.u32();
                if (u)
                {
                    u->slotK[k] = s;
                    u->slotV[k] = v;
                }
            }
        }
        auto comp690(Rd& r) -> void
        {
            r.u32();
            r.sid();
            r.arr([&] { r.sid(); });
            for (int k = 0; k < 3; ++k) r.arr([&] { r.fname(); r.u32(); });
            r.sid();
            r.u8();
        }
        auto comps(Rd& r, Unit* u) -> void
        {
            static const int offs[] = {0x660, 0x668, 0x670, 0x678, 0x680, 0x688, 0x690, 0x698, 0x6a0, 0x6a8, 0x6b0, 0x6b8, 0x6c0, 0x6c8};
            for (int off : offs)
            {
                if (!r.u8()) continue;
                size_t st = r.p;
                switch (off)
                {
                case 0x668:
                    comp668(r);
                    if (u) { u->s668 = st; u->e668 = r.p; }
                    break;
                case 0x670:
                    r.u32();
                    r.u32();
                    r.u32();
                    break;
                case 0x678:
                    comp678(r, u);
                    if (u) { u->s678 = st; u->e678 = r.p; }
                    break;
                case 0x690:
                    comp690(r);
                    break;
                case 0x6a8:
                    r.u32();
                    for (int k = 0; k < 4; ++k) r.sid();
                    r.u8();
                    if (u) { u->s6a8 = st; u->e6a8 = r.p; }
                    break;
                case 0x6b0:
                    r.u32();
                    break;
                case 0x6b8:
                    r.u32();
                    r.skip(48);
                    if (u32at(r.b, 0) >= kVersion6b8Arr)
                    {
                        uint32_t n = r.u32();
                        for (uint32_t k = 0; k < n; ++k)
                        {
                            r.skip(16);
                            r.u32();
                            r.u8();
                        }
                    }
                    r.u32();
                    break;
                case 0x6c0:
                    r.u32();
                    r.arr([&] { r.u32(); });
                    r.arr([&] { r.u32(); });
                    break;
                case 0x6c8:
                    r.u32();
                    r.u32();
                    for (int k = 0; k < 4; ++k) r.u32();
                    break;
                default:
                    throw Unk("component " + hx((uint32_t)off) + " present at " + hx((uint32_t)r.p));
                }
            }
        }
        auto tail(Rd& r) -> void
        {
            r.u8();
            r.u8();
            if (r.u8()) r.skip(24);
            r.u8();
            uint8_t b1da = r.u8();
            for (int k = 0; k < 5; ++k) r.u32();
            r.skip(3);
            for (int k = 0; k < 7; ++k) r.u32();
            r.opt32();
            r.skip(20);
            r.u32();
            r.u8();
            r.u8();
            r.u16();
            r.u16();
            uint16_t n = r.u16();
            r.skip((size_t)n * 16);
            r.u32();
            r.opt32();
            r.opt32();
            if (b1da) r.opt32();
        }

        const std::string kUnitSec = "struct FUnitModel";
        const std::string kItemSec = "struct FModelItem";
        const std::string kContSec = "struct FModelItemContainer";
        const std::string kGrenSec = "struct FModelGrenade";
    }

    auto u32at(const std::string& b, size_t p) -> uint32_t
    {
        if (p + 4 > b.size()) throw Unk("u32 past end at " + hx((uint32_t)p));
        uint32_t v;
        std::memcpy(&v, &b[p], 4);
        return v;
    }

    auto crc32(const char* d, size_t n, uint32_t seed) -> uint32_t
    {
        static uint32_t tab[256];
        static bool init = false;
        if (!init)
        {
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                tab[i] = c;
            }
            init = true;
        }
        uint32_t c = ~seed;
        for (size_t i = 0; i < n; ++i) c = tab[(c ^ (uint8_t)d[i]) & 0xff] ^ (c >> 8);
        return ~c;
    }

    auto Names::parse(const std::string& b) -> void
    {
        if (b.size() < 9) throw Unk("raw too short");
        if (!known_version(u32at(b, 0))) throw Unk("raw version " + hx(u32at(b, 0)));
        nto = u32at(b, 5);
        if (nto >= b.size()) throw Unk("name table offset past end");
        groups.clear();
        index.clear();
        added.clear();
        Names dummy;
        Rd r(b, dummy, nto);
        while (r.p < b.size())
        {
            uint16_t nt = r.u16();
            std::vector<std::vector<std::string>> g(nt);
            for (auto& t : g)
            {
                uint16_t c = r.u16();
                t.reserve(c);
                for (uint16_t i = 0; i < c; ++i)
                {
                    uint16_t n = r.u16();
                    r.need(n);
                    t.emplace_back(b.data() + r.p, n);
                    r.p += n;
                }
            }
            groups.push_back(std::move(g));
        }
        if (groups.size() < 2) throw Unk("name table groups " + std::to_string(groups.size()));
        index.resize(groups.size());
        for (size_t g = 0; g < groups.size(); ++g)
        {
            index[g].resize(groups[g].size());
            for (size_t t = 0; t < groups[g].size(); ++t)
                for (size_t i = 0; i < groups[g][t].size(); ++i) index[g][t][groups[g][t][i]] = (uint16_t)i;
        }
    }
    auto Names::blob() const -> std::string
    {
        std::string o;
        for (auto& g : groups)
        {
            app16(o, (uint16_t)g.size());
            for (auto& t : g)
            {
                app16(o, (uint16_t)t.size());
                for (auto& n : t)
                {
                    app16(o, (uint16_t)n.size());
                    o += n;
                }
            }
        }
        return o;
    }
    auto Names::name(int g, uint8_t t, uint16_t i) const -> const std::string&
    {
        if ((size_t)g >= groups.size() || t >= groups[g].size() || i >= groups[g][t].size()) throw Unk("name ref out of range");
        return groups[g][t][i];
    }
    auto Names::get(int g, uint8_t t, const std::string& n) -> uint16_t
    {
        while (t >= groups[g].size())
        {
            groups[g].emplace_back();
            index[g].emplace_back();
        }
        auto it = index[g][t].find(n);
        if (it != index[g][t].end()) return it->second;
        if (groups[g][t].size() >= 0xffff) throw Unk("name table full");
        uint16_t i = (uint16_t)groups[g][t].size();
        groups[g][t].push_back(n);
        index[g][t][n] = i;
        added.push_back(std::to_string(g) + "/" + std::to_string(t) + "/" + std::to_string(i) + " " + n);
        return i;
    }

    auto find_section(const std::string& b, const std::string& name) -> size_t
    {
        std::string key(2, '\0');
        put16(key, 0, (uint16_t)name.size());
        key += name;
        size_t i = b.find(key);
        if (i == std::string::npos) throw Unk("section " + name + " not found");
        return i;
    }

    auto parse(const std::string& raw) -> Parsed
    {
        Parsed P;
        P.names.parse(raw);
        Rd r(raw, P.names);
        P.isec = find_section(raw, kItemSec);
        r.p = P.isec + 2 + kItemSec.size();
        P.icnt = r.u32();
        for (uint32_t idx = 0; idx < P.icnt; ++idx)
        {
            uint8_t pres = r.u8();
            if (pres == 0) continue;
            if (pres != 1) throw Unk("item presence byte " + std::to_string(pres) + " at " + hx((uint32_t)(r.p - 1)));
            Item d;
            d.idx = idx;
            r.tape = &d.tape;
            item(r, d, nullptr);
            r.tape = nullptr;
            P.itemAt[idx] = P.items.size();
            P.items.push_back(std::move(d));
        }
        P.iend = r.p;
        if (raw.compare(P.iend + 2, kContSec.size(), kContSec) != 0) throw Unk("item section does not end at the container header");
        P.csec = find_section(raw, kContSec);
        if (P.csec != P.iend) throw Unk("container section not right after the item section");
        r.p = P.csec + 2 + kContSec.size();
        P.ccnt = r.u32();
        r.arr([&] { r.u32(); r.u32(); });
        for (uint32_t idx = 0; idx < P.ccnt; ++idx)
        {
            uint8_t pres = r.u8();
            if (pres == 0) continue;
            if (pres != 1) throw Unk("container presence byte " + std::to_string(pres) + " at " + hx((uint32_t)(r.p - 1)));
            Cont d;
            d.idx = idx;
            r.tape = &d.tape;
            container(r, d);
            r.tape = nullptr;
            P.conts.push_back(std::move(d));
        }
        P.cend = r.p;
        if (raw.compare(P.cend + 2, kGrenSec.size(), kGrenSec) != 0) throw Unk("container section does not end at the grenade header");
        size_t us = find_section(raw, kUnitSec);
        if (us >= P.isec) throw Unk("unit section after the item section");
        r.p = us + 2 + kUnitSec.size();
        r.u32();
        r.arr([&] { r.u32(); r.u32(); });
        while (r.p < raw.size() && raw[r.p] == 0) ++r.p;
        if (r.u8() != 1) throw Unk("unit 0 presence");
        r.tape = &P.unit.tape;
        unit_head(r, &P.unit);
        comps(r, &P.unit);
        tail(r);
        r.tape = nullptr;
        P.unit.end = r.p;
        if (P.names.name(1, P.unit.sidT, P.unit.sidI) != "Player") throw Unk("unit 0 is not Player");
        if (!P.unit.e678 || !P.unit.e668 || !P.unit.e6a8) throw Unk("unit 0 lacks component 668/678/6a8");
        while (r.p < raw.size() && raw[r.p] == 0) ++r.p;
        if (r.u8() != 1) throw Unk("unit after 0 presence");
        auto ns = unit_head(r, nullptr);
        P.nextUnitSidT = ns.first;
        P.nextUnitSidI = ns.second;
        return P;
    }

    auto ItemView::key() const -> std::string
    {
        std::string k = sid + "|" + std::to_string(count) + "|" + std::to_string(gx) + "," + std::to_string(gy) + "|" + std::to_string(b40) + "|" +
                        std::to_string(b60) + "|" + hx(weight) + "|" + std::to_string(dtype) + "|" + std::to_string(bytes) + "|" + (inArr ? "a" : "-");
        auto lst = [&](const std::vector<std::string>& v) {
            k += "|[";
            for (auto& s : v) k += s + ",";
            k += "]";
        };
        if (dtype == 0)
        {
            lst(attach);
            lst(upgA);
            lst(upgB);
            k += "|" + hx(u98);
            if (hasState) k += "|" + std::to_string(rounds) + "|" + ammo + "|" + cart + "|" + std::to_string(mag);
        }
        if (dtype == 1)
        {
            k += "|" + hx(cond);
            lst(set48);
            lst(set98);
        }
        return k;
    }

    auto character(const std::string& raw, const Parsed& P) -> Char
    {
        Char c;
        const Unit& u = P.unit;
        std::memcpy(c.pos, &raw[u.atPos], 12);
        std::memcpy(c.u148, u.u148, sizeof c.u148);
        c.inv = u.inv;
        c.hand = u.hand[0];
        const Cont* cont = nullptr;
        for (auto& x : P.conts)
            if ((kCont | x.idx) == u.inv) cont = &x;
        if (!cont) throw Unk("inventory container " + hx(u.inv) + " not found");
        c.money = cont->money;
        c.gw = cont->gw;
        c.gh = cont->gh;
        c.nArr = cont->arr110.size();
        std::vector<uint32_t> ids;
        auto add = [&](uint32_t x) {
            if (std::find(ids.begin(), ids.end(), x) == ids.end()) ids.push_back(x);
        };
        for (uint32_t x : cont->arr110) add(x);
        for (int k = 0; k < 17; ++k)
        {
            c.slots.push_back({u.slotK[k], u.slotV[k]});
            if (u.slotV[k] != kNone) add(u.slotV[k]);
        }
        if (u.hand[0] != kNone) add(u.hand[0]);
        for (auto& d : P.items)
            if (d.owner == u.inv) add(kItem | d.idx);
        std::set<uint32_t> inArr(cont->arr110.begin(), cont->arr110.end());
        for (uint32_t x : ids)
        {
            if ((x & ~kIdx) != kItem) continue;
            auto it = P.itemAt.find(x & kIdx);
            if (it == P.itemAt.end()) continue;
            const Item& d = P.items[it->second];
            c.ids.push_back(x);
            Rd r(raw, P.names, d.at);
            Item tmp;
            ItemView v;
            item(r, tmp, &v);
            v.inArr = inArr.count(x) > 0;
            c.items.push_back(std::move(v));
        }
        for (int k = 0; k < 17; ++k)
        {
            uint32_t x = u.slotV[k];
            if (x == kNone) continue;
            auto it = P.itemAt.find(x & kIdx);
            std::string nm = (x & ~kIdx) == kItem && it != P.itemAt.end() ? P.names.name(1, P.items[it->second].sidT, P.items[it->second].sidI) : "?";
            c.slotSids.push_back(std::to_string(u.slotK[k]) + "=" + nm);
        }
        std::sort(c.slotSids.begin(), c.slotSids.end());
        Rd r(raw, P.names, u.s6a8 + 4);
        for (int k = 0; k < 4; ++k) c.quick[k] = r.sidName();
        return c;
    }

    auto same_char(const Char& a, const Char& b, std::string& why) -> bool
    {
        auto keys = [](const Char& c) {
            std::vector<std::string> v;
            for (auto& i : c.items) v.push_back(i.key());
            std::sort(v.begin(), v.end());
            return v;
        };
        if (keys(a) != keys(b)) { why = "items differ"; return false; }
        if (a.money != b.money) { why = "money differs"; return false; }
        for (int k = 0; k < 4; ++k)
            if (a.quick[k] != b.quick[k]) { why = "quick slots differ"; return false; }
        if (std::memcmp(a.u148, b.u148, sizeof a.u148) != 0) { why = "vitals differ"; return false; }
        if (a.nArr != b.nArr) { why = "item list length differs"; return false; }
        if (a.gw != b.gw || a.gh != b.gh) { why = "grid size differs"; return false; }
        if (a.slotSids != b.slotSids) { why = "slots differ"; return false; }
        return true;
    }

    namespace
    {
        struct Ctx
        {
            const std::string& db;
            const Names& dn;
            Names& hn;
            const std::unordered_map<uint32_t, uint32_t>& idmap;
            uint32_t dinv, hinv;
            std::vector<std::string>& log;

            auto remap(size_t st, size_t en, const std::vector<Tape>& tape, const std::string& what) -> std::string
            {
                std::string out = db.substr(st, en - st);
                std::unordered_set<size_t> tp;
                for (auto& t : tape)
                {
                    if (t.p < st || t.p >= en) continue;
                    tp.insert(t.p);
                    size_t o = t.p - st;
                    if (t.k == Tk::ref)
                    {
                        uint32_t v = u32at(db, t.p);
                        auto it = idmap.find(v);
                        if (it != idmap.end())
                            put32(out, o, it->second);
                        else if (v == dinv)
                            put32(out, o, hinv);
                        else if (v != kNone && v != 0 && ((v >> 27) == 6 || (v >> 27) == 7))
                            log.push_back(what + ": ref " + hx(v) + " at " + hx((uint32_t)t.p) + " has no mapping, kept");
                    }
                    else
                    {
                        int g = t.k == Tk::sid ? 1 : 0;
                        uint8_t tb = (uint8_t)db[t.p];
                        uint16_t i;
                        std::memcpy(&i, &db[t.p + 1], 2);
                        uint16_t ni = hn.get(g, tb, dn.name(g, tb, i));
                        put16(out, o + 1, ni);
                    }
                }
                for (size_t q = st; q + 4 <= en; ++q)
                {
                    uint32_t v;
                    std::memcpy(&v, &db[q], 4);
                    if ((idmap.count(v) || v == dinv) && !tp.count(q)) log.push_back(what + ": untracked donor id " + hx(v) + " at " + hx((uint32_t)q));
                }
                return out;
            }
        };

        auto char_ids(const std::string& raw, const Parsed& P, uint32_t& inv) -> std::vector<uint32_t>
        {
            Char c = character(raw, P);
            inv = c.inv;
            return c.ids;
        }
    }

    auto splice(const std::string& hb, const std::string& db, const Opt& o, Report& rep) -> std::string
    {
        Parsed hP = parse(hb);
        Parsed dP = parse(db);
        if (u32at(hb, 0) != u32at(db, 0)) throw Unk("host save version " + hx(u32at(hb, 0)) + " differs from the client save version " + hx(u32at(db, 0)));
        if (hP.names.blob() != hb.substr(hP.names.nto)) throw Unk("host name table rebuild differs");
        if (dP.names.blob() != db.substr(dP.names.nto)) throw Unk("donor name table rebuild differs");
        Names& hn = hP.names;
        uint32_t hinv = kNone, dinv = kNone;
        std::vector<uint32_t> hids = char_ids(hb, hP, hinv);
        std::vector<uint32_t> dids = char_ids(db, dP, dinv);
        std::set<uint32_t> hset(hids.begin(), hids.end());
        std::unordered_map<uint32_t, uint32_t> idmap;
        for (uint32_t x : dids)
            if (hset.count(x)) idmap[x] = x;
        std::deque<uint32_t> pool;
        for (uint32_t x : hids)
            if (!idmap.count(x)) pool.push_back(x);
        for (uint32_t i = 0; i < hP.icnt; ++i)
            if (!hP.itemAt.count(i)) pool.push_back(kItem | i);
        uint32_t nxt = hP.icnt;
        for (uint32_t x : dids)
        {
            if (idmap.count(x)) continue;
            if (!pool.empty())
            {
                idmap[x] = pool.front();
                pool.pop_front();
            }
            else
                idmap[x] = kItem | nxt++;
        }
        uint32_t newcnt = std::max(hP.icnt, nxt);
        Ctx cx{db, dP.names, hn, idmap, dinv, hinv, rep.log};
        std::unordered_map<uint32_t, std::string> newrec;
        for (uint32_t x : dids)
        {
            const Item& d = dP.items[dP.itemAt.at(x & kIdx)];
            newrec[idmap[x] & kIdx] = cx.remap(d.at, d.end, d.tape, "item " + hx(x) + " " + dP.names.name(1, d.sidT, d.sidI));
        }
        std::set<uint32_t> drop;
        for (uint32_t h : hids)
            if (!newrec.count(h & kIdx)) drop.insert(h & kIdx);
        size_t hdrEnd = hP.isec + 2 + kItemSec.size();
        std::string sec;
        app32(sec, newcnt);
        size_t p = hdrEnd + 4;
        for (uint32_t idx = 0; idx < newcnt; ++idx)
        {
            auto bi = hP.itemAt.find(idx);
            auto nr = newrec.find(idx);
            if (nr != newrec.end())
            {
                sec += '\x01';
                sec += nr->second;
                if (bi != hP.itemAt.end())
                    p = hP.items[bi->second].end;
                else if (idx < hP.icnt)
                {
                    if (hb[p] != 0) throw Unk("expected an empty item slot at " + hx((uint32_t)p));
                    ++p;
                }
            }
            else if (bi != hP.itemAt.end())
            {
                const Item& d = hP.items[bi->second];
                if (d.at - 1 != p || hb[d.at - 1] != 1) throw Unk("item walk out of step at " + hx((uint32_t)p));
                if (drop.count(idx))
                    sec += '\x00';
                else
                    sec.append(hb, d.at - 1, d.end - d.at + 1);
                p = d.end;
            }
            else
            {
                if (hb[p] != 0) throw Unk("expected a gap at " + hx((uint32_t)p));
                sec += '\x00';
                ++p;
            }
        }
        if (p != hP.iend) throw Unk("item section walk ended at " + hx((uint32_t)p) + " not " + hx((uint32_t)hP.iend));
        const Cont* hc = nullptr;
        const Cont* dc = nullptr;
        for (auto& c : hP.conts)
            if ((kCont | c.idx) == hinv) hc = &c;
        for (auto& c : dP.conts)
            if ((kCont | c.idx) == dinv) dc = &c;
        if (!hc || !dc) throw Unk("inventory container missing");
        std::string grid = cx.remap(dc->at110, dc->endGrid, dc->tape, "container arr110+grid");
        std::string hcrec = hb.substr(hc->at, hc->at110 - hc->at) + grid + hb.substr(hc->endGrid, hc->end - hc->endGrid);
        put32(hcrec, hc->atD0 - hc->at, dc->money);
        const Unit& hu = hP.unit;
        const Unit& du = dP.unit;
        std::string c668 = cx.remap(du.s668, du.e668, du.tape, "unit0 comp668");
        std::string c6a8 = cx.remap(du.s6a8, du.e6a8, du.tape, "unit0 comp6a8");
        std::string ueq = hb.substr(hu.s678, hu.e678 - hu.s678);
        auto mapv = [&](uint32_t v) -> uint32_t {
            auto it = idmap.find(v);
            if (it != idmap.end()) return it->second;
            return (v >> 27) == 6 && v != kNone ? kNone : v;
        };
        for (int k = 0; k < 4; ++k) put32(ueq, hu.atHand - hu.s678 + 4 * k, mapv(du.hand[k]));
        put32(ueq, hu.atInv - hu.s678, hinv);
        for (int k = 0; k < 17; ++k)
        {
            ueq[hu.atSlots - hu.s678 + 5 * k] = (char)du.slotK[k];
            put32(ueq, hu.atSlots - hu.s678 + 5 * k + 1, mapv(du.slotV[k]));
        }
        std::string ub = hb.substr(hu.at, hu.end - hu.at);
        if (o.vitals)
            for (int k = 0; k < 8; ++k) put32(ub, hu.atU148 - hu.at + 4 * k, du.u148[k]);
        if (!o.keepPos) ub.replace(hu.atPos - hu.at, 12, db, du.atPos, 12);
        std::string nu;
        size_t a = hu.at, cur = hu.at;
        const std::pair<std::pair<size_t, size_t>, const std::string*> pieces[] = {
            {{hu.s668, hu.e668}, &c668}, {{hu.s678, hu.e678}, &ueq}, {{hu.s6a8, hu.e6a8}, &c6a8}};
        for (auto& pc : pieces)
        {
            nu.append(ub, cur - a, pc.first.first - cur);
            nu += *pc.second;
            cur = pc.first.second;
        }
        nu.append(ub, cur - a, std::string::npos);
        std::string out;
        out.reserve(hb.size() + sec.size() / 8 + 4096);
        out.append(hb, 0, hu.at);
        out += nu;
        out.append(hb, hu.end, hdrEnd - hu.end);
        out += sec;
        out.append(hb, hP.iend, hc->at - hP.iend);
        out += hcrec;
        out.append(hb, hc->end, hn.nto - hc->end);
        uint32_t nto = (uint32_t)out.size();
        out += hn.blob();
        put32(out, 5, nto);
        rep.hostIds = hids;
        rep.donorIds = dids;
        for (uint32_t x : dids) rep.idmap.push_back({x, idmap[x]});
        rep.icntOld = hP.icnt;
        rep.icntNew = newcnt;
        rep.namesAdded = hn.added;
        rep.dropped.assign(drop.begin(), drop.end());
        return out;
    }

    auto file_split(const std::string& file, uint32_t& rawSize, std::string& stream, std::string& err) -> bool
    {
        if (file.size() < 12) { err = "file too short"; return false; }
        uint32_t stored = u32at(file, file.size() - 4);
        uint32_t c = crc32(file.data(), file.size() - 4);
        if (stored != c) { err = "crc mismatch stored " + hx(stored) + " computed " + hx(c); return false; }
        rawSize = u32at(file, 0);
        stream = file.substr(4, file.size() - 8);
        return true;
    }
    auto file_wrap(const std::string& stream, uint32_t rawSize) -> std::string
    {
        std::string f;
        f.reserve(stream.size() + 8);
        app32(f, rawSize);
        f += stream;
        app32(f, crc32(f.data(), f.size()));
        return f;
    }
}
