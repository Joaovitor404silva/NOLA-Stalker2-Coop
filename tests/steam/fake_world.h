#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "fake_backend.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <set>
#include <tuple>
#include <vector>

struct FakeDll
{
    HMODULE mod = nullptr;
    FakeInitFn init = nullptr;
    FakeDeliverFn deliver = nullptr;
    FakePostFn post = nullptr;
    FakeGetFn get = nullptr;
    FakeAppFn app = nullptr;
    bool load(HMODULE m)
    {
        mod = m;
        if (!m) return false;
        init = reinterpret_cast<FakeInitFn>(GetProcAddress(m, "Fake_Init"));
        deliver = reinterpret_cast<FakeDeliverFn>(GetProcAddress(m, "Fake_Deliver"));
        post = reinterpret_cast<FakePostFn>(GetProcAddress(m, "Fake_Post"));
        get = reinterpret_cast<FakeGetFn>(GetProcAddress(m, "Fake_Get"));
        app = reinterpret_cast<FakeAppFn>(GetProcAddress(m, "Fake_App"));
        return init && deliver && post && get && app;
    }
    FakeCounters counters() const
    {
        FakeCounters c{};
        get(&c);
        return c;
    }
};

struct World
{
    struct Lobby
    {
        uint64_t owner = 0;
        std::vector<uint64_t> members;
        int max = 0;
        int type = 0;
        std::map<std::string, std::string> data;
    };
    std::set<uint64_t> limited;
    std::map<uint64_t, std::vector<uint64_t>> friends;
    std::map<uint64_t, std::pair<uint64_t, uint64_t>> playing;
    struct Sent
    {
        uint64_t from, to;
        int ch;
        std::string data;
    };
    std::recursive_mutex mu;
    std::map<uint64_t, Lobby> lobbies;
    uint64_t next = 109775240000000000ULL;
    std::map<uint64_t, FakeDeliverFn> route;
    std::map<uint64_t, std::string> names;
    std::vector<Sent> sends;
    std::vector<std::pair<uint64_t, uint64_t>> invites;
    bool record = false;
    FakeBackend be{};

