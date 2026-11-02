#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "fake_backend.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#define EXP extern "C" __declspec(dllexport)

namespace
{
    struct Ident
    {
        int32_t type;
        int32_t size;
        uint64_t id;
        uint8_t rest[120];
    };
    static_assert(sizeof(Ident) == 136);

    struct Msg
    {
        void* data;
        int size;
        uint32_t conn;
        Ident peer;
        int64_t conn_user;
        int64_t usec;
        int64_t num;
        void (*free_data)(Msg*);
        void (*release)(Msg*);
        int channel;
        int flags;
        int64_t user_data;
        uint16_t lane;
        uint16_t pad;
    };
    static_assert(offsetof(Msg, peer) == 16);

    struct Pending
    {
        uint64_t from;
        int ch;
        std::string data;
    };

    struct Cb
    {
        int id;
        std::string data;
    };

    struct Res
    {
        uint64_t h;
        int id;
        std::string data;
    };

    std::recursive_mutex mu;
    FakeBackend be{};
    bool have_be = false;
    uint64_t self_id = 0;
    int hsu = 0;
    int logged = 0;
    int overlay = 0;
    uint64_t next_h = 1000;
    std::multimap<int, void*> cbs;
    std::map<uint64_t, void*> results;
    std::deque<Cb> cbq;
    std::deque<Res> resq;
    std::set<uint64_t> accepted;
    std::map<uint64_t, std::vector<Pending>> pending;
    std::deque<Pending> inbox;
    FakeCounters ctr{};
    int iface_dummy[8];
    uint32_t app_id = 1643320;
    std::vector<std::pair<std::string, std::string>> filters;
    std::vector<uint64_t> listed;

    void post_cb(int id, const void* d, int n)
    {
        std::lock_guard<std::recursive_mutex> g(mu);
        cbq.push_back({id, std::string(static_cast<const char*>(d), (size_t)n)});
    }

    void post_res(uint64_t h, int id, const void* d, int n)
    {
        std::lock_guard<std::recursive_mutex> g(mu);
        resq.push_back({h, id, std::string(static_cast<const char*>(d), (size_t)n)});
    }

    void** vt(void* o) { return *reinterpret_cast<void***>(o); }
    uint8_t& flags_of(void* o) { return *(reinterpret_cast<uint8_t*>(o) + 8); }
    int& id_of(void* o) { return *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(o) + 12); }

    void release_msg(Msg* m)
    {
        delete[] static_cast<char*>(m->data);
        delete m;
    }

    void* iface(int k) { return hsu ? &iface_dummy[k] : nullptr; }

    std::atomic<DWORD> pump_tid{0};

    void on_api()
    {
        DWORD p = pump_tid.load();
        std::lock_guard<std::recursive_mutex> g(mu);
        ctr.api_calls++;
        if (p && p != GetCurrentThreadId()) ctr.off_pump++;
    }
}

EXP void Fake_Init(const FakeBackend* b, uint64_t self, int hsteamuser, int logged_on, int ov)
{
    std::lock_guard<std::recursive_mutex> g(mu);
    if (b)
    {
        be = *b;
        have_be = true;
    }
    self_id = self;
    hsu = hsteamuser;
    logged = logged_on;
    overlay = ov;
}

EXP void Fake_Deliver(uint64_t from, const void* d, int n, int ch)
{
    std::lock_guard<std::recursive_mutex> g(mu);
    Pending p{from, ch, std::string(static_cast<const char*>(d), (size_t)n)};
    if (accepted.count(from))
    {
        inbox.push_back(std::move(p));
        ctr.delivered++;
        return;
    }
    bool first = pending[from].empty();
    pending[from].push_back(std::move(p));
    ctr.pending_msgs++;
    if (first)
    {
        Ident id{};
        id.type = 16;
        id.size = 8;
        id.id = from;
        post_cb(1251, &id, sizeof(id));
        ctr.sess_posted++;
    }
}

EXP void Fake_Post(int id, const void* d, int n) { post_cb(id, d, n); }

EXP void Fake_Get(FakeCounters* out)
{
    std::lock_guard<std::recursive_mutex> g(mu);
    ctr.registered_cbs = (int)cbs.size();
    ctr.registered_results = (int)results.size();
    *out = ctr;
}

EXP void Fake_App(uint32_t a)
{
    std::lock_guard<std::recursive_mutex> g(mu);
    app_id = a;
}

EXP int32_t SteamAPI_GetHSteamUser() { return hsu; }

