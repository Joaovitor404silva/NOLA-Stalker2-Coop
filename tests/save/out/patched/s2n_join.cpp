#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "s2n_peframe.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
extern "C" bool s2n_save_api_folder(std::wstring* out);
#ifndef S2N_JOIN_TEST
#ifndef RC_UNREAL_DISABLE_PROPERTY_DEPRECATION_WARNINGS
#define RC_UNREAL_DISABLE_PROPERTY_DEPRECATION_WARNINGS
#endif
#include <DynamicOutput/DynamicOutput.hpp>
#include <LuaMadeSimple/LuaMadeSimple.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FField.hpp>
#include <Unreal/FMemory.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>
using namespace RC;
using namespace RC::Unreal;
#endif

namespace s2n_join
{
    static auto narrow(const std::wstring& w) -> std::string
    {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }
    static auto widen(const std::string& s) -> std::wstring
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    }
    static std::string g_last;
    static auto note(const std::string& s) -> void
    {
        g_last = s;
#ifndef S2N_JOIN_TEST
        Output::send<LogLevel::Default>(STR("[S2CoopNative] S2SX: {}\n"), widen(s));
#else
        std::printf("S2SX: %s\n", s.c_str());
#endif
    }
    static auto read_file(const std::wstring& p, std::string& out, size_t maxBytes = 0) -> bool
    {
        out.clear();
        HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER sz{};
        if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (1LL << 30)) { CloseHandle(h); return false; }
        size_t n = (size_t)sz.QuadPart;
        if (maxBytes && n > maxBytes) n = maxBytes;
        out.resize(n);
        size_t got = 0;
        while (got < n)
        {
            DWORD r = 0;
            DWORD want = (DWORD)std::min<size_t>(n - got, 1 << 24);
            if (!ReadFile(h, out.data() + got, want, &r, nullptr) || r == 0) break;
            got += r;
        }
        CloseHandle(h);
        out.resize(got);
        return got == n;
    }
    static auto fnv64(const char* d, size_t n) -> uint64_t
    {
        uint64_t h = 1469598103934665603ULL;
        for (size_t i = 0; i < n; ++i) { h ^= (uint8_t)d[i]; h *= 1099511628211ULL; }
        return h;
    }
    static auto hex16(uint64_t v) -> std::string { char b[24]; std::snprintf(b, sizeof b, "%016llx", (unsigned long long)v); return b; }
    static auto write_atomic(const std::wstring& p, const std::string& data) -> bool
    {
        std::wstring t = p + L".sxtmp";
        HANDLE h = CreateFileW(t.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        size_t put = 0;
        bool ok = true;
        while (put < data.size())
        {
            DWORD w = 0;
            DWORD want = (DWORD)std::min<size_t>(data.size() - put, 1 << 24);
            if (!WriteFile(h, data.data() + put, want, &w, nullptr) || w == 0) { ok = false; break; }
            put += w;
        }
        if (ok) FlushFileBuffers(h);
        CloseHandle(h);
        if (!ok || !MoveFileExW(t.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { DeleteFileW(t.c_str()); return false; }
        std::string back;
        return read_file(p, back) && back.size() == data.size() && fnv64(back.data(), back.size()) == fnv64(data.data(), data.size());
    }
    static auto trim(std::string s) -> std::string
    {
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        return s.substr(i);
    }
    static auto first_line(const std::wstring& p) -> std::string
    {
        std::string d;
        if (!read_file(p, d, 4096)) return {};
        size_t e = d.find('\n');
        return trim(e == std::string::npos ? d : d.substr(0, e));
    }
    static auto wall_ms() -> int64_t
    {
        FILETIME ft;
        GetSystemTimePreciseAsFileTime(&ft);
        ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
        return (int64_t)(u.QuadPart / 10000ULL);
    }
    static auto tok_ok(const std::string& s) -> bool
    {
        if (s.empty() || s.size() > 64) return false;
        for (char c : s) if (!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) return false;
        return true;
    }

    static auto suffix() -> std::wstring
    {
        std::wstring cl = GetCommandLineW() ? GetCommandLineW() : L"";
        std::wstring lo = cl;
        for (auto& c : lo) c = (wchar_t)std::towlower(c);
        const std::wstring key = L"-saveddirsuffix=";
        size_t p = lo.find(key);
        if (p == std::wstring::npos) return {};
        p += key.size();
        bool q = p < cl.size() && cl[p] == L'"';
        if (q) ++p;
        size_t e = p;
        while (e < cl.size() && (q ? cl[e] != L'"' : (cl[e] != L' ' && cl[e] != L'\t'))) ++e;
        return cl.substr(p, e - p);
    }
    static std::wstring g_testDir;
    static auto saved_dir() -> std::wstring
    {
        if (!g_testDir.empty()) return g_testDir;
        wchar_t buf[MAX_PATH * 2] = {};
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
        if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return {};
        std::wstring d = std::wstring(buf, n) + L"\\Stalker2\\Saved";
        std::wstring s = suffix();
        if (!s.empty()) d += L"_" + s;
        return d + L"\\";
    }
    static auto mtime(const std::wstring& p, ULONGLONG& mt) -> bool
    {
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return false;
        mt = ((ULONGLONG)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
        return true;
    }
    static auto data_dir() -> std::wstring
    {
        std::wstring dir = saved_dir();
        if (dir.empty()) return {};
        const wchar_t* plats[] = {L"STEAM", L"NULL"};
        std::wstring best, fo;
        if (!s2n_save_api_folder(&fo) || fo.empty()) fo = L"SaveGames";
        ULONGLONG bm = 0;
        for (auto* pl : plats)
        {
            ULONGLONG mt = 0;
            if (mtime(dir + pl + L"\\" + fo + L"\\CampaignsSave.sav", mt) && (best.empty() || mt > bm)) { best = pl; bm = mt; }
        }
        return best.empty() ? std::wstring() : dir + best + L"\\" + fo + L"\\Data\\";
    }
    static auto newest_guid(const std::wstring& ddir) -> std::string
    {
        if (ddir.empty()) return {};
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW((ddir + L"*.sav").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return {};
        std::wstring best;
        ULONGLONG bm = 0;
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring nm = fd.cFileName;
            if (nm.size() != 36) continue;
            ULONGLONG mt = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
            if (best.empty() || mt > bm) { best = nm.substr(0, 32); bm = mt; }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return narrow(best);
    }

    struct Pkg
    {
        std::string id, guid, cid, ap, data;
        long long size = -1;
        uint64_t hash = 0;
        int kh = -1;
    };
    static auto field(const std::string& hdr, const char* k) -> std::string
    {
        std::string key = std::string(" ") + k + "=";
        size_t p = hdr.find(key);
        if (p == std::string::npos) return {};
        p += key.size();
        size_t e = hdr.find(' ', p);
        return hdr.substr(p, e == std::string::npos ? std::string::npos : e - p);
    }
    static auto build_ap(const std::string& log, int kh, const std::string& cid, int& have) -> std::string
    {
        std::vector<std::string> order;
        std::unordered_map<std::string, int> cnt;
        have = 0;
        size_t p = 0;
        while (p < log.size() && have < kh)
        {
            size_t e = log.find('\n', p);
            std::string ln = log.substr(p, e == std::string::npos ? std::string::npos : e - p);
            p = e == std::string::npos ? log.size() : e + 1;
            while (!ln.empty() && ln.back() == '\r') ln.pop_back();
            size_t i = 0;
            while (i < ln.size() && (isdigit((unsigned char)ln[i]) || ln[i] == '.')) ++i;
            if (i == 0 || i >= ln.size() || ln[i] != '\t' || i + 1 >= ln.size()) continue;
            std::string ev = ln.substr(i + 1);
            if (!cnt.count(ev)) order.push_back(ev);
            ++cnt[ev];
            ++have;
        }
        std::string o = "#S2CA 1" + (cid.empty() ? std::string() : " " + cid) + "\n";
        if (!cid.empty()) o += "W\t" + cid + "\n";
        for (auto& ev : order) o += std::to_string(cnt[ev]) + "\t" + ev + "\n";
        return o;
    }
    static auto pack_text(const Pkg& k) -> std::string
    {
        char h[512];
        std::snprintf(h, sizeof h, "S2SX1 id=%s guid=%s size=%lld hash=%s kh=%d cid=%s ap=%zu\n", k.id.c_str(), k.guid.c_str(), (long long)k.data.size(),
            hex16(fnv64(k.data.data(), k.data.size())).c_str(), k.kh, k.cid.empty() ? "-" : k.cid.c_str(), k.ap.size());
        return std::string(h) + k.ap + k.data;
    }
    static auto parse_pkg(const std::string& b, Pkg& k, std::string& err) -> bool
    {
        size_t e = b.find('\n');
        if (b.compare(0, 6, "S2SX1 ") != 0 || e == std::string::npos || e > 600) { err = "cabecalho invalido"; return false; }
        std::string hdr = b.substr(0, e);
        k.id = field(hdr, "id"); k.guid = field(hdr, "guid"); k.cid = field(hdr, "cid");
        k.size = _atoi64(field(hdr, "size").c_str()); k.kh = atoi(field(hdr, "kh").c_str());
        k.hash = _strtoui64(field(hdr, "hash").c_str(), nullptr, 16);
        long long apn = _atoi64(field(hdr, "ap").c_str());
        if (k.cid == "-") k.cid.clear();
        if (k.id.empty() || k.size <= 0 || apn <= 0 || k.kh < 0) { err = "campos do cabecalho"; return false; }
        if ((long long)(b.size() - e - 1) != apn + k.size) { err = "tamanho " + std::to_string(b.size()) + " nao confere"; return false; }
        k.ap = b.substr(e + 1, (size_t)apn);
        k.data = b.substr(e + 1 + (size_t)apn);
        if (fnv64(k.data.data(), k.data.size()) != k.hash) { err = "hash do save nao confere"; return false; }
        if (k.ap.compare(0, 7, "#S2CA 1") != 0) { err = "registro aplicado invalido"; return false; }
        return true;
    }

    static std::string g_packed;
    static std::string g_refused;
    static double g_hostAt = -1;
    static std::string g_hostSt = "sem pedido";
    static auto host_do(const std::wstring& br, int kh) -> std::string;
    static auto host_tick(const std::wstring& br, int kh, double nowS) -> std::string
    {
        if (nowS < g_hostAt + 1.0) return g_hostSt;
        g_hostAt = nowS;
        g_hostSt = host_do(br, kh);
        return g_hostSt;
    }
    static auto host_do(const std::wstring& br, int kh) -> std::string
    {
        std::string req = first_line(br + L"sx_req.txt");
        if (req.empty()) return "sem pedido";
        if (!tok_ok(req)) return "pedido invalido";
        if (req == g_packed) return "servido " + req;
        if (kh < 0)
        {
            std::string r = req + "/kh";
            if (g_refused != r) { g_refused = r; note("o cliente pediu o save do host (" + req + "), mas o host carregou um save e nao salvou depois: SALVE UMA VEZ (quick save) para o cliente entrar"); }
            return "recusado: o host precisa salvar";
        }
        Pkg k;
        k.id = req;
        k.kh = kh;
        k.cid = first_line(br + L"quest_log.id");
        if (!tok_ok(k.cid)) k.cid.clear();
        std::wstring dd = data_dir();
        k.guid = newest_guid(dd);
        std::string log;
        int have = 0;
        if (k.guid.empty() || !read_file(dd + widen(k.guid) + L".sav", k.data) || k.data.size() < 1024)
        {
            if (g_refused != req) { g_refused = req; note("pedido " + req + ": nenhum save do host legivel em " + narrow(dd)); }
            return "sem save do host";
        }
        read_file(br + L"quest_log.txt", log);
        k.ap = build_ap(log, kh, k.cid, have);
        if (have < kh)
        {
            if (g_refused != req + "/log") { g_refused = req + "/log"; note("pedido " + req + ": quest_log.txt tem " + std::to_string(have) + " eventos e kh=" + std::to_string(kh)); }
            return "log curto";
        }
        if (!write_atomic(br + L"sx_out.pkg", pack_text(k))) { note("pedido " + req + ": sx_out.pkg nao gravou"); return "erro de escrita"; }
        g_packed = req;
        note("save do host " + k.guid.substr(0, 8) + " (" + std::to_string(k.data.size()) + " B, kh=" + std::to_string(kh) + ", campanha " + k.cid +
             ") empacotado para o pedido " + req + " -> sx_out.pkg");
        return "empacotado " + req;
    }

    static auto in_state(const std::wstring& br, const std::string& id, std::string& detail) -> std::string
    {
        std::string h = first_line(br + L"sx_in.pkg");
        if (!h.empty())
        {
            if (field(h, "id") != id) { detail = field(h, "id"); return "other"; }
            detail = "kh=" + field(h, "kh") + " cid=" + field(h, "cid") + " guid=" + field(h, "guid") + " size=" + field(h, "size");
            return "ready";
        }
        std::string pr = first_line(br + L"sx_in.prog");
        if (!pr.empty() && pr.find(id) != std::string::npos) { detail = pr; return "part"; }
        detail.clear();
        return "none";
    }
    static auto install(const std::wstring& br, const std::string& id, std::string& guid, int& kh, std::string& cid, std::string& err) -> bool
    {
        std::string blob;
        if (!read_file(br + L"sx_in.pkg", blob)) { err = "sx_in.pkg ausente"; return false; }
        Pkg k;
        if (!parse_pkg(blob, k, err)) return false;
        if (k.id != id) { err = "pacote de outro pedido (" + k.id + ")"; return false; }
        std::wstring dd = data_dir();
        guid = newest_guid(dd);
        if (guid.empty()) { err = "nenhum save do cliente em " + narrow(dd); return false; }
        std::wstring tgt = dd + widen(guid) + L".sav", bak = br + L"sx_backup_" + widen(guid) + L".sav";
        std::string old;
        if (!read_file(tgt, old) || old.size() < 1024) { err = "save do cliente " + guid + " ilegivel"; return false; }
        if (!write_atomic(bak, old)) { err = "backup nao gravou"; return false; }
        if (!write_atomic(tgt, k.data))
        {
            write_atomic(tgt, old);
            err = "copia por cima de " + guid + " nao conferiu (original devolvido)";
            return false;
        }
        std::string info = guid + ".sav <- save do host " + k.guid + " (pedido " + id + ", kh=" + std::to_string(k.kh) + ", campanha " + k.cid + "); original em sx_backup_" + guid + ".sav\n";
        write_atomic(br + L"sx_backup.txt", info);
        if (!write_atomic(br + L"client_applied.txt", k.ap) || !write_atomic(br + L"n7ap_" + widen(guid) + L".txt", k.ap))
        {
            write_atomic(tgt, old);
            err = "client_applied.txt nao gravou (original devolvido)";
            return false;
        }
        kh = k.kh;
        cid = k.cid;
        note("save do host " + k.guid.substr(0, 8) + " instalado sobre o save novo do cliente " + guid.substr(0, 8) + " (" + std::to_string(k.data.size()) + " B, kh=" +
             std::to_string(kh) + ", campanha " + cid + ")");
        return true;
    }
    static auto undo(const std::wstring& br, const std::string& guid) -> bool
    {
        if (guid.size() != 32 || !tok_ok(guid)) return false;
        std::string old;
        if (!read_file(br + L"sx_backup_" + widen(guid) + L".sav", old) || old.size() < 1024) return false;
        bool ok = write_atomic(data_dir() + widen(guid) + L".sav", old);
        note(std::string("backup devolvido sobre ") + guid.substr(0, 8) + ": " + (ok ? "ok" : "FALHOU"));
        return ok;
    }

    struct Snap
    {
        std::string state, id, hand, w;
        double money = 0;
        int ammo = 0;
        std::vector<std::pair<std::string, int>> items;
    };
    static auto snap_read(const std::wstring& br, Snap& s) -> bool
    {
        std::string d;
        if (!read_file(br + L"sx_snap.txt", d) || d.compare(0, 13, "#S2SX snap 1\n") != 0) return false;
        size_t p = 13;
        while (p < d.size())
        {
            size_t e = d.find('\n', p);
            std::string ln = trim(d.substr(p, e == std::string::npos ? std::string::npos : e - p));
            p = e == std::string::npos ? d.size() : e + 1;
            std::vector<std::string> t;
            size_t q = 0;
            while (true) { size_t x = ln.find('\t', q); t.push_back(ln.substr(q, x == std::string::npos ? std::string::npos : x - q)); if (x == std::string::npos) break; q = x + 1; }
            if (t[0] == "state" && t.size() > 1) s.state = t[1];
            else if (t[0] == "id" && t.size() > 1) s.id = t[1];
            else if (t[0] == "w" && t.size() > 1 && tok_ok(t[1])) s.w = t[1];
            else if (t[0] == "money" && t.size() > 1) s.money = atof(t[1].c_str());
            else if (t[0] == "hand" && t.size() > 2) { s.hand = t[1]; s.ammo = atoi(t[2].c_str()); }
            else if (t[0] == "I" && t.size() > 2 && tok_ok(t[1])) s.items.push_back({t[1], atoi(t[2].c_str())});
        }
        return !s.id.empty();
    }
    static auto snap_write(const std::wstring& br, const Snap& s) -> bool
    {
        char m[64];
        std::snprintf(m, sizeof m, "%.0f", s.money);
        std::string o = "#S2SX snap 1\nid\t" + s.id + "\nstate\t" + s.state + (s.w.empty() ? std::string() : "\nw\t" + s.w) + "\nmoney\t" + m + "\nhand\t" + (s.hand.empty() ? "-" : s.hand) + "\t" + std::to_string(s.ammo) + "\n";
        for (auto& it : s.items) o += "I\t" + it.first + "\t" + std::to_string(it.second) + "\n";
        return write_atomic(br + L"sx_snap.txt", o);
    }
    static auto quest_set(const std::wstring& br) -> std::unordered_set<std::string>
    {
        std::unordered_set<std::string> q;
        std::string d;
        if (!read_file(br + L"s2u_questitems.txt", d)) return q;
        size_t p = 0;
        while (p < d.size())
        {
            size_t e = d.find('\n', p);
            std::string ln = trim(d.substr(p, e == std::string::npos ? std::string::npos : e - p));
            p = e == std::string::npos ? d.size() : e + 1;
            if (tok_ok(ln)) q.insert(ln);
        }
        return q;
    }
    static auto split_csv(const std::string& s) -> std::vector<std::string>
    {
        std::vector<std::string> r;
        size_t p = 0;
        while (p <= s.size())
        {
            size_t e = s.find(',', p);
            std::string x = trim(s.substr(p, e == std::string::npos ? std::string::npos : e - p));
            if (tok_ok(x)) r.push_back(x);
            if (e == std::string::npos) break;
            p = e + 1;
        }
        return r;
    }

#ifndef S2N_JOIN_TEST
    static auto pe_seh(UObject* o, UFunction* f, void* p) -> bool
    {
        uintptr_t pf0 = s2n_pf::save();
        __try { o->ProcessEvent(f, p); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { s2n_pf::fix(pf0); return false; }
    }
    struct Call
    {
        UObject* o = nullptr;
        UFunction* f = nullptr;
        bool bad = false;
        std::vector<uint8_t*> strs;
        alignas(16) uint8_t b[0x200];
        Call(UObject* obj, const wchar_t* name) : o(obj)
        {
            std::memset(b, 0, sizeof b);
            if (!o) { bad = true; return; }
            f = o->GetFunctionByNameInChain(name);
            if (!f || f->GetPropertiesSize() > (int32_t)sizeof b) { f = nullptr; bad = true; }
        }
        ~Call() { for (auto* d : strs) reinterpret_cast<FString*>(d)->~FString(); }
        Call(const Call&) = delete;
        Call& operator=(const Call&) = delete;
        auto P(const wchar_t* n) -> FProperty* { return f ? f->FindProperty(FName(n, FNAME_Add)) : nullptr; }
        auto ob(const wchar_t* n, UObject* v) -> Call&
        {
            FProperty* p = P(n);
            if (!p || p->GetSize() != 8) { bad = true; return *this; }
            *reinterpret_cast<UObject**>(b + p->GetOffset_Internal()) = v;
            return *this;
        }
        auto str(const wchar_t* n, const std::wstring& s) -> Call&
        {
            FProperty* p = P(n);
            if (!p || p->GetSize() != (int32_t)sizeof(FString)) { bad = true; return *this; }
            uint8_t* d = b + p->GetOffset_Internal();
            new (d) FString(s.c_str());
            strs.push_back(d);
            return *this;
        }
        auto run() -> bool { return !bad && f && pe_seh(o, f, b); }
        auto off(const wchar_t* n = STR("ReturnValue")) -> int32_t { FProperty* p = P(n); return p ? p->GetOffset_Internal() : -1; }
        auto ri() -> int32_t { FProperty* p = P(STR("ReturnValue")); return (p && p->GetSize() == 4) ? *reinterpret_cast<int32_t*>(b + p->GetOffset_Internal()) : 0; }
        auto rf() -> float { FProperty* p = P(STR("ReturnValue")); return (p && p->GetSize() == 4) ? *reinterpret_cast<float*>(b + p->GetOffset_Internal()) : 0.f; }
        auto take_str(int32_t at) -> std::wstring
        {
            if (at < 0 || at + 16 > (int32_t)sizeof b) return {};
            auto** data = reinterpret_cast<wchar_t**>(b + at);
            int32_t num = *reinterpret_cast<int32_t*>(b + at + 8);
            std::wstring r;
            if (*data && num > 1 && num < 512) r.assign(*data, (size_t)(num - 1));
            if (*data) (*GMalloc)->Free(*data);
            *data = nullptr;
            return r;
        }
    };
    static auto live_pawn(double a) -> UObject*
    {
        auto p = static_cast<uintptr_t>(a);
        if (!p || !UObject::IsReal(reinterpret_cast<void*>(p))) return nullptr;
        auto* o = reinterpret_cast<UObject*>(p);
        FUObjectItem* it = FUObjectArray::IndexToObject(o->GetInternalIndex());
        if (!it || it->GetUObject() != o) return nullptr;
        if (it->HasAnyFlags(static_cast<EInternalObjectFlags>((1 << 28) | (1 << 21)))) return nullptr;
        return o;
    }
    static auto take_snapshot(const std::wstring& br, UObject* pw, const std::string& sids, const std::string& wc, Snap& s, std::string& err) -> bool
    {
        UObject* med = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Stalker2.Default__CppMediator"));
        if (!med) { err = "CppMediator ausente"; return false; }
        auto Q = quest_set(br);
        auto list = split_csv(sids);
        if (list.empty()) { err = "lista de SIDs vazia"; return false; }
        int fails = 0, skipped = 0;
        std::unordered_map<std::string, int> have;
        for (auto& sid : list)
        {
            if (Q.count(sid)) { ++skipped; continue; }
            Call c(med, STR("GetItemsCountInInventory"));
            c.ob(STR("Obj"), pw).str(STR("ItemName"), widen(sid));
            if (!c.run()) { ++fails; continue; }
            int n = c.ri();
            if (n > 0) { s.items.push_back({sid, n}); have[sid] = n; }
        }
        if (fails > 0 && s.items.empty()) { err = std::to_string(fails) + " chamadas GetItemsCountInInventory falharam"; return false; }
        { Call c(pw, STR("GetMoney")); if (c.run()) s.money = c.rf(); }
        {
            Call c(med, STR("GetPlayerItemInHandsData"));
            if (c.run())
            {
                int32_t base = c.off();
                std::wstring m = c.take_str(base + 8), sup = c.take_str(base + 0x18);
                s.hand = narrow(m);
                if (!tok_ok(s.hand) || Q.count(s.hand)) s.hand.clear();
            }
        }
        { Call c(pw, STR("GetAmmoCountInWeapon")); if (c.run()) s.ammo = c.ri(); }
        if (!s.hand.empty() && !have.count(s.hand)) s.items.push_back({s.hand, 1});
        s.state = "taken";
        s.w = tok_ok(wc) ? wc : std::string();
        s.id = std::to_string(wall_ms());
        if (!snap_write(br, s)) { err = "sx_snap.txt nao gravou"; return false; }
        int tot = 0;
        for (auto& it : s.items) tot += it.second;
        char m[64];
        std::snprintf(m, sizeof m, "%.0f", s.money);
        note("retrato do cliente " + s.id + ": " + std::to_string(s.items.size()) + " tipos / " + std::to_string(tot) + " itens, dinheiro " + m + ", na mao " +
             (s.hand.empty() ? "-" : s.hand) + " (" + std::to_string(s.ammo) + " no pente); " + std::to_string(skipped) + " SIDs de missao pulados" +
             (fails ? (", " + std::to_string(fails) + " chamadas falharam") : ""));
        return true;
    }

    static auto now_s() -> double
    {
        static const auto t0 = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    static auto br_of(const LuaMadeSimple::Lua& lua) -> std::wstring
    {
        if (!lua.is_string()) return {};
        std::wstring b = widen(std::string(lua.get_string()));
        if (!b.empty() && b.back() != L'\\' && b.back() != L'/') b += L"\\";
        return b;
    }
    static auto str_arg(const LuaMadeSimple::Lua& lua) -> std::string { return lua.is_string() ? std::string(lua.get_string()) : std::string(); }
    static auto num_arg(const LuaMadeSimple::Lua& lua, double def) -> double { return lua.is_number() ? lua.get_number() : def; }

    static auto lua_host(const LuaMadeSimple::Lua& lua) -> int
    {
        std::wstring br = br_of(lua);
        int kh = (int)num_arg(lua, -1);
        lua.set_string(br.empty() ? std::string("sem bridge") : host_tick(br, kh, now_s()));
        return 1;
    }
    static auto lua_snap(const LuaMadeSimple::Lua& lua) -> int
    {
        std::wstring br = br_of(lua);
        double addr = num_arg(lua, 0);
        std::string sids = str_arg(lua);
        Snap old;
        if (snap_read(br, old) && old.state == "installed")
        {
            int tot = 0;
            for (auto& it : old.items) tot += it.second;
            lua.set_string(old.id); lua.set_number(tot); lua.set_number(old.money); lua.set_string(old.hand); lua.set_bool(true);
            return 5;
        }
        UObject* pw = live_pawn(addr);
        if (!pw) { lua.set_nil(); lua.set_string("pawn invalido"); return 2; }
        Snap s;
        std::string err;
        if (!take_snapshot(br, pw, sids, std::string(), s, err)) { note("retrato falhou: " + err); lua.set_nil(); lua.set_string(err); return 2; }
        int tot = 0;
        for (auto& it : s.items) tot += it.second;
        lua.set_string(s.id); lua.set_number(tot); lua.set_number(s.money); lua.set_string(s.hand); lua.set_bool(false);
        return 5;
    }
#endif
    static auto get_text(const Snap& s, std::string& items) -> void
    {
        items.clear();
        for (auto& it : s.items) { if (!items.empty()) items += ","; items += it.first + "=" + std::to_string(it.second); }
    }
    static auto mark(const std::wstring& br, const std::string& id, const std::string& st) -> bool
    {
        Snap s;
        if (!snap_read(br, s) || s.id != id || !(st == "installed" || st == "applied" || st == "dropped")) return false;
        s.state = st;
        bool ok = snap_write(br, s);
        note("retrato " + id + " -> " + st + (ok ? "" : " (NAO gravou)"));
        return ok;
    }
#ifndef S2N_JOIN_TEST
    static auto lua_get(const LuaMadeSimple::Lua& lua) -> int
    {
        std::wstring br = br_of(lua);
        Snap s;
        if (!snap_read(br, s)) { lua.set_string("none"); return 1; }
        std::string items, qs;
        get_text(s, items);
        for (auto& q : quest_set(br)) { if (!qs.empty()) qs += ","; qs += q; }
        lua.set_string(s.state); lua.set_string(s.id); lua.set_number(s.money); lua.set_string(items); lua.set_string(qs); lua.set_string(s.hand);
        return 6;
    }
    static auto lua_mark(const LuaMadeSimple::Lua& lua) -> int
    {
        std::wstring br = br_of(lua);
        std::string id = str_arg(lua), st = str_arg(lua);
        lua.set_bool(mark(br, id, st));
        return 1;
    }
    static auto lua_want(const LuaMadeSimple::Lua& lua) -> int
    {
        std::wstring br = br_of(lua);
        std::string id = str_arg(lua);
        if (br.empty()) { lua.set_bool(false); return 1; }
        if (id.empty()) { DeleteFileW((br + L"sx_want.txt").c_str()); lua.set_bool(true); return 1; }
        if (!tok_ok(id)) { lua.set_bool(false); return 1; }
        DeleteFileW((br + L"sx_in.pkg").c_str());
        lua.set_bool(write_atomic(br + L"sx_want.txt", id + "\n"));
        return 1;
    }
    static auto lua_in(const LuaMadeSimple::Lua& lua) -> int
    {
        std::wstring br = br_of(lua);
        std::string id = str_arg(lua), d;
        lua.set_string(in_state(br, id, d));
        lua.set_string(d);
        return 2;
    }
    static auto lua_install(const LuaMadeSimple::Lua& lua) -> int
    {
        std::wstring br = br_of(lua);
        std::string id = str_arg(lua), guid, cid, err;
        int kh = -1;
        if (!install(br, id, guid, kh, cid, err)) { note("instalacao recusada: " + err); lua.set_bool(false); lua.set_string(err); return 2; }
        lua.set_bool(true); lua.set_string(guid); lua.set_number(kh); lua.set_string(cid);
        return 4;
    }
    static auto lua_undo(const LuaMadeSimple::Lua& lua) -> int
    {
        std::wstring br = br_of(lua);
        std::string g = str_arg(lua);
        lua.set_bool(undo(br, g));
        return 1;
    }
    static auto lua_status(const LuaMadeSimple::Lua& lua) -> int { lua.set_string("s2n_join b167: " + g_last); return 1; }

    template <int (*F)(const LuaMadeSimple::Lua&)>
    auto guarded(const LuaMadeSimple::Lua& lua) -> int
    {
        try { return F(lua); }
        catch (const std::exception& ex) { g_last = std::string("excecao: ") + ex.what(); lua.set_nil(); return 1; }
        catch (...) { g_last = "excecao"; lua.set_nil(); return 1; }
    }
#endif
}

extern "C" bool s2n_join_api_host(const std::wstring* br, int kh, double nowS, std::string* out)
{
    try { *out = br->empty() ? std::string("sem bridge") : s2n_join::host_tick(*br, kh, nowS); return true; }
    catch (...) { *out = "excecao"; return false; }
}

extern "C" bool s2n_join_api_get(const std::wstring* br, std::string* state, std::string* id, double* money, std::string* items, std::string* qs, std::string* hand, std::string* w)
{
    try
    {
        s2n_join::Snap s;
        w->clear();
        if (!s2n_join::snap_read(*br, s)) { *state = "none"; id->clear(); *money = 0; items->clear(); qs->clear(); hand->clear(); return true; }
        s2n_join::get_text(s, *items);
        qs->clear();
        for (auto& q : s2n_join::quest_set(*br)) { if (!qs->empty()) *qs += ","; *qs += q; }
        *state = s.state;
        *id = s.id;
        *money = s.money;
        *hand = s.hand;
        *w = s.w;
        return true;
    }
    catch (...) { *state = "none"; return false; }
}

extern "C" bool s2n_join_api_mark(const std::wstring* br, const std::string* id, const std::string* st)
{
    try { return s2n_join::mark(*br, *id, *st); }
    catch (...) { return false; }
}

extern "C" bool s2n_join_api_want(const std::wstring* br, const std::string* id)
{
    try
    {
        if (br->empty()) return false;
        if (id->empty()) { DeleteFileW((*br + L"sx_want.txt").c_str()); return true; }
        if (!s2n_join::tok_ok(*id)) return false;
        DeleteFileW((*br + L"sx_in.pkg").c_str());
        return s2n_join::write_atomic(*br + L"sx_want.txt", *id + "\n");
    }
    catch (...) { return false; }
}

extern "C" bool s2n_join_api_in(const std::wstring* br, const std::string* id, std::string* state, std::string* detail)
{
    try { *state = s2n_join::in_state(*br, *id, *detail); return true; }
    catch (...) { *state = "none"; return false; }
}

extern "C" bool s2n_join_api_install(const std::wstring* br, const std::string* id, std::string* guid, int* kh, std::string* cid, std::string* err)
{
    try
    {
        *kh = -1;
        if (!s2n_join::install(*br, *id, *guid, *kh, *cid, *err)) { s2n_join::note("instalacao recusada: " + *err); return false; }
        return true;
    }
    catch (...) { *err = "excecao"; return false; }
}

extern "C" bool s2n_join_api_undo(const std::wstring* br, const std::string* guid)
{
    try { return s2n_join::undo(*br, *guid); }
    catch (...) { return false; }
}

#ifndef S2N_JOIN_TEST
extern "C" bool s2n_join_api_snap(const std::wstring* br, uint64_t addr, const std::string* sids, const std::string* wc, std::string* id, int* n, double* money, std::string* hand, bool* reused, std::string* err)
{
    try
    {
        s2n_join::Snap old;
        if (s2n_join::snap_read(*br, old) && old.state == "installed" && (wc->empty() || old.w == *wc))
        {
            int tot = 0;
            for (auto& it : old.items) tot += it.second;
            *id = old.id; *n = tot; *money = old.money; *hand = old.hand; *reused = true;
            return true;
        }
        UObject* pw = s2n_join::live_pawn((double)addr);
        if (!pw) { *err = "pawn invalido"; return false; }
        s2n_join::Snap s;
        std::string e;
        if (!s2n_join::take_snapshot(*br, pw, *sids, *wc, s, e)) { s2n_join::note("retrato falhou: " + e); *err = e; return false; }
        int tot = 0;
        for (auto& it : s.items) tot += it.second;
        *id = s.id; *n = tot; *money = s.money; *hand = s.hand; *reused = false;
        return true;
    }
    catch (...) { *err = "excecao"; return false; }
}
#else
extern "C" void s2n_join_api_test_dir(const std::wstring* d) { s2n_join::g_testDir = *d; }
#endif

#ifndef S2N_JOIN_TEST
void s2n_join_register(RC::LuaMadeSimple::Lua& l)
{
    using namespace s2n_join;
    l.register_function("s2n_sx_host", guarded<lua_host>);
    l.register_function("s2n_sx_snap", guarded<lua_snap>);
    l.register_function("s2n_sx_get", guarded<lua_get>);
    l.register_function("s2n_sx_mark", guarded<lua_mark>);
    l.register_function("s2n_sx_want", guarded<lua_want>);
    l.register_function("s2n_sx_in", guarded<lua_in>);
    l.register_function("s2n_sx_install", guarded<lua_install>);
    l.register_function("s2n_sx_undo", guarded<lua_undo>);
    l.register_function("s2n_sx_status", guarded<lua_status>);
}
#endif
