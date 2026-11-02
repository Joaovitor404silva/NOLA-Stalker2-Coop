#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace x0::in::uiq
{
    struct Backend
    {
        std::function<int(const std::string& text)> create;
        std::function<bool(int h, const std::string& text)> set_text;
        std::function<void(int h)> remove;
        std::function<bool(int h)> alive;
    };

    struct Line
    {
        std::string text;
        int prio = 0;
        uint64_t seq = 0;
        double until = 0;
        int h = 0;
        std::string shown;
    };

    int prio_of(std::string_view id);

    class Board
    {
      public:
        explicit Board(Backend b) : be_(std::move(b)) {}
        void show(const std::string& id, const std::string& text, double secs, double now, bool queue, int max_lines);
        void clear(const std::string& id, double now, bool queue, int max_lines);
        void tick(double now, bool queue, int max_lines);
        void clear_prefix(std::string_view prefix, double now, bool queue, int max_lines);
        std::string compose(double now, int max_lines, int* shown = nullptr, int* waiting = nullptr) const;
        size_t lines() const { return L_.size(); }
        int board() const { return h_; }
        const std::string& board_text() const { return txt_; }
        const std::map<std::string, Line>& all() const { return L_; }
        std::function<void(const std::string&)> log;
        double max_life = 0;

      private:
        void expire(double now);
        void render(double now, bool queue, int max_lines);
        void drop_board();
        void drop_ids();
        Backend be_;
        std::map<std::string, Line> L_;
        uint64_t seq_ = 0;
        int h_ = 0;
        std::string txt_;
        int mode_ = -1;
        int lastShown_ = -1, lastWaiting_ = -1;
    };
}
