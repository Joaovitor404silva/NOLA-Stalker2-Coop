#pragma once
#include <cstdint>
#include <string>

namespace x0::in::tutl
{
    constexpr uint32_t kOurs = 19;

    struct WState
    {
        bool exists = false, visible = false;
        uint8_t active = 0, fade_in = 0, fade_out = 0;
        float opacity = 1.f;
    };

    inline bool drawn(const WState& w) { return w.exists && w.visible && (w.fade_in != 0 || w.opacity > 0.01f); }

    inline bool busy(uint32_t n, const WState& w)
    {
        if (!w.exists) return false;
        if (n == kOurs && w.active != 0) return true;
        return drawn(w);
    }

    inline std::string describe(uint32_t n, const WState& w)
    {
        return "widget " + std::to_string(n) + " (active=" + std::to_string(w.active) + " visible=" + std::to_string(w.visible ? 1 : 0) + " drawn=" + std::to_string(drawn(w) ? 1 : 0) + ")";
    }

    enum class Next { Box, Wait, Text };

    struct Waiter
    {
        bool waiting = false;
        double since = 0;

        void reset() { waiting = false; since = 0; }

        Next step(int r, double now, double busy_max, double load_max)
        {
            if (r == 0) { reset(); return Next::Box; }
            if (r != 1 && r != 3) { reset(); return Next::Text; }
            if (!waiting) { waiting = true; since = now; }
            if (now - since < (r == 3 ? load_max : busy_max)) return Next::Wait;
            reset();
            return Next::Text;
        }
    };

    template <class Show, class Text, class Log>
    int create(Waiter& wt, std::string& last, const std::string& txt, double now, double busy_max, int box_handle, Show show, Text text, Log log)
    {
        std::string why;
        int r = show(txt, why);
        Next nx = wt.step(r, now, busy_max, 2.0);
        if (nx == Next::Box)
        {
            if (!last.empty()) log("shown in the box");
            last.clear();
            return box_handle;
        }
        std::string line = (r == 2 ? "text fallback: " : "waiting: ") + why;
        if (nx == Next::Text && r != 2) line = "text fallback after the wait: " + why;
        if (line != last)
        {
            last = line;
            log(line);
        }
        if (nx == Next::Wait) return 0;
        return text(txt);
    }
}
