#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace nola_text
{
    enum class Lang : uint8_t { en = 0, pt = 1, es = 2 };

    enum class T : uint16_t {
#define X(id, en, pt, es) id,
#include "s2m_text_tab.inc"
#undef X
        count_
    };

    struct Row
    {
        const char* id;
        const char* t[3];
    };

    inline constexpr Row kRows[] = {
#define X(id, en, pt, es) {#id, {en, pt, es}},
#include "s2m_text_tab.inc"
#undef X
    };

    inline constexpr size_t kCount = sizeof(kRows) / sizeof(kRows[0]);
    static_assert(kCount == static_cast<size_t>(T::count_), "text table and enum out of step");

    template <class S>
    inline Lang lang_of(const S& l)
    {
        auto c = [&](size_t i) { return i < l.size() ? static_cast<int>(l[i]) | 0x20 : 0; };
        if (c(0) == 'p' && c(1) == 't') return Lang::pt;
        if (c(0) == 'e' && c(1) == 's') return Lang::es;
        return Lang::en;
    }

    inline const char* lang_name(Lang l) { return l == Lang::pt ? "Portuguese" : l == Lang::es ? "Spanish" : "English"; }

    inline const char* get(T id, Lang l) { return kRows[static_cast<size_t>(id)].t[static_cast<size_t>(l)]; }

    using Probe = std::wstring (*)();

    inline std::atomic<uint8_t> g_lang{0};
    inline std::atomic<bool> g_known{false};
    inline std::atomic<Probe> g_probe{nullptr};
    inline std::atomic<int64_t> g_last_ms{0};

    inline void set_probe(Probe p) { g_probe.store(p); }

    inline void force(Lang l)
    {
        g_probe.store(nullptr);
        g_lang.store(static_cast<uint8_t>(l));
        g_known.store(true);
    }

    inline int64_t steady_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    inline void refresh()
    {
        Probe p = g_probe.load();
        if (!p) return;
        int64_t now = steady_ms();
        if (g_known.load() && now - g_last_ms.load() < 2000) return;
        g_last_ms.store(now);
        std::wstring l = p();
        if (l.empty()) return;
        g_lang.store(static_cast<uint8_t>(lang_of(l)));
        g_known.store(true);
    }

    inline Lang cur()
    {
        refresh();
        return static_cast<Lang>(g_lang.load());
    }

    inline std::string tx(T id) { return get(id, cur()); }
    inline std::string tx(T id, Lang l) { return get(id, l); }
}