EXP void SteamAPI_RegisterCallback(void* o, int id)
{
    on_api();
    std::lock_guard<std::recursive_mutex> g(mu);
    flags_of(o) |= 1;
    id_of(o) = id;
    cbs.insert({id, o});
}

EXP void SteamAPI_UnregisterCallback(void* o)
{
    on_api();
    std::lock_guard<std::recursive_mutex> g(mu);
    for (auto it = cbs.begin(); it != cbs.end();)
    {
        if (it->second == o) it = cbs.erase(it);
        else ++it;
    }
    flags_of(o) &= (uint8_t)~1;
}

EXP void SteamAPI_RegisterCallResult(void* o, uint64_t h)
{
    on_api();
    std::lock_guard<std::recursive_mutex> g(mu);
    flags_of(o) |= 1;
    results[h] = o;
}

EXP void SteamAPI_UnregisterCallResult(void* o, uint64_t h)
{
    on_api();
    std::lock_guard<std::recursive_mutex> g(mu);
    auto it = results.find(h);
    if (it != results.end() && it->second == o) results.erase(it);
    flags_of(o) &= (uint8_t)~1;
}

EXP void SteamAPI_RunCallbacks()
{
    std::deque<Cb> q;
    std::deque<Res> r;
    {
        pump_tid.store(GetCurrentThreadId());
        std::lock_guard<std::recursive_mutex> g(mu);
        ctr.runs++;
        q.swap(cbq);
        r.swap(resq);
    }
    for (auto& c : q)
    {
        std::vector<void*> objs;
        {
            std::lock_guard<std::recursive_mutex> g(mu);
            auto range = cbs.equal_range(c.id);
            for (auto it = range.first; it != range.second; ++it) objs.push_back(it->second);
        }
        for (void* o : objs)
        {
            if (!(flags_of(o) & 1) || (flags_of(o) & 2)) continue;
            reinterpret_cast<void (*)(void*, void*)>(vt(o)[1])(o, c.data.data());
        }
    }
    for (auto& x : r)
    {
        void* o = nullptr;
        {
            std::lock_guard<std::recursive_mutex> g(mu);
            auto it = results.find(x.h);
            if (it != results.end())
            {
                o = it->second;
                results.erase(it);
            }
        }
        if (!o)
        {
            ctr.results_dropped++;
            continue;
        }
        int want = reinterpret_cast<int (*)(void*)>(vt(o)[2])(o);
        bool io = want != (int)x.data.size() || id_of(o) != x.id;
        std::string buf = x.data;
        buf.resize((size_t)(want > 0 ? want : 0));
        ctr.results_run++;
        if (io) ctr.results_io_fail++;
        reinterpret_cast<void (*)(void*, void*, bool, uint64_t)>(vt(o)[0])(o, buf.data(), io, x.h);
    }
}

EXP void* SteamAPI_SteamUser_v021() { return iface(0); }
EXP bool SteamAPI_ISteamUser_BLoggedOn(void*) { on_api(); return logged != 0; }
EXP uint64_t SteamAPI_ISteamUser_GetSteamID(void*) { return self_id; }
EXP void* SteamAPI_SteamFriends_v017() { return iface(1); }

EXP void SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog(void*, uint64_t lobby)
{
    on_api();
    if (have_be) be.invite_dialog(be.ctx, self_id, lobby);
}

EXP const char* SteamAPI_ISteamFriends_GetPersonaName(void*) { return have_be ? be.persona(be.ctx, self_id) : ""; }
EXP const char* SteamAPI_ISteamFriends_GetFriendPersonaName(void*, uint64_t id) { on_api(); return have_be ? be.persona(be.ctx, id) : ""; }
EXP void* SteamAPI_SteamMatchmaking_v009() { return iface(2); }

EXP uint64_t SteamAPI_ISteamMatchmaking_CreateLobby(void*, int type, int max)
{
    on_api();
    uint64_t lb = have_be ? be.create_lobby(be.ctx, self_id, type, max) : 0;
    uint64_t h;
    {
        std::lock_guard<std::recursive_mutex> g(mu);
        h = ++next_h;
    }
    uint8_t d[16] = {};
    int32_t res = lb ? 1 : 2;
    std::memcpy(d, &res, 4);
    std::memcpy(d + 8, &lb, 8);
    post_res(h, 513, d, 16);
    return h;
}

