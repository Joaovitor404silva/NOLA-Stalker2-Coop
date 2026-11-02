#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace cen
{
    struct Fld
    {
        std::string k;
        char t = 'z';
        double n = 0;
        std::string s;
    };

    struct Row
    {
        std::string fam, key;
        std::vector<Fld> f;
        Row() = default;
        Row(std::string fm, std::string k) : fam(std::move(fm)), key(std::move(k)) {}
        Fld& put(const std::string& k)
        {
            for (auto& x : f)
                if (x.k == k) return x;
            f.push_back(Fld{k});
            return f.back();
        }
        Row& n(const std::string& k, double v)
        {
            Fld& x = put(k);
            x.t = 'n';
            x.n = v;
            return *this;
        }
        Row& s(const std::string& k, const std::string& v)
        {
            Fld& x = put(k);
            x.t = 's';
            x.s = v;
            return *this;
        }
        Row& b(const std::string& k, bool v)
        {
            Fld& x = put(k);
            x.t = 'b';
            x.n = v ? 1 : 0;
            return *this;
        }
        const Fld* get(std::string_view k) const
        {
            if (k == "key") return nullptr;
            for (auto& x : f)
                if (x.k == k) return &x;
            return nullptr;
        }
        bool text(std::string_view k, std::string& out) const
        {
            if (k == "key")
            {
                out = key;
                return true;
            }
            if (k == "fam")
            {
                out = fam;
                return true;
            }
            const Fld* x = get(k);
            if (!x || x->t == 'z') return false;
            out = x->t == 's' ? x->s : num(x->n);
            return true;
        }
        static std::string num(double v)
        {
            char b[48];
            if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e15)
                std::snprintf(b, sizeof b, "%.0f", v);
            else
                std::snprintf(b, sizeof b, "%.3f", v);
            return b;
        }
    };

    inline std::string jstr(std::string_view s)
    {
        std::string o = "\"";
        for (unsigned char c : s)
        {
            if (c == '"' || c == '\\')
            {
                o += '\\';
                o += (char)c;
            }
            else if (c == '\n')
                o += "\\n";
            else if (c == '\r')
                o += "\\r";
            else if (c == '\t')
                o += "\\t";
            else if (c < 0x20 || c >= 0x7f)
            {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", c);
                o += b;
            }
            else
                o += (char)c;
        }
        return o + "\"";
    }

    inline std::string row_json(const Row& r)
    {
        std::string o = "{\"key\":" + jstr(r.key);
        for (auto& x : r.f)
        {
            o += "," + jstr(x.k) + ":";
            if (x.t == 'n')
                o += std::isfinite(x.n) ? Row::num(x.n) : std::string("null");
            else if (x.t == 'b')
                o += x.n != 0 ? "true" : "false";
            else if (x.t == 's')
                o += jstr(x.s);
            else
                o += "null";
        }
        return o + "}";
    }

    enum Op : int { EQ = 0, NE, LT, LE, GT, GE };
    struct Cond
    {
        std::string k;
        int op = EQ;
        std::string v;
        double nv = 0;
        bool isnum = false;
    };

    inline bool to_num(std::string_view s, double& out)
    {
        if (s == "true")
        {
            out = 1;
            return true;
        }
        if (s == "false")
        {
            out = 0;
            return true;
        }
        if (s.empty()) return false;
        std::string t(s);
        char* e = nullptr;
        out = std::strtod(t.c_str(), &e);
        return e && *e == 0;
    }

    inline std::string trim(std::string_view s)
    {
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
        return std::string(s.substr(a, b - a));
    }

    inline std::vector<std::string> split(std::string_view s, char sep)
    {
        std::vector<std::string> o;
        size_t a = 0;
        while (true)
        {
            size_t p = s.find(sep, a);
            o.push_back(std::string(s.substr(a, p == std::string_view::npos ? std::string_view::npos : p - a)));
            if (p == std::string_view::npos) break;
            a = p + 1;
        }
        return o;
    }

    inline bool parse_where(std::string_view text, std::vector<Cond>& out, std::string& err)
    {
        out.clear();
        std::string t = trim(text);
        if (t.empty() || t == "-") return true;
        for (auto& part : split(t, ','))
        {
            std::string p = trim(part);
            if (p.empty()) continue;
            static const char* ops[] = {"!=", "<=", ">=", "=", "<", ">"};
            static const int opv[] = {NE, LE, GE, EQ, LT, GT};
            size_t at = std::string::npos;
            int op = -1;
            size_t ol = 0;
            for (size_t pos = 0; pos < p.size() && op < 0; ++pos)
                for (int i = 0; i < 6; ++i)
                {
                    size_t l = std::char_traits<char>::length(ops[i]);
                    if (p.compare(pos, l, ops[i]) == 0)
                    {
                        at = pos;
                        op = opv[i];
                        ol = l;
                        break;
                    }
                }
            if (op < 0 || at == 0)
            {
                err = "bad condition '" + p + "'";
                return false;
            }
            Cond c;
            c.k = trim(p.substr(0, at));
            c.op = op;
            c.v = trim(p.substr(at + ol));
            c.isnum = to_num(c.v, c.nv);
            if ((op == LT || op == LE || op == GT || op == GE) && !c.isnum)
            {
                err = "non-numeric compare '" + p + "'";
                return false;
            }
            out.push_back(c);
        }
        return true;
    }

    inline bool cond_ok(const Row& r, const Cond& c)
    {
        std::string tv;
        bool has = r.text(c.k, tv);
        if (!has) return c.op == NE;
        double rv = 0;
        bool rnum = to_num(tv, rv);
        if (c.isnum && rnum)
        {
            switch (c.op)
            {
                case EQ: return rv == c.nv;
                case NE: return rv != c.nv;
                case LT: return rv < c.nv;
                case LE: return rv <= c.nv;
                case GT: return rv > c.nv;
                case GE: return rv >= c.nv;
            }
            return false;
        }
        if (c.op == EQ) return tv == c.v;
        if (c.op == NE) return tv != c.v;
        return false;
    }

    inline bool match(const Row& r, const std::vector<Cond>& cs)
    {
        for (auto& c : cs)
            if (!cond_ok(r, c)) return false;
        return true;
    }

    struct Inv
    {
        std::string id, role, fam, per;
        std::vector<Cond> where;
        int max = 0;
        bool maxViews = false;
        double grace = 0;
        std::vector<std::string> detail;
    };

    inline bool parse_inv(std::string_view text, std::vector<Inv>& out, std::string& err)
    {
        out.clear();
        int ln = 0;
        bool head = false;
        for (auto& raw : split(text, '\n'))
        {
            ++ln;
            std::string l = trim(raw);
            if (l.empty()) continue;
            auto c = split(l, '\t');
            if (!head)
            {
                head = true;
                if (c.size() >= 1 && trim(c[0]) == "id") continue;
            }
            if (c.size() < 8)
            {
                err = "line " + std::to_string(ln) + ": needs 8 columns";
                return false;
            }
            Inv v;
            v.id = trim(c[0]);
            v.role = trim(c[1]);
            v.fam = trim(c[2]);
            std::string e;
            if (!parse_where(c[3], v.where, e))
            {
                err = "line " + std::to_string(ln) + ": " + e;
                return false;
            }
            v.per = trim(c[4]);
            if (v.per == "-") v.per.clear();
            std::string mx = trim(c[5]);
            if (mx == "views")
                v.maxViews = true;
            else
            {
                double d;
                if (!to_num(mx, d) || d < 0)
                {
                    err = "line " + std::to_string(ln) + ": bad max '" + mx + "'";
                    return false;
                }
                v.max = (int)d;
            }
            if (!to_num(trim(c[6]), v.grace) || v.grace < 0)
            {
                err = "line " + std::to_string(ln) + ": bad grace";
                return false;
            }
            std::string d = trim(c[7]);
            if (!d.empty() && d != "-")
                for (auto& x : split(d, ','))
                    if (!trim(x).empty()) v.detail.push_back(trim(x));
            if (v.role != "all" && v.role != "host" && v.role != "client")
            {
                err = "line " + std::to_string(ln) + ": role must be all, host or client";
                return false;
            }
            if (v.id.empty() || v.fam.empty())
            {
                err = "line " + std::to_string(ln) + ": empty id or family";
                return false;
            }
            out.push_back(std::move(v));
        }
        return true;
    }

    inline std::string row_detail(const Row& r, const std::vector<std::string>& fields)
    {
        std::string o;
        for (auto& k : fields)
        {
            std::string v;
            if (!r.text(k, v)) v = "-";
            if (!o.empty()) o += ' ';
            o += k + "=" + v;
        }
        return o;
    }

    struct Hit
    {
        std::string id, gkey, detail;
        double first = 0;
        int count = 0;
        bool due = false;
    };

    class InvState
    {
      public:
        std::vector<Hit> eval(const std::vector<Inv>& invs, const std::vector<Row>& rows, const std::string& role, int views, double now, std::vector<Hit>* current = nullptr)
        {
            std::vector<Hit> fired;
            std::set<std::string> live;
            for (auto& v : invs)
            {
                if (v.role != "all" && v.role != role) continue;
                int mx = v.maxViews ? views : v.max;
                std::map<std::string, std::vector<const Row*>> groups;
                for (auto& r : rows)
                {
                    if (r.fam != v.fam || !match(r, v.where)) continue;
                    std::string g;
                    if (!v.per.empty())
                    {
                        if (!r.text(v.per, g)) g = "-";
                    }
                    groups[g].push_back(&r);
                }
                for (auto& [g, rs] : groups)
                {
                    if ((int)rs.size() <= mx) continue;
                    std::vector<std::string> keys;
                    if (mx == 0)
                        for (auto* r : rs) keys.push_back(v.id + "\x1f" + r->key);
                    else
                        keys.push_back(v.id + "\x1f#" + g);
                    for (size_t i = 0; i < keys.size(); ++i)
                    {
                        const std::string& k = keys[i];
                        live.insert(k);
                        auto it = first_.find(k);
                        if (it == first_.end()) it = first_.emplace(k, now).first;
                        Hit h;
                        h.id = v.id;
                        h.first = it->second;
                        h.count = (int)rs.size();
                        if (mx == 0)
                        {
                            h.gkey = rs[i]->key;
                            h.detail = row_detail(*rs[i], v.detail);
                        }
                        else
                        {
                            h.gkey = v.per.empty() ? std::string("all") : v.per + "=" + g;
                            std::string d = "count=" + std::to_string(rs.size()) + " max=" + std::to_string(mx);
                            for (size_t j = 0; j < rs.size() && j < 4; ++j) d += " [" + rs[j]->key + (v.detail.empty() ? "" : " " + row_detail(*rs[j], v.detail)) + "]";
                            h.detail = d;
                        }
                        h.due = now - it->second >= v.grace;
                        if (current) current->push_back(h);
                        if (h.due && !fired_.count(k))
                        {
                            fired_.insert(k);
                            fired.push_back(h);
                        }
                    }
                }
            }
            for (auto it = first_.begin(); it != first_.end();)
            {
                if (!live.count(it->first))
                {
                    fired_.erase(it->first);
                    it = first_.erase(it);
                }
                else
                    ++it;
            }
            total_ += (int)fired.size();
            return fired;
        }
        int total() const { return total_; }
        int open() const { return (int)first_.size(); }

      private:
        std::map<std::string, double> first_;
        std::set<std::string> fired_;
        int total_ = 0;
    };

    inline uint32_t fnv(std::string_view s, uint32_t h = 2166136261u)
    {
        for (unsigned char c : s)
        {
            h ^= c;
            h *= 16777619u;
        }
        return h;
    }

    inline uint32_t digest(const std::vector<Row>& rows, const std::string& fam, const std::vector<Cond>& where, const std::vector<std::string>& fields, int& n)
    {
        std::vector<std::string> items;
        for (auto& r : rows)
        {
            if (r.fam != fam || !match(r, where)) continue;
            std::string s = r.key;
            for (auto& k : fields)
            {
                std::string v;
                if (!r.text(k, v)) v = "-";
                s += "\x1f" + v;
            }
            items.push_back(s);
        }
        std::sort(items.begin(), items.end());
        n = (int)items.size();
        uint32_t h = 2166136261u;
        for (auto& s : items)
        {
            h = fnv(s, h);
            h = fnv("\n", h);
        }
        return h;
    }

    struct DigestFam
    {
        std::string name, fam;
        std::vector<Cond> where;
        std::vector<std::string> fields;
    };

    inline std::vector<DigestFam> digest_fams()
    {
        std::vector<DigestFam> o;
        auto add = [&](const char* name, const char* fam, const char* where, const char* fields) {
            DigestFam d;
            d.name = name;
            d.fam = fam;
            std::string e;
            parse_where(where, d.where, e);
            if (*fields)
                for (auto& x : split(fields, ',')) d.fields.push_back(x);
            o.push_back(std::move(d));
        };
        add("npc", "npc", "nola=0,player=0,shared=1,hidden=0", "alive");
        add("quest", "quest", "-", "");
        add("taken", "taken", "-", "");
        add("lock", "lock", "-", "locked");
        add("broken", "broken", "-", "");
        add("qg", "qg", "-", "n");
        return o;
    }

    inline std::string hex8(uint32_t h)
    {
        char b[12];
        std::snprintf(b, sizeof b, "%08x", h);
        return b;
    }

    inline std::string wh_line(const std::vector<Row>& rows, int64_t wall, double t, const std::string& role)
    {
        std::string o = "NOLA WH " + std::to_string((long long)wall) + " role=" + role + " t=" + Row::num(std::floor(t));
        for (auto& d : digest_fams())
        {
            int n = 0;
            uint32_t h = digest(rows, d.fam, d.where, d.fields, n);
            o += " " + d.name + "=" + hex8(h) + "/" + std::to_string(n);
        }
        return o;
    }

    inline std::string q1(double v) { return Row::num(std::round(v / 100.0)); }
    inline std::string pos_key(const std::string& cls, double x, double y, double z) { return cls + "@" + q1(x) + "," + q1(y) + "," + q1(z); }

    inline void dedupe_keys(std::vector<Row>& rows)
    {
        std::map<std::string, int> seen;
        for (auto& r : rows)
        {
            int& c = seen[r.fam + "\x1f" + r.key];
            ++c;
            if (c > 1)
            {
                r.key += "#" + std::to_string(c);
                r.b("dupkey", true);
            }
        }
    }

    inline std::string census_json(const std::vector<Row>& rows, const std::vector<std::pair<std::string, std::string>>& head, const std::vector<Hit>& inv, const std::vector<Row>& all_for_digest, int64_t wall,
                                   double t, const std::string& role)
    {
        std::string o = "{\"census\":1";
        for (auto& [k, v] : head) o += "," + jstr(k) + ":" + v;
        std::map<std::string, std::vector<const Row*>> fams;
        for (auto& r : rows) fams[r.fam].push_back(&r);
        o += ",\"rows\":{";
        bool first = true;
        for (auto& [f, rs] : fams)
        {
            if (!first) o += ",";
            first = false;
            o += "\n " + jstr(f) + ":[";
            for (size_t i = 0; i < rs.size(); ++i)
            {
                if (i) o += ",";
                o += "\n  " + row_json(*rs[i]);
            }
            o += "]";
        }
        o += "},\n\"digest\":{";
        first = true;
        for (auto& d : digest_fams())
        {
            int n = 0;
            uint32_t h = digest(all_for_digest, d.fam, d.where, d.fields, n);
            if (!first) o += ",";
            first = false;
            o += jstr(d.name) + ":" + jstr(hex8(h) + "/" + std::to_string(n));
        }
        o += "},\n\"wh\":" + jstr(wh_line(all_for_digest, wall, t, role));
        o += ",\n\"inv\":[";
        for (size_t i = 0; i < inv.size(); ++i)
        {
            if (i) o += ",";
            o += "{\"id\":" + jstr(inv[i].id) + ",\"key\":" + jstr(inv[i].gkey) + ",\"first\":" + Row::num(inv[i].first) + ",\"count\":" + std::to_string(inv[i].count) + ",\"due\":" + (inv[i].due ? "true" : "false") + ",\"detail\":" + jstr(inv[i].detail) + "}";
        }
        return o + "]}\n";
    }
}
