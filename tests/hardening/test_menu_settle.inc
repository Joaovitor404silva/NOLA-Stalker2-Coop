#include <cassert>
#include "s2m_menu_logic.h"
nola::Toggle g_toggle;
bool animation = false;
bool menu_animating() { return animation; }
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <utility>
bool ready = true, in_world = false;
struct View { bool on = true; bool valid() const { return on; } } g_sv;
uint64_t g_settleGeneration = 0;
int refreshes = 0;
bool pawn() { return in_world; }
void refresh_menu(const char*) { ++refreshes; }
std::deque<std::function<void()>> jobs;
namespace x0 {
enum class Mod { session };
void after(Mod, double, std::function<void()> fn) { jobs.push_back(std::move(fn)); }
bool save_store_ready() { return ready; }
template<class... A> void logf(const char*, A...) {}
}
// PRODUCTION_SETTLE
void drain() { int limit=200; while (!jobs.empty() && --limit) { auto fn=std::move(jobs.front()); jobs.pop_front(); fn(); } assert(limit); }
int main() {
    int old = 0, current = 0;
    settle("host on", [&]{ ++old; }, 40);
    settle("host off", [&]{ ++old; }, 40);
    settle("host on again", [&]{ ++current; }, 40);
    drain(); assert(old == 0 && current == 1 && refreshes == 1);
    ready = false;
    settle("waiting", [&]{ ++old; }, 40);
    auto first=std::move(jobs.front()); jobs.pop_front(); first();
    settle("replacement", [&]{ ++current; }, 40);
    ready = true; drain(); assert(old == 0 && current == 2);
    g_toggle.begin();
    settle("enter world", [&]{ ++old; }, 40);
    in_world = true; drain(); assert(old == 0 && g_toggle.accept());
    in_world = false; g_sv.on = false;
    settle("destroyed menu", [&]{ ++old; }, 40); drain(); assert(old == 0);
    g_sv.on = true;
    animation = true;
    int before = refreshes;
    settle("animation", [&]{ ++old; }, 40);
    auto animJob=std::move(jobs.front()); jobs.pop_front(); animJob();
    assert(refreshes == before && !jobs.empty());
    settle("animation replacement", [&]{ ++current; }, 40);
    animation = false; drain(); assert(old == 0 && current == 3);
    settle("cancel delayed completion", [&]{ ++old; }, 40);
    auto doneJob=std::move(jobs.front()); jobs.pop_front(); doneJob();
    ++g_settleGeneration; drain(); assert(old == 0);
    g_toggle.begin(); settle("no popup", nullptr, 40); drain(); assert(g_toggle.accept());
    std::cout << "PASS: rapid menu toggles, retry cancellation, world entry and destroyed menu\n";
}
