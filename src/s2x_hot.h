#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>
#include <polyhook2/Detour/x64Detour.hpp>

namespace RC
{
    class CppUserModBase;
}
namespace RC::Unreal
{
    class UFunction;
}

namespace x0::hot
{
#ifdef S2X_RELEASE
    using Detour = PLH::x64Detour;
    inline uint64_t cb(uint64_t id) { return id; }
    inline std::pair<int, int> ufn(RC::Unreal::UFunction*, std::pair<int, int> ids) { return ids; }
    inline void on_shutdown(std::function<void()>) {}
    inline RC::CppUserModBase* wrap(RC::CppUserModBase* m) { return m; }
#else
    class Detour;
    struct Reg
    {
        std::mutex mu;
        std::vector<Detour*> dets;
        std::vector<uint64_t> cbs;
        struct Ufn
        {
            RC::Unreal::UFunction* f;
            std::pair<int, int> ids;
            int32_t idx;
        };
        std::vector<Ufn> ufns;
        std::vector<std::function<void()>> downs;
    };
    inline Reg& reg()
    {
        static Reg r;
        return r;
    }

    class Detour : public PLH::x64Detour
    {
      public:
        Detour(uint64_t fn, uint64_t cb, uint64_t* tramp) : PLH::x64Detour(fn, cb, tramp)
        {
            std::lock_guard<std::mutex> g(reg().mu);
            reg().dets.push_back(this);
        }
        ~Detour() override
        {
            std::lock_guard<std::mutex> g(reg().mu);
            auto& v = reg().dets;
            v.erase(std::remove(v.begin(), v.end(), this), v.end());
        }
    };

    inline uint64_t cb(uint64_t id)
    {
        std::lock_guard<std::mutex> g(reg().mu);
        reg().cbs.push_back(id);
        return id;
    }

    std::pair<int, int> ufn(RC::Unreal::UFunction* f, std::pair<int, int> ids);

    inline void on_shutdown(std::function<void()> f)
    {
        std::lock_guard<std::mutex> g(reg().mu);
        reg().downs.push_back(std::move(f));
    }

    RC::CppUserModBase* wrap(RC::CppUserModBase* m);
    int shutdown_all();
#endif
}
