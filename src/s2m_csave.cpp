#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "s2m_csave_logic.h"
#include "s2x.h"

extern "C" void s2n_save_api_set_folder(const std::wstring* f);

namespace
{
    uint8_t* exe() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }

    bool rd_seh(const void* p, void* out, size_t n)
    {
        __try { std::memcpy(out, p, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool eq_seh(const uint8_t* p, const uint8_t* b, size_t n)
    {
        __try { return std::memcmp(p, b, n) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    const uint32_t kCmGlobal = 0xa2c8690;
    const uint32_t kSlmGlobal = 0xa2c8698;
    const uint32_t kCmPath = 0xE0;
    const uint32_t kSlmPath = 0x118;
    const uint32_t kCmCount = 0x148;
    const uint32_t kCmCur = 0x150;
    const uint32_t kCmWriting = 0x238;
    const uint32_t kCmWriteAgain = 0x239;
    const uint32_t kAssign = 0x21c034;
    const uint32_t kReread = 0x31ea550;
    const uint32_t kNewest = 0x33ab358;
    const uint8_t kAssignPro[] = {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x89, 0xce, 0x48, 0x39, 0xd1, 0x74, 0x34, 0x44, 0x8b, 0x46, 0x0c};
    const uint8_t kRereadPro[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x68, 0x48, 0x89, 0xd6, 0x48, 0x89, 0xcf};
    const uint8_t kNewestPro[] = {0x56, 0x44, 0x8b, 0x89, 0x48, 0x01, 0x00, 0x00, 0x45, 0x85, 0xc9, 0x0f, 0x8e, 0x91, 0x00, 0x00, 0x00, 0x45, 0x31, 0xc0};

    struct FStr
    {
        wchar_t* data;
        int32_t num;
        int32_t max;
    };

    struct Dlg
    {
        uint64_t p;
        int32_t i;
        int32_t pad;
    };

    using AssignFn = void* (*)(void*, const void*);
    using RereadFn = void (*)(uint64_t, Dlg*);
    using NewestFn = uint64_t (*)(uint64_t, int32_t*);

    struct St
    {
        bool tried = false, codeOk = false, newestOk = false;
        bool haveNormal = false;
        std::wstring normal, coop, coopDir;
        csx::Store active = csx::Store::normal;
        bool hasOv = false;
        csx::Store ov = csx::Store::normal;
        csx::Store apiWant = csx::Store::normal;
        bool pending = false;
        double pendAt = 0;
        bool timedOut = false;
        csx::Block lastBlock = csx::Block::ok;
        csx::Store lastWant = csx::Store::normal;
        int switches = 0, fails = 0;
        std::string lastNote;
        bool exiting = false;
        double exitAt = 0;
    };

    St& S()
    {
        static St s;
        return s;
    }

    uint64_t global(uint32_t rva)
    {
        uint64_t v = 0;
        rd_seh(exe() + rva, &v, 8);
        return v;
    }

    bool fstr_read(uint64_t at, std::wstring& out)
    {
        out.clear();
        FStr f{};
        if (!rd_seh(reinterpret_cast<void*>(at), &f, sizeof f)) return false;
        if (f.num < 0 || f.num > 520 || f.max < f.num) return false;
        if (f.num == 0 || !f.data) return true;
        std::vector<wchar_t> b((size_t)f.num);
        if (!rd_seh(f.data, b.data(), b.size() * sizeof(wchar_t))) return false;
        if (b.back() != 0) return false;
        out.assign(b.data(), b.size() - 1);
        return true;
    }

    bool call_assign(void* dst, const FStr* src)
    {
        __try { reinterpret_cast<AssignFn>(exe() + kAssign)(dst, src); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool call_reread(uint64_t cm)
    {
        Dlg d{0, 0, 0};
        __try { reinterpret_cast<RereadFn>(exe() + kReread)(cm, &d); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool call_newest(uint64_t cm, uint64_t& save)
    {
        __try { save = reinterpret_cast<NewestFn>(exe() + kNewest)(cm, nullptr); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool fstr_assign(uint64_t at, const std::wstring& v)
    {
        std::vector<wchar_t> b(v.begin(), v.end());
        b.push_back(0);
        FStr f{b.data(), (int32_t)b.size(), (int32_t)b.size()};
        if (!call_assign(reinterpret_cast<void*>(at), &f)) return false;
        std::wstring back;
        return fstr_read(at, back) && back == v;
    }

    std::string narrow(const std::wstring& w)
    {
        std::string a;
        for (wchar_t c : w) a += (c > 0 && c < 0x80) ? (char)c : '?';
        return a;
    }

    const std::wstring& name_of(csx::Store s) { return s == csx::Store::coop ? S().coop : S().normal; }

    void install()
    {
        St& s = S();
        s.tried = true;
        bool a = eq_seh(exe() + kAssign, kAssignPro, sizeof kAssignPro);
        bool r = eq_seh(exe() + kReread, kRereadPro, sizeof kRereadPro);
        s.codeOk = a && r;
        s.newestOk = eq_seh(exe() + kNewest, kNewestPro, sizeof kNewestPro);
        x0::logf("NOLA csave: copia de FString (exe+0x%x) %s, releitura do indice (exe+0x%x) %s -> saves co-op separados %s; save mais novo do indice (exe+0x%x) %s", kAssign,
                 a ? "ok" : "com bytes diferentes", kReread, r ? "ok" : "com bytes diferentes", s.codeOk ? "prontos" : "DESLIGADOS", kNewest, s.newestOk ? "ok" : "com bytes diferentes");
    }

    void folder_push(csx::Store st)
    {
        std::wstring f = csx::folder_of(name_of(st));
        if (!f.empty()) s2n_save_api_set_folder(&f);
    }

    bool capture(uint64_t cm, uint64_t slm)
    {
        St& s = S();
        std::string d = x0::fs("coopsavedir", "SaveGamesNOLA");
        std::wstring dir(d.begin(), d.end());
        if (s.haveNormal && (dir == s.coopDir || s.active == csx::Store::coop)) return !s.coop.empty();
        std::wstring seen;
        if (!s.haveNormal)
        {
            std::wstring b;
            if (!fstr_read(cm + kCmPath, seen) || !fstr_read(slm + kSlmPath, b) || seen.empty() || seen != b) return false;
            if (csx::same_ci(csx::folder_of(seen), dir))
            {
                s.normal = L"SaveGames" + std::wstring(1, csx::is_sep(seen.back()) ? seen.back() : L'\\');
                s.active = csx::Store::coop;
                s.coop = seen;
                s.coopDir = dir;
                x0::logf("NOLA csave: o jogo ja estava no store co-op (%s); store normal assumido %s", narrow(seen).c_str(), narrow(s.normal).c_str());
            }
            else
                s.normal = seen;
            s.haveNormal = true;
            folder_push(s.active);
            if (s.active == csx::Store::coop) return true;
        }
        std::wstring cp;
        if (!csx::coop_path(s.normal, dir, cp))
        {
            if (s.lastNote != d)
            {
                s.lastNote = d;
                x0::logf("NOLA csave: coopsavedir '%s' invalido (vazio, caractere proibido, nao ASCII, >48 ou igual ao normal): saves co-op desligados", d.c_str());
            }
            s.coop.clear();
            s.coopDir = dir;
            return false;
        }
        s.coop = cp;
        s.coopDir = dir;
        return true;
    }

    csx::Probe probe(uint64_t cm, uint64_t slm)
    {
        St& s = S();
        csx::Probe p;
        p.pending = s.pending;
        p.objs = cm != 0 && slm != 0;
        if (!p.objs) return p;
        std::wstring a, b;
        const std::wstring& cur = name_of(s.active);
        p.paths_match = fstr_read(cm + kCmPath, a) && fstr_read(slm + kSlmPath, b) && a == cur && b == cur;
        rd_seh(reinterpret_cast<void*>(cm + kCmCount), &p.campaigns, 4);
        rd_seh(reinterpret_cast<void*>(cm + kCmWriting), &p.writing, 1);
        rd_seh(reinterpret_cast<void*>(cm + kCmWriteAgain), &p.write_again, 1);
        p.pawn = !s.exiting && x0::first_of(L"BP_Stalker2Character_C").valid();
        return p;
    }

    bool do_switch(uint64_t cm, uint64_t slm, csx::Store to)
    {
        St& s = S();
        const std::wstring& from = name_of(s.active);
        const std::wstring& dst = name_of(to);
        bool c1 = fstr_assign(cm + kCmPath, dst);
        bool c2 = c1 && fstr_assign(slm + kSlmPath, dst);
        if (!c1 || !c2)
        {
            bool r1 = fstr_assign(cm + kCmPath, from);
            bool r2 = fstr_assign(slm + kSlmPath, from);
            ++s.fails;
            x0::logf("NOLA csave: troca %s -> %s FALHOU (CampaignManager %s, SaveLoadManager %s); voltou ao anterior (%s/%s)", csx::store_name(s.active), csx::store_name(to), c1 ? "ok" : "falhou",
                     c2 ? "ok" : "falhou", r1 ? "ok" : "FALHOU", r2 ? "ok" : "FALHOU");
            if (!r1 || !r2) s.codeOk = false;
            return false;
        }
        if (!call_reread(cm))
        {
            fstr_assign(cm + kCmPath, from);
            fstr_assign(slm + kSlmPath, from);
            ++s.fails;
            s.codeOk = false;
            x0::log("NOLA csave: releitura do indice levantou excecao; caminho restaurado, saves co-op desligados nesta sessao");
            return false;
        }
        csx::Store was = s.active;
        s.active = to;
        s.pending = true;
        s.pendAt = x0::now();
        s.timedOut = false;
        ++s.switches;
        folder_push(to);
        int32_t n = -1, cur = 0;
        rd_seh(reinterpret_cast<void*>(cm + kCmCount), &n, 4);
        rd_seh(reinterpret_cast<void*>(cm + kCmCur), &cur, 4);
        x0::logf("NOLA csave: store %s -> %s (%s); indice relido (campanhas agora %d, atual %d)", csx::store_name(was), csx::store_name(to), narrow(dst).c_str(), (int)n, (int)cur);
        return true;
    }

    csx::Store wanted()
    {
        St& s = S();
        bool on = x0::fi("coopsaves", 1) == 1;
        return csx::want(on, s.hasOv, s.ov, s.apiWant);
    }

    void step()
    {
        St& s = S();
        if (!s.tried) install();
        if (!s.codeOk) return;
        uint64_t cm = global(kCmGlobal), slm = global(kSlmGlobal);
        if (s.pending && cm)
        {
            int32_t n = 0;
            rd_seh(reinterpret_cast<void*>(cm + kCmCount), &n, 4);
            if (n > 0)
            {
                s.pending = false;
                int32_t cur = 0;
                rd_seh(reinterpret_cast<void*>(cm + kCmCur), &cur, 4);
                x0::logf("NOLA csave: indice do store %s carregado (%d campanhas, atual %d)", csx::store_name(s.active), (int)n, (int)cur);
            }
            else if (!s.timedOut && x0::now() - s.pendAt > 30)
            {
                s.timedOut = true;
                s.pending = false;
                x0::logf("NOLA csave: indice do store %s ainda sem campanhas apos 30 s", csx::store_name(s.active));
            }
        }
        if (s.exiting && x0::now() - s.exitAt > 60 && x0::first_of(L"BP_Stalker2Character_C").valid())
        {
            s.exiting = false;
            x0::log("NOLA csave: world exit 60 s ago but the world is still loaded: the store switch waits for the main menu again");
        }
        if (!cm || !slm || !capture(cm, slm)) return;
        csx::Store w = wanted();
        if (w == s.active)
        {
            s.lastBlock = csx::Block::ok;
            if (s.exiting && !s.pending) s.exiting = false;
            return;
        }
        csx::Probe p = probe(cm, slm);
        csx::Block b = csx::safe(p);
        if (b != csx::Block::ok)
        {
            if (b != s.lastBlock || w != s.lastWant)
                x0::logf("NOLA csave: troca para o store %s esperando: %s", csx::store_name(w), csx::block_name(b));
            s.lastBlock = b;
            s.lastWant = w;
            return;
        }
        s.lastBlock = csx::Block::ok;
        s.lastWant = w;
        do_switch(cm, slm, w);
    }

    std::string status_line()
    {
        St& s = S();
        char b[400];
        std::snprintf(b, sizeof b, "csave: store=%s quer=%s pronto=%d codigo=%s normal=%s coop=%s driver=%s trocas=%d falhas=%d espera=%s", csx::store_name(s.active), csx::store_name(wanted()),
                      (int)x0::save_store_ready(), s.codeOk ? "ok" : (s.tried ? "NAO" : "?"), narrow(s.normal).c_str(), narrow(s.coop).c_str(), s.hasOv ? "comando" : "menu",
                      s.switches, s.fails, csx::block_name(s.lastBlock));
        return b;
    }

    void csave_init()
    {
        x0::every(x0::Mod::save, "csave", x0::Rate::t1000, [] { step(); });
        x0::cmd(x0::Mod::save, "savestore", [](const std::vector<std::string>& args) {
            St& s = S();
            std::string a = args.size() < 2 ? "status" : args[1];
            if (a == "coop" || a == "normal")
            {
                s.hasOv = true;
                s.ov = a == "coop" ? csx::Store::coop : csx::Store::normal;
                step();
            }
            else if (a == "auto")
            {
                s.hasOv = false;
                step();
            }
            else if (a != "status")
                return std::string("uso: savestore [normal|coop|auto|status]");
            return status_line();
        });
        x0::status(x0::Mod::save, [] { return status_line(); });
    }

    ::x0::ModReg g_csave_reg(::x0::Mod::save, "csave", &csave_init);
}

namespace x0
{
    void save_store_want(SaveStore st)
    {
        St& s = S();
        s.apiWant = st == SaveStore::coop ? csx::Store::coop : csx::Store::normal;
        if (on_game_thread()) step();
    }

    SaveStore save_store() { return S().active == csx::Store::coop ? SaveStore::coop : SaveStore::normal; }

    void save_store_world_exit()
    {
        St& s = S();
        s.exiting = true;
        s.exitAt = x0::now();
    }

    bool save_store_ready()
    {
        St& s = S();
        return !s.pending && s.haveNormal && wanted() == s.active;
    }

    bool save_store_switching()
    {
        St& s = S();
        return s.codeOk && s.haveNormal && (s.pending || wanted() != s.active);
    }

    int save_store_has_save()
    {
        St& s = S();
        if (!s.tried) install();
        uint64_t cm = global(kCmGlobal);
        if (!s.newestOk || !cm || s.pending) return -1;
        uint64_t save = 0;
        if (!call_newest(cm, save)) return -1;
        return save ? 1 : 0;
    }
}
