// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>

namespace opendepot {

struct FieldDef {
    uint16_t fid = 0;
    const char* name = nullptr;
    const char* group = nullptr;
    uint16_t off = 0;
    uint16_t bits = 0;
    uint8_t msb = 0;
    int blockId = -1;
    const char* tableKey = nullptr;
    const char* recordTag = nullptr;
};

struct FieldBinding {
    uint16_t fid = 0;
    const char* group = nullptr;
    uint16_t off = 0;
    uint16_t bits = 0;
    uint8_t msb = 0;
    int blockId = -1;
};

struct TableColumnDef {
    uint16_t index = 0;
    const char* name = nullptr;
    const char* match = nullptr;
    const FieldBinding* bindings = nullptr;
    size_t bindingCount = 0;
};

struct TableDef {
    const char* key = nullptr;
    const char* title = nullptr;
    const char* coreSection = nullptr;
    const TableColumnDef* columns = nullptr;
    size_t columnCount = 0;
    size_t boundColumnCount = 0;
};

const FieldDef* FieldCatalog(size_t& count);
const FieldDef* FindField(uint16_t fid);
const TableDef* TableCatalog(size_t& count);
const TableDef* FindTable(const char* key);

uint64_t ReadFieldBits(const uint8_t* data, size_t size, uint16_t off,
                       uint16_t bits, uint8_t msb);

}  // namespace opendepot