EXP uint64_t SteamAPI_ISteamMatchmaking_JoinLobby(void*, uint64_t lobby)
{
    on_api();
    int resp = have_be ? be.join_lobby(be.ctx, self_id, lobby) : 2;
    uint64_t h;
    {
        std::lock_guard<std::recursive_mutex> g(mu);
        h = ++next_h;
    }
    uint8_t d[24] = {};
    std::memcpy(d, &lobby, 8);
    uint32_t r = (uint32_t)resp;
    std::memcpy(d + 16, &r, 4);
    post_res(h, 504, d, 24);
    return h;
}

EXP void SteamAPI_ISteamMatchmaking_LeaveLobby(void*, uint64_t lobby)
{
    on_api();
    if (have_be) be.leave_lobby(be.ctx, self_id, lobby);
}

EXP int SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(void*, uint64_t lobby) { on_api(); return have_be ? be.num_members(be.ctx, lobby) : 0; }
EXP uint64_t SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(void*, uint64_t lobby, int i) { on_api(); return have_be ? be.member(be.ctx, lobby, i) : 0; }
EXP bool SteamAPI_ISteamMatchmaking_SetLobbyData(void*, uint64_t lobby, const char* k, const char* v) { on_api(); return have_be && be.set_data(be.ctx, self_id, lobby, k, v); }
EXP const char* SteamAPI_ISteamMatchmaking_GetLobbyData(void*, uint64_t lobby, const char* k) { on_api(); return have_be ? be.get_data(be.ctx, lobby, k) : ""; }
EXP uint64_t SteamAPI_ISteamMatchmaking_GetLobbyOwner(void*, uint64_t lobby) { on_api(); return have_be ? be.owner(be.ctx, lobby) : 0; }
EXP void* SteamAPI_SteamUtils_v010() { return iface(3); }
EXP uint32_t SteamAPI_ISteamUtils_GetAppID(void*) { return app_id; }

EXP bool SteamAPI_ISteamMatchmaking_SetLobbyMemberLimit(void*, uint64_t lobby, int max)
{
    on_api();
    return have_be && be.set_limit(be.ctx, self_id, lobby, max);
}

EXP int SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit(void*, uint64_t lobby)
{
    on_api();
    return have_be ? be.get_limit(be.ctx, lobby) : 0;
}

EXP void SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(void*, const char* k, const char* v, int)
{
    on_api();
    std::lock_guard<std::recursive_mutex> g(mu);
    filters.push_back({k ? k : "", v ? v : ""});
}

EXP void SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter(void*, int) { on_api(); }

EXP uint64_t SteamAPI_ISteamMatchmaking_RequestLobbyList(void*)
{
    on_api();
    std::vector<const char*> ks, vs;
    std::vector<std::pair<std::string, std::string>> f;
    {
        std::lock_guard<std::recursive_mutex> g(mu);
        f.swap(filters);
    }
    for (auto& kv : f)
    {
        ks.push_back(kv.first.c_str());
        vs.push_back(kv.second.c_str());
    }
    uint64_t out[64];
    int n = have_be ? be.list(be.ctx, self_id, ks.data(), vs.data(), (int)f.size(), out, 64) : 0;
    uint64_t h;
    {
        std::lock_guard<std::recursive_mutex> g(mu);
        listed.assign(out, out + n);
        h = ++next_h;
    }
    uint32_t c = (uint32_t)n;
    post_res(h, 510, &c, 4);
    return h;
}

EXP uint64_t SteamAPI_ISteamMatchmaking_GetLobbyByIndex(void*, int i)
{
    on_api();
    std::lock_guard<std::recursive_mutex> g(mu);
    return i >= 0 && i < (int)listed.size() ? listed[(size_t)i] : 0;
}

EXP int SteamAPI_ISteamFriends_GetFriendCount(void*, int)
{
    on_api();
    uint64_t out[64];
    return have_be ? be.friends(be.ctx, self_id, out, 64) : 0;
}

EXP uint64_t SteamAPI_ISteamFriends_GetFriendByIndex(void*, int i, int)
{
    on_api();
    uint64_t out[64];
    int n = have_be ? be.friends(be.ctx, self_id, out, 64) : 0;
    return i >= 0 && i < n ? out[i] : 0;
}

struct FakeFriendGame
{
    uint64_t game;
    uint32_t ip;
    uint16_t port;
    uint16_t query;
    uint64_t lobby;
};

