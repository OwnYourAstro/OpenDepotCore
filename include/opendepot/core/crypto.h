// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace opendepot {

std::array<uint8_t, 16> Md5(const uint8_t* data, size_t size);
std::array<uint8_t, 32> Sha256(const uint8_t* data, size_t size);
std::string Sha256Hex(const std::vector<uint8_t>& data);

// Standard RC4 (ARC4) applied in place with the given key.
void Rc4(const std::vector<uint8_t>& key, std::vector<uint8_t>& data);

}  // namespace opendepot
