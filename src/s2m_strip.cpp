#include "s2m_strip.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace s2m_strip
{
    namespace
    {
        struct Unk : std::runtime_error
        {
            using std::runtime_error::runtime_error;
        };

        constexpr uint32_t kNull = 0xffffffffu;
        constexpr uint32_t kMask = 0x7ffffffu;
        constexpr uint16_t kIgnoreBit = 0x8000;

        auto hex8(uint64_t v) -> std::string
        {
            char b[24];
            std::snprintf(b, sizeof b, "%llx", (unsigned long long)v);
            return b;
        }

        struct KTab
        {
            std::mutex m;
            std::vector<std::string> names;
            std::unordered_map<std::string, uint16_t> idx;
        };
        auto ktab() -> KTab&
        {
            static KTab t;
            return t;
        }
        auto kind(const std::string& n) -> uint16_t
        {
            KTab& t = ktab();
            std::lock_guard<std::mutex> l(t.m);
            auto it = t.idx.find(n);
            if (it != t.idx.end()) return it->second;
            if (t.names.size() >= 0x7fff) throw Unk("too many tag kinds");
            uint16_t id = (uint16_t)t.names.size();
            if (n == "zone_self" || n == "sid" || n == "fn" || n == "lst") id |= kIgnoreBit;
            t.names.push_back(n);
            t.idx[n] = id;
            return id;
        }
        auto kind_names() -> std::vector<std::string>
        {
            KTab& t = ktab();
            std::lock_guard<std::mutex> l(t.m);
            return t.names;
        }
#define K(s) ([]() -> uint16_t { static const uint16_t k_ = kind(s); return k_; }())
        auto kd(const std::string& k, const char* sfx) -> uint16_t
        {
            return kind(k + sfx);
        }

        struct Span
        {
            uint32_t s, e;
        };
        struct Tag
        {
            uint32_t pos;
            uint16_t kind;
        };
        struct Lst
        {
            uint32_t cp, s0, n;
            uint8_t w;
        };
        struct Rec
        {
            uint32_t idx = 0;
            bool hasIdx = true;
            uint32_t st = 0, end = 0, t0 = 0, t1 = 0, l0 = 0, l1 = 0;
        };
        struct Tapes
        {
            std::vector<Tag> tags;
            std::vector<Lst> lsts;
            std::vector<Span> spans, stk;
        };

        struct NT
        {
            std::vector<uint32_t> fn;
            std::vector<std::vector<std::pair<uint32_t, uint16_t>>> sid;
        };

        struct R
        {
            const uint8_t* d;
            size_t n;
            size_t p = 0;
            Tapes* T;
            const NT* nt;

            auto need(size_t k) const -> void
            {
                if (p + k > n) throw Unk("read past end at " + hex8(p));
            }
            auto u8() -> uint8_t
            {
                need(1);
                return d[p++];
            }
            auto u16() -> uint16_t
            {
                need(2);
                uint16_t v;
                std::memcpy(&v, d + p, 2);
                p += 2;
                return v;
            }
            auto u32() -> uint32_t
            {
                need(4);
                uint32_t v;
                std::memcpy(&v, d + p, 4);
                p += 4;
                return v;
            }
            auto u64() -> uint64_t
            {
                need(8);
                uint64_t v;
                std::memcpy(&v, d + p, 8);
                p += 8;
                return v;
            }
            auto skip(size_t k) -> void
            {
                need(k);
                p += k;
            }
            auto f32() -> void { skip(4); }
            auto tag(uint16_t k) -> void
            {
                if (!(k & kIgnoreBit)) T->tags.push_back({(uint32_t)p, k});
            }
            auto ref(uint16_t k) -> uint32_t
            {
                tag(k);
                return u32();
            }
            auto ref() -> uint32_t { return ref(K("ref")); }
            auto s64(const char* k) -> uint64_t
            {
                tag(kind(std::string("s64_") + k));
                return u64();
            }
            auto hid() -> uint32_t { return ref(K("hid")); }
            auto optref(uint16_t k) -> void
            {
                if (u8()) ref(k);
            }
            auto opt8() -> void
            {
                if (u8()) u8();
            }
            auto opt32() -> void
            {
                if (u8()) u32();
            }
            auto boolv() -> bool
            {
                uint8_t v = u8();
                if (v > 1) throw Unk("bool " + hex8(v) + " at " + hex8(p - 1));
                return v != 0;
            }
            auto sidref() -> uint32_t
            {
                uint8_t t = u8();
                uint16_t i = u16();
                if (t >= nt->sid.size() || i >= nt->sid[t].size()) throw Unk("bad sid ref at " + hex8(p - 3));
                return ((uint32_t)t << 16) | i;
            }
            auto fname() -> void
            {
                uint8_t t = u8();
                uint16_t i = u16();
                if (t >= nt->fn.size() || i >= nt->fn[t]) throw Unk("bad fname ref at " + hex8(p - 3));
            }
            auto sidName(uint32_t packed) const -> std::string_view
            {
                auto& e = nt->sid[packed >> 16][packed & 0xffff];
                return std::string_view((const char*)d + e.first, e.second);
            }
            template <class F>
            auto arr(F&& f) -> void
            {
                uint16_t c = u16();
                for (uint16_t i = 0; i < c; ++i) f();
            }
            template <class F>
            auto lst(F&& f, int w = 2) -> void
            {
                uint32_t cp = (uint32_t)p;
                uint32_t cnt = w == 2 ? u16() : u32();
                size_t base = T->stk.size();
                for (uint32_t i = 0; i < cnt; ++i)
                {
                    uint32_t s = (uint32_t)p;
                    f();
                    T->stk.push_back({s, (uint32_t)p});
                }
                if (cnt)
                {
                    Lst l{cp, (uint32_t)T->spans.size(), cnt, (uint8_t)w};
                    T->spans.insert(T->spans.end(), T->stk.begin() + (std::ptrdiff_t)base, T->stk.end());
                    T->lsts.push_back(l);
                }
                T->stk.resize(base);
            }
            auto reflist(uint16_t k) -> void
            {
                lst([&] { ref(k); });
            }
            auto bstr() -> void { skip(u16()); }
            auto u8s(int c) -> void
            {
                for (int i = 0; i < c; ++i) u8();
            }
            auto u32s(int c) -> void
            {
                for (int i = 0; i < c; ++i) u32();
            }
            auto u16a_u8() -> void
            {
                arr([&] { u8(); });
            }
            auto u16a_u32() -> void
            {
                arr([&] { u32(); });
            }
            auto u16a_u64() -> void
            {
                arr([&] { u64(); });
            }
            auto u16a_sid() -> void
            {
                arr([&] { sidref(); });
            }
            auto u16a_fn() -> void
            {
                arr([&] { fname(); });
            }
            auto u16a_blk(size_t k) -> void
            {
                arr([&] { skip(k); });
            }
            auto boolvec() -> void
            {
                if (boolv()) skip(24);
            }
            auto ver() const -> uint32_t
            {
                uint32_t v;
                std::memcpy(&v, d, 4);
                return v;
            }
            auto vec2arr() -> void
            {
                skip(48);
                if (ver() >= 0xBC)
                {
                    uint32_t n = u32();
                    for (uint32_t i = 0; i < n; ++i) { skip(16); u32(); u8(); }
                }
            }
        };

        enum Sk : int
        {
            S_Unit, S_Anomaly, S_AnomalySpawner, S_ArtifactSpawner, S_Lair, S_Marker, S_Squad, S_Item, S_Container,
            S_ContextualAction, S_Zone, S_Destructible, S_Patrol, S_Interactable, S_Quicksilver, S_GlobalVariable,
            S_Scenario, S_WorldMap, S_Trigger, S_Flashlight, S_Population, S_Director, S_Teleport, S_Bark,
            S_VolumeForEffects, S_Count
        };

        struct SecDef
        {
            const char* key;
            const char* name;
            bool pool;
            bool prefix;
        };

        const SecDef kSecs[S_Count] = {
            {"Unit", "struct FUnitModel", true, true},
            {"Anomaly", "struct FModelAnomaly", true, false},
            {"AnomalySpawner", "struct FModelAnomalySpawner", true, false},
            {"ArtifactSpawner", "struct FModelArtifactSpawner", true, false},
            {"Lair", "struct FModelLair", true, true},
            {"Marker", "struct FModelMarker", true, false},
            {"Squad", "struct FModelSquad", true, false},
            {"Item", "struct FModelItem", true, false},
            {"Container", "struct FModelItemContainer", true, true},
            {"ContextualAction", "struct FModelContextualAction", true, false},
            {"Zone", "struct FModelZone", true, false},
            {"Destructible", "struct FDestructibleObjectModel", true, false},
            {"Patrol", "struct FModelPatrol", true, false},
            {"Interactable", "struct FInteractableObjectModel", true, false},
            {"Quicksilver", "struct FModelQuicksilverAnomaly", true, false},
            {"GlobalVariable", "class UGlobalVariableManager", false, false},
            {"Scenario", "class UScenarioManager", false, false},
            {"WorldMap", "class UWorldMapManager", false, false},
            {"Trigger", "class UTriggerManager", false, false},
            {"Flashlight", "class UAIFlashlightUseManager", false, false},
            {"Population", "class UALifePopulationManager", false, false},
            {"Director", "class UALifeDirector", false, false},
            {"Teleport", "class UTeleportManager", false, false},
            {"Bark", "class UBarkManager", false, false},
            {"VolumeForEffects", "struct FVolumeForEffectsModel", true, false},
        };

        struct UnitX
        {
            uint64_t q140 = 0;
            uint32_t ref58 = kNull, ref90 = kNull, inv = kNull, ref18 = kNull;
            bool puppet = false, hasInv = false, hasRef18 = false;
        };
        struct ContX
        {
            uint32_t u68 = 0;
            std::vector<uint32_t> items;
        };

        struct Sec
        {
            int key = 0;
            uint32_t s0 = 0, body = 0, e0 = 0;
            uint32_t cnt = 0;
            bool hasPref = false;
            std::vector<std::pair<uint32_t, uint32_t>> pref;
            std::vector<Rec> recs;
            size_t end = 0;
            std::vector<UnitX> ux;
            std::vector<ContX> cx;
            std::vector<uint32_t> own;
        };

        struct SecHdr
        {
            uint32_t s;
            std::string name;
        };

        auto is_cls(uint8_t c) -> bool
        {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '<' || c == '>' || c == ',' || c == ' ';
        }

        auto find_headers(const uint8_t* b, size_t total, uint32_t nto) -> std::vector<SecHdr>
        {
            std::vector<SecHdr> out;
            size_t lim = std::min<size_t>(nto, total);
            size_t i = 0;
            while (i < lim)
            {
                size_t kw = 0;
                if (b[i] == 's' && i + 7 <= lim && std::memcmp(b + i, "struct ", 7) == 0) kw = 7;
                else if (b[i] == 'c' && i + 6 <= lim && std::memcmp(b + i, "class ", 6) == 0) kw = 6;
                if (!kw || i + kw >= lim || !is_cls(b[i + kw]))
                {
                    ++i;
                    continue;
                }
                size_t e = i + kw;
                while (e < lim && is_cls(b[e])) ++e;
                size_t s = i;
                i = e;
                if (s < 2) continue;
                uint16_t n;
                std::memcpy(&n, b + s - 2, 2);
                if (!(n > 6 && n < 200)) continue;
                if (s + n > total) continue;
                if (!(std::memcmp(b + s, "struct ", std::min<size_t>(7, n)) == 0 && n >= 7) && !(std::memcmp(b + s, "class ", std::min<size_t>(6, n)) == 0 && n >= 6)) continue;
                bool pr = true;
                for (size_t k = 0; k < n; ++k)
                    if (b[s + k] < 32 || b[s + k] >= 127)
                    {
                        pr = false;
                        break;
                    }
                if (!pr) continue;
                out.push_back({(uint32_t)(s - 2), std::string((const char*)b + s, n)});
            }
            return out;
        }

        struct Parsed
        {
            const uint8_t* b = nullptr;
            size_t n = 0;
            uint32_t nto = 0;
            NT nt;
            std::vector<SecHdr> hdr;
            Tapes T;
            std::vector<Sec> secs;
            std::string err;
        };

        auto read_names(Parsed& P) -> void
        {
            R r{P.b, P.n, P.nto, nullptr, nullptr};
            int g = 0;
            while (r.p < P.n)
            {
                uint16_t nt = r.u16();
                for (uint16_t t = 0; t < nt; ++t)
                {
                    uint16_t cnt = r.u16();
                    if (g == 0) P.nt.fn.push_back(cnt);
                    if (g == 1) P.nt.sid.emplace_back();
                    for (uint16_t i = 0; i < cnt; ++i)
                    {
                        uint16_t len = r.u16();
                        r.need(len);
                        if (g == 1) P.nt.sid.back().push_back({(uint32_t)r.p, len});
                        r.p += len;
                    }
                }
                ++g;
            }
            if (g < 2) throw Unk("name table groups " + std::to_string(g));
        }

        auto bounds(const Parsed& P, const char* name, uint32_t& s0, uint32_t& body, uint32_t& e0) -> void
        {
            for (size_t i = 0; i < P.hdr.size(); ++i)
                if (P.hdr[i].name == name)
                {
                    s0 = P.hdr[i].s;
                    body = s0 + 2 + (uint32_t)P.hdr[i].name.size();
                    e0 = i + 1 < P.hdr.size() ? P.hdr[i + 1].s : P.nto;
                    return;
                }
            throw Unk(std::string("no section ") + name);
        }

        namespace up
        {
            auto cond(R& r) -> void;

            auto vec16(R& r) -> void { r.u16a_blk(24); }

            auto upath(R& r) -> void
            {
                r.u32();
                r.arr([&] { r.skip(24); r.u32(); r.u32(); });
                r.ref();
                r.u32();
                r.u32();
            }
            auto b82634d(R& r) -> void { r.ref(); r.u8(); r.u8(); r.u8(); }
            auto b826d7c(R& r) -> void { r.opt8(); r.opt8(); r.opt32(); r.u8s(4); }
            auto p48_0(R& r) -> void
            {
                r.ref(); r.u32(); r.u32(); r.u32();
                r.ref(); r.u32();
                r.ref(); r.vec2arr();
                for (int i = 0; i < 4; ++i) r.ref();
            }
            auto p50_1(R& r) -> void { r.lst([&] { r.ref(K("brain_p50k1_unit")); r.u32(); }); }
            auto e67f048d(R& r) -> void { r.skip(24); r.skip(24); r.fname(); r.fname(); r.u32(); }
            auto e2f96ae8(R& r) -> void
            {
                r.ref(K("hid2"));
                if (r.u8()) e67f048d(r);
                r.skip(24); r.u32(); r.skip(24); r.u32(); r.u8(); r.u32(); r.u32(); r.u32();
            }
            auto e8337b6(R& r) -> void
            {
                r.u32(); r.u8(); r.optref(K("hid2"));
                if (r.u8()) r.u32();
                r.skip(24); r.u32(); r.u32(); r.u8(); r.u8(); r.u32();
            }
            auto b83255b(R& r) -> void
            {
                r.ref();
                r.arr([&] { e2f96ae8(r); });
                r.u32();
                r.arr([&] { r.ref(K("hid2")); });
                r.u16a_u32();
                r.arr([&] { e8337b6(r); });
                r.sidref(); r.u32();
                r.u16a_u32();
                r.arr([&] { r.ref(K("hid2")); r.u32(); r.u32(); });
            }
            auto p50_0(R& r) -> void
            {
                b83255b(r);
                r.opt32(); r.u8(); r.u32(); r.ref(); r.ref();
                r.lst([&] { r.ref(K("brain_r550")); });
                r.ref(); r.opt32(); r.u32();
            }
            auto b834500(R& r) -> void
            {
                r.ref(); r.ref();
                r.arr([&] { r.ref(K("hid2")); });
                r.ref();
                r.bstr(); r.ref(); r.opt32();
            }
            auto p58_0(R& r) -> void
            {
                b834500(r);
                r.ref(); r.u32(); r.u32(); r.opt32();
                if (r.u8()) r.ref(K("oid_a8"));
            }
            auto p58_1(R& r) -> void
            {
                b834500(r);
                r.u32(); r.u32(); r.u32(); r.u8(); r.u8(); r.u8(); r.ref(); r.u16a_u8();
                r.ref(); r.u8();
                if (r.u8()) { r.hid(); r.u8(); }
            }
            auto variant(R& r) -> void
            {
                uint32_t k = r.u32();
                switch (k)
                {
                case 0: case 6: return;
                case 1: case 2: case 8: case 9: r.ref(); return;
                case 3: r.u16a_u64(); r.ref(); return;
                case 4: case 11: r.u32(); r.ref(); return;
                case 5: r.u16a_blk(16); r.u16a_u64(); r.ref(); return;
                case 7: r.skip(16); r.ref(); return;
                case 10: r.ref(K("hid2")); r.ref(); return;
                default: throw Unk("variant kind " + hex8(k) + " at " + hex8(r.p));
                }
            }
            auto target(R& r) -> void
            {
                uint32_t k = r.u32();
                switch (k)
                {
                case 0: return;
                case 1: r.ref(); return;
                case 2: r.skip(24); r.opt8(); return;
                case 3: r.skip(16); r.u8(); return;
                case 4: r.skip(16); r.u32(); r.skip(24); return;
                case 5: r.arr([&] { r.ref(); }); r.ref(K("squad")); return;
                case 6: r.u32(); return;
                case 7: r.ref(K("ref_d")); return;
                default: throw Unk("target kind " + hex8(k) + " at " + hex8(r.p));
                }
            }
            auto c3head(R& r) -> void
            {
                if (!r.u8()) return;
                r.ref(); r.ref(); vec16(r); r.u8();
            }
            auto cond(R& r) -> void
            {
                if (!r.boolv()) return;
                uint32_t t = r.u32();
                switch (t)
                {
                case 0: cond(r); break;
                case 1: r.u32(); break;
                case 2: cond(r); break;
                case 3: c3head(r); r.opt32(); r.optref(K("hid2")); break;
                case 4: r.skip(24); r.boolvec(); r.u32(); r.u8(); break;
                case 5: variant(r); r.sidref(); r.boolvec(); r.u8(); r.u8(); r.u32(); throw Unk("67c202e at " + hex8(r.p));
                case 6: r.u8(); r.u8(); r.u32(); r.u32(); r.boolvec(); break;
                case 7: r.u8(); r.u8(); r.u32(); r.u32(); break;
                case 8: r.skip(16); cond(r); cond(r); break;
                case 9: cond(r); cond(r); break;
                case 10: variant(r); break;
                case 11: case 12: variant(r); r.u8(); break;
                case 13: case 14: case 18: case 21: case 25: case 28: case 31: case 37: break;
                case 15:
                {
                    uint32_t n = r.u32();
                    for (uint32_t i = 0; i < n; ++i) r.ref(K("hid2"));
                    r.u8(); r.u8(); r.u8(); r.u8(); cond(r);
                    break;
                }
                case 17:
                    r.u8(); r.u8(); r.u32(); r.boolvec();
                    if (r.boolv()) target(r);
                    r.u8(); r.skip(12);
                    break;
                case 19: cond(r); break;
                case 20: cond(r); r.u32(); r.u32(); r.u32(); r.u32(); r.u8(); break;
                case 22: r.skip(24); r.u32(); r.u8(); r.u8(); r.u32(); break;
                case 24: r.opt32(); r.skip(24); r.skip(24); r.skip(24); r.u8(); r.u8(); break;
                case 26: variant(r); r.u32(); break;
                case 27: r.opt32(); r.ref(); r.u32(); r.ref(); r.u8(); r.u8(); r.u8(); r.u8(); break;
                case 29: r.ref(); r.u32(); r.optref(K("hid2")); r.opt32(); break;
                case 30: r.u8(); r.skip(24); r.u32(); r.u32(); r.ref(); break;
                case 32: r.hid(); r.u8(); cond(r); break;
                case 34: variant(r); r.sidref(); r.u32(); r.u8(); r.hid(); r.u8(); break;
                case 35: r.boolvec(); break;
                case 36: r.ref(); r.u16a_sid(); r.u16a_sid(); break;
                default: throw Unk("cond type " + hex8(t) + " unimplemented at " + hex8(r.p));
                }
            }
            auto node91a0(R& r) -> void
            {
                r.u32(); r.u32(); r.u32(); r.u8(); r.u8();
                cond(r);
            }
            auto b827dea(R& r) -> void
            {
                r.ref();
                r.u16a_u8();
                uint16_t n = r.u16();
                for (uint16_t i = 0; i < n; ++i)
                {
                    if (r.u8()) r.u8();
                    else node91a0(r);
                }
                r.u16a_u32();
                r.u16a_u32();
                r.u32();
            }
            auto brain_body(R& r) -> void
            {
                b82634d(r);
                b826d7c(r);
                r.ref(); r.opt32(); r.u8(); r.u8(); r.u8(); r.u32();
                if (r.boolv())
                {
                    uint32_t t = r.u32();
                    if (t == 0) p48_0(r);
                    else if (t != 1) throw Unk("p48 type " + hex8(t) + " at " + hex8(r.p));
                }
                if (r.boolv())
                {
                    uint32_t t = r.u32();
                    if (t == 0) p50_0(r);
                    else if (t == 1) p50_1(r);
                    else throw Unk("p50 type " + hex8(t) + " at " + hex8(r.p));
                }
                if (r.boolv())
                {
                    uint32_t t = r.u32();
                    if (t == 0) p58_0(r);
                    else if (t == 1) p58_1(r);
                    else if (t == 2) { r.ref(); r.ref(); }
                    else throw Unk("p58 type " + hex8(t) + " at " + hex8(r.p));
                }
                if (r.boolv())
                {
                    b827dea(r);
                    r.opt32();
                }
                r.u16a_fn();
                r.u16a_u8();
                r.u32(); r.u8();
            }
            auto brain(R& r) -> void
            {
                if (!r.boolv()) return;
                uint32_t t = r.u32();
                if (t != 0 && t != 1) throw Unk("brain type " + hex8(t) + " at " + hex8(r.p));
                brain_body(r);
            }
            auto c660(R& r) -> void
            {
                r.ref();
                if (r.boolv()) upath(r);
                r.ref(K("squad"));
                brain(r);
                r.u8(); r.u8(); r.u8();
            }
            auto sida(R& r) -> void { r.u16a_sid(); }
            auto e82e984(R& r) -> void { sida(r); sida(r); sida(r); sida(r); r.u8(); }
            auto e82eaa5(R& r) -> void
            {
                r.sidref(); r.u32(); r.u8();
                r.arr([&] { e82eaa5(r); });
            }
            auto dc72(R& r) -> void
            {
                r.sidref(); r.u8();
                r.arr([&] { r.hid(); r.u8(); r.boolvec(); });
                r.u8();
                r.arr([&] { r.sidref(); e82e984(r); });
                e82eaa5(r);
            }
            auto d82d115(R& r) -> void
            {
                r.u32();
                for (int i = 0; i < 13; ++i)
                {
                    dc72(r);
                    if (r.boolv()) dc72(r);
                }
                r.arr([&] { dc72(r); });
                r.arr([&] { dc72(r); });
                r.u32();
            }
            auto c688(R& r) -> void
            {
                r.sidref(); r.bstr(); r.bstr();
                uint16_t n = r.u16();
                if (n)
                {
                    r.sidref(); r.u8(); r.u8();
                    throw Unk("6ca9c9c at " + hex8(r.p));
                }
                r.u32(); r.ref(); r.fname();
                if (r.boolv()) d82d115(r);
            }
            auto c698(R& r) -> void
            {
                r.ref(); r.sidref(); r.sidref();
                r.arr([&] { r.fname(); r.skip(12); });
                r.arr([&] { r.hid(); });
                r.skip(24); r.u32();
                r.arr([&] { r.u32(); r.u32(); });
            }
            auto e82f990(R& r) -> void
            {
                r.sidref(); r.u32(); r.skip(12); r.skip(12); r.fname(); r.skip(12);
            }
            auto m8341a6(R& r) -> void { r.u32(); r.u32(); r.u8(); }
            auto unit_head(R& r, UnitX& x) -> void
            {
                x.ref58 = r.ref(K("squad"));
                r.u8(); r.u8();
                r.skip(12);
                x.ref90 = r.ref(K("squad"));
                r.ref(K("ref10"));
                r.u64();
                r.hid();
                r.skip(16);
                r.hid();
                r.ref(K("refe"));
                r.u64(); r.u64();
                uint32_t sid = r.sidref();
                std::string_view nm = r.sidName(sid);
                x.puppet = (nm.size() >= 7 && nm.compare(0, 7, "S2Coop_") == 0) || (nm.size() >= 5 && nm.compare(0, 5, "Nola_") == 0);
                x.q140 = r.u64();
                r.u32s(8);
                r.u16a_u32();
                r.u32();
                for (int i = 0; i < 12; ++i) r.f32();
                r.f32(); r.f32();
                r.u8(); r.u8(); r.u8();
                r.hid();
                r.skip(12);
                r.u32();
                r.u8();
                r.u64();
                r.u32(); r.u32();
                r.u16a_blk(16);
                r.u32s(6);
                for (int i = 0; i < 23; ++i) r.f32();
                r.opt32(); r.opt32(); r.opt32();
                if (r.u8())
                {
                    m8341a6(r);
                    uint32_t n = r.u32();
                    for (uint32_t i = 0; i < n; ++i) { r.u32(); m8341a6(r); }
                    r.u32(); r.u32();
                }
                r.arr([&] { e82f990(r); });
                r.sidref();
                r.u16a_blk(0x60);
                r.u16a_fn();
                r.skip(0x60);
                r.u8();
                r.ref(K("u470_hid"));
                r.ref(K("u4a0_hid"));
                r.sidref();
            }
            auto eff80(R& r) -> void
            {
                r.sidref(); r.ref(); r.u8(); r.opt32();
                r.u32(); r.u8(); r.u32(); r.u32(); r.u32(); r.u32(); r.u32(); r.opt32(); r.u8();
            }
            auto eff38(R& r) -> void
            {
                r.sidref(); r.ref(); r.u8(); r.opt32();
            }
            auto c668(R& r) -> void
            {
                r.arr([&] { eff80(r); });
                r.arr([&] { r.u32(); r.u32(); });
                r.arr([&] { eff80(r); });
                r.arr([&] { eff38(r); });
                r.arr([&] { r.u32(); r.arr([&] { r.u8(); r.u64(); }); });
                r.arr([&] { eff38(r); });
                r.arr([&] { eff38(r); });
                r.ref();
                r.u8();
                for (int i = 0; i < 20; ++i) r.f32();
                r.arr([&] { r.u32(); r.u32(); });
                r.u32(); r.u32();
                r.u8(); r.u64(); r.u64();
                r.u8(); r.u32(); r.u8(); r.u32();
                r.arr([&] { r.sidref(); r.arr([&] { r.sidref(); r.u32(); r.u32(); }); });
            }
            auto c670(R& r) -> void { r.ref(); r.u32(); r.u32(); }
            auto c678(R& r, UnitX& x) -> void
            {
                r.ref();
                r.u8s(5);
                if (r.u8()) r.u32();
                r.u32(); r.u32();
                r.u8(); r.u8();
                r.hid(); r.hid(); r.hid(); r.hid();
                x.inv = r.ref(K("ref7"));
                x.hasInv = true;
                for (int i = 0; i < 17; ++i) { r.u8(); r.hid(); }
            }
            auto c680(R& r) -> void
            {
                for (int i = 0; i < 9; ++i) r.ref();
            }
            auto c690(R& r) -> void
            {
                r.ref(); r.sidref(); r.u16a_sid();
                r.arr([&] { r.fname(); r.u32(); });
                r.arr([&] { r.fname(); r.u32(); });
                r.arr([&] { r.fname(); r.u32(); });
                r.sidref(); r.u8();
            }
            auto c6a8(R& r) -> void
            {
                r.ref();
                for (int i = 0; i < 4; ++i) r.sidref();
                r.u8();
            }
            auto c6b0(R& r) -> void { r.ref(); }
            auto c6b8(R& r) -> void
            {
                r.ref();
                r.vec2arr();
                r.ref();
            }
            auto c6c0(R& r) -> void
            {
                r.ref();
                r.arr([&] { r.ref(K("id32")); });
                r.arr([&] { r.ref(K("id32")); });
            }
            auto c6c8(R& r, UnitX& x) -> void
            {
                r.ref();
                x.ref18 = r.ref(K("ref7"));
                x.hasRef18 = true;
                r.u32s(4);
            }
            auto unit_tail(R& r) -> void
            {
                r.u8(); r.u8();
                if (r.u8()) r.skip(24);
                r.u8();
                uint8_t b1da = r.u8();
                r.u32s(5);
                r.skip(3);
                r.u32s(7);
                r.opt32();
                r.skip(20); r.f32(); r.u8(); r.u8();
                r.u16(); r.u16();
                r.u16a_blk(16);
                r.u32();
                r.opt32(); r.opt32();
                if (b1da) r.opt32();
            }
            auto unit(R& r, UnitX& x) -> void
            {
                unit_head(r, x);
                if (r.boolv()) c660(r);
                if (r.boolv()) c668(r);
                if (r.boolv()) c670(r);
                if (r.boolv()) c678(r, x);
                if (r.boolv()) c680(r);
                if (r.boolv()) c688(r);
                if (r.boolv()) c690(r);
                if (r.boolv()) c698(r);
                if (r.boolv()) throw Unk("component 6a0 present at " + hex8(r.p));
                if (r.boolv()) c6a8(r);
                if (r.boolv()) c6b0(r);
                if (r.boolv()) c6b8(r);
                if (r.boolv()) c6c0(r);
                if (r.boolv()) c6c8(r, x);
                unit_tail(r);
                r.u8();
            }
        }

        namespace is
        {
            auto elem80(R& r) -> void
            {
                r.sidref(); r.ref(); r.u8(); r.opt32();
                r.u32(); r.u8(); r.u32(); r.u32(); r.u32(); r.u32(); r.u32(); r.opt32(); r.u8();
            }
            auto elem38(R& r) -> void { r.sidref(); r.ref(); r.u8(); r.opt32(); }
            auto effects(R& r) -> void
            {
                r.arr([&] { elem80(r); });
                r.arr([&] { r.u32(); r.u32(); });
                r.arr([&] { elem80(r); });
                r.arr([&] { elem38(r); });
                r.arr([&] { r.u32(); r.arr([&] { r.u8(); r.u64(); }); });
                r.arr([&] { elem38(r); });
                r.arr([&] { elem38(r); });
            }
            auto stats26(R& r, int extra) -> void
            {
                for (int i = 0; i < 19; ++i) r.f32();
                r.u8(); r.u8();
                r.u32();
                r.u32s(extra);
            }
            auto weapon_state(R& r) -> void
            {
                r.u16a_u8();
                r.arr([&] { r.u8(); r.sidref(); });
                r.u8(); r.u8(); r.u8();
                r.u32();
                r.sidref();
                r.u32(); r.u32();
                r.u32s(7);
                r.arr([&] { r.u8(); r.u32(); });
                r.u32();
                stats26(r, 2);
                stats26(r, 5);
                r.u32(); r.u8(); r.u32(); r.u32(); r.u32(); r.u8();
                r.sidref();
                r.u32(); r.u32(); r.u32(); r.u8(); r.u32();
                if (r.u8()) throw Unk("weapon state strings opt at " + hex8(r.p));
                r.u8();
                r.u16a_u32();
                r.u32(); r.u32();
                r.u32();
                r.u8(); r.u8(); r.u8(); r.u8(); r.u8();
                r.fname(); r.u8();
            }
            auto data_weapon(R& r) -> void
            {
                r.ref();
                if (r.u8())
                {
                    r.u8();
                    weapon_state(r);
                }
                r.u16a_sid();
                r.u32();
                r.u16a_sid();
                r.u16a_sid();
                r.sidref(); r.sidref();
                r.u8(); r.u32(); r.u8(); r.u8();
                r.arr([&] { r.sidref(); r.sidref(); });
                r.u8();
            }
            auto data_type1(R& r) -> void
            {
                r.ref();
                r.f32();
                r.u16a_sid();
                r.u16a_sid();
                r.sidref();
            }
            auto poly(R& r, uint32_t pt) -> void
            {
                r.ref();
                switch (pt)
                {
                case 0: case 2: case 5: case 6: case 7: case 9: return;
                case 1: r.f32(); r.u32(); return;
                case 3: r.u32(); return;
                case 4: r.f32(); r.f32(); r.f32(); r.skip(24); r.u32(); r.u32(); r.u8(); r.u8(); return;
                case 8: r.skip(24); for (int i = 0; i < 4; ++i) r.skip(16); r.u8(); r.u32(); r.f32(); return;
                default: throw Unk("poly type " + hex8(pt) + " at " + hex8(r.p));
                }
            }
            auto data_type2(R& r) -> void
            {
                r.ref();
                r.u16a_u32();
                r.ref(); r.ref();
                r.u8s(4);
                r.u8();
                r.f32();
                if (r.u8()) poly(r, r.u32());
                if (r.u8()) r.skip(32);
                r.u8();
            }
            auto item_data(R& r, uint32_t t) -> void
            {
                switch (t)
                {
                case 2: data_type2(r); return;
                case 0: data_weapon(r); return;
                case 1: data_type1(r); return;
                case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 11: case 12: r.ref(); return;
                case 10: r.ref(); r.u8(); return;
                default: throw Unk("item data type " + hex8(t) + " at " + hex8(r.p));
                }
            }
            auto item(R& r, uint32_t& owner) -> void
            {
                if (r.u8())
                {
                    effects(r);
                    r.ref();
                    r.u32();
                }
                r.sidref();
                r.u16(); r.u16();
                owner = r.ref();
                r.u32(); r.u8(); r.u32(); r.u8(); r.u8();
                if (r.u8()) item_data(r, r.u32());
                r.u32(); r.u32();
            }
            auto container(R& r, ContX& x) -> void
            {
                r.u8();
                r.skip(12);
                x.u68 = r.u32();
                r.sidref();
                r.u16a_sid();
                r.sidref();
                r.u32(); r.u32();
                r.arr([&] { x.items.push_back(r.ref()); });
                r.arr([&] { x.items.push_back(r.ref()); r.u16(); r.u16(); });
                r.u16(); r.u16();
                r.u8(); r.u32();
                r.u16a_sid();
                r.skip(12);
                r.u32(); r.u8();
                if (r.u8())
                {
                    r.u16a_u32();
                    r.u8(); r.u32(); r.u64(); r.sidref();
                }
                r.ref();
                r.u32();
                r.skip(16); r.u32();
                r.u8(); r.u8();
            }
            auto marker(R& r, uint32_t& o38) -> void
            {
                r.skip(12);
                o38 = r.ref(K("mk_obj"));
                r.ref(K("ref4"));
                r.u32(); r.u8();
                uint32_t k = r.u32();
                switch (k)
                {
                case 0: r.u32(); break;
                case 1: r.sidref(); break;
                case 2: r.sidref(); r.u32(); break;
                case 3: r.skip(24); break;
                default: throw Unk("marker variant " + hex8(k) + " at " + hex8(r.p));
                }
                r.u8();
                r.bstr(); r.bstr();
                r.u8(); r.u8();
            }
        }

        namespace sd
        {
            auto vec(R& r) -> void { r.f32(); r.f32(); r.f32(); }
            auto destructible(R& r) -> void
            {
                r.u8();
                r.u32s(4);
                r.u32();
                vec(r);
                r.u8();
                r.sidref();
                r.u16(); r.u16(); r.u16();
                vec(r); vec(r);
                r.u8(); r.u8(); r.u8();
                r.u32();
            }
            auto c1_head(R& r) -> void { r.ref(K("an_c1_self")); r.u32(); }
            auto c1_td(R& r) -> void { c1_head(r); r.u32(); }
            auto e10c8e27(R& r) -> void
            {
                vec(r); vec(r);
                r.u32s(4);
                r.u8();
                for (int i = 0; i < 12; ++i) r.f32();
                r.u32s(4);
                r.u8(); r.u8();
            }
            auto comp1(R& r) -> void
            {
                if (!r.boolv()) return;
                uint32_t t = r.u32();
                switch (t)
                {
                case 1: c1_head(r); r.lst([&] { r.u32(); r.u32(); r.u32(); r.u8(); }); return;
                case 5: c1_head(r); r.u32(); r.u8(); return;
                case 0xd: case 0x26: c1_td(r); return;
                case 0x15: c1_head(r); r.lst([&] { r.u32s(5); }); return;
                case 0x1d:
                    c1_head(r);
                    r.u32(); vec(r); vec(r);
                    r.u32(); r.u32(); r.u32(); r.u8(); r.u8();
                    r.ref(K("hid_an_t1d_88"));
                    r.u32(); r.u32(); r.u32(); r.u8();
                    return;
                case 0x1f: c1_head(r); r.ref(K("hid_an_t1f_40")); r.u32(); return;
                case 0x27:
                    c1_head(r);
                    vec(r); vec(r);
                    r.u32s(4); r.u8();
                    vec(r); r.u32(); r.u8(); r.u32();
                    r.u32();
                    return;
                case 0x29:
                    c1_head(r);
                    r.arr([&] { e10c8e27(r); });
                    r.u32(); r.u16();
                    return;
                case 0x2b: c1_head(r); r.u32(); r.u8(); r.u8(); r.u32(); r.u8(); return;
                case 0: case 2: case 3: case 4: case 6: case 7: case 8: case 9: case 10: case 0xb: case 0xc: case 0xe: case 0xf: case 0x10:
                case 0x11: case 0x12: case 0x13: case 0x14: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c:
                case 0x1e: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x28: case 0x2a: case 0x2c:
                    c1_head(r);
                    return;
                default: throw Unk("anomaly comp1 type " + hex8(t) + " at " + hex8(r.p));
                }
            }
            auto c2_base(R& r) -> void
            {
                r.ref(K("an_c2_self"));
                r.lst([&] { r.ref(K("an_overlap_key")); r.u8(); r.ref(K("an_overlap_id1c")); });
                r.u8();
            }
            auto e952b46(R& r) -> void
            {
                r.u64(); r.u64(); r.u64();
                r.u32();
                r.lst([&] { r.u32(); });
                r.u8();
            }
            auto comp2(R& r) -> void
            {
                if (!r.boolv()) return;
                uint32_t t = r.u32();
                switch (t)
                {
                case 4:
                    c2_base(r);
                    r.lst([&] { r.u32(); vec(r); r.u32(); r.u8(); r.u8(); });
                    return;
                case 10:
                    c2_base(r);
                    r.arr([&] { vec(r); vec(r); r.u32(); r.u32(); r.u32(); });
                    vec(r);
                    r.ref(K("hid_an_c2ta_a8"));
                    return;
                case 0xb:
                    c2_base(r);
                    r.u32(); r.u32();
                    r.lst([&] { e952b46(r); });
                    r.lst([&] { e952b46(r); });
                    r.u8(); r.f32();
                    return;
                case 0xf:
                    c2_base(r);
                    r.ref(K("an_c2_t0f_container"));
                    return;
                case 0: case 1: case 2: case 3: case 5: case 6: case 7: case 8: case 9: case 0xc: case 0xd: case 0xe: case 0x10:
                case 0x11: case 0x12: case 0x13:
                    c2_base(r);
                    return;
                default: throw Unk("anomaly comp2 type " + hex8(t) + " at " + hex8(r.p));
                }
            }
            auto anomaly(R& r) -> void
            {
                r.u8();
                vec(r);
                r.sidref();
                r.u32s(4);
                r.u32();
                r.u16(); r.u16(); r.u16();
                vec(r);
                comp1(r);
                r.u8(); r.u8();
                comp2(r);
            }
            auto anom_rec(R& r) -> void
            {
                vec(r);
                r.skip(16); r.u32();
                r.u32();
            }
            auto art_rec(R& r) -> void
            {
                vec(r);
                r.skip(16); r.u32();
                r.sidref();
                r.f32();
                r.lst([&] { r.ref(K("artsp_spawned_item")); });
                r.f32();
                r.u8();
            }
            auto lair(R& r) -> void
            {
                r.skip(12);
                r.u16a_sid();
                r.ref(K("lair_squad"));
                r.u64();
                r.u32(); r.u32(); r.u32();
                r.skip(16);
                r.u32();
                r.u8();
                r.lst([&] { r.ref(K("lair_reuniting_unit")); });
                r.f32();
                r.u32();
                r.u8();
            }
            auto contextual(R& r) -> void
            {
                r.u8();
                r.skip(16);
                r.u32();
                r.skip(12);
                r.sidref();
                r.lst([&] { r.ref(K("ctx_slot_unit")); });
                r.u32();
                r.u8();
                r.ref(K("ctx_reserved_unit"));
                r.optref(K("ctx_slot0_unit_hid2"));
                r.lst([&] { r.ref(K("ctx_last_user_unit")); });
            }
            auto elem_body(R& r, uint32_t t) -> void
            {
                switch (t)
                {
                case 0: r.u8(); r.u8(); return;
                case 1: r.bstr(); r.bstr(); r.u32(); r.u8(); return;
                case 2: r.u8(); r.u32(); return;
                case 3: r.u32(); r.u8(); r.u8(); r.u8(); return;
                case 4: case 5: case 7: case 12: r.u32(); return;
                case 6: r.u32(); r.u8(); return;
                case 8: r.u8(); r.u8(); r.u8(); return;
                case 9: r.u32(); r.u32(); r.u32(); return;
                case 10:
                    r.arr([&] { r.sidref(); r.u8(); });
                    r.u32(); r.u32(); r.u8(); r.u8(); r.u32();
                    return;
                case 11: r.u8(); r.u8(); return;
                default: throw Unk("interactable elem type " + hex8(t) + " at " + hex8(r.p));
                }
            }
            auto interactable(R& r) -> void
            {
                r.skip(12);
                r.skip(16);
                r.u8(); r.u8();
                r.arr([&] {
                    if (!r.boolv()) return;
                    uint32_t t = r.u32();
                    r.skip(16);
                    elem_body(r, t);
                });
            }
            auto zone(R& r) -> void
            {
                r.skip(12);
                if (r.u8())
                {
                    uint32_t t = r.u32();
                    if (t > 10) throw Unk("zone shape type " + hex8(t) + " at " + hex8(r.p - 4));
                    r.u16a_blk(16);
                    r.skip(16);
                    r.u32();
                    r.ref(K("zone_self"));
                    switch (t)
                    {
                    case 0: r.sidref(); r.u8(); break;
                    case 2: case 8: r.sidref(); break;
                    case 4: r.ref(K("zone_t4_obj")); r.u8(); r.u32(); break;
                    case 7:
                        r.lst([&] { r.ref(K("zone_unit_list")); });
                        r.u16a_u64();
                        r.u32(); r.u32();
                        r.u16a_blk(16);
                        break;
                    default: break;
                    }
                }
                r.u8();
            }
            auto patrol(R& r) -> void
            {
                r.skip(12);
                r.arr([&] {
                    r.skip(24);
                    r.u16a_u64();
                    r.u64();
                    r.u8();
                    r.u32();
                });
                r.u16a_blk(24);
                r.lst([&] { r.ref(K("patrol_member_hid")); });
                r.optref(K("patrol_squad"));
                r.skip(16);
                r.u32();
                r.u8s(4);
                r.u32(); r.u32();
            }
            auto volume(R& r) -> void
            {
                vec(r);
                r.skip(16);
                r.u8();
                r.lst([&] { r.ref(K("vol_units_inside")); });
            }
        }

        namespace sq
        {
            auto v949ace(R& r, const std::string& k) -> void
            {
                uint32_t t = r.u32();
                switch (t)
                {
                case 0: return;
                case 1: r.ref(kd(k, "_v1")); return;
                case 2: r.skip(24); r.opt8(); return;
                case 3: r.skip(16); r.u8(); return;
                case 4: r.skip(16); r.u32(); r.skip(24); return;
                case 5: r.reflist(kd(k, "_v5_list")); r.ref(kd(k, "_v5_squad")); return;
                case 6: r.u32(); return;
                case 7: r.ref(kd(k, "_v7")); return;
                case 8:
                    r.u16a_u32();
                    r.arr([&] { r.u32(); r.u32(); r.u32(); r.u8(); r.u8(); });
                    r.skip(24);
                    return;
                default: throw Unk("949ace kind " + hex8(t) + " at " + hex8(r.p));
                }
            }
            auto b9470e3(R& r, const std::string& k) -> void
            {
                r.lst([&] { v949ace(r, k); });
                r.u32();
                r.opt32();
                r.opt32();
            }
            auto t949502(R& r) -> void
            {
                r.opt8(); r.opt8(); r.u8(); r.opt32();
                r.u8(); r.u8(); r.u8(); r.u8(); r.u8();
                r.u16a_u8();
                r.u8();
            }
            auto task(R& r, const std::string& k) -> void
            {
                uint32_t t = r.u32();
                t949502(r);
                switch (t)
                {
                case 0:
                    r.ref(kd(k, "_t0_hid2"));
                    r.u16a_blk(16);
                    r.skip(24); r.u32(); r.u8s(6);
                    return;
                case 1: r.u8(); r.u8(); r.u32(); r.u32(); r.skip(16); return;
                case 2: r.u8(); r.u8(); r.u32(); return;
                case 3: case 5: r.u8(); r.u8(); r.u8(); r.skip(24); return;
                case 4: r.u8(); r.u8(); r.u32(); r.skip(24); r.skip(16); r.u32(); return;
                case 6:
                    r.bstr(); r.skip(16); r.u32(); r.opt32(); r.u8(); r.fname(); r.u8(); r.skip(24); r.skip(24); r.u8();
                    return;
                case 7: case 8: case 9: case 17: return;
                case 10: r.u32(); r.skip(16); r.u16a_blk(16); r.u16a_fn(); return;
                case 11:
                    if (r.u8()) v949ace(r, k + "_t11");
                    r.u8(); r.skip(16); r.fname(); r.u16a_blk(24); r.boolvec(); r.u8(); r.skip(12); r.u8();
                    return;
                case 12:
                    r.fname(); r.u8(); r.u8(); r.u32();
                    for (int i = 0; i < 8; ++i) r.u16a_fn();
                    r.u16a_blk(24);
                    r.arr([&] { r.arr([&] { r.skip(16); r.u8(); }); });
                    return;
                case 13: r.u32(); r.skip(16); r.u8(); r.u8(); return;
                case 14: r.skip(24); r.skip(24); r.u8(); r.u32(); return;
                case 15: r.ref(kd(k, "_t15_hid2")); r.u8(); return;
                case 16: r.fname(); r.skip(16); r.u32(); r.u8(); return;
                case 18: r.skip(16); r.u16a_fn(); r.u16a_fn(); return;
                case 19: r.u32(); r.u8(); r.u32(); return;
                default: throw Unk("948d10 kind " + hex8(t) + " at " + hex8(r.p));
                }
            }
            auto b9478a3(R& r, const std::string& k) -> void
            {
                r.reflist(kd(k, "_230_list"));
                r.lst([&] { r.ref(kd(k, "_230_m10_unitkey")); r.skip(24); });
                r.lst([&] { r.ref(kd(k, "_230_s170_unit")); });
                r.boolvec();
                r.boolvec();
            }
            auto b945d41(R& r, const std::string& k) -> void
            {
                b9478a3(r, k);
                r.reflist(kd(k, "_88_list"));
                b9470e3(r, k + "_98");
                if (r.u8()) task(r, k + "_task");
                r.lst([&] { r.ref(kd(k, "_4f8_unitkey")); r.u32(); });
                r.lst([&] { r.ref(kd(k, "_4a8_unitkey")); r.u16a_u32(); });
                r.lst([&] { r.ref(kd(k, "_458_unit")); });
            }
            auto strat1(R& r) -> void
            {
                r.u8(); r.u8(); r.u8(); r.u8();
                r.ref(K("strat_hid_10"));
                r.ref(K("strat_hid_0"));
                r.u32();
                r.bstr();
                r.fname();
                r.u32(); r.u32(); r.u8(); r.u64();
            }
            auto squad(R& r) -> void
            {
                r.ref(K("sq_ref20"));
                r.lst([&] { r.ref(K("sq_member")); r.u8(); });
                r.u8();
                b945d41(r, "g290");
                r.u8();
                r.lst([&] { r.ref(K("sq_188_unitkey")); b945d41(r, "g188"); }, 4);
                r.u8();
                r.reflist(K("sq_70_list"));
                r.reflist(K("sq_80_list"));
                r.reflist(K("sq_90_list"));
                r.reflist(K("sq_a0_list"));
                r.ref(K("sq_b0"));
                r.ref(K("sq_170"));
                if (r.u8()) b9470e3(r, "t858");
                r.lst([&] { r.ref(K("sq_f8_a")); r.ref(K("sq_f8_b")); });
                r.u8(); r.u8(); r.u32(); r.u32();
                r.u16a_blk(24);
                if (r.boolv()) b945d41(r, "g850");
                r.u32();
                r.lst([&] { r.ref(K("sq_1e0_hid2")); b945d41(r, "g1e0"); });
                r.lst([&] { r.ref(K("sq_238_hid2")); b9470e3(r, "t238"); });
                r.u32();
                r.u8();
                if (r.ver() >= 0xBE) r.u32();
                if (r.u8())
                {
                    uint32_t k = r.u32();
                    if (k == 1) strat1(r);
                    else if (k != 0) throw Unk("strategy kind " + hex8(k) + " at " + hex8(r.p));
                }
                r.boolv();
            }
        }

        namespace sc
        {
            auto in_set(uint32_t t, std::initializer_list<uint32_t> s) -> bool
            {
                for (uint32_t x : s)
                    if (x == t) return true;
                return false;
            }
            auto is_ss(uint32_t t) -> bool { return in_set(t, {0, 8, 10, 0x7a, 0x7b, 0x89}); }
            auto is_s(uint32_t t) -> bool
            {
                if (t >= 0x25 && t <= 0x31) return true;
                return in_set(t, {2, 3, 4, 9, 0xc, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x19, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
                                  0x2c, 0x32, 0x34, 0x35, 0x38, 0x40, 0x42, 0x43, 0x44, 0x46, 0x47, 0x48, 0x4a, 0x50, 0x53, 0x5b, 0x61,
                                  0x64, 0x65, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x84, 0x86, 0x87});
            }
            auto is_su(uint32_t t) -> bool
            {
                return in_set(t, {5, 6, 7, 0xe, 0x17, 0x1a, 0x22, 0x23, 0x24, 0x33, 0x36, 0x39, 0x3a, 0x3b, 0x3d, 0x41, 0x49, 0x4b, 0x4c,
                                  0x4e, 0x4f, 0x51, 0x52, 0x54, 0x56, 0x57, 0x59, 0x5a, 0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x62, 0x63, 0x88});
            }
            auto is_sh(uint32_t t) -> bool
            {
                return in_set(t, {0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
                                  0x78, 0x79, 0x83});
            }
            auto unitmap(R& r, uint16_t k) -> void { r.lst([&] { r.ref(k); r.u32(); }); }
            auto node_body(R& r, uint32_t t) -> void
            {
                r.sidref();
                if (is_ss(t)) { r.sidref(); return; }
                if (is_s(t)) return;
                if (is_su(t)) { r.u8(); return; }
                if (is_sh(t)) { r.ref(K("sm_node_hid")); return; }
                switch (t)
                {
                case 1: r.u8(); r.u8(); return;
                case 0xb:
                    r.arr([&] { r.u32(); r.u32(); });
                    r.u16a_u8();
                    r.skip(8);
                    return;
                case 0xd: r.u8(); r.ref(K("sm_node_ref_b")); return;
                case 0xf:
                    r.u8(); r.u8();
                    if (r.boolv())
                    {
                        r.lst([&] { r.ref(K("sm_node_unit")); });
                        r.u8();
                    }
                    return;
                case 0x18: r.u8(); r.ref(K("sm_node_ref_e")); return;
                case 0x21:
                    if (r.boolv())
                    {
                        r.u8(); r.u32(); r.u8(); r.bstr();
                    }
                    return;
                case 0x37:
                    r.arr([&] { r.skip(16); r.u32(); });
                    r.arr([&] { r.u32(); r.lst([&] { r.ref(K("sm_n37_hidlist")); }); });
                    r.lst([&] { r.u32(); r.ref(K("sm_n37_hid")); r.u16a_u32(); });
                    r.lst([&] { r.ref(K("sm_n37_hid2a")); r.ref(K("sm_n37_hid2b")); });
                    return;
                case 0x3c: r.u8(); r.ref(K("sm_n3c_hid")); return;
                case 0x3e: r.u32(); r.u32(); return;
                case 0x3f: r.skip(8); return;
                case 0x45: r.u32(); return;
                case 0x4d: r.u8(); r.lst([&] { r.ref(K("sm_n4d_hidlist")); }); return;
                case 0x55: r.u8(); r.ref(K("sm_n55_squad")); unitmap(r, K("sm_n55_unit")); r.u8(); return;
                case 0x58:
                    r.u8(); r.u32(); r.u32();
                    r.ref(K("sm_n58_squad"));
                    unitmap(r, K("sm_n58_unit"));
                    if (r.boolv()) r.skip(24);
                    return;
                case 0x6f: r.ref(K("sm_node_hid")); r.u8(); return;
                case 0x85: r.u16a_u32(); return;
                default: throw Unk("node type " + hex8(t) + " at " + hex8(r.p));
                }
            }
            auto node(R& r) -> void
            {
                if (!r.u8()) return;
                uint32_t t = r.u32();
                node_body(r, t);
            }
            auto quest(R& r) -> void
            {
                r.sidref();
                r.arr([&] { r.sidref(); node(r); });
                r.u16a_sid();
                r.u16a_sid();
            }
            auto d640(R& r) -> void
            {
                r.arr([&] { r.sidref(); r.u32(); r.arr([&] { r.sidref(); r.u32(); r.u32(); r.u8(); }); });
            }
        }

        struct Ctx
        {
            Parsed& P;
            R r;
            Sec* S = nullptr;
        };

        auto rec_begin(Parsed& P, R& r, Rec& c) -> void
        {
            c.st = (uint32_t)r.p;
            c.t0 = (uint32_t)P.T.tags.size();
            c.l0 = (uint32_t)P.T.lsts.size();
        }
        auto rec_end(Parsed& P, R& r, Rec& c, Sec& S) -> void
        {
            c.end = (uint32_t)r.p;
            c.t1 = (uint32_t)P.T.tags.size();
            c.l1 = (uint32_t)P.T.lsts.size();
            S.recs.push_back(c);
        }

        template <class F>
        auto one_rec(Parsed& P, R& r, Sec& S, uint32_t idx, bool hasIdx, F&& f) -> void
        {
            Rec c;
            c.idx = idx;
            c.hasIdx = hasIdx;
            rec_begin(P, r, c);
            f();
            rec_end(P, r, c, S);
        }

        template <class F>
        auto pool_recs(Parsed& P, R& r, Sec& S, F&& f) -> void
        {
            uint32_t idx = 0;
            while (idx < S.cnt)
            {
                uint8_t pr = r.u8();
                if (pr == 0)
                {
                    ++idx;
                    continue;
                }
                if (pr != 1) throw Unk(std::string(kSecs[S.key].name) + " presence " + hex8(pr) + " at " + hex8(r.p - 1) + " idx " + std::to_string(idx));
                one_rec(P, r, S, idx, true, f);
                ++idx;
            }
        }

        auto parse_section(Parsed& P, int key, Sec& S) -> void
        {
            const SecDef& D = kSecs[key];
            S.key = key;
            bounds(P, D.name, S.s0, S.body, S.e0);
            R r{P.b, P.n, S.body, &P.T, &P.nt};
            if (D.pool)
            {
                S.cnt = r.u32();
                if (D.prefix)
                {
                    S.hasPref = true;
                    uint16_t n = r.u16();
                    S.pref.reserve(n);
                    for (uint16_t i = 0; i < n; ++i)
                    {
                        uint32_t a = r.u32();
                        uint32_t b = r.u32();
                        S.pref.push_back({a, b});
                    }
                }
            }
            switch (key)
            {
            case S_Unit: pool_recs(P, r, S, [&] { UnitX x; up::unit(r, x); S.ux.push_back(x); }); break;
            case S_Anomaly: pool_recs(P, r, S, [&] { sd::anomaly(r); }); break;
            case S_AnomalySpawner: pool_recs(P, r, S, [&] { sd::anom_rec(r); }); break;
            case S_ArtifactSpawner: pool_recs(P, r, S, [&] { sd::art_rec(r); }); break;
            case S_Lair: pool_recs(P, r, S, [&] { sd::lair(r); }); break;
            case S_Marker: pool_recs(P, r, S, [&] { uint32_t o = 0; is::marker(r, o); S.own.push_back(o); }); break;
            case S_Squad: pool_recs(P, r, S, [&] { sq::squad(r); }); break;
            case S_Item: pool_recs(P, r, S, [&] { uint32_t o = 0; is::item(r, o); S.own.push_back(o); }); break;
            case S_Container: pool_recs(P, r, S, [&] { ContX x; is::container(r, x); S.cx.push_back(std::move(x)); }); break;
            case S_ContextualAction: pool_recs(P, r, S, [&] { sd::contextual(r); }); break;
            case S_Zone: pool_recs(P, r, S, [&] { sd::zone(r); }); break;
            case S_Destructible: pool_recs(P, r, S, [&] { sd::destructible(r); }); break;
            case S_Patrol: pool_recs(P, r, S, [&] { sd::patrol(r); }); break;
            case S_Interactable: pool_recs(P, r, S, [&] { sd::interactable(r); }); break;
            case S_VolumeForEffects: pool_recs(P, r, S, [&] { sd::volume(r); }); break;
            case S_Quicksilver:
            {
                pool_recs(P, r, S, [&] {
                    r.lst([&] {
                        if (!r.boolv()) return;
                        r.u32();
                        r.ref(K("qs_point_unit"));
                        r.skip(24);
                    });
                    r.skip(12);
                    r.u32();
                    r.arr([&] { r.lst([&] { r.ref(K("qs_group_unit")); }); });
                    r.skip(16);
                    r.u32();
                });
                auto tail = [&](auto&& f) { one_rec(P, r, S, 0, false, f); };
                tail([&] {
                    r.s64("a1_hdr");
                    r.lst([&] { r.s64("a1_node_key"); r.fname(); r.s64("a1_node_v20"); r.u8(); r.s64("a1_node_parent"); }, 4);
                    r.lst([&] { r.s64("a1_pair_a"); r.s64("a1_pair_b"); r.s64("a1_pair_c"); r.u32(); r.u32(); r.u8(); }, 4);
                    r.u32();
                    r.s64("a1_b78");
                    r.u8();
                    r.lst([&] { r.s64("a1_set"); });
                });
                tail([&] {
                    r.u32(); r.u32();
                    uint32_t n = r.u32();
                    for (uint32_t i = 0; i < n; ++i) { r.u64(); r.u32(); }
                });
                tail([&] { r.lst([&] { r.ref(K("a3_unit")); r.u32(); }); });
                tail([&] { r.lst([&] { r.ref(K("a4_unit")); }); });
                tail([&] { r.arr([&] { r.u64(); r.u64(); }); });
                tail([&] {
                    r.lst([&] {
                        if (!r.boolv()) return;
                        uint32_t t = r.u32();
                        r.u8();
                        r.s64("a6_q10");
                        if (r.boolv()) r.ref(K("pb_h18_unit"));
                        r.s64("a6_q30");
                        if (r.boolv()) r.ref(K("pb_h38_unit"));
                        r.u32(); r.u32();
                        r.lst([&] { r.ref(K("pb_unit")); });
                        if (t == 0 || t == 6 || t == 7) r.ref(K("pb_o30_unit"));
                        if (t == 6)
                        {
                            r.u8(); r.u32();
                            if (r.boolv()) r.sidref();
                        }
                        else if (t > 7) throw Unk("pb type " + hex8(t) + " at " + hex8(r.p));
                    });
                });
                tail([&] { r.u32(); });
                tail([&] { r.u16a_blk(16); });
                tail([&] {
                    r.arr([&] { r.fname(); r.sidref(); });
                    r.arr([&] { r.skip(16); r.sidref(); });
                    r.lst([&] { r.ref(K("d_n3_h2")); r.sidref(); });
                    r.arr([&] { r.skip(16); r.u8(); });
                    for (int i = 0; i < 64; ++i)
                    {
                        if (!r.boolv()) continue;
                        r.u8();
                        for (int j = 0; j < 2; ++j)
                            if (r.boolv()) r.u8();
                        r.arr([&] { r.u64(); r.u8(); });
                        r.lst([&] { r.ref(K("d_slot_h2")); r.u8(); });
                        r.u8(); r.u8();
                    }
                    r.arr([&] { r.u32(); r.u16a_u32(); });
                });
                tail([&] {
                    r.arr([&] { r.skip(16); if (r.boolv()) { r.skip(16); r.u32(); r.ref(K("e1_lair")); } });
                    r.arr([&] { r.skip(16); if (r.boolv()) { r.skip(16); r.u32(); r.ref(K("e2_lair")); } });
                    r.arr([&] {
                        r.skip(16);
                        if (r.boolv())
                        {
                            r.skip(16); r.skip(16); r.u32(); r.fname();
                            r.lst([&] { r.ref(K("e3_h2a")); r.ref(K("e3_h2b")); });
                        }
                    });
                });
                tail([&] {
                    r.lst([&] {
                        r.u8();
                        for (int i = 0; i < 5; ++i) r.ref(kind("f_h2_" + std::to_string(i)));
                        r.u8(); r.u32();
                    });
                });
                break;
            }
            case S_GlobalVariable:
                one_rec(P, r, S, 0, true, [&] {
                    r.arr([&] {
                        r.fname();
                        uint32_t t = r.u32();
                        if (t == 0) r.u32();
                        else if (t == 1) r.sidref();
                        else if (t == 2) r.u8();
                        else throw Unk("globalvar type " + hex8(t) + " at " + hex8(r.p - 4));
                    });
                });
                break;
            case S_Population:
                one_rec(P, r, S, 0, true, [&] {
                    r.u32();
                    r.arr([&] { r.u64(); r.u8(); r.u32(); r.u32(); r.u64(); r.u64(); r.u64(); });
                    r.lst([&] { r.ref(K("hid2_pop150")); });
                    r.arr([&] { r.skip(16); r.u32(); });
                    r.lst([&] { r.skip(16); r.u32(); r.u64(); r.hid(); });
                    r.lst([&] { r.ref(K("model_pop_a8")); });
                });
                break;
            case S_Flashlight:
                one_rec(P, r, S, 0, true, [&] {
                    r.lst([&] {
                        r.ref(K("model_flash"));
                        r.arr([&] {
                            r.u8();
                            if (r.u8()) r.fname();
                            r.skip(24);
                            uint32_t n = r.u32();
                            for (uint32_t i = 0; i < n; ++i) { r.u32(); r.u32(); }
                        });
                        r.u32();
                        r.u8();
                    });
                });
                break;
            case S_WorldMap:
                one_rec(P, r, S, 0, true, [&] {
                    r.lst([&] { r.ref(K("marker_wm180")); });
                    r.arr([&] { r.u8(); r.u8(); });
                    r.lst([&] { r.ref(K("hid2_wm230")); r.u16a_fn(); });
                });
                break;
            case S_Director:
                one_rec(P, r, S, 0, true, [&] {
                    r.lst([&] { r.ref(K("hid2_dir148")); r.sidref(); r.u8(); });
                    if (r.u8()) { r.sidref(); r.sidref(); r.u8(); }
                    r.sidref();
                    r.u8();
                    r.ref(K("hid2_dir1dc"));
                    r.u16a_blk(32);
                    r.u32();
                    r.arr([&] { r.sidref(); r.sidref(); r.u32(); r.u8(); r.u8(); });
                    r.arr([&] { r.sidref(); r.u32(); });
                    r.lst([&] { r.ref(K("hid2_dira8")); r.arr([&] { r.fname(); r.u8(); }); });
                    r.f32();
                    r.u8();
                });
                break;
            case S_Teleport:
                one_rec(P, r, S, 0, true, [&] {
                    r.arr([&] { r.sidref(); r.u16a_sid(); });
                });
                one_rec(P, r, S, 1, true, [&] {
                    uint16_t n = r.u16();
                    if (n) throw Unk("fe5700 map nonzero " + hex8(n) + " at " + hex8(r.p - 2));
                    r.arr([&] { r.fname(); r.u8(); });
                    r.u8(); r.u8();
                    r.arr([&] { r.u8(); r.u32s(5); r.u8(); r.u32(); r.u32(); r.u8(); });
                    r.u32s(4);
                    r.arr([&] { r.u8(); r.u32(); });
                    r.sidref();
                    r.u32(); r.u32(); r.u32();
                    r.skip(16); r.u32(); r.u8(); r.skip(16);
                    r.u8();
                    r.lst([&] { r.ref(K("hid_p490")); });
                    r.lst([&] { r.ref(K("hid_p4a0")); });
                    r.ref(K("hid_p668"));
                    r.u8();
                    r.u32();
                    r.skip(10);
                    r.arr([&] {
                        r.skip(16); r.u64();
                        r.u16a_u8(); r.u8(); r.u8(); r.u8(); r.u32(); r.u8(); r.sidref();
                    });
                    r.arr([&] { r.skip(16); r.u64(); r.u8(); });
                    r.arr([&] { r.skip(16); r.u64(); r.u8(); });
                    r.arr([&] { r.skip(16); r.u64(); });
                });
                break;
            case S_Bark:
                one_rec(P, r, S, 0, true, [&] {
                    r.arr([&] { r.f32(); r.u8(); r.lst([&] { r.ref(K("bark_queue_unit_hid")); }); });
                    const int nb = r.ver() >= 0xBD ? 67 : 64;
                    for (int i = 0; i < nb; ++i) { r.f32(); r.f32(); r.f32(); }
                    r.arr([&] { r.u64(); r.u16a_fn(); });
                    r.arr([&] { r.u64(); r.u16a_fn(); });
                    for (int i = 0; i < nb; ++i) r.lst([&] { r.ref(K("bark_type_unit_list")); });
                    for (int i = 0; i < nb; ++i) r.u64();
                    r.lst([&] { r.ref(K("bark_speaker_cooldown_unit")); r.f32(); });
                    r.arr([&] { r.fname(); r.f32(); });
                    r.arr([&] { r.sidref(); r.f32(); });
                    r.f32();
                    r.u8();
                });
                break;
            case S_Trigger:
            {
                uint16_t n = r.u16();
                for (uint16_t i = 0; i < n; ++i)
                    one_rec(P, r, S, i, true, [&] {
                        r.skip(16);
                        if (r.boolv())
                        {
                            r.skip(16);
                            r.sidref();
                            r.lst([&] { r.ref(K("trig_unit")); });
                            r.u8();
                            r.lst([&] { r.ref(K("trig_obj")); });
                            r.u32();
                        }
                    });
                break;
            }
            case S_Scenario:
            {
                auto rec = [&](uint32_t i, auto&& f) { one_rec(P, r, S, i, false, f); };
                rec(0, [&] { r.lst([&] { r.skip(16); r.ref(K("sm_guid_model")); }, 4); });
                rec(1, [&] { r.arr([&] { r.sidref(); sc::quest(r); }); });
                rec(2, [&] {
                    r.arr([&] {
                        r.sidref();
                        r.sidref(); r.u8();
                        r.arr([&] {
                            r.u32();
                            r.u32(); r.u32(); r.sidref();
                            r.lst([&] { r.ref(K("sm_398_hidlist")); });
                            r.u8(); r.u8(); r.u8();
                        });
                        r.u8(); r.u8(); r.u32(); r.u32();
                    });
                });
                rec(3, [&] { r.arr([&] { r.sidref(); r.u16a_sid(); }); });
                rec(4, [&] { r.arr([&] { r.sidref(); r.u8(); r.u8(); }); });
                rec(5, [&] { r.arr([&] { r.sidref(); sc::d640(r); }); });
                rec(6, [&] { r.arr([&] { r.sidref(); sc::d640(r); }); });
                rec(7, [&] { r.arr([&] { r.sidref(); r.arr([&] { r.sidref(); r.u8(); }); }); });
                rec(8, [&] { r.arr([&] { r.sidref(); r.arr([&] { r.sidref(); r.u8(); }); }); });
                rec(9, [&] { r.sidref(); r.sidref(); r.u8(); });
                rec(10, [&] { r.lst([&] { r.skip(16); r.ref(K("sm_gset_hid")); }); });
                rec(11, [&] { r.arr([&] { r.sidref(); r.u8(); r.sidref(); }); });
                rec(12, [&] { r.u16a_sid(); r.u16a_sid(); });
                rec(13, [&] {
                    uint32_t n = r.u32();
                    for (uint32_t i = 0; i < n; ++i) r.skip(16);
                });
                rec(14, [&] {
                    uint32_t n = r.u32();
                    for (uint32_t i = 0; i < n; ++i)
                    {
                        r.skip(16);
                        r.arr([&] { r.fname(); r.skip(8); });
                    }
                });
                break;
            }
            default: throw Unk("no parser for section " + std::string(D.key));
            }
            S.end = r.p;
            if (S.end != S.e0) throw Unk(std::string(D.name) + " parse end " + hex8(S.end) + " != " + hex8(S.e0));
        }

        auto parse_all(const std::string& raw, Parsed& P) -> void
        {
            P.b = (const uint8_t*)raw.data();
            P.n = raw.size();
            if (P.n < 16) throw Unk("raw too short");
            std::memcpy(&P.nto, P.b + 5, 4);
            if (P.nto >= P.n) throw Unk("name table offset past end");
            read_names(P);
            P.hdr = find_headers(P.b, P.n, P.nto);
            P.secs.resize(S_Count);
            for (int k = 0; k < S_Count; ++k) parse_section(P, k, P.secs[k]);
        }

        struct Removal
        {
            std::unordered_set<uint32_t> U, cont, item, marker, squad;
            std::unordered_set<uint64_t> subj;
            std::unordered_set<uint32_t> ids;
        };

        auto rd32(const Parsed& P, uint32_t pos) -> uint32_t
        {
            uint32_t v;
            std::memcpy(&v, P.b + pos, 4);
            return v;
        }
        auto rd64(const Parsed& P, uint32_t pos) -> uint64_t
        {
            uint64_t v;
            std::memcpy(&v, P.b + pos, 8);
            return v;
        }

        auto removal(const Parsed& P, Removal& rm) -> void
        {
            const Sec& SU = P.secs[S_Unit];
            std::unordered_set<uint32_t> squads, conts;
            for (size_t i = 0; i < SU.recs.size(); ++i)
                if (SU.ux[i].puppet) rm.U.insert(SU.recs[i].idx);
            for (size_t i = 0; i < SU.recs.size(); ++i)
            {
                if (!SU.ux[i].puppet) continue;
                const UnitX& x = SU.ux[i];
                rm.subj.insert(x.q140);
                if (x.ref58 != kNull) squads.insert(x.ref58);
                if (x.ref90 != kNull) squads.insert(x.ref90);
                if (x.hasInv && x.inv != kNull) conts.insert(x.inv);
                if (x.hasRef18 && x.ref18 != kNull) conts.insert(x.ref18);
            }
            const Sec& SC = P.secs[S_Container];
            for (size_t i = 0; i < SC.recs.size(); ++i)
                if (rm.U.count(SC.cx[i].u68)) conts.insert((7u << 27) | SC.recs[i].idx);
            for (uint32_t v : conts)
                if (v >> 27 == 7) rm.cont.insert(v & kMask);
            std::unordered_set<uint32_t> items;
            for (size_t i = 0; i < SC.recs.size(); ++i)
                if (rm.cont.count(SC.recs[i].idx))
                    for (uint32_t g : SC.cx[i].items) items.insert(g);
            const Sec& SI = P.secs[S_Item];
            for (size_t i = 0; i < SI.recs.size(); ++i)
            {
                uint32_t o = SI.own[i];
                if ((o >> 27 == 7 && rm.cont.count(o & kMask)) || (o >> 27 == 0 && rm.U.count(o))) items.insert((6u << 27) | SI.recs[i].idx);
            }
            bool grow = true;
            while (grow)
            {
                grow = false;
                for (size_t i = 0; i < SI.recs.size(); ++i)
                {
                    uint32_t iid = (6u << 27) | SI.recs[i].idx;
                    if (!items.count(iid) && items.count(SI.own[i]))
                    {
                        items.insert(iid);
                        grow = true;
                    }
                }
            }
            for (uint32_t v : items)
                if (v >> 27 == 6) rm.item.insert(v & kMask);
            const Sec& SM = P.secs[S_Marker];
            for (size_t i = 0; i < SM.recs.size(); ++i)
                if (rm.U.count(SM.own[i])) rm.marker.insert(SM.recs[i].idx);
            const Sec& SQ = P.secs[S_Squad];
            std::unordered_map<uint32_t, size_t> sqIdx;
            for (size_t i = 0; i < SQ.recs.size(); ++i) sqIdx[SQ.recs[i].idx] = i;
            for (uint32_t v : squads)
            {
                if (v >> 27 != 5) continue;
                auto it = sqIdx.find(v & kMask);
                if (it == sqIdx.end()) continue;
                const Rec& d = SQ.recs[it->second];
                bool ok = true;
                for (uint32_t t = d.t0; t < d.t1 && ok; ++t)
                {
                    uint32_t x = rd32(P, P.T.tags[t].pos);
                    if (x >> 27 == 0)
                    {
                        if (!rm.U.count(x)) ok = false;
                    }
                    else if (x < 0xfffffff0u) ok = false;
                }
                if (ok) rm.squad.insert(v & kMask);
            }
            for (uint32_t i : rm.U) rm.ids.insert(i);
            for (uint32_t i : rm.marker) rm.ids.insert((4u << 27) | i);
            for (uint32_t i : rm.squad) rm.ids.insert((5u << 27) | i);
            for (uint32_t i : rm.item) rm.ids.insert((6u << 27) | i);
            for (uint32_t i : rm.cont) rm.ids.insert((7u << 27) | i);
        }

        enum Rule : int { R_None = 0, R_Null, R_Del };

        auto rule_of(int key, const std::string& kn) -> Rule
        {
            struct Row
            {
                int key;
                const char* kind;
                Rule rule;
            };
            static const Row rows[] = {
                {S_Unit, "brain_r550", R_Null}, {S_Unit, "u470_hid", R_Null}, {S_Unit, "brain_p50k1_unit", R_Del},
                {S_Zone, "zone_unit_list", R_Del}, {S_Patrol, "patrol_member_hid", R_Null},
                {S_ContextualAction, "ctx_slot_unit", R_Null}, {S_ContextualAction, "ctx_reserved_unit", R_Null},
                {S_ContextualAction, "ctx_last_user_unit", R_Null}, {S_Trigger, "trig_unit", R_Null},
                {S_Scenario, "sm_guid_model", R_Null}, {S_Scenario, "sm_n55_squad", R_Null}, {S_Scenario, "sm_n58_squad", R_Null},
                {S_Scenario, "sm_n55_unit", R_Del}, {S_Scenario, "sm_n58_unit", R_Del},
                {S_Anomaly, "an_overlap_key", R_Del}, {S_Anomaly, "an_overlap_id1c", R_Del},
                {S_Quicksilver, "a3_unit", R_Del}, {S_Quicksilver, "a4_unit", R_Del},
                {S_Flashlight, "model_flash", R_Del}, {S_WorldMap, "marker_wm180", R_Del},
                {S_ArtifactSpawner, "artsp_spawned_item", R_Del}, {S_VolumeForEffects, "vol_units_inside", R_Del},
                {S_Bark, "bark_speaker_cooldown_unit", R_Del}, {S_Bark, "bark_type_unit_list", R_Del}, {S_Bark, "bark_queue_unit_hid", R_Null},
                {S_Quicksilver, "s64_a1_node_key", R_Del}, {S_Quicksilver, "s64_a1_pair_a", R_Del}, {S_Quicksilver, "s64_a1_pair_b", R_Del},
                {S_Quicksilver, "s64_a6_q10", R_Del}, {S_Quicksilver, "s64_a6_q30", R_Del},
            };
            for (const Row& w : rows)
                if (w.key == key && kn == w.kind) return w.rule;
            return R_None;
        }

        struct Edit
        {
            bool del;
            uint32_t s, e, cp;
            uint8_t w;
        };
        struct Pend
        {
            const char* what;
            int key;
            uint32_t idx;
            std::string kn;
            uint32_t pos;
            uint64_t val;
        };
        struct Op
        {
            uint32_t s, e;
            uint32_t val;
            bool hasVal;
        };

        auto plan_edits(const Parsed& P, const Removal& rm, const std::vector<std::string>& kn, std::vector<Edit>& edits, Report& rep, std::vector<Pend>& unknown) -> void
        {
            std::vector<uint8_t> bloom(1 << 16, 0);
            for (uint32_t v : rm.ids) bloom[v & 0xffff] = 1;
            std::unordered_set<uint32_t> dropSet[S_Count];
            dropSet[S_Unit] = rm.U;
            dropSet[S_Marker] = rm.marker;
            dropSet[S_Squad] = rm.squad;
            dropSet[S_Item] = rm.item;
            dropSet[S_Container] = rm.cont;
            std::vector<Pend> pend;
            for (int key = 0; key < S_Count; ++key)
            {
                const Sec& S = P.secs[key];
                bool pool = kSecs[key].pool;
                for (const Rec& d : S.recs)
                {
                    if (pool && d.hasIdx && dropSet[key].count(d.idx)) continue;
                    for (uint32_t ti = d.t0; ti < d.t1; ++ti)
                    {
                        const Tag& t = P.T.tags[ti];
                        const std::string& k = kn[t.kind];
                        bool s64 = k.compare(0, 4, "s64_") == 0;
                        uint64_t v;
                        if (s64)
                        {
                            v = rd64(P, t.pos);
                            if (!rm.subj.count(v)) continue;
                        }
                        else
                        {
                            uint32_t w = rd32(P, t.pos);
                            if (!bloom[w & 0xffff] || !rm.ids.count(w)) continue;
                            v = w;
                        }
                        Rule rule = rule_of(key, k);
                        uint32_t idx = d.hasIdx ? d.idx : 0xffffffffu;
                        if (rule == R_Null)
                        {
                            edits.push_back({false, t.pos, t.pos + 4, 0, 0});
                            rep.fixes[{"null", kSecs[key].key, k}]++;
                            rep.fixTotal++;
                        }
                        else if (rule == R_Del)
                        {
                            bool have = false;
                            Span bs{0, 0};
                            uint32_t bcp = 0;
                            uint8_t bw = 0;
                            for (uint32_t li = d.l0; li < d.l1; ++li)
                            {
                                const Lst& L = P.T.lsts[li];
                                for (uint32_t si = 0; si < L.n; ++si)
                                {
                                    const Span& sp = P.T.spans[L.s0 + si];
                                    if (sp.s <= t.pos && t.pos < sp.e && (!have || sp.e - sp.s < bs.e - bs.s))
                                    {
                                        have = true;
                                        bs = sp;
                                        bcp = L.cp;
                                        bw = L.w;
                                    }
                                }
                            }
                            if (!have)
                            {
                                pend.push_back({"del-without-list", key, idx, k, t.pos, v});
                                continue;
                            }
                            edits.push_back({true, bs.s, bs.e, bcp, bw});
                            rep.fixes[{"del-elem", kSecs[key].key, k}]++;
                            rep.fixTotal++;
                        }
                        else pend.push_back({"no-rule", key, idx, k, t.pos, v});
                    }
                }
            }
            std::vector<Span> dels;
            for (const Edit& e : edits)
                if (e.del) dels.push_back({e.s, e.e});
            std::sort(dels.begin(), dels.end(), [](const Span& a, const Span& b) { return a.s < b.s || (a.s == b.s && a.e > b.e); });
            std::vector<Span> uni;
            for (const Span& s : dels)
            {
                if (!uni.empty() && s.s < uni.back().e)
                {
                    if (s.e > uni.back().e) uni.back().e = s.e;
                }
                else uni.push_back(s);
            }
            for (const Pend& u : pend)
            {
                auto it = std::upper_bound(uni.begin(), uni.end(), u.pos, [](uint32_t p, const Span& s) { return p < s.s; });
                bool in = false;
                if (it != uni.begin())
                {
                    --it;
                    in = it->s <= u.pos && u.pos < it->e;
                }
                if (in)
                {
                    rep.fixes[{"in-deleted-elem", kSecs[u.key].key, u.kn}]++;
                    rep.fixTotal++;
                }
                else unknown.push_back(u);
            }
        }

        auto norm_edits(const Parsed& P, const std::vector<Edit>& edits, std::vector<Op>& ops) -> void
        {
            struct Del
            {
                uint32_t s, e, cp;
                uint8_t w;
            };
            std::vector<Del> dels;
            std::vector<uint32_t> ws;
            for (const Edit& e : edits)
            {
                if (e.del) dels.push_back({e.s, e.e, e.cp, e.w});
                else ws.push_back(e.s);
            }
            std::sort(dels.begin(), dels.end(), [](const Del& a, const Del& b) {
                if (a.s != b.s) return a.s < b.s;
                if (a.e != b.e) return a.e > b.e;
                if (a.cp != b.cp) return a.cp < b.cp;
                return a.w < b.w;
            });
            dels.erase(std::unique(dels.begin(), dels.end(), [](const Del& a, const Del& b) { return a.s == b.s && a.e == b.e && a.cp == b.cp && a.w == b.w; }), dels.end());
            std::vector<Del> keep;
            uint32_t maxEnd = 0;
            bool any = false;
            size_t i = 0;
            while (i < dels.size())
            {
                size_t j = i;
                while (j < dels.size() && dels[j].s == dels[i].s && dels[j].e == dels[i].e) ++j;
                bool contained = any && maxEnd >= dels[i].e;
                if (!contained)
                    for (size_t q = i; q < j; ++q) keep.push_back(dels[q]);
                if (!any || dels[i].e > maxEnd) maxEnd = dels[i].e;
                any = true;
                i = j;
            }
            auto inKeep = [&](uint32_t p) -> bool {
                auto it = std::upper_bound(keep.begin(), keep.end(), p, [](uint32_t v, const Del& d) { return v < d.s; });
                if (it == keep.begin()) return false;
                --it;
                return it->s <= p && p < it->e;
            };
            for (const Del& d : keep) ops.push_back({d.s, d.e, 0, false});
            std::sort(ws.begin(), ws.end());
            ws.erase(std::unique(ws.begin(), ws.end()), ws.end());
            for (uint32_t w : ws)
                if (!inKeep(w)) ops.push_back({w, w + 4, kNull, true});
            std::map<std::pair<uint32_t, uint8_t>, uint32_t> cnt;
            for (const Del& d : keep) cnt[{d.cp, d.w}]++;
            for (auto& kv : cnt)
            {
                uint32_t cp = kv.first.first;
                uint8_t w = kv.first.second;
                if (inKeep(cp)) continue;
                uint32_t old = 0;
                if (w == 2)
                {
                    uint16_t o;
                    std::memcpy(&o, P.b + cp, 2);
                    old = o;
                }
                else std::memcpy(&old, P.b + cp, 4);
                if (old < kv.second) throw Unk("count underflow at " + hex8(cp));
                ops.push_back({cp, cp + w, old - kv.second, true});
            }
            std::sort(ops.begin(), ops.end(), [](const Op& a, const Op& b) { return a.s != b.s ? a.s < b.s : a.e < b.e; });
        }

        auto op_width(const Op& o, uint32_t w) -> bool
        {
            return o.hasVal && o.e - o.s == w;
        }

        auto apply_local(const Parsed& P, const std::vector<Op>& ops, uint32_t lo, uint32_t hi, std::string& out) -> void
        {
            auto it = std::lower_bound(ops.begin(), ops.end(), lo, [](const Op& o, uint32_t v) { return o.s < v; });
            uint32_t q = lo;
            for (; it != ops.end() && it->s < hi; ++it)
            {
                if (it->e > hi) throw Unk("edit crosses range " + hex8(it->s));
                if (it->s < q) throw Unk("overlapping edits at " + hex8(it->s));
                out.append((const char*)P.b + q, it->s - q);
                if (it->hasVal)
                {
                    if (op_width(*it, 2))
                    {
                        uint16_t v = (uint16_t)it->val;
                        out.append((const char*)&v, 2);
                    }
                    else
                    {
                        uint32_t v = it->val;
                        out.append((const char*)&v, 4);
                    }
                }
                q = it->e;
            }
            out.append((const char*)P.b + q, hi - q);
        }

        auto rebuild_pool(const Parsed& P, const Sec& S, const std::unordered_set<uint32_t>& drop, const std::vector<Op>& ops, std::string& out) -> void
        {
            const char* base = (const char*)P.b;
            out.append(base + S.s0, S.body - S.s0);
            std::vector<const Rec*> keep;
            const Rec* tailFirst = nullptr;
            for (const Rec& d : S.recs)
            {
                if (!d.hasIdx)
                {
                    if (!tailFirst || d.st < tailFirst->st) tailFirst = &d;
                    continue;
                }
                if (!drop.count(d.idx)) keep.push_back(&d);
            }
            uint32_t newcnt = 0;
            for (const Rec* d : keep) newcnt = std::max(newcnt, d->idx + 1);
            out.append((const char*)&newcnt, 4);
            if (S.hasPref)
            {
                int t = -1;
                if (S.key == S_Unit) t = 0;
                else if (S.key == S_Marker) t = 4;
                else if (S.key == S_Squad) t = 5;
                else if (S.key == S_Item) t = 6;
                else if (S.key == S_Container) t = 7;
                std::vector<std::pair<uint32_t, uint32_t>> kp;
                for (auto& kv : S.pref)
                    if (!(t >= 0 && (int)(kv.first >> 27) == t && drop.count(kv.first & kMask))) kp.push_back(kv);
                uint16_t n = (uint16_t)kp.size();
                out.append((const char*)&n, 2);
                for (auto& kv : kp)
                {
                    out.append((const char*)&kv.first, 4);
                    out.append((const char*)&kv.second, 4);
                }
            }
            uint32_t next = 0;
            for (const Rec* d : keep)
            {
                if (d->idx > next) out.append(d->idx - next, '\0');
                out.push_back('\1');
                apply_local(P, ops, d->st, d->end, out);
                next = d->idx + 1;
            }
            uint32_t tailStart = tailFirst ? tailFirst->st : S.e0;
            apply_local(P, ops, tailStart, S.e0, out);
        }

        auto residual(const Parsed& O, const Removal& rm, const std::unordered_set<uint64_t>& subj, std::vector<std::string>& bad, std::vector<std::string>& und, const std::vector<std::string>& kn) -> void
        {
            std::vector<Tag> tagged = O.T.tags;
            std::stable_sort(tagged.begin(), tagged.end(), [](const Tag& a, const Tag& b) { return a.pos < b.pos; });
            std::vector<Tag> uniq;
            for (size_t i = 0; i < tagged.size(); ++i)
                if (i + 1 == tagged.size() || tagged[i + 1].pos != tagged[i].pos) uniq.push_back(tagged[i]);
            for (const Tag& t : uniq)
                if (kn[t.kind].compare(0, 4, "s64_") == 0 && subj.count(rd64(O, t.pos)))
                    bad.push_back("subj " + hex8(t.pos) + " " + kn[t.kind]);
            std::vector<uint8_t> bloom(1 << 16, 0);
            for (uint32_t v : rm.ids) bloom[v & 0xffff] = 1;
            std::unordered_set<std::string> decoded;
            for (int k = 0; k < S_Count; ++k) decoded.insert(kSecs[k].name);
            size_t total = O.n;
            for (size_t p = 0; p + 4 <= total; ++p)
            {
                uint32_t v;
                std::memcpy(&v, O.b + p, 4);
                if (!bloom[v & 0xffff] || !rm.ids.count(v)) continue;
                auto tg = std::lower_bound(uniq.begin(), uniq.end(), (uint32_t)p, [](const Tag& t, uint32_t x) { return t.pos < x; });
                if (tg != uniq.end() && tg->pos == p && kn[tg->kind].compare(0, 4, "s64_") != 0)
                    bad.push_back(hex8(v) + " " + hex8(p) + " " + kn[tg->kind]);
                std::string w = "HEADER";
                uint32_t hs = 0;
                bool haveHs = false;
                if (p >= O.nto)
                {
                    w = "NAMETABLES";
                    if (!O.hdr.empty())
                    {
                        hs = O.hdr.back().s;
                        haveHs = true;
                    }
                }
                else
                {
                    auto it = std::upper_bound(O.hdr.begin(), O.hdr.end(), (uint32_t)p, [](uint32_t x, const SecHdr& h) { return x < h.s; });
                    if (it != O.hdr.begin())
                    {
                        --it;
                        w = it->name;
                        hs = it->s;
                        haveHs = true;
                    }
                }
                if (decoded.count(w)) continue;
                if (!haveHs || p >= hs + 2 + w.size()) und.push_back(w + " " + hex8(p));
            }
        }
    }

    auto Report::summary() const -> std::string
    {
        char b[160];
        std::snprintf(b, sizeof b, "stripped U%u C%u I%u M%u S%u, %u fixes", removed[0], removed[1], removed[2], removed[3], removed[4], fixTotal);
        return b;
    }

    auto strip(const std::string& raw, Report& rep, std::string& err) -> int
    {
        try
        {
            rep = Report();
            rep.inSize = raw.size();
            Parsed P;
            parse_all(raw, P);
            Removal rm;
            removal(P, rm);
            rep.removed[0] = (uint32_t)rm.U.size();
            rep.removed[1] = (uint32_t)rm.cont.size();
            rep.removed[2] = (uint32_t)rm.item.size();
            rep.removed[3] = (uint32_t)rm.marker.size();
            rep.removed[4] = (uint32_t)rm.squad.size();
            rep.puppets.assign(rm.U.begin(), rm.U.end());
            std::sort(rep.puppets.begin(), rep.puppets.end());
            rep.ntoIn = P.nto;
            std::vector<std::string> kn = kind_names();
            std::vector<Edit> edits;
            std::vector<Pend> unknown;
            plan_edits(P, rm, kn, edits, rep, unknown);
            if (!unknown.empty())
            {
                for (const Pend& u : unknown)
                    rep.unhandled.push_back(std::string(u.what) + " " + kSecs[u.key].key + " idx " + std::to_string((int32_t)u.idx) + " " + u.kn + " at " + hex8(u.pos) + " value " + hex8(u.val));
                err = "unhandled reference: " + rep.unhandled.front();
                return kUnhandled;
            }
            std::vector<Op> ops;
            norm_edits(P, edits, ops);
            std::vector<const Sec*> order;
            for (const Sec& S : P.secs) order.push_back(&S);
            std::sort(order.begin(), order.end(), [](const Sec* a, const Sec* b) { return a->s0 < b->s0; });
            std::string body;
            body.reserve(raw.size());
            const char* base = raw.data();
            uint32_t q = order.front()->s0;
            body.append(base, q);
            std::unordered_set<uint32_t> none;
            for (const Sec* S : order)
            {
                if (S->s0 < q) throw Unk("section overlap");
                body.append(base + q, S->s0 - q);
                if (kSecs[S->key].pool)
                {
                    const std::unordered_set<uint32_t>* drop = &none;
                    if (S->key == S_Unit) drop = &rm.U;
                    else if (S->key == S_Marker) drop = &rm.marker;
                    else if (S->key == S_Squad) drop = &rm.squad;
                    else if (S->key == S_Item) drop = &rm.item;
                    else if (S->key == S_Container) drop = &rm.cont;
                    rebuild_pool(P, *S, *drop, ops, body);
                }
                else apply_local(P, ops, S->s0, S->e0, body);
                q = S->e0;
            }
            body.append(base + q, P.nto - q);
            uint32_t newnto = (uint32_t)body.size();
            std::memcpy(&body[5], &newnto, 4);
            body.append(base + P.nto, raw.size() - P.nto);
            rep.ntoOut = newnto;
            std::unordered_set<uint64_t> subj = rm.subj;
            P = Parsed();
            Parsed O;
            parse_all(body, O);
            std::vector<std::string> kn2 = kind_names();
            std::vector<std::string> bad, und;
            residual(O, rm, subj, bad, und, kn2);
            if (!bad.empty() || !und.empty())
            {
                rep.residual = bad;
                rep.residual.insert(rep.residual.end(), und.begin(), und.end());
                err = "residual reference after strip: " + rep.residual.front();
                return kResidual;
            }
            rep.outSize = body.size();
            rep.raw = std::move(body);
            return kOk;
        }
        catch (const std::exception& x)
        {
            err = std::string("strip parse: ") + x.what();
            return kParse;
        }
    }
}
