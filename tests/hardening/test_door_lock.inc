#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <iostream>
using Ref = int;
const char* tf(bool b) { return b ? "true" : "false"; }
template<class... A> std::string fmt(const char*, A...) { return {}; }
struct Env {
    bool host=false, present=true, locked=true, accepts=false, readable=true;
    int calls=0;
    bool is_host() { return host; }
    int flag(const char*, int n) { return n; }
    void list_class(const char*, std::vector<Ref>& out) { if(present) out.push_back(1); }
    bool call_bool(Ref, const char*, bool& out) { out=locked; return readable; }
    bool call(Ref, const char* method, std::initializer_list<int>) {
        ++calls; if(accepts) locked=std::string(method)=="LockDoor"; return accepts;
    }
    void log(const std::string&) {}
};
struct World {
    struct OutEnt { std::string key; bool open=false; double at=0; };
    struct DoorPend { std::string st; double at=0; int n=0; };
    struct Door {
        std::map<std::string, DoorPend> lpend;
        std::map<std::string, double> lans;
        std::map<std::string, char> lapp;
        std::map<std::string, OutEnt> lout;
    } door_;
    Env env_;
    Ref key_find(const std::vector<Ref>& objs, const std::string&) { return objs.empty()?0:objs[0]; }
    bool door_key(Ref, std::string& k) { k="actual"; return true; }
    void put_out(std::map<std::string, OutEnt>& out, const std::string& k, OutEnt e) { out[k]=e; }
    void lock_receive(const std::string&, char, double);
};
// PRODUCTION_LOCK_RECEIVE
int main() {
    World w;
    w.lock_receive("wire", 'u', 0);
    assert(w.env_.calls==1 && w.door_.lapp.empty() && w.door_.lpend.count("wire"));
    w.lock_receive("wire", 'u', 1); assert(w.env_.calls==1);
    w.env_.accepts=true;
    w.lock_receive("wire", 'u', 2);
    assert(w.env_.calls==2 && !w.env_.locked && w.door_.lpend.empty());
    assert(w.door_.lapp.at("wire")=='u' && w.door_.lapp.at("actual")=='u');
    w.lock_receive("wire", 'u', 4); assert(w.env_.calls==2);
    World missing; missing.env_.present=false;
    for(int n=0;n<=200;++n) missing.lock_receive("wire", 'u', n*10);
    assert(missing.door_.lapp.empty() && missing.door_.lpend.empty());
    missing.env_.present=true; missing.env_.accepts=true;
    missing.lock_receive("wire", 'u', 2010); assert(!missing.env_.locked);
    World host; host.env_.host=true; host.env_.locked=false;
    host.lock_receive("wire", 'l', 0);
    assert(host.env_.calls==0 && host.door_.lout.count("actual"));
    std::cout << "PASS: rejected door unlock retries, backoff, verified cache, late loading and host authority\n";
}