    World()
    {
        be.ctx = this;
        be.create_lobby = [](void* c, uint64_t u, int type, int max) -> uint64_t
        {
            World& w = *static_cast<World*>(c);
            std::lock_guard<std::recursive_mutex> g(w.mu);
            uint64_t id = ++w.next;
            Lobby& l = w.lobbies[id];
            l.owner = u;
            l.members = {u};
            l.max = max;
            l.type = type;
            return id;
        };
        be.join_lobby = [](void* c, uint64_t u, uint64_t lb) -> int
        {
            World& w = *static_cast<World*>(c);
            std::lock_guard<std::recursive_mutex> g(w.mu);
            auto it = w.lobbies.find(lb);
            if (it == w.lobbies.end()) return 2;
            Lobby& l = it->second;
            if (std::find(l.members.begin(), l.members.end(), u) != l.members.end()) return 1;
            if (w.limited.count(u)) return 7;
            if ((int)l.members.size() >= l.max) return 4;
            l.members.push_back(u);
            return 1;
        };
        be.leave_lobby = [](void* c, uint64_t u, uint64_t lb)
        {
            World& w = *static_cast<World*>(c);
            std::lock_guard<std::recursive_mutex> g(w.mu);
            auto it = w.lobbies.find(lb);
            if (it == w.lobbies.end()) return;
            Lobby& l = it->second;
            l.members.erase(std::remove(l.members.begin(), l.members.end(), u), l.members.end());
            if (l.members.empty())
            {
                w.lobbies.erase(it);
                return;
            }
            if (l.owner == u) l.owner = l.members.front();
        };
        be.num_members = [](void* c, uint64_t lb) -> int
        {
            World& w = *static_cast<World*>(c);
            std::lock_guard<std::recursive_mutex> g(w.mu);
            auto it = w.lobbies.find(lb);
            return it == w.lobbies.end() ? 0 : (int)it->second.members.size();
        };
        be.member = [](void* c, uint64_t lb, int i) -> uint64_t
        {
            World& w = *static_cast<World*>(c);
            std::lock_guard<std::recursive_mutex> g(w.mu);
            auto it = w.lobbies.find(lb);
            if (it == w.lobbies.end() || i < 0 || i >= (int)it->second.members.size()) return 0;
            return it->second.members[(size_t)i];
        };
        be.owner = [](void* c, uint64_t lb) -> uint64_t
        {
            World& w = *static_cast<World*>(c);
            std::lock_guard<std::recursive_mutex> g(w.mu);
            auto it = w.lobbies.find(lb);
            return it == w.lobbies.end() ? 0 : it->second.owner;
        };
        be.set_data = [](void* c, uint64_t u, uint64_t lb, const char* k, const char* v) -> bool
        {
            World& w = *static_cast<World*>(c);
            std::lock_guard<std::recursive_mutex> g(w.mu);
            auto it = w.lobbies.find(lb);
            if (it == w.lobbies.end() || it->second.owner != u) return false;
            it->second.data[k] = v;
            return true;
        };
        be.get_data = [](void* c, uint64_t lb, const char* k) -> const char*
        {
            World& w = *static_cast<World*>(c);
            std::lock_guard<std::recursive_mutex> g(w.mu);
            auto it = w.lobbies.find(lb);
            if (it == w.lobbies.end()) return "";
            auto d = it->second.data.find(k);
            return d == it->second.data.end() ? "" : d->second.c_str();
        };
        be.send = [](void* c, uint64_t from, uint64_t to, const void* d, int n, int ch)
        {
            World& w = *static_cast<World*>(c);
            FakeDeliverFn fn = nullptr;
            {
                std::lock_guard<std::recursive_mutex> g(w.mu);
                if (w.record) w.sends.push_back({from, to, ch, std::string(static_cast<const char*>(d), (size_t)n)});
                auto it = w.route.find(to);
                if (it != w.route.end()) fn = it->second;
            }
            if (fn) fn(from, d, n, ch);
        };
        be.invite_dialog = [](void* c, uint64_t u, uint64_t lb)
        {
            World& w = *static_cast<World*>(c);
            std::lock_guard<std::recursive_mutex> g(w.mu);
            w.invites.push_back({u, lb});
        };
        be.set_limit = [](void* c, uint64_t u, uint64_t lb, int max) -> bool
        {
            World& w = *static_cast<World*>(c);
            std::lock_guard<std::recursive_mutex> g(w.mu);
            auto it = w.lobbies.find(lb);
            if (it == w.lobbies.end() || it->second.owner != u) return false;
            it->second.max = max;
            return true;
        };
        be.get_limit = [](void* c, uint64_t lb) -> int
        {
            World& w = *static_cast<World*>(c);
            std::lock_guard<std::recursive_mutex> g(w.mu);
            auto it = w.lobbies.find(lb);
            return it == w.lobbies.end() ? 0 : it->second.max;
        };
        be.list = [](void* c, uint64_t, const char* const* ks, const char* const* vs, int nf, uint64_t* out, int max) -> int
        {
            World& w = *static_cast<World*>(c);
            std::lock_guard<std::recursive_mutex> g(w.mu);
            int n = 0;
            for (auto& [id, l] : w.lobbies)
            {
                if (l.type != 2 || n >= max) continue;
                bool ok = true;
                for (int i = 0; i < nf && ok; ++i)
                {
                    auto d = l.data.find(ks[i]);
                    ok = d != l.data.end() && d->second == vs[i];
                }
                if (ok) out[n++] = id;
            }
            return n;
        };
        be.friends = [](void* c, uint64_t u, uint64_t* out, int max) -> int
        {
            World& w = *static_cast<World*>(c);
            std::lock_guard<std::recursive_mutex> g(w.mu);
            auto it = w.friends.find(u);
            if (it == w.friends.end()) return 0;
            int n = 0;
            for (uint64_t f : it->second)
                if (n < max) out[n++] = f;
            return n;
        };
        be.friend_game = [](void* c, uint64_t u, uint64_t* game, uint64_t* lb) -> bool
        {
            World& w = *static_cast<World*>(c);
            std::lock_guard<std::recursive_mutex> g(w.mu);
            auto it = w.playing.find(u);
            if (it == w.playing.end()) return false;
            *game = it->second.first;
            *lb = it->second.second;
            return true;
        };
        be.persona = [](void* c, uint64_t u) -> const char*
        {
            World& w = *static_cast<World*>(c);
            std::lock_guard<std::recursive_mutex> g(w.mu);
            auto it = w.names.find(u);
            return it == w.names.end() ? "" : it->second.c_str();
        };
    }

    bool member_of(uint64_t lb, uint64_t u)
    {
        std::lock_guard<std::recursive_mutex> g(mu);
        auto it = lobbies.find(lb);
        return it != lobbies.end() && std::find(it->second.members.begin(), it->second.members.end(), u) != it->second.members.end();
    }
};
