#include "s2m_text.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace nola_text;

static int g_fail = 0;
static int g_n = 0;

static void check(bool ok, const std::string& what)
{
    ++g_n;
    if (!ok) ++g_fail;
    std::printf("%s %s\n", ok ? "OK  " : "FAIL", what.c_str());
}

static std::vector<std::string> holes(const std::string& s)
{
    std::vector<std::string> o;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '{')
        {
            size_t e = s.find('}', i);
            if (e == std::string::npos) { o.push_back("{unclosed"); break; }
            o.push_back(s.substr(i, e - i + 1));
            i = e;
        }
        else if (s[i] == '%')
        {
            size_t j = i + 1;
            while (j < s.size() && std::string("-+ #0123456789.").find(s[j]) != std::string::npos) ++j;
            o.push_back(s.substr(i, j + 1 - i));
            i = j;
        }
    }
    std::sort(o.begin(), o.end());
    return o;
}

static std::wstring g_probeLang;
static std::wstring probe() { return g_probeLang; }

int main()
{
    check(kCount > 0, "table: has " + std::to_string(kCount) + " ids");
    std::set<std::string> ids;
    const std::set<std::string> same = {"menu_title", "popup_ok"};
    int bad = 0;
    for (size_t i = 0; i < kCount; ++i)
    {
        const Row& r = kRows[i];
        bool dup = !ids.insert(r.id).second;
        bool empty = !r.t[0][0] || !r.t[1][0] || !r.t[2][0];
        bool pe = holes(r.t[0]) != holes(r.t[1]) || holes(r.t[0]) != holes(r.t[2]);
        bool copy = !same.count(r.id) && (std::string(r.t[0]) == r.t[1] || std::string(r.t[0]) == r.t[2]);
        bool utf = false;
        for (int l = 0; l < 3; ++l)
            for (const unsigned char* c = reinterpret_cast<const unsigned char*>(r.t[l]); *c; ++c)
            {
                if (*c >= 0xc0)
                {
                    int n = *c >= 0xf0 ? 3 : *c >= 0xe0 ? 2 : 1;
                    for (int k = 1; k <= n; ++k)
                        if ((c[k] & 0xc0) != 0x80) utf = true;
                    c += n;
                }
                else if (*c >= 0x80) utf = true;
            }
        if (dup || empty || pe || copy || utf)
        {
            ++bad;
            check(false, std::string("id ") + r.id + (dup ? " duplicate" : "") + (empty ? " missing a language" : "") + (pe ? " placeholders differ between en/pt/es" : "") +
                             (copy ? " pt or es is a copy of en" : "") + (utf ? " bad UTF-8" : ""));
        }
    }
    check(bad == 0, "every id has en, pt and es, with the same placeholders, translated, valid UTF-8");
    check(std::string(get(T::menu_host, Lang::en)) == "Host co-op" && std::string(get(T::menu_host, Lang::pt)) == "Hospedar co-op" && std::string(get(T::menu_host, Lang::es)) == "Crear co-op",
          "get: menu_host in the three languages");
    check(std::string(kRows[static_cast<size_t>(T::qb_host_only)].id) == "qb_host_only", "enum value indexes its own row");

    struct C { const char* l; Lang want; };
    const C cases[] = {{"pt-BR", Lang::pt}, {"pt-PT", Lang::pt}, {"pt", Lang::pt}, {"PT-br", Lang::pt}, {"en", Lang::en}, {"en-US", Lang::en}, {"en-GB", Lang::en}, {"ru", Lang::en},
                       {"de", Lang::en}, {"fr", Lang::en}, {"uk", Lang::en}, {"zh-Hans", Lang::en}, {"", Lang::en}, {"p", Lang::en}, {"e", Lang::en}, {"es", Lang::es}, {"es-ES", Lang::es},
                       {"es-419", Lang::es}, {"es-MX", Lang::es}, {"ES-ar", Lang::es}};
    for (const C& c : cases)
    {
        std::string n = c.l;
        std::wstring w(n.begin(), n.end());
        check(lang_of(n) == c.want && lang_of(w) == c.want, std::string("lang_of(\"") + c.l + "\") = " + lang_name(c.want));
    }

    force(Lang::en);
    check(tx(T::menu_join) == "Join co-op", "tx: forced English");
    force(Lang::pt);
    check(tx(T::menu_join) == "Entrar no co-op", "tx: forced Portuguese");
    force(Lang::es);
    check(tx(T::menu_join) == "Unirse al co-op", "tx: forced Spanish");
    check(tx(T::menu_join, Lang::en) == "Join co-op", "tx: explicit language ignores the current one");

    g_known.store(false);
    g_lang.store(0);
    g_probeLang = L"pt-BR";
    set_probe(&probe);
    check(cur() == Lang::pt && tx(T::popup_ok) == "OK", "probe: first call reads the game language (pt-BR)");
    g_probeLang = L"es-419";
    check(cur() == Lang::pt, "probe: a change inside the refresh interval is not read yet");
    g_last_ms.store(steady_ms() - 5000);
    check(cur() == Lang::es, "probe: after the interval the new game language is read (es-419)");
    g_probeLang = L"";
    g_last_ms.store(steady_ms() - 5000);
    check(cur() == Lang::es, "probe: a failed read keeps the last language");
    g_probeLang = L"ru";
    g_last_ms.store(steady_ms() - 5000);
    check(cur() == Lang::en, "probe: ru falls back to English");
    g_known.store(false);
    g_lang.store(static_cast<uint8_t>(Lang::pt));
    g_probeLang = L"";
    check(cur() == Lang::pt, "probe: before the language is known a failed read keeps the default");
    set_probe(nullptr);

    std::printf("%d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
