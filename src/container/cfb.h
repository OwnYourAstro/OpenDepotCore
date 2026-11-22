// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace opendepot::cfb {

struct Stream {
    std::string name;
    std::vector<uint8_t> data;
};

// Writes a compound file containing the given root streams plus one storage
// with its own streams. Streams below the 4096-byte cutoff are placed in the
// mini stream. The output is a standard v3 MS-CFB file.
bool Write(const std::filesystem::path& path, const std::vector<Stream>& root_streams,
           const std::string& storage_name, const std::vector<Stream>& storage_streams,
           std::string& err);

// Minimal read-only MS-CFB (Compound File Binary) reader. It supports the
// subset used by ASTRO 25 .cpg files: a root stream plus one storage that holds
// numbered data streams. It does not use OLE32, so it can back the portable
// container path on every platform.
class File {
public:
    bool Open(const std::filesystem::path& path, std::string& err);

    bool ReadRoot(const std::string& name, std::vector<uint8_t>& out,
                  std::string& err) const;
    bool ReadStorage(const std::string& storage, const std::string& name,
                     std::vector<uint8_t>& out, std::string& err) const;
    bool ListStreams(const std::string& storage, std::vector<std::string>& names,
                     std::string& err) const;

private:
    struct Entry {
        std::string name;
        uint8_t type = 0;
        uint32_t left = 0xFFFFFFFF;
        uint32_t right = 0xFFFFFFFF;
        uint32_t child = 0xFFFFFFFF;
        uint32_t start = 0xFFFFFFFE;
        uint64_t size = 0;
    };

    const uint8_t* Sector(uint32_t index) const;
    bool ValidSector(uint32_t index) const;
    bool ReadFatChain(uint32_t start, size_t size, bool exact,
                      std::vector<uint8_t>& out) const;
    bool ReadMiniChain(uint32_t start, size_t size, std::vector<uint8_t>& out) const;
    bool ReadEntry(uint32_t id, std::vector<uint8_t>& out, std::string& err) const;
    int FindChild(uint32_t parent, const std::string& name) const;
    void Collect(uint32_t id, std::vector<uint32_t>& out, size_t& guard) const;

    std::vector<uint8_t> data_;
    uint32_t sector_size_ = 0;
    uint32_t mini_sector_size_ = 0;
    uint32_t mini_cutoff_ = 0;
    std::vector<uint32_t> fat_;
    std::vector<uint32_t> mini_fat_;
    std::vector<uint8_t> mini_stream_;
    std::vector<Entry> dir_;
};

}  // namespace opendepot::cfb
