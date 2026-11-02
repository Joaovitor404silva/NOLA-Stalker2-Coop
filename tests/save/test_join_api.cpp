#include <Windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

extern "C" bool s2n_join_api_host(const std::wstring* br, int kh, double nowS, std::string* out);
extern "C" bool s2n_join_api_get(const std::wstring* br, std::string* state, std::string* id, double* money, std::string* items, std::string* qs, std::string* hand, std::string* w);
extern "C" bool s2n_join_api_mark(const std::wstring* br, const std::string* id, const std::string* st);
extern "C" bool s2n_join_api_want(const std::wstring* br, const std::string* id);
extern "C" bool s2n_join_api_in(const std::wstring* br, const std::string* id, std::string* state, std::string* detail);
extern "C" bool s2n_join_api_install(const std::wstring* br, const std::string* id, std::string* guid, int* kh, std::string* cid, std::string* err);
extern "C" bool s2n_join_api_undo(const std::wstring* br, const std::string* guid);
extern "C" void s2n_join_api_test_dir(const std::wstring* d);
extern "C" void s2n_join_api_test_boot(long long ms);
extern "C" void s2n_save_api_set_folder(const std::wstring* f);
extern "C" bool s2n_save_api_folder(std::wstring* out);

namespace fs = std::filesystem;
static int fails = 0;
static void check(bool ok, const char* what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++fails;
}
static void put(const fs::path& p, const std::string& d)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(d.data(), (std::streamsize)d.size());
}
static std::string get(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

int main(int argc, char** argv)
{
    fs::path root = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "s2m_save_join_api";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::path H = root / "host", C = root / "client";
    std::wstring hs = (H / "saved").wstring() + L"\\", cs = (C / "saved").wstring() + L"\\";
    std::wstring hb = (H / "B").wstring() + L"\\", cb = (C / "B").wstring() + L"\\";
    std::string hostSave(2000, 'H'), cliSave(1500, 'C');
    const std::string G1 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", G2 = "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";
    put(H / "saved/STEAM/SaveGames/CampaignsSave.sav", "idx");
    put(H / ("saved/STEAM/SaveGames/Data/" + G1 + ".sav"), hostSave);
    put(H / "B/quest_log.id", "cidX\n");
    put(H / "B/quest_log.txt", "1.5\tJ,a,b,c,s\n2.0\tN,s2\n3.0\tJ,a,b,c,s\n4.0\tJ,z\n");
    put(C / "saved/STEAM/SaveGames/CampaignsSave.sav", "idx");
    put(C / ("saved/STEAM/SaveGames/Data/" + G2 + ".sav"), cliSave);
    put(C / "B/sx_snap.txt", "#S2SX snap 1\nid\tSX1\nstate\ttaken\nmoney\t650\nhand\tGunX\t12\nI\tA012A\t3\nI\tGunX\t1\n");
    put(C / "B/s2u_questitems.txt", "QITEM1\n");

    std::string st, id, items, qs, hand, wc, d, guid, cid, err, out;
    double money = 0;
    int kh = -1;
    std::string sx = "SX1";
    check(s2n_join_api_want(&cb, &sx), "want SX1 writes sx_want.txt");
    check(get(C / "B/sx_want.txt") == "SX1\n", "sx_want.txt = SX1");
    check(s2n_join_api_in(&cb, &sx, &st, &d) && st == "none", "in before package = none");

    s2n_join_api_test_dir(&hs);
    put(H / "B/sx_req.txt", "SX1\n");
    check(s2n_join_api_host(&hb, -1, 10.0, &out) && out == "recusado: o host precisa salvar", "host refuses while kh=-1");
    check(s2n_join_api_host(&hb, 3, 10.5, &out) && out == "recusado: o host precisa salvar", "host rate limit 1 s keeps last state");
    check(s2n_join_api_host(&hb, 3, 12.0, &out) && out == "empacotado SX1", "host packs with kh=3");
    std::string pkg = get(H / "B/sx_out.pkg");
    check(pkg.compare(0, 6, "S2SX1 ") == 0, "sx_out.pkg header");
    check(s2n_join_api_host(&hb, 3, 13.5, &out) && out == "servido SX1", "same queue head is served once");
    put(H / "B/sx_req.txt", "SX2\n");
    check(s2n_join_api_host(&hb, 4, 15.0, &out) && out == "empacotado SX2", "next queue head SX2 (late joiner while SX1 is in) gets its own package");
    std::string pkg2 = get(H / "B/sx_out.pkg");
    std::string h1 = pkg.substr(0, pkg.find('\n')), h2 = pkg2.substr(0, pkg2.find('\n'));
    std::printf("     SX1 header: %s\n     SX2 header: %s\n", h1.c_str(), h2.c_str());
    check(h2.find("id=SX2") != std::string::npos && h2.find("kh=4") != std::string::npos && h1.find("id=SX1") != std::string::npos, "SX2 package carries its own id and the current kh");
    put(H / "B/sx_req.txt", "SX1\n");
    check(s2n_join_api_host(&hb, 3, 16.5, &out) && out == "empacotado SX1", "head back to SX1 re-packs SX1");
    check(get(H / "B/sx_out.pkg") == pkg, "SX1 re-pack is byte-equal to the first SX1 package");

    put(C / "B/sx_in.pkg", pkg);
    s2n_join_api_test_dir(&cs);
    check(s2n_join_api_in(&cb, &sx, &st, &d) && st == "ready", "in after package = ready");
    std::printf("     detail: %s\n", d.c_str());
    std::string other = "SX9";
    check(!s2n_join_api_install(&cb, &other, &guid, &kh, &cid, &err), "install refuses another request id");
    check(s2n_join_api_install(&cb, &sx, &guid, &kh, &cid, &err), "install SX1");
    std::printf("     guid=%s kh=%d cid=%s err=%s\n", guid.c_str(), kh, cid.c_str(), err.c_str());
    check(guid == G2 && kh == 3 && cid == "cidX", "install returns client newest guid, kh, cid");
    check(get(C / ("saved/STEAM/SaveGames/Data/" + G2 + ".sav")) == hostSave, "client newest save now holds the host bytes");
    check(get(C / ("B/sx_backup_" + G2 + ".sav")) == cliSave, "backup of the client save");
    std::string ap = get(C / "B/client_applied.txt");
    std::printf("     client_applied: %s", ap.c_str());
    check(ap == "#S2CA 1 cidX\nW\tcidX\n2\tJ,a,b,c,s\n1\tN,s2\n", "client_applied = first kh=3 host steps, campaign stamped");
    check(get(C / ("B/n7ap_" + G2 + ".txt")) == ap, "n7ap copy");

    check(s2n_join_api_get(&cb, &st, &id, &money, &items, &qs, &hand, &wc) && st == "taken" && id == "SX1", "get taken SX1");
    std::printf("     items=%s qs=%s hand=%s money=%.0f\n", items.c_str(), qs.c_str(), hand.c_str(), money);
    check(items == "A012A=3,GunX=1" && qs == "QITEM1" && hand == "GunX" && money == 650, "get fields");
    std::string inst = "installed", bad = "bogus";
    check(!s2n_join_api_mark(&cb, &sx, &bad), "mark refuses unknown state");
    check(!s2n_join_api_mark(&cb, &other, &inst), "mark refuses other id");
    check(s2n_join_api_mark(&cb, &sx, &inst), "mark installed");
    check(s2n_join_api_get(&cb, &st, &id, &money, &items, &qs, &hand, &wc) && st == "installed", "get installed");
    std::string empty;
    check(s2n_join_api_want(&cb, &empty) && !fs::exists(C / "B/sx_want.txt"), "want '' removes sx_want.txt");
    check(s2n_join_api_undo(&cb, &guid), "undo");
    check(get(C / ("saved/STEAM/SaveGames/Data/" + G2 + ".sav")) == cliSave, "undo restores the client save");
    std::string badg = "XYZ";
    check(!s2n_join_api_undo(&cb, &badg), "undo refuses a bad guid");

    std::wstring fo, nola = L"SaveGamesNOLA", norm = L"SaveGames";
    check(s2n_save_api_folder(&fo) && fo == L"SaveGames", "store folder default SaveGames");
    s2n_save_api_set_folder(&nola);
    check(s2n_save_api_folder(&fo) && fo == nola, "store folder set to SaveGamesNOLA");
    const std::string G3 = "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC", G4 = "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD";
    std::string hostCoop(1800, 'h'), cliCoop(1700, 'c');
    put(H / "saved/STEAM/SaveGamesNOLA/CampaignsSave.sav", "idx");
    put(H / ("saved/STEAM/SaveGamesNOLA/Data/" + G3 + ".sav"), hostCoop);
    put(C / "saved/STEAM/SaveGamesNOLA/CampaignsSave.sav", "idx");
    put(C / ("saved/STEAM/SaveGamesNOLA/Data/" + G4 + ".sav"), cliCoop);
    std::string sx3 = "SX3";
    check(s2n_join_api_want(&cb, &sx3), "coop store: want SX3");
    s2n_join_api_test_dir(&hs);
    put(H / "B/sx_req.txt", "SX3\n");
    check(s2n_join_api_host(&hb, 3, 30.0, &out) && out == "empacotado SX3", "coop store: host packs SX3");
    std::string pkg3 = get(H / "B/sx_out.pkg");
    check(pkg3.find(hostCoop) != std::string::npos && pkg3.find(hostSave) == std::string::npos, "coop store: package holds the host co-op save, not the normal one");
    put(C / "B/sx_in.pkg", pkg3);
    s2n_join_api_test_dir(&cs);
    std::string g3;
    check(s2n_join_api_install(&cb, &sx3, &g3, &kh, &cid, &err) && g3 == G4, "coop store: install targets the client co-op save");
    check(get(C / ("saved/STEAM/SaveGamesNOLA/Data/" + G4 + ".sav")) == hostCoop, "coop store: client co-op save holds the host bytes");
    check(get(C / ("saved/STEAM/SaveGames/Data/" + G2 + ".sav")) == cliSave && get(H / ("saved/STEAM/SaveGames/Data/" + G1 + ".sav")) == hostSave, "coop store: normal saves untouched");
    check(s2n_join_api_undo(&cb, &g3) && get(C / ("saved/STEAM/SaveGamesNOLA/Data/" + G4 + ".sav")) == cliCoop, "coop store: undo restores the client co-op save");
    s2n_save_api_set_folder(&norm);
    s2n_join_api_test_dir(&hs);
    put(H / "B/sx_req.txt", "91760000000001\n");
    check(s2n_join_api_host(&hb, -1, 40.0, &out) && out == "recusado: o host precisa salvar", "world request while the host loaded and did not save (kh=-1): refused, not served from the frozen save");
    check(s2n_join_api_host(&hb, 3, 41.5, &out) && out.rfind("world refused: ", 0) == 0, "world request with kh=3 and nothing frozen: goes to the frozen save check");

    std::wstring ob = (H / "B_old").wstring() + L"\\";
    put(H / "B_old/sx_req.txt", "1792414609757\n");
    s2n_join_api_test_boot(100000000000000LL);
    check(s2n_join_api_host(&ob, 3, 50.0, &out) && out == "sem pedido", "request file older than this game run: removed at the first read, not acted on");
    check(!fs::exists(H / "B_old/sx_req.txt"), "stale sx_req.txt deleted");
    put(H / "B_old/sx_req.txt", "SX7\n");
    check(s2n_join_api_host(&ob, 3, 51.5, &out) && out.rfind("recusado", 0) != 0 && out != "sem pedido" && fs::exists(H / "B_old/sx_req.txt"), "a request written after the first read is live (checked once per bridge)");
    std::printf("     live request after the boot check: %s\n", out.c_str());
    s2n_join_api_test_boot(0);
    std::wstring nb = (H / "B_new").wstring() + L"\\";
    put(H / "B_new/sx_req.txt", "SX8\n");
    check(s2n_join_api_host(&nb, 3, 53.0, &out) && fs::exists(H / "B_new/sx_req.txt") && out != "sem pedido", "request newer than the game start: kept and handled");
    std::printf("%s (%d failed)\n", fails ? "FAIL" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
