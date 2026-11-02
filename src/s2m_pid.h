#pragma once
#include <string>
#include <utility>
#include <vector>

namespace s2m_pid
{
    struct Member
    {
        std::string key;
        std::string name;
        std::string iid;
    };

    struct Entry
    {
        std::string guid;
        std::string owner;
        int kh = 0;
        std::string made;
    };

    struct Campaign
    {
        std::string cid;
        std::vector<Member> members;
        std::vector<Entry> entries;
    };

    using Peer = std::pair<std::string, std::string>;

    enum class Load
    {
        ok,
        absent,
        bad
    };

    auto valid_iid(const std::string& iid) -> bool;
    auto make_iid(std::string& iid, std::string& err) -> bool;
    auto parse_player(const std::string& text, std::string& iid, std::string& err) -> bool;
    auto dump_player(const std::string& iid) -> std::string;
    auto load_iid(const std::wstring& dir, std::string& iid, std::string& err) -> Load;
    auto ensure_iid(const std::wstring& dir, std::string& iid, std::string& err) -> bool;

    auto normalize(const std::string& name) -> std::string;
    auto compare_form(const std::string& name) -> std::string;

    auto find_member(const Campaign& c, const std::string& key) -> const Member*;
    auto resolve(Campaign& c, const std::string& name, const std::string& iid, const std::vector<Peer>& peers) -> std::string;
    auto resolve_all(Campaign& c, const std::vector<Peer>& players) -> std::vector<std::string>;
    auto file_form(const Campaign& c, const std::string& key) -> std::string;

    auto parse_campaign(const std::string& text, Campaign& out, std::string& err) -> bool;
    auto dump_campaign(const Campaign& c) -> std::string;
    auto load_campaign(const std::wstring& dir, Campaign& out, std::string& err) -> Load;
    auto save_campaign(const std::wstring& dir, const Campaign& c, std::string& err) -> bool;
}
