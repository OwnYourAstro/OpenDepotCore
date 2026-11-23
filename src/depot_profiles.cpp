// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include "opendepot/core/depot_profiles.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace opendepot {
namespace {

constexpr char kMagic[8] = {'O', 'D', 'P', 'R', 'O', 'F', '1', '\0'};

bool ReadBytes(std::istream& in, void* out, size_t n) {
    return bool(in.read(static_cast<char*>(out), static_cast<std::streamsize>(n)));
}
bool ReadU16(std::istream& in, uint16_t& v) {
    unsigned char b[2];
    if (!ReadBytes(in, b, 2)) return false;
    v = uint16_t(b[0] | (b[1] << 8));
    return true;
}
bool ReadU32(std::istream& in, uint32_t& v) {
    unsigned char b[4];
    if (!ReadBytes(in, b, 4)) return false;
    v = uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) |
        (uint32_t(b[3]) << 24);
    return true;
}

}  // namespace

bool ListDepotProfiles(const std::string& database_path, std::vector<DepotProfileInfo>& out,
                       std::string& err) {
    out.clear();
    err.clear();
    std::ifstream in(database_path, std::ios::binary);
    if (!in) {
        err = "cannot open native profile database: " + database_path;
        return false;
    }
    char magic[8];
    if (!ReadBytes(in, magic, sizeof(magic)) ||
        std::string(magic, sizeof(magic)) != std::string(kMagic, sizeof(kMagic))) {
        err = "invalid native profile database";
        return false;
    }
    uint32_t record_count = 0;
    if (!ReadU32(in, record_count) || record_count > 100000) {
        err = "invalid profile record dictionary";
        return false;
    }
    for (uint32_t i = 0; i < record_count; ++i) {
        uint16_t type = 0;
        uint32_t size = 0;
        if (!ReadU16(in, type) || !ReadU32(in, size) || size > 1024 * 1024) {
            err = "invalid profile record entry";
            return false;
        }
        in.seekg(static_cast<std::streamoff>(size), std::ios::cur);
        if (!in) {
            err = "truncated profile record entry";
            return false;
        }
    }
    uint32_t profile_count = 0;
    if (!ReadU32(in, profile_count) || profile_count > 100000) {
        err = "invalid profile count";
        return false;
    }
    for (uint32_t i = 0; i < profile_count; ++i) {
        uint16_t model_len = 0, package_len = 0;
        if (!ReadU16(in, model_len) || model_len > 256) {
            err = "invalid profile model name";
            return false;
        }
        DepotProfileInfo info;
        info.model.resize(model_len);
        if (model_len && !ReadBytes(in, info.model.data(), model_len)) return false;
        if (!ReadU16(in, package_len) || package_len > 256) {
            err = "invalid profile package name";
            return false;
        }
        info.package.resize(package_len);
        if (package_len && !ReadBytes(in, info.package.data(), package_len)) return false;
        char header[800];
        if (!ReadBytes(in, header, sizeof(header))) return false;
        uint32_t count = 0;
        if (!ReadU32(in, count) || count > record_count) {
            err = "invalid profile record count";
            return false;
        }
        in.seekg(static_cast<std::streamoff>(count) * 4, std::ios::cur);
        if (!in) {
            err = "truncated profile record list";
            return false;
        }
        out.push_back(std::move(info));
    }
    return true;
}

bool LoadDepotProfile(const std::string& database_path, const std::string& model,
                      const std::string& package, Codeplug& out, std::string& err) {
    err.clear();
    std::ifstream in(database_path, std::ios::binary);
    if (!in) {
        err = "cannot open native profile database: " + database_path;
        return false;
    }

    char magic[8];
    if (!ReadBytes(in, magic, sizeof(magic)) || std::string(magic, sizeof(magic)) !=
                                                   std::string(kMagic, sizeof(kMagic))) {
        err = "invalid native profile database";
        return false;
    }

    uint32_t record_count = 0;
    if (!ReadU32(in, record_count) || record_count > 100000) {
        err = "invalid profile record dictionary";
        return false;
    }
    std::vector<std::vector<uint8_t>> records(record_count);
    std::vector<uint16_t> types(record_count);
    for (uint32_t i = 0; i < record_count; ++i) {
        uint32_t size = 0;
        if (!ReadU16(in, types[i]) || !ReadU32(in, size) || size > 1024 * 1024) {
            err = "invalid profile record entry";
            return false;
        }
        records[i].resize(size);
        if (size && !ReadBytes(in, records[i].data(), size)) {
            err = "truncated profile record entry";
            return false;
        }
    }

    uint32_t profile_count = 0;
    if (!ReadU32(in, profile_count) || profile_count > 100000) {
        err = "invalid profile count";
        return false;
    }
    for (uint32_t i = 0; i < profile_count; ++i) {
        uint16_t model_len = 0, package_len = 0;
        if (!ReadU16(in, model_len) || model_len > 256) {
            err = "invalid profile model name";
            return false;
        }
        std::string profile_model(model_len, '\0');
        if (model_len && !ReadBytes(in, profile_model.data(), model_len)) return false;
        if (!ReadU16(in, package_len) || package_len > 256) {
            err = "invalid profile package name";
            return false;
        }
        std::string profile_package(package_len, '\0');
        if (package_len && !ReadBytes(in, profile_package.data(), package_len)) return false;

        Codeplug profile;
        if (!ReadBytes(in, profile.header.raw.data(), profile.header.raw.size())) return false;
        profile.encType = (profile.header.raw[8] & 0x18) >> 3;
        if (profile.encType == 1) {
            const size_t len = profile.header.raw[0x239];
            if (len && 0x23A + len <= profile.header.raw.size())
                profile.keyBlob.assign(profile.header.raw.begin() + 0x23A,
                                       profile.header.raw.begin() + 0x23A + len);
            const char seed[] = "A8Z6J9QP5";
            profile.seed.assign(seed, seed + 9);
        }
        uint32_t count = 0;
        if (!ReadU32(in, count) || count > record_count) {
            err = "invalid profile record count";
            return false;
        }
        std::vector<uint32_t> indices(count);
        for (uint32_t& index : indices) {
            if (!ReadU32(in, index) || index >= record_count) {
                err = "invalid profile record index";
                return false;
            }
        }
        if (profile_model != model || profile_package != package) continue;

        for (uint32_t index : indices) {
            Record r;
            r.type = types[index];
            r.raw = records[index];
            if (r.raw.size() < 8) {
                err = "invalid record in profile";
                return false;
            }
            r.id = (uint32_t(r.raw[0]) << 24) | (uint32_t(r.raw[1]) << 16) |
                   (uint32_t(r.raw[2]) << 8) | r.raw[3];
            r.checksum = uint16_t((r.raw[r.raw.size() - 2] << 8) | r.raw.back());
            r.verified = true;
            profile.records.push_back(std::move(r));
        }
        out = std::move(profile);
        return true;
    }

    err = "no native profile for " + model + " / " + package;
    return false;
}

}  // namespace opendepot
