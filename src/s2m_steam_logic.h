#pragma once
#include <cstdint>
#include <string>
#include <utility>

#include "s2m_text.h"
#include "s2r_steam.h"

namespace nola_steam
{
    inline std::string fill(const std::string& t, const s2r::SteamNote& n, const std::string& why = {})
    {
        const std::pair<const char*, std::string> keys[] = {
            {"{name}", n.name.empty() ? std::string("?") : n.name}, {"{n}", std::to_string(n.n)}, {"{max}", std::to_string(n.max)}, {"{id}", std::to_string(n.lobby)}, {"{why}", why}};
        std::string o;
        size_t i = 0;
        while (i < t.size())
        {
            bool hit = false;
            for (auto& [k, v] : keys)
            {
                size_t kl = std::char_traits<char>::length(k);
                if (t.compare(i, kl, k) == 0)
                {
                    o += v;
                    i += kl;
                    hit = true;
                    break;
                }
            }
            if (!hit) o += t[i++];
        }
        return o;
    }

    inline std::string why(nola_text::Lang l, const s2r::SteamNote& n)
    {
        using nola_text::T;
        switch (n.code)
        {
        case 2: return nola_text::tx(T::steam_why_gone, l);
        case 3: return nola_text::tx(T::steam_why_denied, l);
        case 4: return nola_text::tx(T::steam_why_full, l);
        case 5: return nola_text::tx(T::steam_why_error, l);
        case 6: return nola_text::tx(T::steam_why_banned, l);
        case 7: return nola_text::tx(T::steam_why_limited, l);
        case 8: return nola_text::tx(T::steam_why_clan, l);
        case 9: return nola_text::tx(T::steam_why_community, l);
        case 10: return nola_text::tx(T::steam_why_blocked_you, l);
        case 11: return nola_text::tx(T::steam_why_you_blocked, l);
        case s2r::steam::ENTER_NOT_NOLA: return nola_text::tx(T::steam_why_not_nola, l);
        case s2r::steam::ENTER_NO_HOST: return nola_text::tx(T::steam_why_no_host, l);
        }
        return nola_text::tx(n.why.empty() ? T::steam_why_error : T::steam_why_unknown, l);
    }

    inline std::string text(const s2r::SteamNote& n, nola_text::Lang l, bool dev)
    {
        using K = s2r::SteamNote::Kind;
        using nola_text::T;
        auto t = [&](T id) { return nola_text::tx(id, l); };
        switch (n.kind)
        {
        case K::hosting: return fill(t(dev ? T::steam_hosting_id : T::steam_hosting), n);
        case K::closed: return t(T::steam_closed);
        case K::member_in: return fill(t(T::steam_member_in), n);
        case K::member_out: return fill(t(T::steam_member_out), n);
        case K::host_left: return fill(t(T::steam_host_left), n);
        case K::join_fail: return fill(t(T::steam_fail), n, why(l, n));
        case K::unavailable: return t(T::steam_unavailable);
        case K::joined:
        case K::none: return {};
        }
        return {};
    }
}
