// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include "opendepot/core/crypto.h"

#include <utility>

namespace opendepot {

void Rc4(const std::vector<uint8_t>& key, std::vector<uint8_t>& data) {
    if (key.empty() || data.empty()) return;

    uint8_t s[256];
    for (int i = 0; i < 256; ++i) s[i] = static_cast<uint8_t>(i);

    int j = 0;
    for (int i = 0; i < 256; ++i) {
        j = (j + s[i] + key[static_cast<size_t>(i) % key.size()]) & 0xff;
        std::swap(s[i], s[j]);
    }

    int i = 0;
    j = 0;
    for (uint8_t& byte : data) {
        i = (i + 1) & 0xff;
        j = (j + s[i]) & 0xff;
        std::swap(s[i], s[j]);
        byte ^= s[(s[i] + s[j]) & 0xff];
    }
}

}  // namespace opendepot
