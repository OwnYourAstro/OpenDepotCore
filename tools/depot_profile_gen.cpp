// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "opendepot/core/adk.h"

namespace {

void WriteU16(std::ofstream& out, uint16_t v) {
    const unsigned char b[2] = {uint8_t(v), uint8_t(v >> 8)};
    out.write(reinterpret_cast<const char*>(b), 2);
}
void WriteU32(std::ofstream& out, uint32_t v) {
    const unsigned char b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
    out.write(reinterpret_cast<const char*>(b), 4);
}
void WriteBytes(std::ofstream& out, const void* p, size_t n) {
    out.write(static_cast<const char*>(p), static_cast<std::streamsize>(n));
}

std::string RecordKey(const opendepot::Record& r) {
    std::string key;
    key.reserve(r.raw.size() + 2);
    key.push_back(char(r.type >> 8));
    key.push_back(char(r.type));
    key.append(reinterpret_cast<const char*>(r.raw.data()), r.raw.size());
    return key;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: depot-profile-gen <depot_codeplugs_dir> <out.bin>\n";
        return 2;
    }
    const std::filesystem::path source = argv[1];
    const std::filesystem::path output = argv[2];

    struct Profile {
        std::string model;
        std::string package;
        opendepot::Codeplug cp;
        std::vector<uint32_t> record_indices;
    };
    std::vector<Profile> profiles;
    struct DictionaryRecord {
        uint16_t type = 0x0080;
        std::vector<uint8_t> raw;
    };
    std::vector<DictionaryRecord> dictionary;
    std::unordered_map<std::string, uint32_t> dictionary_index;

    for (const auto& entry : std::filesystem::directory_iterator(source)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".cpg") continue;
        const std::string stem = entry.path().stem().string();
        const size_t dash = stem.find('-');
        if (dash == std::string::npos) continue;

        opendepot::Codeplug cp;
        if (!opendepot::LoadCodeplug(entry.path(), cp)) {
            std::cerr << "skip " << entry.path().filename().string() << ": " << cp.error << "\n";
            continue;
        }
        Profile profile;
        profile.model = cp.header.model;
        profile.package = stem.substr(dash + 1);
        std::replace(profile.package.begin(), profile.package.end(), '_', ' ');
        profile.cp = std::move(cp);
        for (const auto& record : profile.cp.records) {
            const std::string key = RecordKey(record);
            auto it = dictionary_index.find(key);
            uint32_t index = 0;
            if (it == dictionary_index.end()) {
                index = static_cast<uint32_t>(dictionary.size());
                dictionary_index.emplace(key, index);
                dictionary.push_back({record.type, record.raw});
            } else {
                index = it->second;
            }
            profile.record_indices.push_back(index);
        }
        profiles.push_back(std::move(profile));
    }

    std::sort(profiles.begin(), profiles.end(), [](const Profile& a, const Profile& b) {
        if (a.model != b.model) return a.model < b.model;
        return a.package < b.package;
    });

    std::ofstream out(output, std::ios::binary);
    if (!out) {
        std::cerr << "cannot open output\n";
        return 2;
    }
    const char magic[8] = {'O', 'D', 'P', 'R', 'O', 'F', '1', '\0'};
    WriteBytes(out, magic, sizeof(magic));
    WriteU32(out, static_cast<uint32_t>(dictionary.size()));
    for (const auto& record : dictionary) {
        WriteU16(out, record.type);
        WriteU32(out, static_cast<uint32_t>(record.raw.size()));
        WriteBytes(out, record.raw.data(), record.raw.size());
    }
    WriteU32(out, static_cast<uint32_t>(profiles.size()));
    for (const auto& profile : profiles) {
        WriteU16(out, static_cast<uint16_t>(profile.model.size()));
        WriteBytes(out, profile.model.data(), profile.model.size());
        WriteU16(out, static_cast<uint16_t>(profile.package.size()));
        WriteBytes(out, profile.package.data(), profile.package.size());
        WriteBytes(out, profile.cp.header.raw.data(), profile.cp.header.raw.size());
        WriteU32(out, static_cast<uint32_t>(profile.record_indices.size()));
        for (uint32_t index : profile.record_indices) WriteU32(out, index);
    }
    std::cout << "profiles=" << profiles.size() << " records=" << dictionary.size()
              << " bytes=" << std::filesystem::file_size(output) << "\n";
    return 0;
}
