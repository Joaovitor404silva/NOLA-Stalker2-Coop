#include "s2x_uiq.h"
#include <algorithm>
#include <set>

namespace x0::in::uiq
{
    namespace
    {
        struct PrioRow
        {
            const char* prefix;
            int prio;
        };
        constexpr PrioRow kPrio[] = {
            {"s2m_rescue", 80},
            {"s2m_revive", 75},
            {"s2m_session_role", 70},
            {"s2m_session_ver", 70},
            {"s2m_save_", 60},
            {"s2m_session_peer", 50},
        };
    }

    int prio_of(std::string_view id)
    {
        int best = 40;
        size_t len = 0;
        for (const PrioRow& r : kPrio)
        {
            std::string_view p(r.prefix);
            if (p.size() > len && id.substr(0, p.size()) == p)
            {
                best = r.prio;
                len = p.size();
            }
        }
        return best;
    }

    void Board::show(const std::string& id, const std::string& text, double secs, double now, bool queue, int max_lines)
    {
        if (text.empty())
        {
            clear(id, now, queue, max_lines);
            return;
        }
        expire(now);
        auto it = L_.find(id);
        if (it == L_.end())
        {
            Line l;
            l.seq = ++seq_;
            l.prio = prio_of(id);
            it = L_.emplace(id, std::move(l)).first;
        }
        it->second.text = text;
        it->second.until = secs > 0 ? now + secs : (max_life > 0 ? now + max_life : 0);
        render(now, queue, max_lines);
    }

    void Board::clear(const std::string& id, double now, bool queue, int max_lines)
    {
        expire(now);
        auto it = L_.find(id);
        if (it != L_.end())
        {
            if (it->second.h) be_.remove(it->second.h);
            L_.erase(it);
        }
        render(now, queue, max_lines);
    }

    void Board::clear_prefix(std::string_view prefix, double now, bool queue, int max_lines)
    {
        for (auto it = L_.begin(); it != L_.end();)
        {
            if (it->first.compare(0, prefix.size(), prefix) == 0)
            {
                if (it->second.h) be_.remove(it->second.h);
                it = L_.erase(it);
            }
            else ++it;
        }
        expire(now);
        render(now, queue, max_lines);
    }

    void Board::tick(double now, bool queue, int max_lines)
    {
        expire(now);
        render(now, queue, max_lines);
    }

    void Board::expire(double now)
    {
        for (auto it = L_.begin(); it != L_.end();)
        {
            if (it->second.until > 0 && it->second.until <= now)
            {
                if (it->second.h) be_.remove(it->second.h);
                it = L_.erase(it);
            }
            else ++it;
        }
    }

    std::string Board::compose(double now, int max_lines, int* shown, int* waiting) const
    {
        std::vector<const std::pair<const std::string, Line>*> v;
        for (const auto& e : L_)
            if (!e.second.text.empty() && (e.second.until <= 0 || e.second.until > now)) v.push_back(&e);
        std::stable_sort(v.begin(), v.end(), [](const auto* a, const auto* b) {
            if (a->second.prio != b->second.prio) return a->second.prio > b->second.prio;
            return a->second.seq < b->second.seq;
        });
        std::set<std::string> seen;
        std::string out;
        int n = 0, w = 0;
        for (const auto* e : v)
        {
            if (!seen.insert(e->second.text).second) continue;
            if (max_lines > 0 && n >= max_lines) { ++w; continue; }
            if (n) out += "\n";
            out += e->second.text;
            ++n;
        }
        if (shown) *shown = n;
        if (waiting) *waiting = w;
        return out;
    }

    void Board::drop_board()
    {
        if (h_) be_.remove(h_);
        h_ = 0;
        txt_.clear();
    }

    void Board::drop_ids()
    {
        for (auto& e : L_)
        {
            if (e.second.h) be_.remove(e.second.h);
            e.second.h = 0;
            e.second.shown.clear();
        }
    }

    void Board::render(double now, bool queue, int max_lines)
    {
        int m = queue ? 1 : 0;
        if (mode_ != m)
        {
            if (m == 1) drop_ids();
            else drop_board();
            mode_ = m;
        }
        if (!queue)
        {
            for (auto& [id, l] : L_)
            {
                if (l.h && be_.alive(l.h))
                {
                    if (l.shown != l.text && be_.set_text(l.h, l.text)) l.shown = l.text;
                    continue;
                }
                if (l.h) be_.remove(l.h);
                l.h = be_.create(l.text);
                l.shown = l.h ? l.text : std::string();
            }
            return;
        }
        int shown = 0, waiting = 0;
        std::string text = compose(now, max_lines, &shown, &waiting);
        if (log && (shown != lastShown_ || waiting != lastWaiting_))
            log("board " + std::to_string(shown) + " line(s) shown, " + std::to_string(waiting) + " waiting, " + std::to_string(L_.size()) + " active");
        lastShown_ = shown;
        lastWaiting_ = waiting;
        if (text.empty())
        {
            drop_board();
            return;
        }
        if (h_ && be_.alive(h_))
        {
            if (txt_ != text && be_.set_text(h_, text)) txt_ = text;
            return;
        }
        if (h_) be_.remove(h_);
        h_ = be_.create(text);
        txt_ = h_ ? text : std::string();
    }
}
