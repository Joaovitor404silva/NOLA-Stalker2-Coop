#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace frc
{
    constexpr char kMagic[8] = {'N', 'O', 'L', 'A', 'F', 'R', '1', 0};
    constexpr uint32_t kVersion = 1;
    constexpr uint32_t kHdrSize = 4096;
    constexpr uint32_t kRecSize = 128;

    enum Kind : uint8_t
    {
        k_none = 0,
        k_tx_snap = 1,
        k_rx_snap = 2,
        k_tx_tag = 3,
        k_rx_tag = 4,
        k_handled = 5,
        k_log = 6,
        k_state = 7,
        k_trigger = 8,
        k_meta = 9,
        k_drop = 10,
    };

    enum Fam : uint8_t
    {
        f_net = 0,
        f_session,
        f_load,
        f_travel,
        f_npc,
        f_body,
        f_combat,
        f_container,
        f_lock,
        f_quest,
        f_item,
        f_save,
        f_world,
        f_ui,
        f_inv,
        f_other,
        f_count
    };

    enum State : uint32_t
    {
        s_open = 1,
        s_closed = 2,
    };

    struct Hdr
    {
        char magic[8];
        uint32_t version;
        uint32_t hdr_size;
        uint32_t rec_size;
        uint32_t cap;
        uint64_t next;
        uint64_t qpc_freq;
        uint64_t qpc0;
        int64_t wall0_us;
        uint32_t pid;
        uint32_t state;
        uint32_t dumps;
        int32_t net_pid;
        int64_t dump_wall_us;
        uint64_t dump_qpc;
        char role[16];
        char sid[32];
        char host[64];
        char build[32];
        char reason[64];
        char tracked[256];
    };
    static_assert(offsetof(Hdr, next) == 24);
    static_assert(offsetof(Hdr, pid) == 56);
    static_assert(offsetof(Hdr, dump_wall_us) == 72);
    static_assert(offsetof(Hdr, role) == 88);
    static_assert(offsetof(Hdr, tracked) == 296);
    static_assert(sizeof(Hdr) == 552);

    struct Rec
    {
        uint64_t n;
        uint64_t qpc;
        int64_t wall_us;
        uint32_t seq;
        uint32_t hash;
        uint32_t size;
        uint16_t aux;
        int8_t peer;
        uint8_t kind;
        uint8_t fam;
        uint8_t pad[3];
        char tag[8];
        char text[76];
    };
    static_assert(offsetof(Rec, seq) == 24);
    static_assert(offsetof(Rec, aux) == 36);
    static_assert(offsetof(Rec, tag) == 44);
    static_assert(offsetof(Rec, text) == 52);
    static_assert(sizeof(Rec) == kRecSize);

    inline uint32_t fnv(std::string_view s)
    {
        uint32_t h = 2166136261u;
        for (unsigned char c : s)
        {
            h ^= c;
            h *= 16777619u;
        }
        return h;
    }

    inline void put_str(char* dst, size_t cap, std::string_view s)
    {
        size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
        for (size_t i = 0; i < n; ++i)
        {
            unsigned char c = (unsigned char)s[i];
            dst[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
        }
        std::memset(dst + n, 0, cap - n);
    }

    inline size_t bytes_for(uint32_t cap) { return (size_t)kHdrSize + (size_t)cap * kRecSize; }

    class Ring
    {
      public:
        bool attach(void* mem, size_t bytes, uint32_t cap, bool init, uint64_t qpc_freq, uint64_t qpc0, int64_t wall0_us, uint32_t pid)
        {
            if (!mem || cap == 0 || bytes < bytes_for(cap)) return false;
            hdr_ = reinterpret_cast<Hdr*>(mem);
            recs_ = reinterpret_cast<Rec*>(reinterpret_cast<uint8_t*>(mem) + kHdrSize);
            cap_ = cap;
            if (init)
            {
                std::memset(mem, 0, bytes_for(cap));
                std::memcpy(hdr_->magic, kMagic, 8);
                hdr_->version = kVersion;
                hdr_->hdr_size = kHdrSize;
                hdr_->rec_size = kRecSize;
                hdr_->cap = cap;
                hdr_->qpc_freq = qpc_freq;
                hdr_->qpc0 = qpc0;
                hdr_->wall0_us = wall0_us;
                hdr_->pid = pid;
                hdr_->state = s_open;
                hdr_->net_pid = -1;
            }
            return true;
        }

        bool ok() const { return hdr_ != nullptr; }
        Hdr* hdr() const { return hdr_; }
        uint32_t cap() const { return cap_; }
        size_t bytes() const { return bytes_for(cap_); }
        uint64_t count() const { return hdr_ ? std::atomic_ref<uint64_t>(hdr_->next).load(std::memory_order_acquire) : 0; }

        void put(uint64_t qpc, int64_t wall_us, uint8_t kind, uint8_t fam, int peer, std::string_view tag, uint32_t seq, uint32_t hash, uint32_t size, uint16_t aux,
                 std::string_view text)
        {
            if (!hdr_) return;
            uint64_t i = std::atomic_ref<uint64_t>(hdr_->next).fetch_add(1, std::memory_order_acq_rel);
            Rec& r = recs_[i % cap_];
            std::atomic_ref<uint64_t>(r.n).store(0, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            r.qpc = qpc;
            r.wall_us = wall_us;
            r.seq = seq;
            r.hash = hash;
            r.size = size;
            r.aux = aux;
            r.peer = (int8_t)(peer < -128 ? -128 : peer > 127 ? 127 : peer);
            r.kind = kind;
            r.fam = fam;
            r.pad[0] = r.pad[1] = r.pad[2] = 0;
            put_str(r.tag, sizeof r.tag, tag);
            put_str(r.text, sizeof r.text, text);
            std::atomic_ref<uint64_t>(r.n).store(i + 1, std::memory_order_release);
        }

        bool copy_to(void* dst, size_t cap) const
        {
            if (!hdr_ || cap < bytes()) return false;
            std::memcpy(dst, hdr_, bytes());
            return true;
        }

      private:
        Hdr* hdr_ = nullptr;
        Rec* recs_ = nullptr;
        uint32_t cap_ = 0;
    };

    struct TagTab
    {
        struct E
        {
            char tag[8];
            uint32_t hash;
            uint32_t skipped;
            double at;
        };
        static constexpr int kMax = 96;
        E e[kMax];
        int n = 0;

        E* find(std::string_view t)
        {
            for (int i = 0; i < n; ++i)
                if (std::strncmp(e[i].tag, t.data(), t.size()) == 0 && (t.size() >= 8 || e[i].tag[t.size()] == 0)) return &e[i];
            if (n >= kMax) return nullptr;
            E& x = e[n++];
            std::memset(x.tag, 0, sizeof x.tag);
            std::memcpy(x.tag, t.data(), t.size() < 7 ? t.size() : 7);
            x.hash = 0;
            x.skipped = 0;
            x.at = -1e9;
            return &x;
        }
        void clear() { n = 0; }
    };

    inline bool up_az(char c) { return c >= 'A' && c <= 'Z'; }
    inline bool tag_ch(char c) { return up_az(c) || (c >= '0' && c <= '9'); }

    template <class Fn> void each_tag(std::string_view x1, Fn&& fn)
    {
        size_t i = 0;
        while (i < x1.size())
        {
            if (x1[i] != '#') { ++i; continue; }
            size_t j = i + 1;
            if (j < x1.size() && up_az(x1[j]))
            {
                while (j < x1.size() && tag_ch(x1[j])) ++j;
                if (j < x1.size() && x1[j] == ':')
                {
                    size_t pb = j + 1, pe = x1.find('#', pb);
                    if (pe == std::string_view::npos) pe = x1.size();
                    fn(x1.substr(i + 1, j - i - 1), x1.substr(pb, pe - pb));
                    i = pe;
                    continue;
                }
            }
            ++i;
        }
    }

    struct Clock
    {
        uint64_t qpc;
        int64_t wall_us;
        double t;
    };

    inline int tag_diff(Ring& ring, TagTab& tab, const Clock& c, uint8_t kind, int peer, uint32_t seq, std::string_view x1, double min_gap)
    {
        int put = 0;
        each_tag(x1, [&](std::string_view tag, std::string_view pay) {
            if (tag.size() > 7) return;
            TagTab::E* e = tab.find(tag);
            if (!e) return;
            uint32_t h = fnv(pay);
            if (h == e->hash) return;
            if (c.t - e->at < min_gap)
            {
                ++e->skipped;
                return;
            }
            uint16_t sk = (uint16_t)(e->skipped > 0xffff ? 0xffff : e->skipped);
            ring.put(c.qpc, c.wall_us, kind, f_net, peer, tag, seq, h, (uint32_t)pay.size(), sk, pay);
            e->hash = h;
            e->at = c.t;
            e->skipped = 0;
            ++put;
        });
        return put;
    }
}
