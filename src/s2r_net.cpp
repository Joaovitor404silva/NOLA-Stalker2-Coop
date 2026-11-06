#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#include "s2r_net.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace s2r
{
    static std::string fmt(const char* f, ...)
    {
        char buf[2048];
        va_list ap;
        va_start(ap, f);
        int n = std::vsnprintf(buf, sizeof(buf), f, ap);
        va_end(ap);
        if (n < 0) return {};
        if (n < (int)sizeof(buf)) return std::string(buf, (size_t)n);
        std::string big((size_t)n + 1, '\0');
        va_start(ap, f);
        std::vsnprintf(big.data(), big.size(), f, ap);
        va_end(ap);
        big.resize((size_t)n);
        return big;
    }

    double clock_s()
    {
        static const double inv = []
        {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            return 1.0 / (double)f.QuadPart;
        }();
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return (double)c.QuadPart * inv;
    }

    std::string narrow(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s((size_t)n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }

    std::wstring widen(const std::string& s)
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    }

    namespace js
    {
        V V::Str(std::string v)
        {
            V r;
            r.t = string;
            r.s = std::move(v);
            return r;
        }
        V V::Int(int64_t v)
        {
            V r;
            r.t = integer;
            r.s = std::to_string(v);
            return r;
        }
        V V::Real(double v)
        {
            V r;
            r.t = real;
            r.d = v;
            return r;
        }
        V V::Arr()
        {
            V r;
            r.t = array;
            return r;
        }
        V V::Object()
        {
            V r;
            r.t = object;
            return r;
        }

        static double digits_to_double(const std::string& s)
        {
            double x = 0;
            auto r = std::from_chars(s.data(), s.data() + s.size(), x);
            if (r.ec == std::errc::result_out_of_range) x = std::strtod(s.c_str(), nullptr);
            return x;
        }

        double V::num() const
        {
            if (t == boolean) return b ? 1.0 : 0.0;
            if (t == real) return d;
            if (t == integer) return digits_to_double(s);
            return 0;
        }

        const V* V::get(std::string_view k) const
        {
            if (t != object) return nullptr;
            for (auto& kv : o)
                if (kv.first == k) return &kv.second;
            return nullptr;
        }
        V* V::get(std::string_view k)
        {
            if (t != object) return nullptr;
            for (auto& kv : o)
                if (kv.first == k) return &kv.second;
            return nullptr;
        }
        void V::set(std::string_view k, V v)
        {
            if (V* p = get(k))
            {
                *p = std::move(v);
                return;
            }
            o.emplace_back(std::string(k), std::move(v));
        }
        bool V::pop(std::string_view k, V* out)
        {
            if (t != object) return false;
            for (auto it = o.begin(); it != o.end(); ++it)
                if (it->first == k)
                {
                    if (out) *out = std::move(it->second);
                    o.erase(it);
                    return true;
                }
            return false;
        }

        bool utf8_valid(std::string_view s)
        {
            size_t i = 0, n = s.size();
            const uint8_t* p = reinterpret_cast<const uint8_t*>(s.data());
            while (i < n)
            {
                uint8_t c = p[i];
                if (c < 0x80)
                {
                    ++i;
                    continue;
                }
                int len;
                uint32_t cp;
                if (c >= 0xC2 && c <= 0xDF) len = 2, cp = c & 0x1F;
                else if (c >= 0xE0 && c <= 0xEF) len = 3, cp = c & 0x0F;
                else if (c >= 0xF0 && c <= 0xF4) len = 4, cp = c & 0x07;
                else return false;
                if (i + (size_t)len > n) return false;
                for (int k = 1; k < len; ++k)
                {
                    if ((p[i + k] & 0xC0) != 0x80) return false;
                    cp = (cp << 6) | (p[i + k] & 0x3F);
                }
                if (len == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return false;
                if (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) return false;
                i += (size_t)len;
            }
            return true;
        }

        static void put_cp(std::string& o, uint32_t cp)
        {
            if (cp < 0x80) o.push_back((char)cp);
            else if (cp < 0x800)
            {
                o.push_back((char)(0xC0 | (cp >> 6)));
                o.push_back((char)(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                o.push_back((char)(0xE0 | (cp >> 12)));
                o.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                o.push_back((char)(0x80 | (cp & 0x3F)));
            }
            else
            {
                o.push_back((char)(0xF0 | (cp >> 18)));
                o.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
                o.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                o.push_back((char)(0x80 | (cp & 0x3F)));
            }
        }

        static uint32_t get_cp(const std::string& s, size_t& i)
        {
            uint8_t c = (uint8_t)s[i];
            if (c < 0x80)
            {
                ++i;
                return c;
            }
            int len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
            uint32_t cp = len == 4 ? (c & 0x07u) : len == 3 ? (c & 0x0Fu) : len == 2 ? (c & 0x1Fu) : c;
            size_t k = 1;
            for (; k < (size_t)len && i + k < s.size(); ++k) cp = (cp << 6) | ((uint8_t)s[i + k] & 0x3Fu);
            i += k;
            return cp;
        }

        struct Parser
        {
            const char* p;
            const char* e;
            const char* b;
            std::string err;
            void ws()
            {
                while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
            }
            bool fail(const char* what)
            {
                if (err.empty()) err = fmt("%s: char %lld", what, (long long)(p - b));
                return false;
            }
            static int hexv(char c)
            {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            }
            bool hex4(const char* q, uint32_t& v)
            {
                if (e - q < 4) return false;
                v = 0;
                for (int k = 0; k < 4; ++k)
                {
                    int h = hexv(q[k]);
                    if (h < 0) return false;
                    v = (v << 4) | (uint32_t)h;
                }
                return true;
            }
            bool str(std::string& out)
            {
                ++p;
                while (true)
                {
                    if (p >= e) return fail("Unterminated string starting at");
                    char c = *p;
                    if (c == '"')
                    {
                        ++p;
                        return true;
                    }
                    if ((uint8_t)c < 0x20) return fail("Invalid control character at");
                    if (c != '\\')
                    {
                        out.push_back(c);
                        ++p;
                        continue;
                    }
                    ++p;
                    if (p >= e) return fail("Unterminated string starting at");
                    char x = *p++;
                    switch (x)
                    {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u':
                    {
                        uint32_t cp;
                        if (!hex4(p, cp)) return fail("Invalid \\uXXXX escape");
                        p += 4;
                        if (cp >= 0xD800 && cp <= 0xDBFF && e - p >= 7 && p[0] == '\\' && p[1] == 'u')
                        {
                            uint32_t c2;
                            if (!hex4(p + 2, c2)) return fail("Invalid \\uXXXX escape");
                            if (c2 >= 0xDC00 && c2 <= 0xDFFF)
                            {
                                cp = 0x10000 + (((cp - 0xD800) << 10) | (c2 - 0xDC00));
                                p += 6;
                            }
                        }
                        put_cp(out, cp);
                        break;
                    }
                    default: --p; return fail("Invalid \\escape");
                    }
                }
            }
            bool number(V& v)
            {
                const char* s = p;
                const char* q = p;
                if (q < e && *q == '-') ++q;
                if (q < e && *q >= '1' && *q <= '9')
                {
                    while (q < e && *q >= '0' && *q <= '9') ++q;
                }
                else if (q < e && *q == '0') ++q;
                else return fail("Expecting value");
                bool isf = false;
                if (q + 1 < e && *q == '.' && q[1] >= '0' && q[1] <= '9')
                {
                    isf = true;
                    q += 2;
                    while (q < e && *q >= '0' && *q <= '9') ++q;
                }
                if (q < e && (*q == 'e' || *q == 'E'))
                {
                    const char* save = q;
                    ++q;
                    if (q < e && (*q == '+' || *q == '-')) ++q;
                    const char* ds = q;
                    while (q < e && *q >= '0' && *q <= '9') ++q;
                    if (q == ds) q = save;
                    else isf = true;
                }
                std::string txt(s, q);
                p = q;
                if (isf)
                {
                    v.t = V::real;
                    v.d = digits_to_double(txt);
                    return true;
                }
                size_t nd = txt.size() - (txt[0] == '-' ? 1 : 0);
                if (nd > 4300) return fail("Exceeds the limit (4300 digits) for integer string conversion");
                if (txt == "-0") txt = "0";
                v.t = V::integer;
                v.s = std::move(txt);
                return true;
            }
            bool lit(const char* w)
            {
                size_t n = std::strlen(w);
                if ((size_t)(e - p) >= n && std::memcmp(p, w, n) == 0)
                {
                    p += n;
                    return true;
                }
                return false;
            }
            bool value(V& v, int depth)
            {
                if (depth > 900) return fail("maximum recursion depth exceeded");
                if (p >= e) return fail("Expecting value");
                char c = *p;
                if (c == '"')
                {
                    v.t = V::string;
                    return str(v.s);
                }
                if (c == '{')
                {
                    v.t = V::object;
                    ++p;
                    ws();
                    if (p < e && *p == '}')
                    {
                        ++p;
                        return true;
                    }
                    while (true)
                    {
                        if (p >= e || *p != '"') return fail("Expecting property name enclosed in double quotes");
                        std::string k;
                        if (!str(k)) return false;
                        ws();
                        if (p >= e || *p != ':') return fail("Expecting ':' delimiter");
                        ++p;
                        ws();
                        V item;
                        if (!value(item, depth + 1)) return false;
                        if (V* ex = v.get(k)) *ex = std::move(item);
                        else v.o.emplace_back(std::move(k), std::move(item));
                        ws();
                        if (p < e && *p == '}')
                        {
                            ++p;
                            return true;
                        }
                        if (p >= e || *p != ',') return fail("Expecting ',' delimiter");
                        ++p;
                        ws();
                    }
                }
                if (c == '[')
                {
                    v.t = V::array;
                    ++p;
                    ws();
                    if (p < e && *p == ']')
                    {
                        ++p;
                        return true;
                    }
                    while (true)
                    {
                        V item;
                        if (!value(item, depth + 1)) return false;
                        v.a.push_back(std::move(item));
                        ws();
                        if (p < e && *p == ']')
                        {
                            ++p;
                            return true;
                        }
                        if (p >= e || *p != ',') return fail("Expecting ',' delimiter");
                        ++p;
                        ws();
                    }
                }
                if (c == 'n' && lit("null"))
                {
                    v.t = V::null;
                    return true;
                }
                if (c == 't' && lit("true"))
                {
                    v.t = V::boolean;
                    v.b = true;
                    return true;
                }
                if (c == 'f' && lit("false"))
                {
                    v.t = V::boolean;
                    v.b = false;
                    return true;
                }
                if (c == 'N' && lit("NaN"))
                {
                    v.t = V::real;
                    v.d = std::numeric_limits<double>::quiet_NaN();
                    return true;
                }
                if (c == 'I' && lit("Infinity"))
                {
                    v.t = V::real;
                    v.d = std::numeric_limits<double>::infinity();
                    return true;
                }
                if (c == '-' && lit("-Infinity"))
                {
                    v.t = V::real;
                    v.d = -std::numeric_limits<double>::infinity();
                    return true;
                }
                return number(v);
            }
        };

        bool parse(std::string_view text, V& out, std::string* err)
        {
            out = V{};
            if (!utf8_valid(text))
            {
                if (err) *err = "invalid utf-8";
                return false;
            }
            Parser ps{text.data(), text.data() + text.size(), text.data(), {}};
            ps.ws();
            if (!ps.value(out, 0))
            {
                if (err) *err = ps.err;
                return false;
            }
            ps.ws();
            if (ps.p != ps.e)
            {
                ps.fail("Extra data");
                if (err) *err = ps.err;
                return false;
            }
            return true;
        }

        std::string float_repr(double x)
        {
            if (std::isnan(x)) return "nan";
            if (std::isinf(x)) return x > 0 ? "inf" : "-inf";
            char buf[64];
            auto r = std::to_chars(buf, buf + sizeof(buf), x, std::chars_format::scientific);
            std::string sc(buf, r.ptr);
            std::string out;
            size_t i = 0;
            if (sc[0] == '-')
            {
                out.push_back('-');
                i = 1;
            }
            size_t epos = sc.find('e');
            std::string digits;
            for (size_t k = i; k < epos; ++k)
                if (sc[k] != '.') digits.push_back(sc[k]);
            int ex = std::atoi(sc.c_str() + epos + 1);
            int decpt = ex + 1;
            int n = (int)digits.size();
            if (decpt <= -4 || decpt > 16)
            {
                out.push_back(digits[0]);
                if (n > 1)
                {
                    out.push_back('.');
                    out.append(digits, 1, std::string::npos);
                }
                int e2 = decpt - 1;
                out += fmt("e%c%02d", e2 < 0 ? '-' : '+', e2 < 0 ? -e2 : e2);
                return out;
            }
            if (decpt <= 0)
            {
                out += "0.";
                out.append((size_t)(-decpt), '0');
                out += digits;
            }
            else if (decpt >= n)
            {
                out += digits;
                out.append((size_t)(decpt - n), '0');
                out += ".0";
            }
            else
            {
                out.append(digits, 0, (size_t)decpt);
                out.push_back('.');
                out.append(digits, (size_t)decpt, std::string::npos);
            }
            return out;
        }

        static void dump_str(const std::string& s, std::string& o)
        {
            static const char* hx = "0123456789abcdef";
            o.push_back('"');
            size_t i = 0;
            while (i < s.size())
            {
                uint32_t cp = get_cp(s, i);
                switch (cp)
                {
                case '"': o += "\\\""; continue;
                case '\\': o += "\\\\"; continue;
                case '\n': o += "\\n"; continue;
                case '\r': o += "\\r"; continue;
                case '\t': o += "\\t"; continue;
                case '\b': o += "\\b"; continue;
                case '\f': o += "\\f"; continue;
                default: break;
                }
                if (cp >= 0x20 && cp < 0x7F)
                {
                    o.push_back((char)cp);
                    continue;
                }
                auto u4 = [&](uint32_t u)
                {
                    o += "\\u";
                    o.push_back(hx[(u >> 12) & 0xF]);
                    o.push_back(hx[(u >> 8) & 0xF]);
                    o.push_back(hx[(u >> 4) & 0xF]);
                    o.push_back(hx[u & 0xF]);
                };
                if (cp >= 0x10000)
                {
                    uint32_t v = cp - 0x10000;
                    u4(0xD800 | (v >> 10));
                    u4(0xDC00 | (v & 0x3FF));
                }
                else u4(cp);
            }
            o.push_back('"');
        }

        void dump(const V& v, std::string& o, bool compact)
        {
            switch (v.t)
            {
            case V::null: o += "null"; return;
            case V::boolean: o += v.b ? "true" : "false"; return;
            case V::integer: o += v.s; return;
            case V::real:
                if (std::isnan(v.d)) o += "NaN";
                else if (std::isinf(v.d)) o += v.d > 0 ? "Infinity" : "-Infinity";
                else o += float_repr(v.d);
                return;
            case V::string: dump_str(v.s, o); return;
            case V::array:
                o.push_back('[');
                for (size_t i = 0; i < v.a.size(); ++i)
                {
                    if (i) o += compact ? "," : ", ";
                    dump(v.a[i], o, compact);
                }
                o.push_back(']');
                return;
            case V::object:
                o.push_back('{');
                for (size_t i = 0; i < v.o.size(); ++i)
                {
                    if (i) o += compact ? "," : ", ";
                    dump_str(v.o[i].first, o);
                    o += compact ? ":" : ": ";
                    dump(v.o[i].second, o, compact);
                }
                o.push_back('}');
                return;
            }
        }

        std::string dumps(const V& v, bool compact)
        {
            std::string o;
            dump(v, o, compact);
            return o;
        }

        static std::string str_repr(const std::string& s)
        {
            bool sq = s.find('\'') != std::string::npos, dq = s.find('"') != std::string::npos;
            char q = (sq && !dq) ? '"' : '\'';
            std::string o(1, q);
            size_t i = 0;
            while (i < s.size())
            {
                uint32_t cp = get_cp(s, i);
                if (cp == (uint32_t)q || cp == '\\')
                {
                    o.push_back('\\');
                    o.push_back((char)cp);
                }
                else if (cp == '\t') o += "\\t";
                else if (cp == '\n') o += "\\n";
                else if (cp == '\r') o += "\\r";
                else if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) o += fmt("\\x%02x", cp);
                else if (cp >= 0xD800 && cp <= 0xDFFF) o += fmt("\\u%04x", cp);
                else put_cp(o, cp);
            }
            o.push_back(q);
            return o;
        }

        std::string py_repr(const V& v)
        {
            switch (v.t)
            {
            case V::null: return "None";
            case V::boolean: return v.b ? "True" : "False";
            case V::integer: return v.s;
            case V::real: return float_repr(v.d);
            case V::string: return str_repr(v.s);
            case V::array:
            {
                std::string o = "[";
                for (size_t i = 0; i < v.a.size(); ++i)
                {
                    if (i) o += ", ";
                    o += py_repr(v.a[i]);
                }
                return o + "]";
            }
            case V::object:
            {
                std::string o = "{";
                for (size_t i = 0; i < v.o.size(); ++i)
                {
                    if (i) o += ", ";
                    o += str_repr(v.o[i].first) + ": " + py_repr(v.o[i].second);
                }
                return o + "}";
            }
            }
            return {};
        }

        std::string py_str(const V& v)
        {
            if (v.t == V::string) return v.s;
            return py_repr(v);
        }

        static bool int_text(const V& v, std::string& out)
        {
            if (v.t == V::boolean)
            {
                out = v.b ? "1" : "0";
                return true;
            }
            if (v.t == V::integer)
            {
                out = v.s;
                return true;
            }
            if (v.t == V::real && std::isfinite(v.d) && std::floor(v.d) == v.d && std::fabs(v.d) < 9.2e18)
            {
                out = std::to_string((long long)v.d);
                return true;
            }
            return false;
        }

        static bool num_eq(const V& a, const V& b)
        {
            if (a.t == V::real && b.t == V::real) return a.d == b.d;
            if (a.t != V::real && b.t != V::real)
            {
                std::string x, y;
                int_text(a, x);
                int_text(b, y);
                return x == y;
            }
            const V& r = a.t == V::real ? a : b;
            const V& i = a.t == V::real ? b : a;
            std::string x, y;
            if (!int_text(r, x)) return false;
            int_text(i, y);
            return x == y;
        }

        bool py_eq(const V& a, const V& b)
        {
            if (a.is_num() && b.is_num()) return num_eq(a, b);
            if (a.t != b.t) return false;
            switch (a.t)
            {
            case V::null: return true;
            case V::string: return a.s == b.s;
            case V::array:
                if (a.a.size() != b.a.size()) return false;
                for (size_t i = 0; i < a.a.size(); ++i)
                    if (!py_eq(a.a[i], b.a[i])) return false;
                return true;
            case V::object:
                if (a.o.size() != b.o.size()) return false;
                for (auto& kv : a.o)
                {
                    const V* o = b.get(kv.first);
                    if (!o || !py_eq(kv.second, *o)) return false;
                }
                return true;
            default: return false;
            }
        }

        bool py_lt(const V& a, const V& b, bool* type_error)
        {
            if (type_error) *type_error = false;
            if (a.is_num() && b.is_num())
            {
                if (a.t != V::real && b.t != V::real)
                {
                    std::string x, y;
                    int_text(a, x);
                    int_text(b, y);
                    long long xi = 0, yi = 0;
                    auto r1 = std::from_chars(x.data(), x.data() + x.size(), xi);
                    auto r2 = std::from_chars(y.data(), y.data() + y.size(), yi);
                    if (r1.ec == std::errc() && r2.ec == std::errc()) return xi < yi;
                }
                return a.num() < b.num();
            }
            if (a.t == V::string && b.t == V::string) return a.s < b.s;
            if (type_error) *type_error = true;
            return false;
        }

        bool key_of(const V& v, std::string& out)
        {
            switch (v.t)
            {
            case V::null: out = "N"; return true;
            case V::boolean:
            case V::integer:
            {
                std::string t;
                int_text(v, t);
                out = "i" + t;
                return true;
            }
            case V::real:
            {
                std::string t;
                if (int_text(v, t)) out = "i" + t;
                else out = "r" + float_repr(v.d);
                return true;
            }
            case V::string: out = "s" + v.s; return true;
            default: return false;
            }
        }
    }

    namespace z
    {
        static const uint32_t* crc_table()
        {
            static uint32_t t[256];
            static bool ready = [&]
            {
                for (uint32_t i = 0; i < 256; ++i)
                {
                    uint32_t c = i;
                    for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                    t[i] = c;
                }
                return true;
            }();
            (void)ready;
            return t;
        }

        uint32_t crc32(std::string_view data, uint32_t crc)
        {
            const uint32_t* t = crc_table();
            uint32_t c = crc ^ 0xFFFFFFFFu;
            for (unsigned char ch : data) c = t[(c ^ ch) & 0xFF] ^ (c >> 8);
            return c ^ 0xFFFFFFFFu;
        }

        uint32_t adler32(std::string_view data, uint32_t a)
        {
            uint32_t s1 = a & 0xFFFF, s2 = (a >> 16) & 0xFFFF;
            size_t i = 0, n = data.size();
            while (i < n)
            {
                size_t blk = std::min<size_t>(n - i, 5552);
                for (size_t k = 0; k < blk; ++k)
                {
                    s1 += (unsigned char)data[i + k];
                    s2 += s1;
                }
                s1 %= 65521;
                s2 %= 65521;
                i += blk;
            }
            return (s2 << 16) | s1;
        }

        static const uint16_t LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const uint8_t LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const uint16_t DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const uint8_t DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        static const uint8_t CLORD[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

        struct BitW
        {
            std::string& o;
            uint64_t acc = 0;
            int n = 0;
            void put(uint32_t v, int bits)
            {
                if (!bits) return;
                acc |= (uint64_t)(v & ((1u << bits) - 1)) << n;
                n += bits;
                while (n >= 8)
                {
                    o.push_back((char)(acc & 0xFF));
                    acc >>= 8;
                    n -= 8;
                }
            }
            void align()
            {
                if (n > 0) put(0, 8 - n);
            }
        };

        static void build_lengths(const uint32_t* freq, int nsym, int maxbits, uint8_t* len)
        {
            std::vector<uint32_t> f(freq, freq + nsym);
            while (true)
            {
                std::fill(len, len + nsym, (uint8_t)0);
                struct Node
                {
                    uint64_t w;
                    int left, right;
                };
                std::vector<Node> nodes;
                std::vector<int> leaf_node(nsym, -1);
                using Item = std::pair<uint64_t, int>;
                std::vector<Item> heap;
                for (int s = 0; s < nsym; ++s)
                    if (f[s])
                    {
                        leaf_node[s] = (int)nodes.size();
                        nodes.push_back({f[s], -1 - s, -1});
                        heap.push_back({((uint64_t)f[s] << 20) | (uint64_t)s, leaf_node[s]});
                    }
                if (heap.empty()) return;
                if (heap.size() == 1)
                {
                    for (int s = 0; s < nsym; ++s)
                        if (f[s]) len[s] = 1;
                    return;
                }
                auto cmp = [](const Item& a, const Item& b) { return a.first > b.first; };
                std::make_heap(heap.begin(), heap.end(), cmp);
                uint64_t seq = (uint64_t)nsym;
                while (heap.size() > 1)
                {
                    std::pop_heap(heap.begin(), heap.end(), cmp);
                    Item a = heap.back();
                    heap.pop_back();
                    std::pop_heap(heap.begin(), heap.end(), cmp);
                    Item b = heap.back();
                    heap.pop_back();
                    uint64_t w = nodes[a.second].w + nodes[b.second].w;
                    nodes.push_back({w, a.second, b.second});
                    heap.push_back({(w << 20) | (seq++ & 0xFFFFF), (int)nodes.size() - 1});
                    std::push_heap(heap.begin(), heap.end(), cmp);
                }
                int maxd = 0;
                std::vector<std::pair<int, int>> st;
                st.push_back({heap[0].second, 0});
                while (!st.empty())
                {
                    auto [ni, d] = st.back();
                    st.pop_back();
                    const Node& nd = nodes[ni];
                    if (nd.left < 0)
                    {
                        int s = -1 - nd.left;
                        len[s] = (uint8_t)std::min(d, 255);
                        maxd = std::max(maxd, d);
                    }
                    else
                    {
                        st.push_back({nd.left, d + 1});
                        st.push_back({nd.right, d + 1});
                    }
                }
                if (maxd <= maxbits) return;
                for (auto& x : f)
                    if (x) x = (x + 1) / 2;
            }
        }

        static void canon(const uint8_t* len, int n, uint16_t* code)
        {
            uint16_t blc[16] = {0}, next[16] = {0};
            for (int i = 0; i < n; ++i) blc[len[i]]++;
            blc[0] = 0;
            uint16_t c = 0;
            for (int b = 1; b < 16; ++b)
            {
                c = (uint16_t)((c + blc[b - 1]) << 1);
                next[b] = c;
            }
            for (int i = 0; i < n; ++i)
            {
                if (!len[i])
                {
                    code[i] = 0;
                    continue;
                }
                uint16_t v = next[len[i]]++;
                uint16_t r = 0;
                for (int k = 0; k < len[i]; ++k)
                {
                    r = (uint16_t)((r << 1) | (v & 1));
                    v >>= 1;
                }
                code[i] = r;
            }
        }

        struct Sym
        {
            uint16_t lit;
            uint16_t dist;
        };

        static int len_index(int l)
        {
            int i = 28;
            while (LBASE[i] > l) --i;
            return i;
        }
        static int dist_index(int d)
        {
            int i = 29;
            while (DBASE[i] > d) --i;
            return i;
        }

        static void force_two(uint32_t* f, int n)
        {
            int used = 0;
            for (int i = 0; i < n; ++i)
                if (f[i]) ++used;
            for (int i = 0; i < n && used < 2; ++i)
                if (!f[i])
                {
                    f[i] = 1;
                    ++used;
                }
        }

        static void emit_block(BitW& w, const std::vector<Sym>& syms, size_t s0, size_t s1, std::string_view raw, bool final)
        {
            uint32_t lf[286] = {0}, df[30] = {0};
            for (size_t i = s0; i < s1; ++i)
            {
                const Sym& s = syms[i];
                if (!s.dist) lf[s.lit]++;
                else
                {
                    lf[257 + len_index(s.lit)]++;
                    df[dist_index(s.dist)]++;
                }
            }
            lf[256]++;
            uint64_t extra = 0;
            for (size_t i = s0; i < s1; ++i)
                if (syms[i].dist) extra += LEXT[len_index(syms[i].lit)] + DEXT[dist_index(syms[i].dist)];
            uint32_t lfd[286], dfd[30];
            std::memcpy(lfd, lf, sizeof(lf));
            std::memcpy(dfd, df, sizeof(df));
            force_two(lfd, 286);
            force_two(dfd, 30);
            uint8_t ll[286], dl[30];
            build_lengths(lfd, 286, 15, ll);
            build_lengths(dfd, 30, 15, dl);
            int hlit = 286;
            while (hlit > 257 && !ll[hlit - 1]) --hlit;
            int hdist = 30;
            while (hdist > 1 && !dl[hdist - 1]) --hdist;
            std::vector<uint8_t> seq;
            seq.insert(seq.end(), ll, ll + hlit);
            seq.insert(seq.end(), dl, dl + hdist);
            std::vector<std::pair<uint8_t, uint8_t>> rle;
            for (size_t i = 0; i < seq.size();)
            {
                uint8_t v = seq[i];
                size_t j = i;
                while (j < seq.size() && seq[j] == v) ++j;
                size_t run = j - i;
                if (v == 0)
                {
                    while (run >= 11)
                    {
                        size_t k = std::min<size_t>(run, 138);
                        rle.push_back({18, (uint8_t)(k - 11)});
                        run -= k;
                    }
                    if (run >= 3)
                    {
                        rle.push_back({17, (uint8_t)(run - 3)});
                        run = 0;
                    }
                    while (run--) rle.push_back({0, 0});
                }
                else
                {
                    rle.push_back({v, 0});
                    --run;
                    while (run >= 3)
                    {
                        size_t k = std::min<size_t>(run, 6);
                        rle.push_back({16, (uint8_t)(k - 3)});
                        run -= k;
                    }
                    while (run--) rle.push_back({v, 0});
                }
                i = j;
            }
            uint32_t cf[19] = {0};
            for (auto& r : rle) cf[r.first]++;
            force_two(cf, 19);
            uint8_t cl[19];
            build_lengths(cf, 19, 7, cl);
            int hclen = 19;
            while (hclen > 4 && !cl[CLORD[hclen - 1]]) --hclen;
            uint64_t dyn = 3 + 5 + 5 + 4 + 3ull * (uint64_t)hclen;
            for (auto& r : rle) dyn += cl[r.first] + (r.first == 16 ? 2 : r.first == 17 ? 3 : r.first == 18 ? 7 : 0);
            for (int i = 0; i < 286; ++i) dyn += (uint64_t)lf[i] * ll[i];
            for (int i = 0; i < 30; ++i) dyn += (uint64_t)df[i] * dl[i];
            dyn += extra;
            uint8_t fl[288], fdl[30];
            for (int i = 0; i < 288; ++i) fl[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
            for (int i = 0; i < 30; ++i) fdl[i] = 5;
            uint64_t fix = 3;
            for (int i = 0; i < 286; ++i) fix += (uint64_t)lf[i] * fl[i];
            for (int i = 0; i < 30; ++i) fix += (uint64_t)df[i] * fdl[i];
            fix += extra;
            uint64_t nst = (raw.size() + 65534) / 65535;
            if (!nst) nst = 1;
            uint64_t sto = nst * (3 + 7 + 32) + 8ull * raw.size();
            if (sto < dyn && sto < fix)
            {
                size_t off = 0;
                do
                {
                    size_t k = std::min<size_t>(raw.size() - off, 65535);
                    bool last = final && off + k == raw.size();
                    w.put(last ? 1 : 0, 1);
                    w.put(0, 2);
                    w.align();
                    w.put((uint32_t)k, 16);
                    w.put((uint32_t)(~k & 0xFFFF), 16);
                    for (size_t i = 0; i < k; ++i) w.put((unsigned char)raw[off + i], 8);
                    off += k;
                } while (off < raw.size());
                return;
            }
            const uint8_t* L;
            const uint8_t* D;
            uint16_t lc[288], dc[30];
            w.put(final ? 1 : 0, 1);
            if (fix <= dyn)
            {
                w.put(1, 2);
                canon(fl, 288, lc);
                canon(fdl, 30, dc);
                L = fl;
                D = fdl;
            }
            else
            {
                w.put(2, 2);
                w.put((uint32_t)(hlit - 257), 5);
                w.put((uint32_t)(hdist - 1), 5);
                w.put((uint32_t)(hclen - 4), 4);
                for (int i = 0; i < hclen; ++i) w.put(cl[CLORD[i]], 3);
                uint16_t cc[19];
                canon(cl, 19, cc);
                for (auto& r : rle)
                {
                    w.put(cc[r.first], cl[r.first]);
                    if (r.first == 16) w.put(r.second, 2);
                    else if (r.first == 17) w.put(r.second, 3);
                    else if (r.first == 18) w.put(r.second, 7);
                }
                canon(ll, 286, lc);
                canon(dl, 30, dc);
                L = ll;
                D = dl;
            }
            for (size_t i = s0; i < s1; ++i)
            {
                const Sym& s = syms[i];
                if (!s.dist)
                {
                    w.put(lc[s.lit], L[s.lit]);
                    continue;
                }
                int li = len_index(s.lit);
                w.put(lc[257 + li], L[257 + li]);
                w.put((uint32_t)(s.lit - LBASE[li]), LEXT[li]);
                int di = dist_index(s.dist);
                w.put(dc[di], D[di]);
                w.put((uint32_t)(s.dist - DBASE[di]), DEXT[di]);
            }
            w.put(lc[256], L[256]);
        }

        std::string compress(std::string_view data, int level)
        {
            (void)level;
            const int MAX_CHAIN = 128, GOOD = 8, NICE = 128, LAZY = 16;
            const size_t WSIZE = 32768;
            const int HBITS = 15;
            size_t n = data.size();
            const uint8_t* d = reinterpret_cast<const uint8_t*>(data.data());
            std::vector<int32_t> head((size_t)1 << HBITS, -1);
            std::vector<int32_t> prev(n ? n : 1, -1);
            auto hash = [&](size_t i) { return (uint32_t)(((d[i] << 10) ^ (d[i + 1] << 5) ^ d[i + 2]) * 2654435761u) >> (32 - HBITS); };
            auto insert = [&](size_t i)
            {
                if (i + 2 >= n) return;
                uint32_t h = hash(i);
                prev[i] = head[h];
                head[h] = (int32_t)i;
            };
            auto longest = [&](size_t i, int prev_len, int& best_dist)
            {
                int best = 0;
                best_dist = 0;
                if (i + 2 >= n) return 0;
                int maxlen = (int)std::min<size_t>(258, n - i);
                int chain = MAX_CHAIN;
                if (prev_len >= GOOD) chain >>= 2;
                int32_t c = prev[i];
                while (c >= 0 && i - (size_t)c <= WSIZE && chain-- > 0)
                {
                    const uint8_t* a = d + i;
                    const uint8_t* b = d + c;
                    if (b[best] == a[best] && b[0] == a[0] && b[1] == a[1])
                    {
                        int l = 0;
                        while (l < maxlen && a[l] == b[l]) ++l;
                        if (l > best)
                        {
                            best = l;
                            best_dist = (int)(i - (size_t)c);
                            if (l >= NICE || l >= maxlen) break;
                        }
                    }
                    c = prev[c];
                }
                if (best == 3 && best_dist > 4096) best = 0;
                if (best < 3) best = 0;
                return best;
            };
            std::vector<Sym> syms;
            syms.reserve(n / 2 + 16);
            std::vector<size_t> sympos;
            sympos.reserve(n / 2 + 16);
            size_t i = 0;
            bool have_prev = false;
            int prev_len = 0, prev_dist = 0;
            while (i < n)
            {
                insert(i);
                int len = 0, dist = 0;
                if (!have_prev || prev_len < LAZY) len = longest(i, have_prev ? prev_len : 0, dist);
                if (have_prev)
                {
                    if (prev_len >= 3 && len <= prev_len)
                    {
                        syms.push_back({(uint16_t)prev_len, (uint16_t)prev_dist});
                        sympos.push_back(i - 1);
                        size_t end = i - 1 + (size_t)prev_len;
                        for (size_t j = i + 1; j < end; ++j) insert(j);
                        i = end;
                        have_prev = false;
                        continue;
                    }
                    syms.push_back({d[i - 1], 0});
                    sympos.push_back(i - 1);
                }
                prev_len = len;
                prev_dist = dist;
                have_prev = true;
                ++i;
            }
            if (have_prev)
            {
                if (prev_len >= 3)
                {
                    syms.push_back({(uint16_t)prev_len, (uint16_t)prev_dist});
                    sympos.push_back(n - 1);
                }
                else
                {
                    syms.push_back({d[n - 1], 0});
                    sympos.push_back(n - 1);
                }
            }
            std::string out;
            out.reserve(n / 3 + 64);
            out.push_back((char)0x78);
            out.push_back((char)(uint8_t)0x9C);
            BitW w{out};
            const size_t BLK = 16384;
            if (syms.empty()) emit_block(w, syms, 0, 0, std::string_view(), true);
            for (size_t s0 = 0; s0 < syms.size(); s0 += BLK)
            {
                size_t s1 = std::min(syms.size(), s0 + BLK);
                size_t r0 = sympos[s0];
                size_t r1 = s1 < syms.size() ? sympos[s1] : n;
                emit_block(w, syms, s0, s1, data.substr(r0, r1 - r0), s1 == syms.size());
            }
            w.align();
            uint32_t ad = adler32(data);
            out.push_back((char)(ad >> 24));
            out.push_back((char)(ad >> 16));
            out.push_back((char)(ad >> 8));
            out.push_back((char)ad);
            return out;
        }

        struct BitR
        {
            const uint8_t* p;
            size_t n;
            size_t pos = 0;
            uint64_t acc = 0;
            int cnt = 0;
            bool err = false;
            uint32_t bits(int need)
            {
                while (cnt < need)
                {
                    if (pos >= n)
                    {
                        err = true;
                        return 0;
                    }
                    acc |= (uint64_t)p[pos++] << cnt;
                    cnt += 8;
                }
                uint32_t v = (uint32_t)(acc & ((1ull << need) - 1));
                acc >>= need;
                cnt -= need;
                return v;
            }
            void align()
            {
                acc = 0;
                cnt = 0;
            }
        };

        struct Huff
        {
            uint16_t count[16];
            uint16_t sym[320];
        };

        enum class Kind : uint8_t { codes, lens, dists, fixed };

        static bool huff_build(Huff& h, const uint8_t* len, int n, Kind k)
        {
            std::memset(h.count, 0, sizeof(h.count));
            for (int i = 0; i < n; ++i) h.count[len[i]]++;
            if (h.count[0] == n) return k == Kind::dists || k == Kind::fixed;
            int left = 1, maxl = 0;
            for (int b = 1; b < 16; ++b)
            {
                left <<= 1;
                left -= h.count[b];
                if (left < 0) return false;
                if (h.count[b]) maxl = b;
            }
            if (left > 0 && k != Kind::fixed && (k == Kind::codes || maxl != 1)) return false;
            uint16_t offs[16];
            offs[1] = 0;
            for (int b = 1; b < 15; ++b) offs[b + 1] = (uint16_t)(offs[b] + h.count[b]);
            for (int i = 0; i < n; ++i)
                if (len[i]) h.sym[offs[len[i]]++] = (uint16_t)i;
            return true;
        }

        static int huff_decode(BitR& br, const Huff& h)
        {
            int code = 0, first = 0, index = 0;
            for (int l = 1; l < 16; ++l)
            {
                code |= (int)br.bits(1);
                if (br.err) return -1;
                int count = h.count[l];
                if (code - count < first) return h.sym[index + (code - first)];
                index += count;
                first += count;
                first <<= 1;
                code <<= 1;
            }
            return -1;
        }

        static bool inflate_codes(BitR& br, std::string& out, const Huff& lc, const Huff& dc, std::string* err, size_t max_output)
        {
            while (true)
            {
                int s = huff_decode(br, lc);
                if (s < 0)
                {
                    if (err) *err = "invalid code";
                    return false;
                }
                if (s < 256)
                {
                    if (out.size() >= max_output) { if (err) *err = "output too large"; return false; }
                    out.push_back((char)s);
                    continue;
                }
                if (s == 256) return true;
                s -= 257;
                if (s >= 29)
                {
                    if (err) *err = "invalid literal/length code";
                    return false;
                }
                size_t l = LBASE[s] + br.bits(LEXT[s]);
                int ds = huff_decode(br, dc);
                if (ds < 0 || ds >= 30)
                {
                    if (err) *err = "invalid distance code";
                    return false;
                }
                size_t dist = DBASE[ds] + br.bits(DEXT[ds]);
                if (br.err)
                {
                    if (err) *err = "incomplete or truncated stream";
                    return false;
                }
                if (dist > out.size())
                {
                    if (err) *err = "invalid distance too far back";
                    return false;
                }
                if (out.size() > max_output || l > max_output - out.size())
                {
                    if (err) *err = "output too large";
                    return false;
                }
                size_t from = out.size() - dist;
                for (size_t k = 0; k < l; ++k) out.push_back(out[from + k]);
            }
        }

        bool decompress(std::string_view data, std::string& out, std::string* err, size_t max_output)
        {
            out.clear();
            const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
            if (data.size() < 2)
            {
                if (err) *err = "incomplete or truncated stream";
                return false;
            }
            if ((p[0] & 0x0F) != 8 || (p[0] >> 4) > 7 || ((p[0] << 8) | p[1]) % 31 != 0 || (p[1] & 0x20))
            {
                if (err) *err = "incorrect header check";
                return false;
            }
            BitR br{p + 2, data.size() - 2};
            bool final = false;
            while (!final)
            {
                final = br.bits(1) != 0;
                uint32_t type = br.bits(2);
                if (br.err)
                {
                    if (err) *err = "incomplete or truncated stream";
                    return false;
                }
                if (type == 0)
                {
                    br.align();
                    if (br.pos + 4 > br.n)
                    {
                        if (err) *err = "incomplete or truncated stream";
                        return false;
                    }
                    uint32_t len = br.p[br.pos] | (br.p[br.pos + 1] << 8);
                    uint32_t nlen = br.p[br.pos + 2] | (br.p[br.pos + 3] << 8);
                    br.pos += 4;
                    if ((len ^ 0xFFFF) != nlen)
                    {
                        if (err) *err = "invalid stored block lengths";
                        return false;
                    }
                    if (br.pos + len > br.n)
                    {
                        if (err) *err = "incomplete or truncated stream";
                        return false;
                    }
                    if (out.size() > max_output || len > max_output - out.size())
                    { if (err) *err = "output too large"; return false; }
                    out.append(reinterpret_cast<const char*>(br.p + br.pos), len);
                    br.pos += len;
                }
                else if (type == 1)
                {
                    static Huff fl, fd;
                    static bool ready = []
                    {
                        uint8_t l[288], dl[30];
                        for (int i = 0; i < 288; ++i) l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
                        for (int i = 0; i < 30; ++i) dl[i] = 5;
                        huff_build(fl, l, 288, Kind::fixed);
                        huff_build(fd, dl, 30, Kind::fixed);
                        return true;
                    }();
                    (void)ready;
                    if (!inflate_codes(br, out, fl, fd, err, max_output)) return false;
                }
                else if (type == 2)
                {
                    int hlit = (int)br.bits(5) + 257, hdist = (int)br.bits(5) + 1, hclen = (int)br.bits(4) + 4;
                    if (br.err || hlit > 286 || hdist > 30)
                    {
                        if (err) *err = "too many length or distance symbols";
                        return false;
                    }
                    uint8_t cl[19] = {0};
                    for (int i = 0; i < hclen; ++i) cl[CLORD[i]] = (uint8_t)br.bits(3);
                    Huff ch;
                    if (!huff_build(ch, cl, 19, Kind::codes))
                    {
                        if (err) *err = "invalid code lengths set";
                        return false;
                    }
                    uint8_t lens[316] = {0};
                    int idx = 0;
                    while (idx < hlit + hdist)
                    {
                        int s = huff_decode(br, ch);
                        if (s < 0)
                        {
                            if (err) *err = "invalid code lengths set";
                            return false;
                        }
                        if (s < 16)
                        {
                            lens[idx++] = (uint8_t)s;
                            continue;
                        }
                        uint8_t v = 0;
                        int rep;
                        if (s == 16)
                        {
                            if (!idx)
                            {
                                if (err) *err = "invalid bit length repeat";
                                return false;
                            }
                            v = lens[idx - 1];
                            rep = 3 + (int)br.bits(2);
                        }
                        else if (s == 17) rep = 3 + (int)br.bits(3);
                        else rep = 11 + (int)br.bits(7);
                        if (idx + rep > hlit + hdist)
                        {
                            if (err) *err = "invalid bit length repeat";
                            return false;
                        }
                        while (rep--) lens[idx++] = v;
                    }
                    if (!lens[256])
                    {
                        if (err) *err = "invalid code -- missing end-of-block";
                        return false;
                    }
                    Huff lc, dc;
                    if (!huff_build(lc, lens, hlit, Kind::lens))
                    {
                        if (err) *err = "invalid literal/lengths set";
                        return false;
                    }
                    if (!huff_build(dc, lens + hlit, hdist, Kind::dists))
                    {
                        if (err) *err = "invalid distances set";
                        return false;
                    }
                    if (!inflate_codes(br, out, lc, dc, err, max_output)) return false;
                }
                else
                {
                    if (err) *err = "invalid block type";
                    return false;
                }
                if (br.err)
                {
                    if (err) *err = "incomplete or truncated stream";
                    return false;
                }
            }
            br.align();
            if (br.pos + 4 > br.n)
            {
                if (err) *err = "incomplete or truncated stream";
                return false;
            }
            uint32_t want = ((uint32_t)br.p[br.pos] << 24) | ((uint32_t)br.p[br.pos + 1] << 16) | ((uint32_t)br.p[br.pos + 2] << 8) | br.p[br.pos + 3];
            if (want != adler32(out))
            {
                if (err) *err = "incorrect data check";
                return false;
            }
            return true;
        }
    }

    namespace fs
    {
        static bool read(const std::wstring& path, std::string& out)
        {
            HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) return false;
            LARGE_INTEGER sz;
            if (!GetFileSizeEx(h, &sz))
            {
                CloseHandle(h);
                return false;
            }
            out.resize((size_t)sz.QuadPart);
            size_t got = 0;
            while (got < out.size())
            {
                DWORD r = 0;
                DWORD want = (DWORD)std::min<size_t>(out.size() - got, 1u << 30);
                if (!ReadFile(h, out.data() + got, want, &r, nullptr) || r == 0) break;
                got += r;
            }
            out.resize(got);
            CloseHandle(h);
            return true;
        }
        static bool write(const std::wstring& path, std::string_view data)
        {
            HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) return false;
            size_t put = 0;
            bool ok = true;
            while (put < data.size())
            {
                DWORD w = 0;
                DWORD want = (DWORD)std::min<size_t>(data.size() - put, 1u << 30);
                if (!WriteFile(h, data.data() + put, want, &w, nullptr) || w == 0)
                {
                    ok = false;
                    break;
                }
                put += w;
            }
            CloseHandle(h);
            return ok;
        }
        static bool append(const std::wstring& path, std::string_view data)
        {
            HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) return false;
            DWORD w = 0;
            bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &w, nullptr) && w == data.size();
            CloseHandle(h);
            return ok;
        }
        static bool replace(const std::wstring& from, const std::wstring& to)
        {
            return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
        }
        static bool stat(const std::wstring& path, int64_t& mtime, uint64_t& size)
        {
            WIN32_FILE_ATTRIBUTE_DATA a;
            if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return false;
            mtime = ((int64_t)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
            size = ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
            return true;
        }
        static std::string text_mode(std::string_view s)
        {
            std::string o;
            o.reserve(s.size() + 8);
            for (char c : s)
            {
                if (c == '\n') o.push_back('\r');
                o.push_back(c);
            }
            return o;
        }
        static std::string universal_newlines(std::string_view s)
        {
            std::string o;
            o.reserve(s.size());
            for (size_t i = 0; i < s.size(); ++i)
            {
                char c = s[i];
                if (c == '\r')
                {
                    o.push_back('\n');
                    if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
                }
                else o.push_back(c);
            }
            return o;
        }
    }

    static bool py_space(uint32_t cp)
    {
        return (cp >= 0x09 && cp <= 0x0D) || (cp >= 0x1C && cp <= 0x20) || cp == 0x85 || cp == 0xA0 || cp == 0x1680 ||
               (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000;
    }

    static std::string py_strip(const std::string& s)
    {
        size_t b = 0, e = s.size();
        while (b < e)
        {
            size_t i = b;
            uint32_t cp = js::get_cp(s, i);
            if (!py_space(cp)) break;
            b = i;
        }
        while (e > b)
        {
            size_t st = e - 1;
            while (st > b && ((uint8_t)s[st] & 0xC0) == 0x80) --st;
            size_t i = st;
            uint32_t cp = js::get_cp(s, i);
            if (!py_space(cp)) break;
            e = st;
        }
        return s.substr(b, e - b);
    }

    static bool read_text_strip(const std::wstring& path, std::string& out, bool& decode_error)
    {
        decode_error = false;
        std::string raw;
        if (!fs::read(path, raw)) return false;
        if (!js::utf8_valid(raw))
        {
            decode_error = true;
            return true;
        }
        out = py_strip(fs::universal_newlines(raw));
        return true;
    }

    static bool ascii_digits(const std::string& s)
    {
        if (s.empty()) return false;
        for (char c : s)
            if (c < '0' || c > '9') return false;
        return true;
    }

    static bool py_int(const js::V& v, int64_t& out)
    {
        using js::V;
        if (v.t == V::boolean)
        {
            out = v.b ? 1 : 0;
            return true;
        }
        if (v.t == V::integer)
        {
            auto r = std::from_chars(v.s.data(), v.s.data() + v.s.size(), out);
            return r.ec == std::errc();
        }
        if (v.t == V::real)
        {
            if (!std::isfinite(v.d) || std::fabs(v.d) > 9.2e18) return false;
            out = (int64_t)v.d;
            return true;
        }
        if (v.t == V::string)
        {
            std::string s = py_strip(v.s);
            std::string t;
            for (char c : s)
                if (c != '_') t.push_back(c);
            size_t k = 0;
            if (!t.empty() && (t[0] == '+' || t[0] == '-')) k = 1;
            if (k >= t.size()) return false;
            for (size_t i = k; i < t.size(); ++i)
                if (t[i] < '0' || t[i] > '9') return false;
            auto r = std::from_chars(t.data() + (t[0] == '+' ? 1 : 0), t.data() + t.size(), out);
            return r.ec == std::errc();
        }
        return false;
    }

    namespace proto
    {
        using js::V;

        std::string pack(uint8_t typ, const V& obj)
        {
            std::string body = js::dumps(obj, true);
            std::string d = "S2";
            d.push_back((char)PROTO);
            d.push_back((char)typ);
            d += z::compress(body, 6);
            return d;
        }

        Unpack unpack(std::string_view data, uint8_t& typ, V& obj, std::string& err)
        {
            if (!data.empty() && data[0] == '{')
            {
                err = "peer relay speaks protocol v1 (plain JSON datagram): update the other PC's relay and main.lua";
                return Unpack::v1;
            }
            if (data.size() < 5 || data[0] != 'S' || data[1] != '2')
            {
                err = fmt("unknown datagram (%d bytes)", (int)data.size());
                return Unpack::unknown;
            }
            if ((uint8_t)data[2] != PROTO)
            {
                err = fmt("peer relay speaks protocol v%d, this relay v%d", (int)(uint8_t)data[2], PROTO);
                return Unpack::version;
            }
            typ = (uint8_t)data[3];
            std::string raw, zerr;
            if (!z::decompress(data.substr(4), raw, &zerr, MAX_PACKET_JSON))
            {
                err = "error('Error -3 while decompressing data: " + zerr + "')";
                return Unpack::corrupt;
            }
            std::string jerr;
            if (!js::utf8_valid(raw))
            {
                err = "UnicodeDecodeError('utf-8')";
                return Unpack::corrupt;
            }
            if (!js::parse(raw, obj, &jerr))
            {
                err = "JSONDecodeError('" + jerr + "')";
                return Unpack::corrupt;
            }
            return Unpack::ok;
        }

        void AppCache::put(const std::string& key, V v)
        {
            auto it = map.find(key);
            if (it != map.end())
            {
                it->second = std::move(v);
                return;
            }
            map.emplace(key, std::move(v));
            order.push_back(key);
        }

        static const V& get_or_null(const V& o, std::string_view k)
        {
            static const V nil;
            const V* p = o.get(k);
            return p ? *p : nil;
        }

        static const V& get_or_zero(const V& o, std::string_view k)
        {
            static const V zero = V::Int(0);
            const V* p = o.get(k);
            return p ? *p : zero;
        }

        Sender::Err Sender::split(std::string_view txt, double now, const AppCache& have, std::vector<std::string>& out, std::string& msg)
        {
            out.clear();
            std::vector<const AppCache*> hv{&have};
            std::vector<std::string> common, hdrs;
            Err e = split_n(txt, now, hv, common, hdrs, msg);
            if (e != Err::none) return e;
            out.reserve(common.size() + 1);
            out.push_back(std::move(hdrs[0]));
            for (auto& d : common) out.push_back(std::move(d));
            return Err::none;
        }

        Sender::Err Sender::split_n(std::string_view txt, double now, const std::vector<const AppCache*>& haves, std::vector<std::string>& common, std::vector<std::string>& hdrs,
                                    std::string& msg)
        {
            common.clear();
            hdrs.assign(haves.size(), std::string());
            V m;
            if (!js::parse(txt, m)) return Err::json;
            if (m.t != V::object)
            {
                msg = "local snapshot is not a JSON object";
                return Err::shape;
            }
            const V* ver = m.get("v");
            if (!ver || !js::py_eq(*ver, V::Int(PROTO)))
            {
                msg = fmt("local main.lua writes snapshot v=%s, this relay needs v=%d: update main.lua (or use the v1 relay)", ver ? js::py_repr(*ver).c_str() : "None", PROTO);
                return Err::proto;
            }
            V npcs = V::Arr(), apps = V::Arr();
            m.pop("npcs", &npcs);
            m.pop("apps", &apps);
            V vx1;
            bool has_vx = m.pop("vx1", &vx1) && vx1.t == V::array;
            std::string ns;
            if (const V* nd = m.get("need")) ns = js::py_str(*nd);
            std::vector<std::string> need;
            {
                size_t s = 0;
                while (s <= ns.size())
                {
                    size_t c = ns.find(',', s);
                    if (c == std::string::npos) c = ns.size();
                    if (c > s) need.emplace_back(ns.substr(s, c - s));
                    s = c + 1;
                }
            }
            local_need_ = std::set<std::string>(need.begin(), need.end());
            if (npcs.t != V::array || apps.t != V::array)
            {
                msg = "local snapshot: npcs/apps are not lists";
                return Err::shape;
            }
            V ids = V::Arr();
            std::vector<std::string> keys;
            for (auto& n : npcs.a)
            {
                const V* id = n.t == V::object ? n.get("id") : nullptr;
                std::string k;
                if (!id || !js::key_of(*id, k))
                {
                    msg = "local snapshot: npc without a usable id";
                    return Err::shape;
                }
                ids.a.push_back(*id);
                keys.push_back(std::move(k));
            }
            std::vector<std::string> akeys;
            for (auto& a : apps.a)
            {
                const V* h = a.t == V::object ? a.get("a") : nullptr;
                std::string k;
                if (!h || !js::key_of(*h, k))
                {
                    msg = "local snapshot: app without a usable hash";
                    return Err::shape;
                }
                akeys.push_back(std::move(k));
            }
            V sid = get_or_null(m, "sid");
            const std::string new_sid = js::py_str(sid);
            if (new_sid != sid_) { npc_sent_.clear(); app_sent_.clear(); }
            sid_ = new_sid;
            V f = get_or_null(m, "f");
            for (size_t hi = 0; hi < haves.size(); ++hi)
            {
                const AppCache* have = haves[hi];
                if (!have) continue;
                std::string joined;
                bool first = true;
                for (auto& h : need)
                {
                    if (have->has("s" + h)) continue;
                    if (!first) joined.push_back(',');
                    joined += h;
                    first = false;
                }
                V mm = m;
                mm.set("need", V::Str(joined));
                mm.set("ids", ids);
                if (hi > 0 && has_vx && hi - 1 < vx1.a.size() && vx1.a[hi - 1].t == V::string) mm.set("x1", vx1.a[hi - 1]);
                hdrs[hi] = pack(T_HDR, mm);
            }
            static const char* SIG[7] = {"x", "y", "z", "yaw", "a", "s", "wp"};
            for (size_t i = 0; i < npcs.a.size(); ++i)
            {
                const V& n = npcs.a[i];
                std::vector<V> sig;
                sig.reserve(7);
                for (auto* k : SIG) sig.push_back(get_or_null(n, k));
                auto it = npc_sent_.find(keys[i]);
                if (it != npc_sent_.end() && now - it->second.at < KEEPALIVE)
                {
                    bool same = true;
                    for (size_t k = 0; k < 7 && same; ++k) same = js::py_eq(it->second.sig[k], sig[k]);
                    if (same) continue;
                }
                npc_sent_[keys[i]] = Sent{std::move(sig), now};
                V o = V::Object();
                o.set("sid", sid);
                o.set("f", f);
                o.set("n", n);
                common.push_back(pack(T_NPC, o));
            }
            for (size_t i = 0; i < apps.a.size(); ++i)
            {
                auto it = app_sent_.find(akeys[i]);
                double last = it == app_sent_.end() ? -1e9 : it->second;
                if (now - last < APP_MIN_GAP) continue;
                app_sent_[akeys[i]] = now;
                common.push_back(pack(T_APP, apps.a[i]));
            }
            for (auto it = npc_sent_.begin(); it != npc_sent_.end();)
            {
                if (now - it->second.at > 30) it = npc_sent_.erase(it);
                else ++it;
            }
            return Err::none;
        }

        Assembler::Err Assembler::feed(std::string_view data, double now, std::string& msg, uint8_t* typ_out)
        {
            uint8_t typ = 0;
            V obj;
            Unpack u = unpack(data, typ, obj, msg);
            if (u == Unpack::v1 || u == Unpack::unknown || u == Unpack::version) return Err::proto;
            if (u == Unpack::corrupt) return Err::corrupt;
            if (typ_out) *typ_out = typ;
            if (typ == T_HDR)
            {
                if (obj.t != V::object)
                {
                    msg = "AttributeError('header is not an object')";
                    return Err::corrupt;
                }
                if (has_hdr_)
                {
                    bool same_sid = js::py_eq(get_or_null(hdr_, "sid"), get_or_null(obj, "sid"));
                    if (same_sid && js::py_lt(get_or_zero(obj, "f"), get_or_zero(hdr_, "f")))
                    {
                        ++stale;
                        return Err::none;
                    }
                    if (!same_sid)
                    {
                        // A previous session can still have datagrams in flight after reconnect.
                        const std::string incoming = js::py_str(get_or_null(obj, "sid"));
                        if (retired_sids_.count(incoming)) { ++stale; return Err::none; }
                        // Fail closed instead of evicting history and accepting an old session.
                        if (retired_sids_.size() >= 256) { msg = "session history full: reconnect relay"; return Err::corrupt; }
                        retired_sids_.insert(js::py_str(get_or_null(hdr_, "sid")));
                        npcs_.clear();
                    }
                }
                hdr_ = std::move(obj);
                has_hdr_ = true;
                ++hdr_n;
            }
            else if (typ == T_NPC)
            {
                const V* n = obj.t == V::object ? obj.get("n") : nullptr;
                if (!n)
                {
                    msg = "KeyError('n')";
                    return Err::corrupt;
                }
                const V* id = n->t == V::object ? n->get("id") : nullptr;
                std::string k;
                if (!id || !js::key_of(*id, k))
                {
                    msg = "KeyError('id')";
                    return Err::corrupt;
                }
                if (retired_sids_.count(js::py_str(get_or_null(obj, "sid")))) { ++stale; return Err::none; }
                auto it = npcs_.find(k);
                if (has_hdr_ && !js::py_eq(get_or_null(obj, "sid"), get_or_null(hdr_, "sid")))
                { ++stale; return Err::none; }
                if (it != npcs_.end() && js::py_eq(it->second.sid, get_or_null(obj, "sid")) && js::py_lt(get_or_zero(obj, "f"), it->second.f))
                {
                    ++stale;
                    return Err::none;
                }
                Npc rec{get_or_null(obj, "sid"), get_or_zero(obj, "f"), *n, now};
                npcs_[k] = std::move(rec);
            }
            else if (typ == T_APP)
            {
                const V* h = obj.t == V::object ? obj.get("a") : nullptr;
                std::string k;
                if (!h || !js::key_of(*h, k))
                {
                    msg = "KeyError('a')";
                    return Err::corrupt;
                }
                apps.put(k, std::move(obj));
                if (apps.map.size() > 512)
                {
                    size_t drop = std::min<size_t>(64, apps.order.size());
                    for (size_t i = 0; i < drop; ++i) apps.map.erase(apps.order[i]);
                    apps.order.erase(apps.order.begin(), apps.order.begin() + (ptrdiff_t)drop);
                }
            }
            else
            {
                msg = fmt("unknown datagram type %d", (int)typ);
                return Err::proto;
            }
            return Err::none;
        }

        bool Assembler::render(const std::set<std::string>& local_need, double now, double dx, std::string& out, int pid)
        {
            if (!has_hdr_) return false;
            V m = hdr_;
            V ids = V::Arr();
            m.pop("ids", &ids);
            if (ids.t != V::array) ids = V::Arr();
            std::set<std::string> keep;
            std::vector<std::string> idk(ids.a.size());
            std::vector<bool> idok(ids.a.size(), false);
            for (size_t i = 0; i < ids.a.size(); ++i)
                if (js::key_of(ids.a[i], idk[i]))
                {
                    idok[i] = true;
                    keep.insert(idk[i]);
                }
            for (auto it = npcs_.begin(); it != npcs_.end();)
            {
                double age = now - it->second.at;
                if ((!keep.count(it->first) && age > 2.0) || age > 30) it = npcs_.erase(it);
                else ++it;
            }
            const V& msid = get_or_null(m, "sid");
            V list = V::Arr();
            for (size_t i = 0; i < ids.a.size(); ++i)
            {
                if (!idok[i]) continue;
                auto it = npcs_.find(idk[i]);
                if (it == npcs_.end() || !js::py_eq(it->second.sid, msid) || now - it->second.at > NPC_STALE) continue;
                V n = it->second.n;
                if (dx != 0)
                    if (V* x = n.get("x"))
                        if (x->is_num()) *x = V::Real(x->num() + dx);
                list.a.push_back(std::move(n));
            }
            if (dx != 0)
                if (V* x = m.get("x"))
                    if (x->is_num()) *x = V::Real(x->num() + dx);
            V al = V::Arr();
            for (auto& h : local_need)
            {
                auto it = apps.map.find("s" + h);
                if (it != apps.map.end()) al.a.push_back(it->second);
            }
            if (pid >= 0) m.set("pid", V::Int(pid));
            m.set("apps", std::move(al));
            m.set("npcs", std::move(list));
            out = js::dumps(m, false);
            return true;
        }

        struct SxError : std::runtime_error
        {
            using std::runtime_error::runtime_error;
        };

        SaveXfer::SaveXfer(std::wstring dir, Say say) : dir_(std::move(dir)), say_(std::move(say))
        {
            std::string cur;
            bool bad = false;
            if (read_text_strip(p(L"sx_req.txt"), cur, bad) && DeleteFileW(p(L"sx_req.txt").c_str()))
                say_(fmt("relay sx: request %s left in sx_req.txt by an earlier run removed: no client asks for it", bad ? "?" : cur.c_str()));
        }

        SaveXfer::~SaveXfer()
        {
            if (part_) std::fclose((std::FILE*)part_);
        }

        std::wstring SaveXfer::p(const wchar_t* name) const
        {
            std::wstring d = dir_;
            if (!d.empty() && d.back() != L'\\' && d.back() != L'/') d.push_back(L'\\');
            return d + name;
        }

        static bool pkg_id_of(const std::string& b, std::string& id)
        {
            size_t nl = b.find('\n');
            std::string hdr = nl == std::string::npos ? (b.empty() ? std::string() : b.substr(0, b.size() - 1)) : b.substr(0, nl);
            for (auto& c : hdr)
                if ((uint8_t)c >= 0x80) c = '?';
            id.clear();
            size_t i = 0;
            auto sp = [](char c) { return c == ' ' || (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x1F); };
            while (i < hdr.size())
            {
                while (i < hdr.size() && sp(hdr[i])) ++i;
                size_t s = i;
                while (i < hdr.size() && !sp(hdr[i])) ++i;
                if (i > s && hdr.compare(s, 3, "id=") == 0 && i - s >= 3)
                {
                    id = hdr.substr(s + 3, i - s - 3);
                    return true;
                }
            }
            return false;
        }

        void SaveXfer::load_pkg()
        {
            int64_t mt;
            uint64_t sz;
            if (!fs::stat(p(L"sx_out.pkg"), mt, sz))
            {
                has_pkg_ = false;
                pkg_.clear();
                has_pkg_id_ = false;
                pkg_id_.clear();
                return;
            }
            if (has_pkg_ && mt == pkg_mtime_ && sz == pkg_size_) return;
            std::string b;
            if (!fs::read(p(L"sx_out.pkg"), b)) return;
            if (b.size() != sz) return;
            has_pkg_id_ = pkg_id_of(b, pkg_id_);
            pkg_ = std::move(b);
            pkg_mtime_ = mt;
            pkg_size_ = sz;
            has_pkg_ = true;
            pkg_crc_ = z::crc32(pkg_);
        }

        void SaveXfer::on_want(const V& obj, const Send& send)
        {
            const V* idv = obj.get("id");
            std::string wid = idv ? js::py_str(*idv) : std::string();
            if (!ascii_digits(wid)) return;
            load_pkg();
            if (!(has_pkg_id_ && pkg_id_ == wid))
            {
                std::string cur;
                bool bad = false;
                if (!read_text_strip(p(L"sx_req.txt"), cur, bad)) cur.clear();
                if (bad) throw SxError("UnicodeDecodeError('utf-8')");
                if (cur != wid)
                {
                    if (!fs::write(p(L"sx_req.txt.tmp"), fs::text_mode(wid + "\n"))) throw SxError("OSError('sx_req.txt.tmp')");
                    if (!fs::replace(p(L"sx_req.txt.tmp"), p(L"sx_req.txt"))) throw SxError("PermissionError('sx_req.txt')");
                    stats.req++;
                    say_(fmt("relay sx: client asked for the host save (request %s) -> sx_req.txt", wid.c_str()));
                }
                req_id_ = wid;
                req_at_ = now_;
                V o = V::Object();
                o.set("id", V::Str(wid));
                o.set("st", V::Str("wait"));
                send(pack(T_SXI, o));
                return;
            }
            serve(wid, pkg_, pkg_crc_, obj, send, stats);
        }

        void SaveXfer::serve(const std::string& wid, const std::string& pkg_, uint32_t pkg_crc_, const V& obj, const Send& send, SxStats& stats)
        {
            uint64_t n = (pkg_.size() + SX_CH - 1) / SX_CH;
            V o = V::Object();
            o.set("id", V::Str(wid));
            o.set("st", V::Str("ok"));
            o.set("size", V::Int((int64_t)pkg_.size()));
            o.set("crc", V::Int((int64_t)pkg_crc_));
            o.set("ch", V::Int((int64_t)SX_CH));
            send(pack(T_SXI, o));
            uint64_t widn = 0;
            auto pr = std::from_chars(wid.data(), wid.data() + wid.size(), widn);
            bool wid_ok = pr.ec == std::errc();
            int k = 0;
            const V* r = obj.get("r");
            if (!r) return;
            if (r->t != V::array) throw SxError("TypeError('r')");
            size_t lim = std::min<size_t>(r->a.size(), 16);
            for (size_t ri = 0; ri < lim; ++ri)
            {
                const V& pair = r->a[ri];
                if (pair.t != V::array || pair.a.size() != 2) throw SxError("ValueError('r pair')");
                int64_t a, b;
                if (!py_int(pair.a[0], a) || !py_int(pair.a[1], b)) throw SxError("ValueError('r value')");
                int64_t lo = std::max<int64_t>(0, a);
                int64_t hi = std::min<int64_t>((int64_t)n, b + 1);
                for (int64_t i = lo; i < hi; ++i)
                {
                    if (k >= SX_WIN) return;
                    if (!wid_ok || i > 0xFFFFFFFFll) throw SxError("error('argument out of range')");
                    std::string d = "S2";
                    d.push_back((char)PROTO);
                    d.push_back((char)T_SXC);
                    for (int s = 0; s < 8; ++s) d.push_back((char)((widn >> (8 * s)) & 0xFF));
                    uint32_t i32 = (uint32_t)i;
                    for (int s = 0; s < 4; ++s) d.push_back((char)((i32 >> (8 * s)) & 0xFF));
                    size_t off = (size_t)i * SX_CH;
                    d.append(pkg_, off, std::min<size_t>(SX_CH, pkg_.size() - off));
                    send(d);
                    ++k;
                    stats.sent++;
                }
            }
        }

        void SaveXfer::reset()
        {
            if (part_)
            {
                std::fclose((std::FILE*)part_);
                part_ = nullptr;
            }
            has_info_ = false;
            got_.clear();
            asked_.clear();
        }

        void SaveXfer::start(const V& info)
        {
            int64_t size, crc, ch;
            const V* a = info.get("size");
            const V* b = info.get("crc");
            const V* c = info.get("ch");
            if (!a || !b || !c) throw SxError("KeyError('size')");
            if (!py_int(*a, size) || !py_int(*b, crc) || !py_int(*c, ch)) throw SxError("ValueError('info')");
            if (ch == 0) throw SxError("ZeroDivisionError('integer division or modulo by zero')");
            if (size < 0 || ch < 0) throw SxError("ValueError('negative size')");
            if (size > SX_MAX_SIZE || ch > SX_MAX_CH || ((uint64_t)size + (uint64_t)ch - 1) / (uint64_t)ch > SX_MAX_N) throw SxError("ValueError('size or chunk too large')");
            uint64_t n = ((uint64_t)size + (uint64_t)ch - 1) / (uint64_t)ch;
            std::string head = fmt("%s %lld %lld %lld\n", want_id_.c_str(), (long long)size, (long long)crc, (long long)ch);
            std::vector<uint8_t> got((size_t)n, 0);
            std::string m;
            if (fs::read(p(L"sx_in.map"), m))
            {
                size_t nl = m.find('\n');
                std::string mh = nl == std::string::npos ? std::string() : m.substr(0, nl + 1);
                for (auto& ch2 : mh)
                    if ((uint8_t)ch2 >= 0x80) ch2 = '?';
                int64_t pm;
                uint64_t psz;
                if (nl != std::string::npos && mh == head && m.size() - nl - 1 == n)
                {
                    if (!fs::stat(p(L"sx_in.part"), pm, psz)) psz = UINT64_MAX;
                    if (psz == (uint64_t)size)
                    {
                        got.assign(m.begin() + (ptrdiff_t)nl + 1, m.end());
                        uint64_t sum = 0;
                        for (auto g : got) sum += g;
                        say_(fmt("relay sx: resuming request %s: %llu/%llu chunks already here", want_id_.c_str(), (unsigned long long)sum, (unsigned long long)n));
                    }
                }
            }
            bool any = false;
            for (auto g : got)
                if (g)
                {
                    any = true;
                    break;
                }
            if (!any)
            {
                HANDLE h = CreateFileW(p(L"sx_in.part").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h == INVALID_HANDLE_VALUE) throw SxError("OSError('sx_in.part')");
                LARGE_INTEGER li;
                li.QuadPart = size;
                bool ok = SetFilePointerEx(h, li, nullptr, FILE_BEGIN) && SetEndOfFile(h);
                CloseHandle(h);
                if (!ok) throw SxError("OSError('sx_in.part truncate')");
            }
            std::FILE* f = nullptr; _wfopen_s(&f, p(L"sx_in.part").c_str(), L"r+b");
            if (!f) throw SxError("OSError('sx_in.part open')");
            part_ = f;
            has_info_ = true;
            size_ = (uint64_t)size;
            crc_ = (uint32_t)crc;
            ch_ = (uint64_t)ch;
            n_ = n;
            head_ = head;
            got_ = std::move(got);
        }

        void SaveXfer::save_map()
        {
            std::string m = head_;
            m.append(reinterpret_cast<const char*>(got_.data()), got_.size());
            if (!fs::write(p(L"sx_in.map.tmp"), m) || !fs::replace(p(L"sx_in.map.tmp"), p(L"sx_in.map"))) throw SxError("OSError('sx_in.map')");
            uint64_t sum = 0;
            for (auto g : got_) sum += g;
            std::string pr = fs::text_mode(fmt("%s %llu/%llu\n", want_id_.c_str(), (unsigned long long)sum, (unsigned long long)n_));
            if (!fs::write(p(L"sx_in.prog.tmp"), pr) || !fs::replace(p(L"sx_in.prog.tmp"), p(L"sx_in.prog"))) throw SxError("OSError('sx_in.prog')");
        }

        void SaveXfer::finish()
        {
            std::fclose((std::FILE*)part_);
            part_ = nullptr;
            std::string b;
            if (!fs::read(p(L"sx_in.part"), b)) throw SxError("OSError('sx_in.part read')");
            if (z::crc32(b) != crc_)
            {
                stats.crcbad++;
                say_(fmt("relay sx: request %s CRC32 bad: asking every chunk again", want_id_.c_str()));
                got_.assign((size_t)n_, 0);
                asked_.clear();
                std::FILE* f = nullptr; _wfopen_s(&f, p(L"sx_in.part").c_str(), L"r+b");
                if (!f) throw SxError("OSError('sx_in.part reopen')");
                part_ = f;
                return;
            }
            if (!fs::replace(p(L"sx_in.part"), p(L"sx_in.pkg"))) throw SxError("PermissionError('sx_in.pkg')");
            DeleteFileW(p(L"sx_in.map").c_str());
            fs::write(p(L"sx_in.prog"), fs::text_mode(fmt("%s %llu/%llu done\n", want_id_.c_str(), (unsigned long long)n_, (unsigned long long)n_)));
            stats.done++;
            say_(fmt("relay sx: request %s complete: %llu B -> sx_in.pkg", want_id_.c_str(), (unsigned long long)b.size()));
            reset();
            want_id_ = "done:" + want_id_;
        }

        void SaveXfer::on_info(const V& obj)
        {
            if (!has_want_ || has_info_) return;
            const V* idv = obj.get("id");
            std::string id = idv ? js::py_str(*idv) : std::string("None");
            if (id != want_id_) return;
            const V* st = obj.get("st");
            if (!st || st->t != V::string || st->s != "ok") return;
            start(obj);
        }

        void SaveXfer::on_chunk(std::string_view data)
        {
            if (!has_info_ || data.size() < 16) return;
            uint64_t wid = 0;
            uint32_t i = 0;
            for (int s = 0; s < 8; ++s) wid |= (uint64_t)(uint8_t)data[4 + s] << (8 * s);
            for (int s = 0; s < 4; ++s) i |= (uint32_t)(uint8_t)data[12 + s] << (8 * s);
            if (std::to_string(wid) != want_id_ || i >= n_ || got_[i]) return;
            std::FILE* f = (std::FILE*)part_;
            _fseeki64(f, (int64_t)i * (int64_t)ch_, SEEK_SET);
            std::fwrite(data.data() + 16, 1, data.size() - 16, f);
            got_[i] = 1;
            stats.recv++;
            asked_.erase(i);
            for (auto g : got_)
                if (!g) return;
            finish();
        }

        void SaveXfer::tick(double now, const Send& send)
        {
            now_ = now;
            if (!req_id_.empty() && now - req_at_ > SX_REQ_LIVE)
            {
                std::string cur;
                bool bad = false;
                if (read_text_strip(p(L"sx_req.txt"), cur, bad) && !bad && cur == req_id_ && DeleteFileW(p(L"sx_req.txt").c_str()))
                    say_(fmt("relay sx: request %s dropped from sx_req.txt (no client asks for it now)", req_id_.c_str()));
                req_id_.clear();
            }
            auto done = [&] { return want_id_.rfind("done:", 0) == 0; };
            if (now >= want_chk_)
            {
                want_chk_ = now + 0.25;
                std::string wid;
                bool bad = false;
                if (!read_text_strip(p(L"sx_want.txt"), wid, bad)) wid.clear();
                if (bad) throw SxError("UnicodeDecodeError('utf-8')");
                bool none = !ascii_digits(wid);
                if (none)
                {
                    if (has_want_ && !done())
                    {
                        if (part_) save_map();
                        reset();
                    }
                    has_want_ = false;
                    want_id_.clear();
                }
                else if ((!has_want_ || wid != want_id_) && !(has_want_ && want_id_ == "done:" + wid))
                {
                    if (part_) save_map();
                    reset();
                    has_want_ = true;
                    want_id_ = wid;
                    say_(fmt("relay sx: asking the host for its save (request %s)", wid.c_str()));
                }
            }
            if (!has_want_ || done() || now < next_want_) return;
            next_want_ = now + SX_GAP;
            if (!has_info_)
            {
                if (now - last_info_ask_ >= 0.5)
                {
                    last_info_ask_ = now;
                    V o = V::Object();
                    o.set("id", V::Str(want_id_));
                    V r = V::Arr();
                    V pr = V::Arr();
                    pr.a.push_back(V::Int(0));
                    pr.a.push_back(V::Int(SX_WIN - 1));
                    r.a.push_back(std::move(pr));
                    o.set("r", std::move(r));
                    send(pack(T_SXW, o));
                }
                return;
            }
            V r = V::Arr();
            int k = 0;
            uint64_t i = 0;
            auto askable = [&](uint64_t j)
            {
                auto it = asked_.find(j);
                double at = it == asked_.end() ? -1e9 : it->second;
                return !got_[(size_t)j] && now - at >= SX_RETRY;
            };
            while (i < n_ && k < SX_WIN && r.a.size() < 16)
            {
                if (askable(i))
                {
                    uint64_t a = i;
                    while (i < n_ && k < SX_WIN && askable(i))
                    {
                        asked_[i] = now;
                        ++i;
                        ++k;
                    }
                    V pr = V::Arr();
                    pr.a.push_back(V::Int((int64_t)a));
                    pr.a.push_back(V::Int((int64_t)i - 1));
                    r.a.push_back(std::move(pr));
                }
                else ++i;
            }
            if (!r.a.empty())
            {
                V o = V::Object();
                o.set("id", V::Str(want_id_));
                o.set("r", std::move(r));
                send(pack(T_SXW, o));
            }
            if (now >= map_at_)
            {
                map_at_ = now + 1.0;
                save_map();
            }
        }

        void SaveXfer::feed(std::string_view data, const Send& send)
        {
            uint8_t typ = (uint8_t)data[3];
            if (typ == T_SXC)
            {
                on_chunk(data);
                return;
            }
            uint8_t t2;
            V obj;
            std::string err;
            if (unpack(data, t2, obj, err) != Unpack::ok) throw SxError(err);
            if (obj.t != V::object) throw SxError("AttributeError('not an object')");
            if (typ == T_SXW) on_want(obj, send);
            else if (typ == T_SXI) on_info(obj);
        }
    }

    std::set<std::string> relay_tags_d1(std::string_view tsv)
    {
        std::set<std::string> out;
        size_t b = 0;
        bool head = true;
        while (b < tsv.size())
        {
            size_t e = tsv.find('\n', b);
            if (e == std::string_view::npos) e = tsv.size();
            std::string_view ln = tsv.substr(b, e - b);
            b = e + 1;
            if (!ln.empty() && ln.back() == '\r') ln.remove_suffix(1);
            if (head)
            {
                head = false;
                continue;
            }
            std::vector<std::string_view> f;
            size_t s = 0;
            while (true)
            {
                size_t t = ln.find('\t', s);
                if (t == std::string_view::npos)
                {
                    f.push_back(ln.substr(s));
                    break;
                }
                f.push_back(ln.substr(s, t - s));
                s = t + 1;
            }
            if (f.size() < 4 || f[0].size() < 2 || f[0][0] != 's' || f[1] != "srelay") continue;
            if (f[3] == "1") out.insert(std::string(f[0].substr(1)));
        }
        return out;
    }

    static bool rt_up(char c) { return c >= 'A' && c <= 'Z'; }
    static bool rt_ch(char c) { return rt_up(c) || (c >= '0' && c <= '9'); }

    std::string x1_relay(std::string_view x1, const std::set<std::string>& keep)
    {
        std::string o;
        if (keep.count("@W")) o.append(x1.substr(0, x1.find('#')));
        size_t i = 0;
        while (i < x1.size())
        {
            if (x1[i] != '#')
            {
                ++i;
                continue;
            }
            size_t j = i + 1;
            if (j < x1.size() && rt_up(x1[j]))
            {
                while (j < x1.size() && rt_ch(x1[j])) ++j;
                if (j < x1.size() && x1[j] == ':')
                {
                    size_t pe = x1.find('#', j + 1);
                    if (pe == std::string_view::npos) pe = x1.size();
                    if (keep.count(std::string(x1.substr(i + 1, j - i - 1)))) o.append(x1.substr(i, pe - i));
                    i = pe;
                    continue;
                }
            }
            ++i;
        }
        return o;
    }

    namespace
    {
        using js::V;

        std::string addr_key(const sockaddr_in& a)
        {
            char b[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &a.sin_addr, b, sizeof b);
            return std::string(b) + ":" + std::to_string((int)ntohs(a.sin_port));
        }

        V num_or(const V& h, const char* k, int64_t d)
        {
            const V* v = h.get(k);
            return v ? *v : V::Int(d);
        }

        std::string roster_json(const Roster& r)
        {
            V o = V::Object();
            o.set("hub", V::Int(r.hub ? 1 : 0));
            o.set("full", V::Int(r.full ? 1 : 0));
            o.set("you", V::Int(r.you));
            o.set("max", V::Int(r.max));
            o.set("seq", V::Int((int64_t)r.seq));
            V ps = V::Arr();
            for (auto& e : r.peers)
            {
                V q = V::Object();
                q.set("pid", V::Int(e.pid));
                q.set("sid", V::Str(e.sid));
                q.set("st", V::Str(e.st));
                q.set("addr", V::Str(e.addr));
                ps.a.push_back(std::move(q));
            }
            o.set("peers", std::move(ps));
            if (!r.world.empty()) o.set("w", V::Str(r.world));
            return js::dumps(o, false);
        }

        struct Hub
        {
            struct Conn
            {
                bool used = false;
                bool up = false;
                bool dirty = false;
                int pid = 0;
                sockaddr_in addr{};
                std::string akey, sid, last_sid, want_id;
                proto::Assembler asmb;
                double last_rx = -1, silent_at = 0, peer_at = -1e9, want_at = -1e9;
                uint64_t peer_n = 0, rx = 0;
                std::map<std::string, double> fwd;
            };
            struct Pkg
            {
                std::string b;
                uint32_t crc = 0;
                double used = 0;
            };

            const Config& cfg;
            int max = 1;
            std::function<bool(const std::string&, const sockaddr_in&)> send;
            std::function<void(const std::string&)> say;
            std::function<void(const char*, const std::string&)> warn;
            std::function<void(int, const std::string&)> view_bye;
            std::function<void(const Roster&)> publish;
            std::vector<Conn> c;
            std::map<std::string, int> by_addr;
            std::map<std::string, double> refused;
            std::string nonce;
            bool roster_dirty = true;
            std::string world;
            double roster_at = -1e9, pkg_at = -1e9;
            uint64_t roster_seq = 0;
            std::map<std::string, Pkg> pkgs;
            std::deque<std::string> q;
            bool pkg_has = false;
            int64_t pkg_mtime = 0;
            uint64_t pkg_size = 0;
            proto::SxStats sxs;
            uint64_t peer_tx = 0, roster_tx = 0, full_tx = 0, app_fwd = 0, byes_rx = 0;

            explicit Hub(const Config& k) : cfg(k), max(std::clamp(k.maxclients, 1, proto::MAXV)), c((size_t)std::clamp(k.maxclients, 1, proto::MAXV)) {}

            int count() const
            {
                int n = 0;
                for (auto& k : c) n += k.used ? 1 : 0;
                return n;
            }

            std::wstring p(const wchar_t* name) const
            {
                std::wstring d = cfg.dir;
                if (!d.empty() && d.back() != L'\\' && d.back() != L'/') d.push_back(L'\\');
                return d + name;
            }

            int free_slot(const std::string& sid) const
            {
                if (!sid.empty())
                    for (int i = 0; i < max; ++i)
                        if (!c[(size_t)i].used && c[(size_t)i].last_sid == sid) return i;
                for (int i = 0; i < max; ++i)
                    if (!c[(size_t)i].used) return i;
                return -1;
            }

            void admit(int i, const sockaddr_in& from, const std::string& ak, const std::string& sid, double now)
            {
                Conn& k = c[(size_t)i];
                std::string keep = k.last_sid;
                k = Conn{};
                k.last_sid = keep;
                k.used = true;
                k.up = true;
                k.pid = i + 1;
                k.addr = from;
                k.akey = ak;
                k.sid = sid;
                k.last_rx = now;
                by_addr[ak] = i;
                refused.erase(ak);
                roster_dirty = true;
                say(fmt("relay hub: pid %d admitted from %s (sid=%s); players %d of %d", k.pid, ak.c_str(), sid.empty() ? "-" : sid.c_str(), count(), max));
            }

            void drop(int i, const std::string& why)
            {
                Conn& k = c[(size_t)i];
                by_addr.erase(k.akey);
                std::string ls = k.sid.empty() ? k.last_sid : k.sid;
                int pid = k.pid;
                std::string ak = k.akey, sid = k.sid;
                k = Conn{};
                k.last_sid = ls;
                roster_dirty = true;
                say(fmt("relay hub: pid %d %s (sid=%s, %s); players %d of %d", pid, why.c_str(), sid.empty() ? "-" : sid.c_str(), ak.c_str(), count(), max));
            }

            void refuse(const sockaddr_in& from, const std::string& ak, double now)
            {
                auto it = refused.find(ak);
                if (it != refused.end() && now - it->second < 1.0) return;
                if (it == refused.end()) say(fmt("relay hub: FULL: %s refused (max %d clients)", ak.c_str(), max));
                refused[ak] = now;
                V o = V::Object();
                o.set("max", V::Int(max));
                if (send(proto::pack(proto::T_FULL, o), from)) ++full_tx;
            }

            void rx(std::string_view data, const sockaddr_in& from, double now, Stats& st, uint64_t& tot_recv)
            {
                std::string ak = addr_key(from);
                bool s2 = data.size() >= 4 && data[0] == 'S' && data[1] == '2' && (uint8_t)data[2] == proto::PROTO;
                uint8_t typ = s2 ? (uint8_t)data[3] : 0;
                auto it = by_addr.find(ak);
                int i = it == by_addr.end() ? -1 : it->second;
                if (s2 && typ == proto::T_BYE)
                {
                    if (i < 0) return;
                    uint8_t bt = 0;
                    V bo;
                    std::string berr, bsid;
                    if (proto::unpack(data, bt, bo, berr) == proto::Unpack::ok && bo.t == V::object)
                        if (const V* v = bo.get("sid")) bsid = js::py_str(*v);
                    ++byes_rx;
                    view_bye(i, bsid);
                    drop(i, "left (BYE: the client game is closing)");
                    return;
                }
                if (!s2)
                {
                    uint8_t t = 0;
                    V o;
                    std::string err;
                    proto::unpack(data, t, o, err);
                    st.badpeer++;
                    warn("peer", err);
                    return;
                }
                if (i < 0)
                {
                    if (typ == proto::T_HDR)
                    {
                        uint8_t t2 = 0;
                        V obj;
                        std::string err;
                        if (proto::unpack(data, t2, obj, err) != proto::Unpack::ok || obj.t != V::object)
                        {
                            st.badpeer++;
                            warn("peer", "corrupt datagram: " + err);
                            return;
                        }
                        std::string sid = js::py_str(proto::get_or_null(obj, "sid"));
                        for (int j = 0; j < max; ++j)
                        {
                            Conn& o = c[(size_t)j];
                            if (!o.used || o.sid.empty() || o.sid != sid) continue;
                            say(fmt("relay hub: pid %d moved %s -> %s (same sid %s)", o.pid, o.akey.c_str(), ak.c_str(), sid.c_str()));
                            by_addr.erase(o.akey);
                            o.addr = from;
                            o.akey = ak;
                            by_addr[ak] = j;
                            refused.erase(ak);
                            roster_dirty = true;
                            i = j;
                            break;
                        }
                        if (i < 0)
                        {
                            int j = free_slot(sid);
                            if (j < 0)
                            {
                                refuse(from, ak, now);
                                return;
                            }
                            admit(j, from, ak, sid, now);
                            i = j;
                        }
                    }
                    else if (typ == proto::T_SXW)
                    {
                        int j = free_slot("");
                        if (j < 0)
                        {
                            refuse(from, ak, now);
                            return;
                        }
                        admit(j, from, ak, "", now);
                        i = j;
                    }
                    else return;
                }
                Conn& k = c[(size_t)i];
                k.last_rx = now;
                ++k.rx;
                if (!k.up)
                {
                    k.up = true;
                    roster_dirty = true;
                    say(fmt("relay hub: pid %d back after %.0f s silent", k.pid, now - k.silent_at));
                }
                if (typ == proto::T_SXW)
                {
                    try
                    {
                        want(k, data, now);
                    }
                    catch (const std::exception& ex)
                    {
                        warn("sx", std::string("save transfer: ") + ex.what());
                    }
                    return;
                }
                if (typ == proto::T_SXI || typ == proto::T_SXC || typ == proto::T_PEER || typ == proto::T_ROSTER || typ == proto::T_FULL) return;
                std::string msg;
                uint64_t h0 = k.asmb.hdr_n;
                auto e = k.asmb.feed(data, now, msg);
                if (e == proto::Assembler::Err::none)
                {
                    st.recv++;
                    tot_recv++;
                    k.dirty = true;
                    if (k.asmb.hdr_n != h0)
                        if (const V* h = k.asmb.hdr())
                        {
                            std::string sid = js::py_str(proto::get_or_null(*h, "sid"));
                            if (sid != k.sid)
                            {
                                if (!k.sid.empty()) say(fmt("relay hub: pid %d new session sid=%s (was %s)", k.pid, sid.c_str(), k.sid.c_str()));
                                k.sid = sid;
                                roster_dirty = true;
                            }
                        }
                }
                else if (e == proto::Assembler::Err::proto)
                {
                    st.badpeer++;
                    warn("peer", msg);
                }
                else
                {
                    st.badpeer++;
                    warn("peer", "corrupt datagram: " + msg);
                }
            }

            void want(Conn& k, std::string_view data, double now)
            {
                uint8_t t2 = 0;
                V obj;
                std::string err;
                if (proto::unpack(data, t2, obj, err) != proto::Unpack::ok) throw std::runtime_error(err);
                if (obj.t != V::object) throw std::runtime_error("AttributeError('not an object')");
                const V* idv = obj.get("id");
                std::string wid = idv ? js::py_str(*idv) : std::string();
                if (!ascii_digits(wid)) return;
                k.want_id = wid;
                k.want_at = now;
                pkg_scan(now);
                sockaddr_in to = k.addr;
                auto snd = [&](const std::string& d) { send(d, to); };
                auto it = pkgs.find(wid);
                if (it != pkgs.end())
                {
                    it->second.used = now;
                    proto::SaveXfer::serve(wid, it->second.b, it->second.crc, obj, snd, sxs);
                    return;
                }
                if (std::find(q.begin(), q.end(), wid) == q.end())
                {
                    q.push_back(wid);
                    sxs.req++;
                    say(fmt("relay sx: pid %d asked for the host save (request %s); join queue %d", k.pid, wid.c_str(), (int)q.size()));
                }
                pkg_next(now);
                V o = V::Object();
                o.set("id", V::Str(wid));
                o.set("st", V::Str("wait"));
                snd(proto::pack(proto::T_SXI, o));
            }

            void pkg_scan(double now)
            {
                int64_t mt = 0;
                uint64_t sz = 0;
                if (!fs::stat(p(L"sx_out.pkg"), mt, sz))
                {
                    pkg_has = false;
                    return;
                }
                if (pkg_has && mt == pkg_mtime && sz == pkg_size) return;
                std::string b;
                if (!fs::read(p(L"sx_out.pkg"), b) || b.size() != sz) return;
                pkg_has = true;
                pkg_mtime = mt;
                pkg_size = sz;
                std::string id;
                if (!proto::pkg_id_of(b, id)) return;
                Pkg& g = pkgs[id];
                g.crc = z::crc32(b);
                g.b = std::move(b);
                g.used = now;
                auto qi = std::find(q.begin(), q.end(), id);
                if (qi != q.end()) q.erase(qi);
                int asking = 0;
                for (auto& k : c)
                    if (k.used && k.want_id == id) ++asking;
                say(fmt("relay sx: host package %s ready (%llu B) for %d asking client(s)", id.c_str(), (unsigned long long)g.b.size(), asking));
                while ((int)pkgs.size() > max + 1)
                {
                    auto old = pkgs.begin();
                    for (auto j = pkgs.begin(); j != pkgs.end(); ++j)
                        if (j->second.used < old->second.used) old = j;
                    pkgs.erase(old);
                }
                pkg_next(now);
            }

            void pkg_next(double now)
            {
                for (auto j = q.begin(); j != q.end();)
                {
                    bool live = false;
                    for (auto& k : c)
                        if (k.used && k.want_id == *j && now - k.want_at < 10) live = true;
                    if (live)
                    {
                        ++j;
                        continue;
                    }
                    say(fmt("relay sx: request %s dropped from the join queue (no client asks for it now)", j->c_str()));
                    j = q.erase(j);
                }
                if (q.empty())
                {
                    std::string cur;
                    bool bad = false;
                    if (read_text_strip(p(L"sx_req.txt"), cur, bad) && DeleteFileW(p(L"sx_req.txt").c_str()))
                        say(fmt("relay sx: join queue empty, request %s removed from sx_req.txt", bad ? "?" : cur.c_str()));
                    return;
                }
                std::string head = q.front();
                std::string cur;
                bool bad = false;
                if (!read_text_strip(p(L"sx_req.txt"), cur, bad)) cur.clear();
                if (bad) throw std::runtime_error("UnicodeDecodeError('utf-8')");
                if (cur == head) return;
                if (!fs::write(p(L"sx_req.txt.tmp"), fs::text_mode(head + "\n"))) throw std::runtime_error("OSError('sx_req.txt.tmp')");
                if (!fs::replace(p(L"sx_req.txt.tmp"), p(L"sx_req.txt"))) throw std::runtime_error("PermissionError('sx_req.txt')");
                say(fmt("relay sx: join queue head %s -> sx_req.txt (%d waiting)", head.c_str(), (int)q.size()));
            }

            proto::Sender::Err out(proto::Sender& snd, const std::string& txt, double now, Stats& st, uint64_t& tot_sent, std::string& msg)
            {
                std::vector<const proto::AppCache*> hv((size_t)max, nullptr);
                for (int i = 0; i < max; ++i)
                    if (c[(size_t)i].used) hv[(size_t)i] = &c[(size_t)i].asmb.apps;
                std::vector<std::string> common, hdrs;
                auto e = snd.split_n(txt, now, hv, common, hdrs, msg);
                if (e != proto::Sender::Err::none) return e;
                size_t h = txt.find("#H:");
                if (h != std::string::npos)
                {
                    size_t he = txt.find_first_of("#\"", h + 3);
                    std::string hp = txt.substr(h + 3, he == std::string::npos ? std::string::npos : he - h - 3);
                    size_t lc = hp.rfind(',');
                    if (lc != std::string::npos) nonce = hp.substr(lc + 1);
                }
                for (int i = 0; i < max; ++i)
                {
                    Conn& k = c[(size_t)i];
                    if (!k.used) continue;
                    auto tx = [&](const std::string& dg)
                    {
                        if (send(dg, k.addr))
                        {
                            st.sent++;
                            tot_sent++;
                            st.bytes += dg.size();
                            st.maxlen = std::max<uint64_t>(st.maxlen, dg.size());
                            if (dg.size() > proto::MTU_SAFE) st.big++;
                        }
                        else st.senderr++;
                    };
                    tx(hdrs[(size_t)i]);
                    for (auto& d : common) tx(d);
                }
                return e;
            }

            void fan_out(double now)
            {
                static const char* const K[] = {"f", "t", "x", "y", "z", "yaw", "vx", "vy"};
                static const int64_t D[] = {0, -1, 0, 0, 0, 0, 0, 0};
                for (auto& k : c)
                {
                    if (!k.used || !k.up) continue;
                    const V* h = k.asmb.hdr();
                    if (!h || k.asmb.hdr_n == k.peer_n || now - k.peer_at < cfg.peer_gap) continue;
                    V po = V::Object();
                    po.set("pid", V::Int(k.pid));
                    po.set("sid", proto::get_or_null(*h, "sid"));
                    for (size_t f = 0; f < 8; ++f) po.set(K[f], num_or(*h, K[f], D[f]));
                    const V* x1 = h->get("x1");
                    po.set("x1", V::Str(x1_relay(x1 && x1->t == V::string ? std::string_view(x1->s) : std::string_view(), cfg.relay_tags)));
                    std::string dg = proto::pack(proto::T_PEER, po);
                    for (auto& j : c)
                        if (&j != &k && j.used && j.up && send(dg, j.addr)) ++peer_tx;
                    k.peer_n = k.asmb.hdr_n;
                    k.peer_at = now;
                }
            }

            void forward_apps(double now)
            {
                for (auto& j : c)
                {
                    if (!j.used || !j.up) continue;
                    const V* h = j.asmb.hdr();
                    const V* nd = h ? h->get("need") : nullptr;
                    if (nd && nd->t == V::string && !nd->s.empty())
                    {
                        const std::string& ns = nd->s;
                        size_t s = 0;
                        int sent = 0;
                        while (s <= ns.size() && sent < 16)
                        {
                            size_t e = ns.find(',', s);
                            if (e == std::string::npos) e = ns.size();
                            std::string hh = ns.substr(s, e - s);
                            s = e + 1;
                            if (hh.empty()) continue;
                            auto fi = j.fwd.find(hh);
                            if (fi != j.fwd.end() && now - fi->second < proto::APP_MIN_GAP) continue;
                            for (auto& k : c)
                            {
                                if (&k == &j || !k.used) continue;
                                auto a = k.asmb.apps.map.find("s" + hh);
                                if (a == k.asmb.apps.map.end()) continue;
                                if (send(proto::pack(proto::T_APP, a->second), j.addr)) ++app_fwd;
                                j.fwd[hh] = now;
                                ++sent;
                                break;
                            }
                        }
                    }
                    for (auto it = j.fwd.begin(); it != j.fwd.end();)
                    {
                        if (now - it->second > 30) it = j.fwd.erase(it);
                        else ++it;
                    }
                }
            }

            void roster(double now)
            {
                V peers = V::Arr();
                Roster r;
                r.hub = true;
                r.you = 0;
                r.max = max;
                r.world = world;
                for (auto& k : c)
                {
                    if (!k.used) continue;
                    const char* st = k.up ? "up" : "silent";
                    V e = V::Object();
                    e.set("pid", V::Int(k.pid));
                    e.set("sid", V::Str(k.sid));
                    e.set("st", V::Str(st));
                    peers.a.push_back(std::move(e));
                    r.peers.push_back({k.pid, k.sid, st, k.akey});
                }
                for (auto& k : c)
                {
                    if (!k.used) continue;
                    V o = V::Object();
                    o.set("you", V::Int(k.pid));
                    o.set("max", V::Int(max));
                    o.set("host", V::Str(nonce));
                    o.set("peers", peers);
                    if (!world.empty()) o.set("w", V::Str(world));
                    if (send(proto::pack(proto::T_ROSTER, o), k.addr)) ++roster_tx;
                }
                if (roster_dirty)
                {
                    r.seq = ++roster_seq;
                    publish(r);
                }
                roster_dirty = false;
                roster_at = now;
            }

            void tick(double now)
            {
                for (int i = 0; i < max; ++i)
                {
                    Conn& k = c[(size_t)i];
                    if (!k.used) continue;
                    double age = now - k.last_rx;
                    if (k.up && age >= cfg.slot_silent)
                    {
                        k.up = false;
                        k.silent_at = now;
                        roster_dirty = true;
                        say(fmt("relay hub: pid %d silent: no datagram for %.0f s", k.pid, age));
                    }
                    if (!k.up && cfg.slot_left > 0 && age >= cfg.slot_left) drop(i, fmt("freed: no datagram for %.0f s", age));
                }
                for (auto it = refused.begin(); it != refused.end();)
                {
                    if (now - it->second > 60) it = refused.erase(it);
                    else ++it;
                }
                if (!q.empty() && now - pkg_at >= 1.0)
                {
                    pkg_at = now;
                    try
                    {
                        pkg_scan(now);
                        pkg_next(now);
                    }
                    catch (const std::exception& ex)
                    {
                        warn("sx", std::string("save transfer: ") + ex.what());
                    }
                }
                fan_out(now);
                forward_apps(now);
                if (roster_dirty || now - roster_at >= 1.0) roster(now);
            }

            void render(const std::set<std::string>& need, double now, double dx, const std::function<void(int, std::string)>& put)
            {
                for (auto& k : c)
                {
                    if (!k.used || !k.dirty) continue;
                    k.dirty = false;
                    std::string rin;
                    if (k.asmb.render(need, now, dx, rin, k.pid)) put(k.pid - 1, std::move(rin));
                }
            }

            void all_dirty()
            {
                for (auto& k : c)
                    if (k.used) k.dirty = true;
            }

            std::string line() const
            {
                std::string o;
                for (auto& k : c)
                {
                    if (!k.used) continue;
                    if (!o.empty()) o += " ";
                    o += fmt("%d:%s:%s:rx%llu", k.pid, k.up ? "up" : "silent", k.akey.c_str(), (unsigned long long)k.rx);
                }
                return o.empty() ? std::string("-") : o;
            }
        };

        struct CView
        {
            int pid = -1;
            std::string sid;
            V f;
            bool has_f = false;
        };

        struct CRx
        {
            std::function<void(const std::string&)> say;
            std::function<void(const char*, const std::string&)> warn;
            std::function<void(int, const std::string&)> view_bye;
            std::function<void(const Roster&)> publish;
            std::function<Roster()> roster;
            std::function<void(int, std::string, double)> put_view;
            int you = -1, max = 1;
            bool full = false;
            double full_at = -1e9;
            CView cv[proto::MAXV];
            uint64_t peer_rx = 0, roster_rx = 0, full_rx = 0;
            void drop(int v, const char* why)
            {
                if (cv[v].pid < 0) return;
                say(fmt("relay: player pid %d (view %d, sid=%s) %s", cv[v].pid, v, cv[v].sid.c_str(), why));
                view_bye(v, cv[v].sid);
                cv[v] = CView{};
            }
            void rx(std::string_view data, uint8_t typ, double now, Stats& st)
            {
                uint8_t t2 = 0;
                js::V obj;
                std::string err;
                if (proto::unpack(data, t2, obj, err) != proto::Unpack::ok || obj.t != js::V::object)
                {
                    st.badpeer++;
                    warn("peer", "corrupt datagram: " + err);
                    return;
                }
                if (typ == proto::T_FULL)
                {
                    ++full_rx;
                    int64_t mx = 0;
                    if (const js::V* m = obj.get("max")) py_int(*m, mx);
                    if (!full || now - full_at >= 30)
                    {
                        say(fmt("relay: the host is FULL (max %lld clients): this game was not admitted", (long long)mx));
                        full_at = now;
                    }
                    if (!full)
                    {
                        full = true;
                        Roster r = roster();
                        r.full = true;
                        r.seq++;
                        publish(r);
                    }
                    return;
                }
                if (typ == proto::T_ROSTER)
                {
                    ++roster_rx;
                    int64_t assigned_pid = -1, mx = 1;
                    if (const js::V* v = obj.get("you")) py_int(*v, assigned_pid);
                    if (const js::V* v = obj.get("max")) py_int(*v, mx);
                    if (mx < 1) mx = 1;
                    if (mx > proto::MAXV) mx = proto::MAXV;
                    if (assigned_pid < 1 || assigned_pid > mx)
                    {
                        st.badpeer++;
                        warn("roster", "invalid local player slot");
                        return;
                    }
                    Roster r;
                    r.hub = true;
                    r.you = (int)assigned_pid;
                    r.max = (int)mx;
                    if (const js::V* ps = obj.get("peers"))
                        if (ps->t == js::V::array)
                            for (auto& e : ps->a)
                            {
                                if (e.t != js::V::object) continue;
                                RosterEnt re;
                                int64_t pid = 0;
                                if (const js::V* v = e.get("pid")) py_int(*v, pid);
                                re.pid = (int)pid;
                                if (const js::V* v = e.get("sid")) re.sid = js::py_str(*v);
                                if (const js::V* v = e.get("st")) re.st = js::py_str(*v);
                                r.peers.push_back(std::move(re));
                            }
                    if (const js::V* v = obj.get("w")) r.world = js::py_str(*v);
                    bool you_changed = r.you != you;
                    for (int v = 1; v < proto::MAXV; ++v)
                    {
                        if (cv[v].pid < 0) continue;
                        bool in = false;
                        for (auto& e : r.peers)
                            if (e.pid == cv[v].pid && e.sid == cv[v].sid) in = true;
                        if (you_changed) drop(v, "dropped: this game got a new slot on the host");
                        else if (!in) drop(v, "left the host roster");
                    }
                    if (you_changed) say(fmt("relay: the host admitted this game as pid %d of %d", r.you, r.max));
                    you = r.you;
                    max = r.max;
                    Roster cur = roster();
                    bool same = cur.hub && !cur.full && cur.you == r.you && cur.max == r.max && cur.world == r.world && cur.peers.size() == r.peers.size();
                    for (size_t i = 0; same && i < r.peers.size(); ++i)
                        same = cur.peers[i].pid == r.peers[i].pid && cur.peers[i].sid == r.peers[i].sid && cur.peers[i].st == r.peers[i].st;
                    full = false;
                    if (!same)
                    {
                        r.seq = cur.seq + 1;
                        publish(r);
                    }
                    return;
                }
                ++peer_rx;
                if (you < 1) return;
                int64_t pid = -1;
                if (const js::V* v = obj.get("pid")) py_int(*v, pid);
                if (pid < 1 || pid == you) return;
                int v = pid < you ? (int)pid : (int)pid - 1;
                if (v < 1 || v >= proto::MAXV || v >= max) return;
                std::string sid = js::py_str(proto::get_or_null(obj, "sid"));
                CView& w = cv[v];
                const js::V& f = proto::get_or_zero(obj, "f");
                if (w.pid == pid && w.sid == sid && w.has_f && js::py_lt(f, w.f))
                {
                    st.stale++;
                    return;
                }
                if (w.pid != pid || w.sid != sid)
                {
                    if (w.pid >= 0) drop(v, "replaced");
                    say(fmt("relay: player pid %d (sid=%s) now in view %d", (int)pid, sid.c_str(), v));
                }
                w.pid = (int)pid;
                w.sid = sid;
                w.f = f;
                w.has_f = true;
                js::V m = js::V::Object();
                m.set("v", js::V::Int(proto::PROTO));
                m.set("sid", js::V::Str(sid));
                static const char* const K[] = {"f", "t", "x", "y", "z", "vx", "vy", "yaw"};
                static const int64_t D[] = {0, -1, 0, 0, 0, 0, 0, 0};
                for (size_t k = 0; k < 8; ++k) m.set(K[k], num_or(obj, K[k], D[k]));
                m.set("gt", js::V::Real(-1.0));
                m.set("wt", js::V::Real(-1.0));
                m.set("wx", js::V::Int(-1));
                m.set("avmesh", js::V::Str(""));
                m.set("avparts", js::V::Str(""));
                m.set("x1", proto::get_or_null(obj, "x1"));
                m.set("shots", js::V::Str(""));
                m.set("need", js::V::Str(""));
                m.set("pid", js::V::Int(pid));
                m.set("apps", js::V::Arr());
                m.set("npcs", js::V::Arr());
                put_view(v, js::dumps(m, false), now);
            }
        };
    }

    struct Net::Impl
    {
        Config cfg;
        LogFn log;
        std::thread th;
        std::atomic<bool> stop{false};
        std::atomic<bool> alive{false};
        std::atomic<bool> hubm{false};
        HANDLE wake = nullptr;
        std::mutex mo;
        std::string out_text;
        bool out_new = false;
        std::string world_out;
        std::mutex mi;
        std::string in_text;
        uint64_t in_seq = 0;
        std::string vin_text[proto::MAXV];
        uint64_t vin_seq[proto::MAXV] = {};
        std::atomic<bool> bye_req{false};
        std::atomic<bool> bye_done{false};
        uint64_t bye_n = 0;
        std::string bye_sid;
        uint64_t vbye_n[proto::MAXV] = {};
        std::string vbye_sid[proto::MAXV];
        Roster ros;
        mutable std::mutex ms;
        Stats pub;
        void run();
    };

    Net::Net() : p_(std::make_unique<Impl>()) { p_->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr); }

    Net::~Net()
    {
        stop();
        if (p_->wake) CloseHandle(p_->wake);
    }

    bool Net::start(const Config& c, LogFn log)
    {
        stop();
        if (c.listen <= 0 || c.listen > 65535 || c.peer_port <= 0 || c.peer_port > 65535 || c.peer_host.empty()) return false;
        p_->cfg = c;
        if (p_->cfg.loop_ms < 1) p_->cfg.loop_ms = 1;
        p_->log = std::move(log);
        p_->stop = false;
        p_->bye_req = false;
        p_->bye_done = false;
        p_->hubm = c.role == "host" && c.maxclients > 1;
        {
            std::lock_guard<std::mutex> g(p_->mo);
            p_->out_text.clear();
            p_->out_new = false;
            p_->world_out.clear();
        }
        {
            std::lock_guard<std::mutex> g(p_->ms);
            p_->pub = Stats{};
        }
        {
            std::lock_guard<std::mutex> g(p_->mi);
            p_->ros = Roster{};
            if (p_->hubm)
            {
                p_->ros.hub = true;
                p_->ros.you = 0;
                p_->ros.max = std::clamp(c.maxclients, 1, proto::MAXV);
            }
        }
        p_->alive = true;
        p_->th = std::thread([this] { p_->run(); });
        return true;
    }

    void Net::stop()
    {
        if (!p_->th.joinable()) return;
        p_->stop = true;
        SetEvent(p_->wake);
        p_->th.join();
        p_->alive = false;
    }

    bool Net::running() const { return p_->alive.load(); }

    bool Net::hub() const { return p_->hubm.load(); }

    void Net::put_out(std::string text)
    {
        {
            std::lock_guard<std::mutex> g(p_->mo);
            p_->out_text = std::move(text);
            p_->out_new = true;
        }
        SetEvent(p_->wake);
    }

    bool Net::take_in(std::string& text, uint64_t& seq)
    {
        std::lock_guard<std::mutex> g(p_->mi);
        if (p_->in_seq == seq) return false;
        text = p_->in_text;
        seq = p_->in_seq;
        return true;
    }

    bool Net::take_in(int view, std::string& text, uint64_t& seq)
    {
        if (view == 0) return take_in(text, seq);
        if (view < 0 || view >= proto::MAXV) return false;
        std::lock_guard<std::mutex> g(p_->mi);
        if (p_->vin_seq[view] == seq) return false;
        text = p_->vin_text[view];
        seq = p_->vin_seq[view];
        return true;
    }

    bool Net::bye(int wait_ms)
    {
        if (!p_->alive.load()) return false;
        p_->bye_done = false;
        p_->bye_req = true;
        SetEvent(p_->wake);
        for (int i = 0; i < wait_ms && !p_->bye_done.load(); ++i) Sleep(1);
        return p_->bye_done.load();
    }

    bool Net::take_bye(uint64_t& seen, std::string& sid)
    {
        std::lock_guard<std::mutex> g(p_->mi);
        if (p_->bye_n == seen) return false;
        seen = p_->bye_n;
        sid = p_->bye_sid;
        return true;
    }

    bool Net::take_bye(int view, uint64_t& seen, std::string& sid)
    {
        if (view == 0) return take_bye(seen, sid);
        if (view < 0 || view >= proto::MAXV) return false;
        std::lock_guard<std::mutex> g(p_->mi);
        if (p_->vbye_n[view] == seen) return false;
        seen = p_->vbye_n[view];
        sid = p_->vbye_sid[view];
        return true;
    }

    void Net::put_world(std::string w)
    {
        std::lock_guard<std::mutex> g(p_->mo);
        p_->world_out = std::move(w);
    }

    Roster Net::roster() const
    {
        std::lock_guard<std::mutex> g(p_->mi);
        return p_->ros;
    }

    Stats Net::stats() const
    {
        std::lock_guard<std::mutex> g(p_->ms);
        return p_->pub;
    }

    const Config& Net::config() const { return p_->cfg; }

    void Net::Impl::run()
    {
        auto say = [&](const std::string& s)
        {
            if (log) log(s);
        };
        std::unordered_map<std::string, double> last_warn;
        auto warn = [&](const char* key, const std::string& msg)
        {
            double t = clock_s();
            auto it = last_warn.find(key);
            if (it != last_warn.end() && t - it->second < 5) return;
            last_warn[key] = t;
            say("RELAY ERROR: " + msg);
        };
        try
        {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            {
                say("RELAY ERROR: WSAStartup failed");
                alive = false;
                return;
            }
            say(fmt("relay v%d listen=%d peer=%s:%d dir=%s", proto::PROTO, cfg.listen, cfg.peer_host.c_str(), cfg.peer_port, narrow(cfg.dir).c_str()));
            const bool hub_on = hubm.load();
            const bool client_on = cfg.role != "host";
            SOCKET s = INVALID_SOCKET;
            double next_bind = 0;
            int bind_fails = 0;
            sockaddr_in peer{};
            bool peer_ok = false;
            double next_resolve = 0;
            auto resolve = [&]
            {
                peer = sockaddr_in{};
                peer.sin_family = AF_INET;
                peer.sin_port = htons((u_short)cfg.peer_port);
                if (inet_pton(AF_INET, cfg.peer_host.c_str(), &peer.sin_addr) == 1) return true;
                addrinfo hints{}, *res = nullptr;
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_DGRAM;
                if (getaddrinfo(cfg.peer_host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
                peer.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
                freeaddrinfo(res);
                return true;
            };
            auto try_bind = [&]
            {
                SOCKET t = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (t == INVALID_SOCKET) return false;
                sockaddr_in a{};
                a.sin_family = AF_INET;
                a.sin_port = htons((u_short)cfg.listen);
                bool loop = cfg.local_only || (!hub_on && cfg.peer_host.rfind("127.", 0) == 0);
                a.sin_addr.s_addr = loop ? htonl(INADDR_LOOPBACK) : htonl(INADDR_ANY);
                if (bind(t, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0)
                {
                    int e = WSAGetLastError();
                    closesocket(t);
                    ++bind_fails;
                    warn("bind", fmt("bind UDP %s:%d failed (WSA %d): is another relay (relay_v2.py) using the port?", loop ? "127.0.0.1" : "0.0.0.0", cfg.listen, e));
                    return false;
                }
                u_long nb = 1;
                ioctlsocket(t, FIONBIO, &nb);
                BOOL off = FALSE;
                DWORD ret = 0;
                WSAIoctl(t, SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &ret, nullptr, nullptr);
                s = t;
                if (bind_fails) say(fmt("relay: UDP %s:%d bound after %d failed tries", loop ? "127.0.0.1" : "0.0.0.0", cfg.listen, bind_fails));
                return true;
            };
            proto::Sender snd;
            proto::Assembler asmb;
            proto::SaveXfer sx(cfg.dir, say);
            std::mt19937_64 rng(std::random_device{}());
            std::uniform_real_distribution<double> uni(0.0, 1.0);
            Stats st;
            uint64_t tot_sent = 0, tot_recv = 0, tot_written = 0, byes_rx = 0, byes_tx = 0;
            uint64_t disc_rx = 0, disc_tx = 0;
            double last_rx = -1, last_in = -1, peer_seen = -1;
            std::string last;
            bool has_last = false;
            std::set<std::string> last_need;
            bool has_last_need = false;
            double last_stat = clock_s();
            auto sx_send = [&](const std::string& dg)
            {
                if (s == INVALID_SOCKET || !peer_ok)
                {
                    warn("sxsend", "sx sendto failed: no socket or peer address");
                    return;
                }
                if (sendto(s, dg.data(), (int)dg.size(), 0, reinterpret_cast<sockaddr*>(&peer), sizeof(peer)) == SOCKET_ERROR)
                    warn("sxsend", fmt("sx sendto failed: WSA %d", WSAGetLastError()));
            };
            std::vector<std::string> dgs;
            std::vector<char> buf(65536);
            HANDLE tmr = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
            if (!tmr) tmr = CreateWaitableTimerW(nullptr, FALSE, nullptr);
            std::wstring bd = cfg.dir;
            if (!bd.empty() && bd.back() != L'\\' && bd.back() != L'/') bd.push_back(L'\\');
            const std::wstring out_path = bd + L"out.json", in_path = bd + L"in.json", in_tmp = bd + L"in.json.tmp";
            auto put_file = [&](const std::wstring& path, const std::string& text)
            {
                std::wstring tmp = path + L".tmp";
                if (!fs::write(tmp, text)) return false;
                for (int k = 0; k < 5; ++k)
                {
                    if (fs::replace(tmp, path)) return true;
                    Sleep(10);
                }
                return false;
            };
            auto put_view = [&](int v, std::string rin, double now)
            {
                bool wrote = true;
                if (cfg.files) wrote = put_file(v == 0 ? in_path : bd + L"in_v" + std::to_wstring(v) + L".json", rin);
                {
                    std::lock_guard<std::mutex> g(mi);
                    if (v == 0)
                    {
                        in_text = std::move(rin);
                        in_seq++;
                    }
                    else
                    {
                        vin_text[v] = std::move(rin);
                        vin_seq[v]++;
                    }
                }
                if (wrote)
                {
                    st.written++;
                    tot_written++;
                }
                last_in = now;
            };
            auto view_bye = [&](int v, const std::string& bsid)
            {
                std::lock_guard<std::mutex> g(mi);
                if (v == 0)
                {
                    ++bye_n;
                    bye_sid = bsid;
                }
                else if (v > 0 && v < proto::MAXV)
                {
                    ++vbye_n[v];
                    vbye_sid[v] = bsid;
                }
            };
            auto publish = [&](const Roster& r)
            {
                {
                    std::lock_guard<std::mutex> g(mi);
                    ros = r;
                }
                if (cfg.files) put_file(bd + L"roster.json", roster_json(r));
            };
            std::unique_ptr<Hub> hub;
            if (hub_on)
            {
                hub = std::make_unique<Hub>(cfg);
                hub->send = [&](const std::string& dg, const sockaddr_in& to)
                {
                    if (s == INVALID_SOCKET) return false;
                    if (sendto(s, dg.data(), (int)dg.size(), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) != SOCKET_ERROR) return true;
                    warn("send", fmt("sendto %s failed (%d bytes): WSA %d", addr_key(to).c_str(), (int)dg.size(), WSAGetLastError()));
                    return false;
                };
                hub->say = say;
                hub->warn = warn;
                hub->view_bye = view_bye;
                hub->publish = publish;
                std::string tl;
                for (auto& t : cfg.relay_tags) tl += (tl.empty() ? "" : ",") + t;
                say(fmt("relay hub: host for up to %d clients; relay tags %s; slot silent %.0f s, freed %.0f s", hub->max, tl.empty() ? "(none)" : tl.c_str(), cfg.slot_silent,
                        cfg.slot_left));
                publish(Roster{true, false, 0, hub->max, 0, {}});
            }
            CRx crx;
            crx.say = say;
            crx.warn = warn;
            crx.view_bye = view_bye;
            crx.publish = publish;
            crx.roster = [&]
            {
                std::lock_guard<std::mutex> g(mi);
                return ros;
            };
            crx.put_view = put_view;
            while (!stop)
            {
                double now = clock_s();
                bool dirty = false;
                if (s == INVALID_SOCKET && now >= next_bind)
                {
                    next_bind = now + 5;
                    try_bind();
                }
                if (!peer_ok && now >= next_resolve)
                {
                    next_resolve = now + 5;
                    peer_ok = resolve();
                    if (!peer_ok) warn("resolve", fmt("peer address %s does not resolve", cfg.peer_host.c_str()));
                }
                std::string txt;
                bool got = false;
                if (cfg.files) got = fs::read(out_path, txt);
                else
                {
                    std::lock_guard<std::mutex> g(mo);
                    if (out_new)
                    {
                        txt.swap(out_text);
                        out_new = false;
                        got = true;
                    }
                }
                if (got)
                {
                    bool utf_ok = js::utf8_valid(txt);
                    if (utf_ok) txt = py_strip(fs::universal_newlines(txt));
                    if (!utf_ok)
                    {
                        st.badlocal++;
                        warn("local", "local snapshot is not valid UTF-8");
                    }
                    else if (!txt.empty() && (!has_last || txt != last))
                    {
                        last = txt;
                        has_last = true;
                        std::string msg;
                        auto e = hub ? hub->out(snd, txt, now, st, tot_sent, msg) : snd.split(txt, now, asmb.apps, dgs, msg);
                        if (e == proto::Sender::Err::none)
                        {
                            if (!hub)
                                for (auto& dg : dgs)
                                {
                                    bool ok = s != INVALID_SOCKET && peer_ok &&
                                              sendto(s, dg.data(), (int)dg.size(), 0, reinterpret_cast<sockaddr*>(&peer), sizeof(peer)) != SOCKET_ERROR;
                                    if (ok)
                                    {
                                        st.sent++;
                                        tot_sent++;
                                        st.bytes += dg.size();
                                        st.maxlen = std::max<uint64_t>(st.maxlen, dg.size());
                                        if (dg.size() > proto::MTU_SAFE) st.big++;
                                    }
                                    else
                                    {
                                        st.senderr++;
                                        int we = s == INVALID_SOCKET ? -1 : !peer_ok ? -2 : WSAGetLastError();
                                        warn("send", fmt("sendto failed (%d bytes): %s", (int)dg.size(),
                                                         we == -1 ? "socket not bound" : we == -2 ? "peer address unresolved" : fmt("WSA %d", we).c_str()));
                                    }
                                }
                            if (!cfg.record.empty()) fs::append(cfg.record, fs::text_mode(txt + "\n"));
                        }
                        else if (e == proto::Sender::Err::json) st.badlocal++;
                        else
                        {
                            st.badlocal++;
                            warn("local", msg);
                        }
                        if (!has_last_need || snd.local_need() != last_need)
                        {
                            last_need = snd.local_need();
                            has_last_need = true;
                            dirty = true;
                            if (hub) hub->all_dirty();
                        }
                    }
                }
                if (s != INVALID_SOCKET)
                {
                    const double receive_started = clock_s();
                    for (size_t received = 0; received < 256 && clock_s() - receive_started < 0.004; ++received)
                    {
                        sockaddr_in from{};
                        int fl = sizeof(from);
                        int r = recvfrom(s, buf.data(), (int)buf.size(), 0, reinterpret_cast<sockaddr*>(&from), &fl);
                        if (r == SOCKET_ERROR)
                        {
                            int e = WSAGetLastError();
                            if (e == WSAEWOULDBLOCK || e == WSAECONNRESET) break;
                            if (e == WSAEMSGSIZE)
                            {
                                st.badpeer++;
                                warn("peer", "datagram larger than 65535 bytes");
                                continue;
                            }
                            warn("recv", fmt("recvfrom failed: WSA %d", e));
                            break;
                        }
                        std::string_view data(buf.data(), (size_t)r);
                        if (cfg.loss > 0 && uni(rng) < cfg.loss)
                        {
                            st.lost++;
                            continue;
                        }
                        if (data.size() >= 4 && data[0] == 'S' && data[1] == '2' && (uint8_t)data[2] == proto::PROTO &&
                            ((uint8_t)data[3] == proto::T_DISC || (uint8_t)data[3] == proto::T_DISCR))
                        {
                            int64_t nonce = 0;
                            if (!client_on && disc::is_probe(data, nonce))
                            {
                                ++disc_rx;
                                if (hub || (peer_ok && from.sin_addr.s_addr == peer.sin_addr.s_addr))
                                {
                                    disc::Info di;
                                    di.name = cfg.disc_name.empty() ? std::string("host") : cfg.disc_name;
                                    di.id = snd.sid().empty() ? di.name + ":" + std::to_string(cfg.listen) : snd.sid();
                                    di.port = cfg.listen;
                                    di.hub = hub != nullptr;
                                    di.max = hub ? hub->max + 1 : 2;
                                    di.n = 1 + (hub ? hub->count() : (peer_seen >= 0 && now - peer_seen < 5 ? 1 : 0));
                                    std::string dg = disc::reply(di, nonce);
                                    if (sendto(s, dg.data(), (int)dg.size(), 0, reinterpret_cast<const sockaddr*>(&from), sizeof(from)) != SOCKET_ERROR) ++disc_tx;
                                }
                            }
                            continue;
                        }
                        if (!hub && (!peer_ok || from.sin_addr.s_addr != peer.sin_addr.s_addr || from.sin_port != peer.sin_port))
                        { st.badpeer++; continue; }
                        last_rx = now;
                        if (hub)
                        {
                            hub->rx(data, from, now, st, tot_recv);
                            continue;
                        }
                        if (data.size() >= 4 && data[0] == 'S' && data[1] == '2' && (uint8_t)data[2] == proto::PROTO && (uint8_t)data[3] == proto::T_BYE)
                        {
                            uint8_t bt = 0;
                            js::V bo;
                            std::string berr;
                            std::string bsid;
                            if (proto::unpack(data, bt, bo, berr) == proto::Unpack::ok && bo.t == js::V::object)
                                if (const js::V* v = bo.get("sid")) bsid = js::py_str(*v);
                            bool dup = false;
                            {
                                std::lock_guard<std::mutex> g(mi);
                                dup = bye_n > 0 && bye_sid == bsid;
                                if (!dup) { ++bye_n; bye_sid = bsid; }
                            }
                            ++byes_rx;
                            if (!dup) say("relay: BYE received from the peer (sid=" + bsid + "): the other game is closing");
                            continue;
                        }
                        if (data.size() >= 4 && data[0] == 'S' && data[1] == '2' && (uint8_t)data[2] == proto::PROTO &&
                            ((uint8_t)data[3] == proto::T_SXW || (uint8_t)data[3] == proto::T_SXI || (uint8_t)data[3] == proto::T_SXC))
                        {
                            try
                            {
                                sx.feed(data, sx_send);
                            }
                            catch (const std::exception& ex)
                            {
                                warn("sx", std::string("save transfer: ") + ex.what());
                            }
                            continue;
                        }
                        if (client_on && data.size() >= 4 && data[0] == 'S' && data[1] == '2' && (uint8_t)data[2] == proto::PROTO &&
                            ((uint8_t)data[3] == proto::T_PEER || (uint8_t)data[3] == proto::T_ROSTER || (uint8_t)data[3] == proto::T_FULL))
                        {
                            crx.rx(data, (uint8_t)data[3], now, st);
                            continue;
                        }
                        std::string msg;
                        auto e = asmb.feed(data, now, msg);
                        if (e == proto::Assembler::Err::none)
                        {
                            st.recv++;
                            tot_recv++;
                            dirty = true;
                            peer_seen = now;
                        }
                        else if (e == proto::Assembler::Err::proto)
                        {
                            st.badpeer++;
                            warn("peer", msg);
                        }
                        else
                        {
                            st.badpeer++;
                            warn("peer", "corrupt datagram: " + msg);
                        }
                    }
                }
                if (bye_req.load() && !bye_done.load())
                {
                    int ok = 0;
                    if (hub)
                    {
                        js::V bo = js::V::Object();
                        bo.set("sid", js::V::Str(snd.sid()));
                        std::string dg = proto::pack(proto::T_BYE, bo);
                        for (auto& k : hub->c)
                            if (k.used)
                                for (int r = 0; r < 3; ++r)
                                    if (hub->send(dg, k.addr)) ++ok;
                    }
                    else if (s != INVALID_SOCKET && peer_ok)
                    {
                        js::V bo = js::V::Object();
                        bo.set("sid", js::V::Str(snd.sid()));
                        std::string dg = proto::pack(proto::T_BYE, bo);
                        for (int k = 0; k < 3; ++k)
                            if (sendto(s, dg.data(), (int)dg.size(), 0, reinterpret_cast<sockaddr*>(&peer), sizeof(peer)) != SOCKET_ERROR) ++ok;
                    }
                    byes_tx += ok;
                    bye_req = false;
                    bye_done = true;
                }
                if (hub)
                {
                    {
                        std::lock_guard<std::mutex> g(mo);
                        if (world_out != hub->world)
                        {
                            hub->world = world_out;
                            hub->roster_dirty = true;
                        }
                    }
                    hub->tick(now);
                    hub->render(snd.local_need(), now, cfg.dx, [&](int v, std::string rin) { put_view(v, std::move(rin), now); });
                }
                else
                {
                    try
                    {
                        sx.tick(now, sx_send);
                    }
                    catch (const std::exception& ex)
                    {
                        warn("sx", std::string("save transfer: ") + ex.what());
                    }
                }
                if (dirty && !hub)
                {
                    std::string rin;
                    if (asmb.render(snd.local_need(), now, cfg.dx, rin))
                    {
                        bool wrote = true;
                        if (cfg.files)
                        {
                            wrote = false;
                            if (fs::write(in_tmp, rin))
                                for (int k = 0; k < 5 && !wrote; ++k)
                                {
                                    wrote = fs::replace(in_tmp, in_path);
                                    if (!wrote) Sleep(10);
                                }
                        }
                        {
                            std::lock_guard<std::mutex> g(mi);
                            in_text = std::move(rin);
                            in_seq++;
                        }
                        if (wrote)
                        {
                            st.written++;
                            tot_written++;
                        }
                        last_in = now;
                    }
                }
                if (now - last_stat >= 10)
                {
                    uint64_t stale = asmb.stale, apps_n = asmb.apps.size(), npcs_n = asmb.npc_count();
                    if (hub)
                    {
                        stale = apps_n = npcs_n = 0;
                        for (auto& k : hub->c)
                            if (k.used)
                            {
                                stale += k.asmb.stale;
                                apps_n += k.asmb.apps.size();
                                npcs_n += k.asmb.npc_count();
                            }
                    }
                    say(fmt("relay stats 10s: sent=%llu (%llu B, max %llu B, over %d B: %llu) recv=%llu dropped_test=%llu stale=%llu in.json=%llu send_err=%llu bad_local=%llu bad_peer=%llu apps_cached=%llu npcs=%llu",
                            (unsigned long long)st.sent, (unsigned long long)st.bytes, (unsigned long long)st.maxlen, (int)proto::MTU_SAFE, (unsigned long long)st.big,
                            (unsigned long long)st.recv, (unsigned long long)st.lost, (unsigned long long)stale, (unsigned long long)st.written,
                            (unsigned long long)st.senderr, (unsigned long long)st.badlocal, (unsigned long long)st.badpeer, (unsigned long long)apps_n,
                            (unsigned long long)npcs_n));
                    if (hub)
                    {
                        say(fmt("relay hub 10s: clients %d of %d [%s] peer_tx=%llu roster_tx=%llu full_tx=%llu app_fwd=%llu bye_rx=%llu", hub->count(), hub->max, hub->line().c_str(),
                                (unsigned long long)hub->peer_tx, (unsigned long long)hub->roster_tx, (unsigned long long)hub->full_tx, (unsigned long long)hub->app_fwd,
                                (unsigned long long)hub->byes_rx));
                        if (hub->sxs.any())
                        {
                            say(fmt("relay sx 10s: chunks sent=%llu recv=%llu done=%llu crc_bad=%llu requests=%llu", (unsigned long long)hub->sxs.sent,
                                    (unsigned long long)hub->sxs.recv, (unsigned long long)hub->sxs.done, (unsigned long long)hub->sxs.crcbad, (unsigned long long)hub->sxs.req));
                            hub->sxs = proto::SxStats{};
                        }
                    }
                    else if (crx.peer_rx || crx.roster_rx || crx.full_rx)
                        say(fmt("relay views 10s: you=%d max=%d peer_rx=%llu roster_rx=%llu full_rx=%llu", crx.you, crx.max, (unsigned long long)crx.peer_rx, (unsigned long long)crx.roster_rx,
                                (unsigned long long)crx.full_rx));
                    if (sx.stats.any())
                    {
                        say(fmt("relay sx 10s: chunks sent=%llu recv=%llu done=%llu crc_bad=%llu requests=%llu", (unsigned long long)sx.stats.sent,
                                (unsigned long long)sx.stats.recv, (unsigned long long)sx.stats.done, (unsigned long long)sx.stats.crcbad, (unsigned long long)sx.stats.req));
                        sx.stats = proto::SxStats{};
                    }
                    st = Stats{};
                    last_stat = now;
                }
                {
                    std::lock_guard<std::mutex> g(ms);
                    pub = st;
                    pub.stale = asmb.stale;
                    pub.apps = asmb.apps.size();
                    pub.npcs = asmb.npc_count();
                    pub.tot_sent = tot_sent;
                    pub.tot_recv = tot_recv;
                    pub.tot_written = tot_written;
                    pub.byes_rx = byes_rx;
                    pub.byes_tx = byes_tx;
                    pub.peer_rx = crx.peer_rx;
                    pub.roster_rx = crx.roster_rx;
                    pub.full_rx = crx.full_rx;
                    pub.disc_rx = disc_rx;
                    pub.disc_tx = disc_tx;
                    if (hub)
                    {
                        pub.byes_rx = hub->byes_rx;
                        pub.peer_tx = hub->peer_tx;
                        pub.roster_tx = hub->roster_tx;
                        pub.full_tx = hub->full_tx;
                        pub.app_fwd = hub->app_fwd;
                        pub.conns = hub->count();
                    }
                    pub.bound = s != INVALID_SOCKET;
                    pub.last_rx = last_rx;
                    pub.last_in = last_in;
                }
                double spent = clock_s() - now;
                double wait_ms = (double)cfg.loop_ms - spent * 1000.0;
                if (wait_ms < 1) wait_ms = 1;
                LARGE_INTEGER due;
                due.QuadPart = -(LONGLONG)(wait_ms * 10000.0);
                if (tmr && SetWaitableTimer(tmr, &due, 0, nullptr, nullptr, FALSE))
                {
                    HANDLE hs[2] = {wake, tmr};
                    WaitForMultipleObjects(2, hs, FALSE, (DWORD)wait_ms + 50);
                }
                else WaitForSingleObject(wake, (DWORD)wait_ms);
            }
            if (s != INVALID_SOCKET) closesocket(s);
            if (tmr) CloseHandle(tmr);
            WSACleanup();
        }
        catch (const std::exception& ex)
        {
            say(std::string("RELAY ERROR: net thread stopped: ") + ex.what());
        }
        catch (...)
        {
            say("RELAY ERROR: net thread stopped: unknown exception");
        }
        alive = false;
    }

    namespace disc
    {
        namespace
        {
            bool unpack_t(std::string_view dg, uint8_t want, js::V& o)
            {
                if (dg.size() < 5 || dg[0] != 'S' || dg[1] != '2' || (uint8_t)dg[2] != proto::PROTO || (uint8_t)dg[3] != want) return false;
                uint8_t t = 0;
                std::string err;
                return proto::unpack(dg, t, o, err) == proto::Unpack::ok && t == want && o.t == js::V::object;
            }

            int64_t iget(const js::V& o, const char* k, int64_t d)
            {
                int64_t x = d;
                if (const js::V* v = o.get(k))
                    if (!py_int(*v, x)) x = d;
                return x;
            }

            std::string sget(const js::V& o, const char* k, size_t maxlen)
            {
                const js::V* v = o.get(k);
                if (!v || v->t != js::V::string) return {};
                std::string r = v->s.substr(0, maxlen);
                for (char& c : r)
                    if ((unsigned char)c < 0x20) c = ' ';
                return r;
            }
        }

        std::string probe(int64_t nonce)
        {
            js::V o = js::V::Object();
            o.set("nola", js::V::Int(1));
            o.set("nonce", js::V::Int(nonce));
            return proto::pack(proto::T_DISC, o);
        }

        bool is_probe(std::string_view dg, int64_t& nonce)
        {
            js::V o;
            if (!unpack_t(dg, proto::T_DISC, o) || iget(o, "nola", 0) != 1) return false;
            nonce = iget(o, "nonce", 0);
            return true;
        }

        std::string reply(const Info& i, int64_t nonce)
        {
            js::V o = js::V::Object();
            o.set("nola", js::V::Int(1));
            o.set("nonce", js::V::Int(nonce));
            o.set("name", js::V::Str(i.name));
            o.set("id", js::V::Str(i.id));
            o.set("n", js::V::Int(i.n));
            o.set("max", js::V::Int(i.max));
            o.set("port", js::V::Int(i.port));
            o.set("hub", js::V::Int(i.hub ? 1 : 0));
            return proto::pack(proto::T_DISCR, o);
        }

        bool parse_reply(std::string_view dg, Host& h, int64_t& nonce)
        {
            js::V o;
            if (!unpack_t(dg, proto::T_DISCR, o) || iget(o, "nola", 0) != 1) return false;
            int64_t port = iget(o, "port", 0), n = iget(o, "n", -1), mx = iget(o, "max", 0);
            if (port < 1 || port > 65535 || mx < 1 || mx > 16 || n < 0 || n > 16) return false;
            Host r;
            r.name = sget(o, "name", 64);
            r.id = sget(o, "id", 64);
            r.port = (int)port;
            r.n = (int)n;
            r.max = (int)mx;
            r.hub = iget(o, "hub", 0) == 1;
            if (r.name.empty()) r.name = "host";
            nonce = iget(o, "nonce", 0);
            h = r;
            return true;
        }

        bool add_host(std::vector<Host>& v, const Host& h)
        {
            for (auto& e : v)
                if ((!h.id.empty() && e.id == h.id) || (e.ip == h.ip && e.port == h.port)) return false;
            v.push_back(h);
            return true;
        }

        int pick(const std::vector<Host>& v, const std::vector<std::string>& pref)
        {
            for (auto& want : pref)
                if (!want.empty())
                    for (size_t i = 0; i < v.size(); ++i)
                        if (v[i].addr() == want && !v[i].full()) return (int)i;
            int best = -1;
            for (size_t i = 0; i < v.size(); ++i)
            {
                if (v[i].full()) continue;
                const Host& b = best < 0 ? v[i] : v[(size_t)best];
                if (best < 0 || v[i].name < b.name || (v[i].name == b.name && v[i].addr() < b.addr())) best = (int)i;
            }
            return best;
        }

        bool parse_addr(std::string_view s, std::string& ip, int& port)
        {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
            size_t c = s.rfind(':');
            if (c == std::string_view::npos || c == 0 || c + 1 >= s.size()) return false;
            int p = 0;
            for (char ch : s.substr(c + 1))
            {
                if (ch < '0' || ch > '9') return false;
                p = p * 10 + (ch - '0');
                if (p > 65535) return false;
            }
            if (p < 1) return false;
            ip = std::string(s.substr(0, c));
            port = p;
            return true;
        }

        std::vector<Target> targets(const std::vector<int>& ports, const std::vector<Target>& extra, bool bcast)
        {
            std::vector<Target> t;
            auto add = [&](const std::string& ip, int port)
            {
                if (ip.empty() || port < 1 || port > 65535) return;
                for (auto& e : t)
                    if (e.ip == ip && e.port == port) return;
                t.push_back({ip, port});
            };
            for (auto& e : extra) add(e.ip, e.port);
            for (int p : ports)
            {
                if (bcast) add("*", p);
                add("127.0.0.1", p);
            }
            return t;
        }

        std::vector<Host> run(const std::vector<Target>& t, int wait_ms, std::string* err)
        {
            std::vector<Host> found;
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            {
                if (err) *err = "WSAStartup failed";
                return found;
            }
            SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (s == INVALID_SOCKET)
            {
                if (err) *err = fmt("socket failed: WSA %d", WSAGetLastError());
                WSACleanup();
                return found;
            }
            BOOL yes = TRUE;
            setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
            BOOL off = FALSE;
            DWORD ret = 0;
            WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &ret, nullptr, nullptr);
            sockaddr_in me{};
            me.sin_family = AF_INET;
            me.sin_addr.s_addr = htonl(INADDR_ANY);
            if (bind(s, reinterpret_cast<sockaddr*>(&me), sizeof(me)) != 0)
            {
                if (err) *err = fmt("bind failed: WSA %d", WSAGetLastError());
                closesocket(s);
                WSACleanup();
                return found;
            }
            std::vector<uint32_t> bc;
            bc.push_back(INADDR_BROADCAST);
            {
                INTERFACE_INFO ii[32];
                DWORD got = 0;
                if (WSAIoctl(s, SIO_GET_INTERFACE_LIST, nullptr, 0, ii, sizeof(ii), &got, nullptr, nullptr) == 0)
                    for (DWORD k = 0; k < got / sizeof(INTERFACE_INFO); ++k)
                    {
                        if (!(ii[k].iiFlags & IFF_UP) || (ii[k].iiFlags & IFF_LOOPBACK) || !(ii[k].iiFlags & IFF_BROADCAST)) continue;
                        uint32_t a = ntohl(ii[k].iiAddress.AddressIn.sin_addr.s_addr), m = ntohl(ii[k].iiNetmask.AddressIn.sin_addr.s_addr);
                        uint32_t b = a | ~m;
                        if (a == 0 || m == 0 || std::find(bc.begin(), bc.end(), b) != bc.end()) continue;
                        bc.push_back(b);
                    }
            }
            std::mt19937_64 rng(std::random_device{}());
            int64_t nonce = (int64_t)(rng() & 0x7fffffffffffULL) + 1;
            std::string pr = probe(nonce);
            auto send_all = [&]
            {
                for (auto& x : t)
                {
                    sockaddr_in a{};
                    a.sin_family = AF_INET;
                    a.sin_port = htons((u_short)x.port);
                    if (x.ip == "*")
                    {
                        for (uint32_t b : bc)
                        {
                            a.sin_addr.s_addr = htonl(b);
                            sendto(s, pr.data(), (int)pr.size(), 0, reinterpret_cast<sockaddr*>(&a), sizeof(a));
                        }
                        continue;
                    }
                    if (inet_pton(AF_INET, x.ip.c_str(), &a.sin_addr) != 1)
                    {
                        addrinfo hints{}, *res = nullptr;
                        hints.ai_family = AF_INET;
                        hints.ai_socktype = SOCK_DGRAM;
                        if (getaddrinfo(x.ip.c_str(), nullptr, &hints, &res) != 0 || !res) continue;
                        a.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
                        freeaddrinfo(res);
                    }
                    sendto(s, pr.data(), (int)pr.size(), 0, reinterpret_cast<sockaddr*>(&a), sizeof(a));
                }
            };
            double t0 = clock_s();
            double end = t0 + (wait_ms < 100 ? 100 : wait_ms) / 1000.0;
            double resend = t0 + 0.4;
            send_all();
            std::vector<char> buf(4096);
            while (true)
            {
                double now = clock_s();
                if (now >= end) break;
                if (now >= resend)
                {
                    send_all();
                    resend = now + 0.4;
                }
                double w = std::min(end, resend) - now;
                if (w < 0) w = 0;
                fd_set rs;
                FD_ZERO(&rs);
                FD_SET(s, &rs);
                timeval tv{};
                tv.tv_sec = (long)w;
                tv.tv_usec = (long)((w - (double)tv.tv_sec) * 1e6);
                int r = select(0, &rs, nullptr, nullptr, &tv);
                if (r <= 0) continue;
                sockaddr_in from{};
                int fl = sizeof(from);
                int n = recvfrom(s, buf.data(), (int)buf.size(), 0, reinterpret_cast<sockaddr*>(&from), &fl);
                if (n <= 0) continue;
                Host h;
                int64_t rn = 0;
                if (!parse_reply(std::string_view(buf.data(), (size_t)n), h, rn) || rn != nonce) continue;
                char ip[INET_ADDRSTRLEN] = {0};
                inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
                h.ip = ip;
                h.ms = (clock_s() - t0) * 1000.0;
                add_host(found, h);
            }
            closesocket(s);
            WSACleanup();
            return found;
        }
    }
}
