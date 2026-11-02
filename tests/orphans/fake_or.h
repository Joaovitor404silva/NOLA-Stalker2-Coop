#pragma once
#include "s2x.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fo
{
    struct Obj
    {
        int idx = 0, serial = 1;
        bool alive = true, pawn = false;
        std::string cls, name, full;
        x0::Vec3 loc, vel, cmVel, cmAcc;
        x0::Rot rot;
        bool isAlive = true, threat = false, sleeping = false, tickOn = true, cmReq = false;
        long long uid = 0, pid = 0;
        Obj *owner = nullptr, *ctl = nullptr, *brain = nullptr, *cm = nullptr, *mesh = nullptr, *ai = nullptr, *mont = nullptr, *weapon = nullptr, *skel = nullptr;
        Obj *face = nullptr, *faceAi = nullptr, *spk = nullptr, *ev = nullptr, *animRef = nullptr;
        std::set<std::string> socks;
        bool hidden = false, listed = false;
    };
    struct Timer
    {
        double at;
        int seq;
        std::function<void()> fn;
    };
    struct World
    {
        std::vector<std::unique_ptr<Obj>> objs;
        std::map<std::string, Obj*> byName;
        std::map<std::wstring, Obj*> paths;
        double t = 100;
        std::string role = "host";
        std::map<std::string, x0::Val> flags;
        x0::Snap snap;
        std::string x1;
        std::vector<std::string> trace;
        std::map<std::string, std::function<void()>> rows;
        std::map<std::string, x0::Producer> tags;
        std::map<std::wstring, std::function<void(const x0::Ref&)>> hooks;
        std::vector<std::function<void(const x0::Snap&)>> snaps;
        std::vector<Timer> timers;
        int tseq = 0;
        std::string lock;
        std::map<std::string, Obj*> nat;
        std::vector<std::pair<x0::Mod, void (*)()>> inits;
        int nviews = 1, you = 1;
        std::string vx1[4];
        std::map<int, Obj*> pup, pupw;
    };
    World& W();
    Obj* make(const std::string& cls, const std::string& name, bool path = false, const std::string& full = "");
    x0::Ref ref(Obj* o);
    void tr(const std::string& s);
    void init_modules();
    void advance(double t);
}
