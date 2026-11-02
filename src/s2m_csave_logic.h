#pragma once
#include <cstdint>
#include <cwctype>
#include <string>

namespace csx
{
    enum class Store : uint8_t { normal = 0, coop = 1 };

    inline const char* store_name(Store s) { return s == Store::coop ? "coop" : "normal"; }

    enum class Block : uint8_t { ok = 0, noobj, mismatch, nologin, writing, world, pending };

    inline const char* block_name(Block b)
    {
        switch (b)
        {
        case Block::ok: return "ok";
        case Block::noobj: return "sem CampaignManager/SaveLoadManager";
        case Block::mismatch: return "SaveGamesPath do jogo nao e o nome esperado";
        case Block::nologin: return "indice de saves ainda nao lido (antes do login)";
        case Block::writing: return "indice de saves sendo gravado";
        case Block::world: return "mundo carregado (so troca no menu principal)";
        case Block::pending: return "releitura do indice em andamento";
        }
        return "?";
    }

    inline Store want(bool on, bool has_override, Store ov, Store menu)
    {
        if (!on) return Store::normal;
        return has_override ? ov : menu;
    }

    struct Probe
    {
        bool objs = false;
        bool paths_match = false;
        int32_t campaigns = 0;
        uint8_t writing = 0, write_again = 0;
        bool pawn = false;
        bool pending = false;
    };

    inline Block safe(const Probe& p)
    {
        if (p.pending) return Block::pending;
        if (!p.objs) return Block::noobj;
        if (!p.paths_match) return Block::mismatch;
        if (p.campaigns <= 0) return Block::nologin;
        if (p.writing || p.write_again) return Block::writing;
        if (p.pawn) return Block::world;
        return Block::ok;
    }

    inline bool is_sep(wchar_t c) { return c == L'\\' || c == L'/'; }

    inline std::wstring folder_of(const std::wstring& path)
    {
        std::wstring f = path;
        while (!f.empty() && is_sep(f.back())) f.pop_back();
        return f;
    }

    inline bool same_ci(const std::wstring& a, const std::wstring& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::towlower(a[i]) != std::towlower(b[i])) return false;
        return true;
    }

    inline bool coop_path(const std::wstring& normal, const std::wstring& dir, std::wstring& out)
    {
        out.clear();
        std::wstring nf = folder_of(normal);
        if (nf.empty() || dir.empty() || dir.size() > 48) return false;
        if (dir == L"." || dir == L"..") return false;
        for (wchar_t c : dir)
        {
            if (c < 0x20 || c >= 0x7f || c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|') return false;
        }
        if (dir.back() == L'.' || dir.back() == L' ' || dir.front() == L' ') return false;
        if (same_ci(dir, nf)) return false;
        wchar_t sep = is_sep(normal.empty() ? 0 : normal.back()) ? normal.back() : L'\\';
        out = dir + sep;
        return true;
    }
}
