// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include <cstring>

#include "opendepot/core/version.h"
#include "opendepot/opendepot_c.h"

int main() {
    if (std::strcmp(opendepot::VersionString(), "0.1.0") != 0) return 1;
    if (opendepot::VersionMajor() != 0) return 2;
    if (opendepot::VersionMinor() != 1) return 3;
    if (opendepot::VersionPatch() != 0) return 4;

    if (std::strcmp(od_version_string(), opendepot::VersionString()) != 0) return 5;
    if (od_version_major() != 0 || od_version_minor() != 1 || od_version_patch() != 0) return 6;

    od_free_string(nullptr);
    return 0;
}
