// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include "opendepot/core/version.h"

#include "opendepot/version_config.h"

namespace opendepot {

int VersionMajor() { return OPENDEPOT_VERSION_MAJOR; }
int VersionMinor() { return OPENDEPOT_VERSION_MINOR; }
int VersionPatch() { return OPENDEPOT_VERSION_PATCH; }

const char* VersionString() { return OPENDEPOT_VERSION_STRING; }

}  // namespace opendepot
