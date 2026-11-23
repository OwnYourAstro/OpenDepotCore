// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>

namespace opendepot {

// One CPS codeplug block type and the on-disk record tag (Strm_N ISH tag) it
// serializes to. Recovered live from ASTRO 25 Portable Depot (H46UCF9PW6BN):
// CcgIshBlkLayout::GetBlkID() (type) and CcgIshBlkLayout::GetIshType() (tag).
struct BlockTagInfo {
    uint16_t type;
    uint16_t tag;
    const char* name;
};

// Verified block type -> record tag catalog. Returns the table and its length.
const BlockTagInfo* BlockTagCatalog(std::size_t& count);

const BlockTagInfo* FindBlockByTag(uint16_t tag);
const BlockTagInfo* FindBlockByType(uint16_t type);

// Block name for a record tag, or "" when unknown.
const char* BlockNameForTag(uint16_t tag);

}  // namespace opendepot
