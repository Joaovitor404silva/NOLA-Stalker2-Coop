#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace s2m_wpkg
{
    struct Pkg
    {
        std::string id, guid, cid, ap, data, own;
        long long size = -1;
        uint64_t hash = 0;
        int kh = -1;
        int hs = -1;
    };

    struct Frozen
    {
        std::string guid, cid;
        int kh = -1;
        int hs = -1;
        long long size = -1;
        uint64_t hash = 0;
    };

    struct Stored
    {
        std::string id, cid, guid, file;
        int kh = -1;
        int hs = -1;
        long long size = -1;
    };

    auto fnv64(const char* d, size_t n) -> uint64_t;
    auto hex16(uint64_t v) -> std::string;
    auto field(const std::string& hdr, const char* k) -> std::string;
    auto build_ap(const std::string& log, int kh, const std::string& cid, int& have) -> std::string;
    auto pack(const Pkg& k) -> std::string;
    auto parse(const std::string& b, Pkg& k, std::string& err) -> bool;

    auto world_id(long long wallMs) -> std::string;
    auto is_world_id(const std::string& id) -> bool;

    auto read_file(const std::wstring& p, std::string& out) -> bool;
    auto write_atomic(const std::wstring& p, const std::string& data) -> bool;

    auto freeze(const std::wstring& br, const std::wstring& savFile, const std::string& guid, int hs, int kh, const std::string& cid, std::string& err) -> bool;
    auto frozen(const std::wstring& br, Frozen& f, std::string* data, std::string& err) -> bool;
    auto pack_world(const std::wstring& br, const std::string& req, const std::string& questLog, const std::string& own, std::string& out, Frozen& f, std::string& err) -> bool;
    auto store(const std::wstring& br, const std::string& blob, int keep, Stored& s, std::string& err) -> bool;
    auto last(const std::wstring& br, Stored& s) -> bool;
}
