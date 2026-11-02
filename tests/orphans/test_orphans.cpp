#include "fake_or.h"
#include "s2m_wshot.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace s2m_aiwatch
{
    void tick();
}
namespace s2m_console
{
    std::string cmd_name(std::string_view full);
}

using fo::Obj;
using fo::W;
using namespace s2m_wshot::pure;

namespace
{
    std::vector<std::string> toks(const std::string& l)
    {
        std::vector<std::string> v;
        std::istringstream s(l);
        std::string t;
        while (s >> t) v.push_back(t);
        return v;
    }
    std::string rest(const std::string& l, int n)
    {
        size_t p = 0;
        for (int i = 0; i < n; ++i)
        {
            while (p < l.size() && l[p] == ' ') ++p;
            while (p < l.size() && l[p] != ' ') ++p;
        }
        while (p < l.size() && l[p] == ' ') ++p;
        return l.substr(p);
    }
    double D(const std::string& s) { return std::strtod(s.c_str(), nullptr); }
    Obj* N(const std::string& n)
    {
        auto it = W().byName.find(n);
        return it == W().byName.end() ? nullptr : it->second;
    }
    Obj* me()
    {
        for (auto& o : W().objs)
            if (o->alive && o->cls == "BP_Stalker2Character_C") return o.get();
        return nullptr;
    }
    Obj* sub(Obj* owner, const std::string& cls, const std::string& name)
    {
        Obj* o = fo::make(cls, name);
        o->owner = owner;
        return o;
    }
    std::string hexs(const std::string& s)
    {
        static const char* H = "0123456789abcdef";
        std::string o;
        for (unsigned char c : s) { o.push_back(H[c >> 4]); o.push_back(H[c & 15]); }
        return o;
    }
    std::string unhex(const std::string& s)
    {
        std::string o;
        for (size_t i = 0; i + 1 < s.size(); i += 2) o.push_back((char)std::strtol(s.substr(i, 2).c_str(), nullptr, 16));
        return o;
    }
    std::string num(double v)
    {
        char b[64];
        std::snprintf(b, sizeof b, "%.17g", v);
        return b;
    }

