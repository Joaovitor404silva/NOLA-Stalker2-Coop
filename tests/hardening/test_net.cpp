#include "s2r_net.h"
#include <cassert>
#include <iostream>
using namespace s2r;
using js::V;
V header(const char* sid, int f) {
    V v=V::Object(); v.set("sid",V::Str(sid));v.set("f",V::Int(f));
    V ids=V::Arr();ids.a.push_back(V::Int(1));v.set("ids",ids); return v;
}
V npc(const char* sid,int f) {
    V v=header(sid,f), n=V::Object();n.set("id",V::Int(1));n.set("x",V::Int(f));v.set("n",n);return v;
}
int main() {
    std::string err,out;
    for(int level: {0,6}) {
        auto wire=z::compress(std::string(5000,'x'),level);
        assert(z::decompress(wire,out,&err,5000) && out.size()==5000);
        assert(!z::decompress(wire,out,&err,4999) && out.size()<=4999);
        assert(!z::decompress(wire,out,&err,0) && out.empty());
    }
    V big=V::Object();big.set("data",V::Str(std::string(proto::MAX_PACKET_JSON+1,'a')));
    uint8_t typ=0;V decoded;
    assert(proto::unpack(proto::pack(proto::T_HDR,big),typ,decoded,err)==proto::Unpack::corrupt);
    proto::Assembler a;
    auto feed=[&](uint8_t t,V v) { assert(a.feed(proto::pack(t,v),1,err)==proto::Assembler::Err::none); };
    feed(proto::T_HDR,header("old",20));feed(proto::T_NPC,npc("old",20));
    feed(proto::T_HDR,header("new",1));feed(proto::T_NPC,npc("new",2));
    feed(proto::T_HDR,header("old",21));feed(proto::T_NPC,npc("old",22));
    feed(proto::T_NPC,npc("unknown",50));feed(proto::T_HDR,header("new",0));
    assert(a.hdr()->get("sid")->s=="new");assert(a.stale==4);
    assert(a.render({},1,0,out));assert(js::parse(out,decoded));
    assert(decoded.get("npcs")->a.size()==1);
    assert(decoded.get("npcs")->a[0].get("x")->num()==2);
    // An unchanged NPC must be resent immediately after changing session.
    proto::Sender sender;proto::AppCache cache;std::vector<std::string> packets;
    auto snapshot=[&](const char* sid){V v=header(sid,1);v.set("v",V::Int(2));V ns=V::Arr();ns.a.push_back(*npc(sid,1).get("n"));v.set("npcs",ns);return js::dumps(v,true);};
    assert(sender.split(snapshot("a"),1,cache,packets,err)==proto::Sender::Err::none);assert(packets.size()==2);
    assert(sender.split(snapshot("b"),1.1,cache,packets,err)==proto::Sender::Err::none);assert(packets.size()==2);
    std::cout<<"PASS: decompression budgets, session replay rejection, NPC isolation, reconnect resend\n";
}
