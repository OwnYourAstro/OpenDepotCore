// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cstdio>
#include <string>

#include "opendepot/core/adk.h"

namespace {

bool HasTag(const opendepot::Codeplug& cp, uint16_t tag) {
    for (const auto& r : cp.records)
        if (static_cast<uint16_t>(r.id >> 16) == tag) return true;
    return false;
}

void RemoveTag(opendepot::Codeplug& cp, uint16_t tag) {
    cp.records.erase(std::remove_if(cp.records.begin(), cp.records.end(),
                                    [&](const opendepot::Record& r) {
                                        return static_cast<uint16_t>(r.id >> 16) == tag;
                                    }),
                     cp.records.end());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: validate_test <fixture.cpg>\n");
        return 2;
    }
    opendepot::Codeplug cp;
    if (!opendepot::LoadCodeplug(argv[1], cp)) {
        std::printf("load failed: %s\n", cp.error.c_str());
        return 2;
    }

    std::string err;
    if (!opendepot::ValidateRadioCodeplug(cp, err)) {
        std::printf("complete fixture refused: %s\n", err.c_str());
        return 1;
    }

    for (uint16_t tag : {0x0016, 0x0FFD, 0x0FFF}) {
        if (!HasTag(cp, tag)) {
            std::printf("fixture does not carry tag 0x%04X\n", tag);
            return 1;
        }
        opendepot::Codeplug broken = cp;
        RemoveTag(broken, tag);
        err.clear();
        if (opendepot::ValidateRadioCodeplug(broken, err)) {
            std::printf("incomplete set accepted after removing tag 0x%04X\n", tag);
            return 1;
        }
        if (err.find("missing required tag") == std::string::npos) {
            std::printf("unexpected error for tag 0x%04X: %s\n", tag, err.c_str());
            return 1;
        }
    }

    opendepot::Codeplug no_dir = cp;
    no_dir.records.erase(std::remove_if(no_dir.records.begin(), no_dir.records.end(),
                                        [](const opendepot::Record& r) {
                                            return r.id == 0x00470000u;
                                        }),
                         no_dir.records.end());
    err.clear();
    if (opendepot::ValidateRadioCodeplug(no_dir, err) ||
        err.find("directory record") == std::string::npos) {
        std::printf("missing-directory guard failed: %s\n", err.c_str());
        return 1;
    }

    std::printf("validate ok\n");
    return 0;
}