EXP bool SteamAPI_ISteamFriends_GetFriendGamePlayed(void*, uint64_t id, FakeFriendGame* out)
{
    on_api();
    uint64_t game = 0, lobby = 0;
    if (!have_be || !be.friend_game(be.ctx, id, &game, &lobby)) return false;
    std::memset(out, 0, sizeof *out);
    out->game = game;
    out->lobby = lobby;
    return true;
}
EXP bool SteamAPI_ISteamUtils_IsOverlayEnabled(void*) { return overlay != 0; }
EXP void* SteamAPI_SteamNetworkingMessages_SteamAPI_v002() { return iface(4); }

EXP void SteamAPI_SteamNetworkingIdentity_Clear(Ident* i) { std::memset(i, 0, sizeof(Ident)); }

EXP void SteamAPI_SteamNetworkingIdentity_SetSteamID64(Ident* i, uint64_t id)
{
    i->type = 16;
    i->size = 8;
    i->id = id;
}

EXP uint64_t SteamAPI_SteamNetworkingIdentity_GetSteamID64(const Ident* i) { return i->type == 16 ? i->id : 0; }

EXP int SteamAPI_ISteamNetworkingMessages_SendMessageToUser(void*, const Ident* to, const void* d, uint32_t n, int, int ch)
{
    uint64_t t = SteamAPI_SteamNetworkingIdentity_GetSteamID64(to);
    if (!t) return 2;
    {
        std::lock_guard<std::recursive_mutex> g(mu);
        if (!accepted.count(t))
        {
            accepted.insert(t);
            auto it = pending.find(t);
            if (it != pending.end())
            {
                for (auto& p : it->second) inbox.push_back(std::move(p));
                pending.erase(it);
            }
        }
        ctr.sent++;
    }
    if (have_be) be.send(be.ctx, self_id, t, d, (int)n, ch);
    return 1;
}

namespace
{
    std::atomic<int> recv_block{0};
    std::atomic<bool> freed{false};
    std::atomic<uint64_t> recv_calls{0}, recv_after_free{0};
}

EXP void Fake_BlockReceive(int ms) { recv_block.store(ms); }
EXP void Fake_Free(int on) { freed.store(on != 0); }
EXP void Fake_RecvCalls(uint64_t* calls, uint64_t* after_free)
{
    *calls = recv_calls.load();
    *after_free = recv_after_free.load();
}

EXP int SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel(void*, int ch, Msg** out, int max)
{
    recv_calls.fetch_add(1);
    if (freed.load()) recv_after_free.fetch_add(1);
    int b = recv_block.load();
    if (b > 0) Sleep((DWORD)b);
    while (recv_block.load() < 0) Sleep(1);
    std::lock_guard<std::recursive_mutex> g(mu);
    int n = 0;
    for (auto it = inbox.begin(); it != inbox.end() && n < max;)
    {
        if (it->ch != ch)
        {
            ++it;
            continue;
        }
        Msg* m = new Msg{};
        m->size = (int)it->data.size();
        char* b = new char[it->data.size() + 1];
        std::memcpy(b, it->data.data(), it->data.size());
        m->data = b;
        m->peer.type = 16;
        m->peer.size = 8;
        m->peer.id = it->from;
        m->channel = ch;
        m->release = &release_msg;
        out[n++] = m;
        it = inbox.erase(it);
    }
    return n;
}

EXP bool SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser(void*, const Ident* who)
{
    uint64_t t = SteamAPI_SteamNetworkingIdentity_GetSteamID64(who);
    std::lock_guard<std::recursive_mutex> g(mu);
    ctr.accepted_calls++;
    auto it = pending.find(t);
    if (it == pending.end()) return false;
    accepted.insert(t);
    for (auto& p : it->second)
    {
        inbox.push_back(std::move(p));
        ctr.delivered++;
    }
    pending.erase(it);
    return true;
}

EXP bool SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser(void*, const Ident* who)
{
    uint64_t t = SteamAPI_SteamNetworkingIdentity_GetSteamID64(who);
    std::lock_guard<std::recursive_mutex> g(mu);
    ctr.closes++;
    accepted.erase(t);
    pending.erase(t);
    return true;
}

EXP void SteamAPI_SteamNetworkingMessage_t_Release(Msg* m)
{
    if (m && m->release) m->release(m);
}

EXP void* SteamAPI_SteamNetworkingUtils_SteamAPI_v004() { return iface(5); }

EXP void SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess(void*)
{
    std::lock_guard<std::recursive_mutex> g(mu);
    ctr.relay_init++;
}
