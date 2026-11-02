#pragma once
#include <cstdint>

struct FakeBackend
{
    void* ctx;
    uint64_t (*create_lobby)(void* ctx, uint64_t user, int type, int max);
    int (*join_lobby)(void* ctx, uint64_t user, uint64_t lobby);
    void (*leave_lobby)(void* ctx, uint64_t user, uint64_t lobby);
    int (*num_members)(void* ctx, uint64_t lobby);
    uint64_t (*member)(void* ctx, uint64_t lobby, int i);
    uint64_t (*owner)(void* ctx, uint64_t lobby);
    bool (*set_data)(void* ctx, uint64_t user, uint64_t lobby, const char* k, const char* v);
    const char* (*get_data)(void* ctx, uint64_t lobby, const char* k);
    void (*send)(void* ctx, uint64_t from, uint64_t to, const void* d, int n, int ch);
    void (*invite_dialog)(void* ctx, uint64_t user, uint64_t lobby);
    const char* (*persona)(void* ctx, uint64_t user);
    bool (*set_limit)(void* ctx, uint64_t user, uint64_t lobby, int max);
    int (*get_limit)(void* ctx, uint64_t lobby);
    int (*list)(void* ctx, uint64_t user, const char* const* keys, const char* const* vals, int nf, uint64_t* out, int max);
    int (*friends)(void* ctx, uint64_t user, uint64_t* out, int max);
    bool (*friend_game)(void* ctx, uint64_t user, uint64_t* game, uint64_t* lobby);
};

struct FakeCounters
{
    int registered_cbs;
    int registered_results;
    uint64_t runs;
    uint64_t results_run;
    uint64_t results_io_fail;
    uint64_t results_dropped;
    uint64_t sess_posted;
    uint64_t accepted_calls;
    uint64_t pending_msgs;
    uint64_t closes;
    uint64_t relay_init;
    uint64_t sent;
    uint64_t delivered;
    uint64_t off_pump;
    uint64_t api_calls;
};

typedef void (*FakeInitFn)(const FakeBackend* b, uint64_t self, int hsteamuser, int logged_on, int overlay);
typedef void (*FakeDeliverFn)(uint64_t from, const void* d, int n, int ch);
typedef void (*FakePostFn)(int id, const void* d, int n);
typedef void (*FakeGetFn)(FakeCounters* out);
typedef void (*FakeAppFn)(uint32_t app);
