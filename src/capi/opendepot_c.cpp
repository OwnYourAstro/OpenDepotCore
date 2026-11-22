// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include "opendepot/opendepot_c.h"

#include <cstdlib>

#include "opendepot/core/version.h"

extern "C" {

int od_version_major(void) { return opendepot::VersionMajor(); }
int od_version_minor(void) { return opendepot::VersionMinor(); }
int od_version_patch(void) { return opendepot::VersionPatch(); }
const char* od_version_string(void) { return opendepot::VersionString(); }

void od_free_string(char* s) { std::free(s); }

}  // extern "C"
