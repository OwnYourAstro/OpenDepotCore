// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

#include "opendepot/core/adk.h"

namespace opendepot {

struct DepotProfileInfo {
    std::string model;
    std::string package;
};

// Native, generated profile database. Profiles are clean Depot-produced
// codeplugs keyed by (model, package). The database is an offline build
// artifact; loading it never launches Depot or reads a user template.
bool ListDepotProfiles(const std::string& database_path, std::vector<DepotProfileInfo>& out,
                       std::string& err);
bool LoadDepotProfile(const std::string& database_path, const std::string& model,
                      const std::string& package, Codeplug& out, std::string& err);

// Builds a complete (partition, tag, instance, length) read list from the
// native profile database. When the model is known, every profile for that
// model is unioned; otherwise every profile in the database is unioned. This is
// data-driven and deliberately not a model allowlist.
bool BuildDepotReadRequests(const std::string& database_path, const std::string& model,
                            std::vector<ReadRequest>& out, std::string& err);

}  // namespace opendepot
