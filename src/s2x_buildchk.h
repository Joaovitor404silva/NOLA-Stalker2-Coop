#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bchk
{
    struct Sig
    {
        const char* name;
        uint32_t rva;
        const char* pat;
        bool man;
    };

    struct Expect
    {
        uint32_t ts;
        uint32_t text_vs;
        const char* ver;
        const char* build;
        int manifest;
        int extra;
        const Sig* sigs;
        size_t n;
    };

    struct Image
    {
        virtual ~Image() = default;
        virtual bool read(uint32_t rva, void* out, size_t n) const = 0;
    };

    struct FileImage : Image
    {
        std::vector<uint8_t> data;
        bool load(const wchar_t* path);
        bool read(uint32_t rva, void* out, size_t n) const override;
    };

    struct MemImage : Image
    {
        const uint8_t* base = nullptr;
        uint32_t size = 0;
        explicit MemImage(const void* module);
        bool read(uint32_t rva, void* out, size_t n) const override;
    };

    struct Result
    {
        bool ok = false;
        bool headers = false;
        bool ident = false;
        uint32_t ts = 0;
        uint32_t text_vs = 0;
        std::string ver;
        int total = 0;
        int bad = 0;
        int unread = 0;
        std::string first;
        uint32_t first_rva = 0;
    };

    const Expect& table();
    Result run_check(const Image& img, const Expect& e);
    std::string found_text(const Result& r, const Expect& e);
    std::string expect_text(const Expect& e);
    std::string log_line(const Result& r, const Expect& e);
    std::wstring message(const Result& r, const Expect& e);
    bool enabled(std::string_view flags_txt);
}
