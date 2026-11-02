#pragma once
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "s2m_travel.h"

using namespace s2t;

struct FakeEnv : Env {
    bool host = false;
    double t = 100;
    long long unix = 1700000000;
    std::vector<std::string> logs;
    std::map<std::string, double> flags;
    bool peer = true;
    std::map<std::string, std::string> x1;
    bool hasRemote = false;
    Remote remote;
    bool kzOut = false;
    bool pawnOk = true;
    Vec3 pos;
    double yaw = 0;
    std::uint64_t addr = 1000;
    int mm = 1;
    double hp = 100;
    double bleed = 0;
    bool falling = false;
    bool cinLocked = false;
    bool sleeping = false;
    bool dlgValid = false;
    bool dlgVis = false;
    bool dlgFound = false;
    bool lifeDownV = false, lifeFrozenV = false, emGodV = false, ndDeadV = false, leadMine = false;
    bool accept = true;
    bool teleportAccept = true;
    std::vector<std::string> calls;
    std::function<TraceHit(double, double, double, double, double, double)> traceFn;
    std::uint64_t nextActor = 500;
    int stream = 0;
    std::vector<std::uint64_t> destroyed;
    double protect = 0;
    std::string godUid = "uid-1";
    int teleportTries = 0;
    std::function<bool(double, double, double)> teleportOk;
    int nviews = 1;
    bool hubOn = false;
    int mypid = 1;
    std::map<int, std::map<std::string, std::string>> vx1;
    std::map<int, Remote> vremote;

    FakeEnv() {
        std::ifstream f("D:\\modding\\cpp\\S2CoopPort\\data\\flags_s2core.tsv");
        std::string line;
        std::getline(f, line);
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::vector<std::string> c;
            std::stringstream ss(line);
            std::string x;
            while (std::getline(ss, x, '\t')) c.push_back(x);
            if (c.size() >= 4 && c[0].size() > 1 && c[0][0] == 's') flags[c[0].substr(1)] = std::stod(c[3]);
        }
        flags["leashm"] = 350;
        traceFn = [](double, double, double, double, double, double) { return TraceHit{true, false, false, 0}; };
    }

    void advance(double dt) { t += dt; }
    bool isHost() override { return host; }
    double now() override { return t; }
    long long unixTime() override { return unix + static_cast<long long>(t - 100); }
    void log(const std::string& line) override { logs.push_back(line); }
    bool flag(const char* name, double& out) override {
        auto it = flags.find(name);
        if (it == flags.end()) return false;
        out = it->second;
        return true;
    }
    bool peerUp() override { return peer; }
    bool x1part(const char* tag, std::string& out) override {
        auto it = x1.find(tag);
        if (it == x1.end()) return false;
        out = it->second;
        return true;
    }
    int views() override { return nviews; }
    bool hub() override { return hubOn; }
    bool x1partV(int v, const char* tag, std::string& out) override {
        if (v == 0) return x1part(tag, out);
        auto it = vx1[v].find(tag);
        if (it == vx1[v].end()) return false;
        out = it->second;
        return true;
    }
    bool remoteInV(int v, const char* what, Remote& out) override {
        if (v == 0) return remoteIn(what, out);
        auto it = vremote.find(v);
        if (it == vremote.end()) return false;
        out = it->second;
        return true;
    }
    int viewPid(int v) override { return host ? v + 1 : (v == 0 ? 0 : -1); }
    int myPid() override { return host ? 0 : mypid; }
    bool remoteIn(const char*, Remote& out) override {
        if (!hasRemote || kzOut) return false;
        out = remote;
        return true;
    }
    bool inWorld(double x, double y, double z) override {
        if (std::isnan(x) || std::isnan(y) || std::isnan(z)) return false;
        if (std::fabs(x) > 5000000 || std::fabs(y) > 5000000) return false;
        return z >= -20000 && z <= 1000000;
    }
    bool pawnValid() override { return pawnOk; }
    bool pawnPos(Vec3& o) override { if (!pawnOk) return false; o = pos; return true; }
    bool pawnYaw(double& o) override { if (!pawnOk) return false; o = yaw; return true; }
    std::uint64_t pawnAddr() override { return addr; }
    bool pawnSetLocation(double x, double y, double z) override {
        calls.push_back("set " + std::to_string((long)x) + "," + std::to_string((long)y) + "," + std::to_string((long)z));
        if (!pawnOk || !accept) return false;
        pos = {x, y, z};
        return true;
    }
    bool pawnTeleportTo(double x, double y, double z, double) override {
        ++teleportTries;
        calls.push_back("tp " + std::to_string((long)x) + "," + std::to_string((long)y) + "," + std::to_string((long)z));
        if (!pawnOk) return false;
        if (teleportOk && !teleportOk(x, y, z)) return false;
        if (!teleportAccept) return false;
        pos = {x, y, z};
        return true;
    }
    bool setMovementMode(int m) override { calls.push_back("mm" + std::to_string(m)); mm = m; return pawnOk; }
    int movementMode() override { return mm; }
    bool disableMovement() override { calls.push_back("disable"); mm = 0; return pawnOk; }
    bool stopMovement() override { calls.push_back("stop"); return pawnOk; }
    bool isFalling(bool& o) override { o = falling; return pawnOk; }
    bool getHP(double& o) override { o = hp; return pawnOk; }
    bool forceSetHP(double v) override { calls.push_back("hp" + std::to_string((long)v)); hp = v; return pawnOk; }
    bool getBleeding(double& o) override { o = bleed; return pawnOk; }
    bool forceSetBleeding(double v) override { bleed = v; return pawnOk; }
    bool enableInput() override { return true; }
    bool disableInput() override { return true; }
    std::string unkillableOn() override { calls.push_back("god on"); return godUid; }
    bool unkillableOff(const std::string&) override { calls.push_back("god off"); return true; }
    bool pcCinematicLocked() override { return cinLocked; }
    bool pawnSleeping() override { return sleeping; }
    void dialogRescan() override { dlgValid = dlgFound; }
    bool dialogValid() override { return dlgValid; }
    bool dialogVisible() override { return dlgVis; }
    TraceHit trace(double ax, double ay, double az, double bx, double by, double bz) override { return traceFn(ax, ay, az, bx, by, bz); }
    ActorRef spawnStreamingSource(double, double, double) override { calls.push_back("src"); return ++nextActor; }
    int streamReady(double, double, double, double radius) override { calls.push_back("stream " + std::to_string(static_cast<int>(radius))); return stream; }
    void destroyActor(ActorRef r) override { destroyed.push_back(r); }
    bool legacySafeTeleport(double x, double y, double z) override {
        calls.push_back("legacy " + std::to_string((long)x) + "," + std::to_string((long)y) + "," + std::to_string((long)z));
        return true;
    }
    bool lifeDown() override { return lifeDownV; }
    bool lifeDownFrozen() override { return lifeFrozenV; }
    bool emGodHeld() override { return emGodV; }
    bool ndDead() override { return ndDeadV; }
    bool leadLockMine() override { return leadMine; }
    void dmgProtectUntil(double v) override { if (v > protect) protect = v; }

    bool hasLog(const std::string& sub) const {
        for (auto& l : logs) if (l.find(sub) != std::string::npos) return true;
        return false;
    }
    int countLog(const std::string& sub) const {
        int n = 0;
        for (auto& l : logs) if (l.find(sub) != std::string::npos) ++n;
        return n;
    }
    bool hasCall(const std::string& s) const {
        for (auto& c : calls) if (c == s) return true;
        return false;
    }
};

inline std::string slurp(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