    int run_pure(const char* path)
    {
        std::ifstream f(path, std::ios::binary);
        std::string line;
        while (std::getline(f, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            std::string kind = line.substr(0, tab), a = unhex(line.substr(tab + 1)), out;
            if (kind == "wvrule")
            {
                auto r = wv_rule(a);
                out = r ? (r->path + "|" + r->vx + "|" + r->sid) : "nil";
            }
            else if (kind == "wssafe") out = ws_safe(a) ? "1" : "0";
            else if (kind == "wvsafe") out = wv_safe(a) ? "1" : "0";
            else if (kind == "wsparse")
            {
                for (auto& e : ws_parse(a)) out += num(e.sq) + "," + e.nm + "," + e.k + "," + e.pl + "," + num(e.n) + ";";
            }
            else if (kind == "strip") out = strip_dot(a) + "|" + strip_class(a) + "|" + (has_dot_leaf(a) ? "1" : "0") + "|" + last_seg(a);
            else if (kind == "cmd") out = s2m_console::cmd_name(a);
            else continue;
            std::printf("%s\t%s\t%s\n", kind.c_str(), hexs(a).c_str(), hexs(out).c_str());
        }
        return 0;
    }

    Obj* pawn(const std::string& n, double x, double y, double z, double yaw, long long uid, long long pid)
    {
        Obj* p = fo::make("BP_NPC_C", n);
        p->pawn = true;
        p->loc = {x, y, z};
        p->rot = {0, yaw, 0};
        p->uid = uid;
        p->pid = pid;
        p->ctl = sub(p, "AIController", n + "_ctl");
        p->brain = sub(p->ctl, "BrainComponent", n + "_brain");
        p->ctl->brain = p->brain;
        p->cm = sub(p, "CharacterMovementComponent", n + "_cm");
        p->mesh = sub(p, "SkeletalMeshComponent", n + "_mesh");
        p->ai = sub(p->mesh, "AnimInstance", n + "_ai");
        p->mesh->ai = p->ai;
        return p;
    }
    Obj* montage(const std::string& full)
    {
        if (full == "-") return nullptr;
        size_t d = full.rfind('.');
        std::string nm = d == std::string::npos ? full : full.substr(d + 1);
        return fo::make("AnimMontage", nm, false, full);
    }

    int run_scen(const char* path)
    {
        std::ifstream f(path, std::ios::binary);
        std::string line;
        int ln = 0;
        fo::init_modules();
        std::string lastText;
        bool hasText = false;
        while (std::getline(f, line))
        {
            ++ln;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto t = toks(line);
            if (t.empty() || t[0][0] == ';') continue;
            W().trace.clear();
            const std::string& c = t[0];
            if (c == "role") W().role = t[1];
            else if (c == "t") fo::advance(D(t[1]));
            else if (c == "flag") W().flags[t[1]] = (t[2].find('.') != std::string::npos) ? x0::Val::F(D(t[2])) : x0::Val::I(std::atoll(t[2].c_str()));
            else if (c == "me")
            {
                Obj* m = me();
                if (!m) { m = fo::make("BP_Stalker2Character_C", "Me"); m->pawn = true; }
                m->loc = {D(t[1]), D(t[2]), D(t[3])};
            }
            else if (c == "newme")
            {
                Obj* m = me();
                x0::Vec3 l = m ? m->loc : x0::Vec3{};
                if (m) m->alive = false;
                Obj* n = fo::make("BP_Stalker2Character_C", "Me");
                n->pawn = true;
                n->loc = l;
            }
            else if (c == "remote")
            {
                W().snap.ok = true;
                if (t[1] == "none") W().snap.has_pos = false;
                else
                {
                    W().snap.has_pos = true;
                    W().snap.x = D(t[1]);
                    W().snap.y = D(t[2]);
                    W().snap.z = D(t[3]);
                }
            }
            else if (c == "pawn") pawn(t[1], D(t[2]), D(t[3]), D(t[4]), D(t[5]), std::atoll(t[6].c_str()), std::atoll(t[7].c_str()));
            else if (c == "mv")
            {
                Obj* o = N(t[1]);
                o->loc = {D(t[2]), D(t[3]), D(t[4])};
                if (t.size() > 5) o->rot.yaw = D(t[5]);
            }
            else if (c == "vel") N(t[1])->vel = {D(t[2]), D(t[3]), 0};
            else if (c == "threat") N(t[1])->threat = t[2] == "1";
            else if (c == "dead") N(t[1])->isAlive = false;
            else if (c == "gone") N(t[1])->alive = false;
            else if (c == "lock") W().lock = t[1] == "-" ? "" : t[1];
            else if (c == "sleep") me()->sleeping = t[1] == "1";
            else if (c == "cmcon") N(t[1])->cm->tickOn = true;
            else if (c == "x1")
            {
                W().snap.ok = true;
                W().snap.seq = (uint64_t)std::atoll(t[1].c_str());
                W().snap.t = D(t[2]);
                W().x1 = t.size() > 3 ? rest(line, 3) : "";
            }
            else if (c == "weapon")
            {
                Obj* o = N(t[1]);
                o->weapon = sub(o, "SkeletalMeshComponent", t[1] + "_wpn");
                o->weapon->skel = fo::make("SkeletalMesh", t[2]);
                if (t.size() > 3 && t[3] != "-")
                {
                    std::string s = t[3];
                    size_t b = 0;
                    while (b <= s.size())
                    {
                        size_t e = s.find(',', b);
                        o->weapon->socks.insert(s.substr(b, e == std::string::npos ? std::string::npos : e - b));
                        if (e == std::string::npos) break;
                        b = e + 1;
                    }
                }
            }
            else if (c == "montage") N(t[1])->ai->mont = montage(t[2]);
            else if (c == "face")
            {
                Obj* o = N(t[1]);
                o->ai->face = sub(o->ai, "SkeletalMeshComponent", t[1] + "_face");
                o->ai->face->ai = sub(o->ai->face, "AnimInstance", t[1] + "_fai");
            }
            else if (c == "facemont") N(t[1])->ai->face->ai->mont = montage(t[2]);
            else if (c == "animref")
            {
                Obj* m = N(t[1])->ai->mont;
                if (m) m->animRef = montage(t[2]);
            }
            else if (c == "speaker")
            {
                Obj* o = N(t[1]);
                Obj* vc = fo::make("VoiceoverSpeechComponent", t[1] + "_vc");
                vc->spk = sub(o, "AkComponent", t[1] + "_spk");
            }
            else if (c == "spkev")
            {
                Obj* s = N(t[1] + "_spk");
                if (t[2] == "-") s->ev = nullptr;
                else
                {
                    size_t d = t[2].rfind('.');
                    s->ev = fo::make("AkAudioEvent", t[2].substr(d + 1), false, t[2]);
                }
            }
            else if (c == "asset")
            {
                size_t d = t[2].rfind('.');
                fo::make(t[1], t[2].substr(d + 1), true, t[2]);
            }
            else if (c == "natactor") W().nat[t[1]] = N(t[2]);
            else if (c == "mgr")
            {
                fo::make("CustomConsoleManagerAM", "AM");
                fo::make("CustomConsoleManagerSS", "SS");
            }
            else if (c == "med") fo::make("CppMediator", "Default__CppMediator", true, "/Script/Stalker2.Default__CppMediator");
            else if (c == "cdo")
            {
                fo::make("AkGameplayStatics", "AkGS", true, "/Script/AkAudio.Default__AkGameplayStatics");
                fo::make("NiagaraFunctionLibrary", "Niagara", true, "/Script/Niagara.Default__NiagaraFunctionLibrary");
            }
            else if (c == "step")
            {
                if (t[1] == "ws") W().rows["wshotWs"]();
                else if (t[1] == "aiw") W().rows["aiwatch"]();
            }
            else if (c == "out")
            {
                std::string s = W().tags["WS"]();
                fo::tr("out " + s);
            }
            else if (c == "shot")
            {
                Obj* o = N(t[1]);
                Obj* k = N(t[1] + "_iac");
                if (!k) k = sub(o, "ItemAppearanceComponent", t[1] + "_iac");
                W().hooks[L"/Script/Stalker2.ItemAppearanceComponent:OnShotSoundStart"](fo::ref(k));
            }
            else
            {
                std::printf("bad command line %d: %s\n", ln, line.c_str());
                return 2;
            }
            (void)lastText;
            (void)hasText;
            std::vector<std::string> tr = W().trace;
            std::sort(tr.begin(), tr.end());
            std::printf("== %d %s\n", ln, line.c_str());
            for (auto& s : tr) std::printf("%s\n", s.c_str());
        }
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::printf("usage: test_orphans pure|scen file\n"); return 2; }
    std::string m = argv[1];
    if (m == "pure") return run_pure(argv[2]);
    if (m == "scen") return run_scen(argv[2]);
    return 2;
}
