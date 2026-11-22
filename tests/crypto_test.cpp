// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "opendepot/core/adk.h"
#include "opendepot/core/crypto.h"

namespace {

std::string Hex(const uint8_t* data, size_t size) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out.push_back(kHex[data[i] >> 4]);
        out.push_back(kHex[data[i] & 0x0f]);
    }
    return out;
}

int Check(const char* name, const std::string& got, const char* want) {
    if (got == want) return 0;
    std::printf("FAIL %s\n  got  %s\n  want %s\n", name, got.c_str(), want);
    return 1;
}

}  // namespace

int main() {
    int failures = 0;

    {
        const auto d = opendepot::Md5(nullptr, 0);
        failures += Check("md5 empty", Hex(d.data(), d.size()),
                          "d41d8cd98f00b204e9800998ecf8427e");
    }
    {
        const char* s = "abc";
        const auto d = opendepot::Md5(reinterpret_cast<const uint8_t*>(s), 3);
        failures += Check("md5 abc", Hex(d.data(), d.size()),
                          "900150983cd24fb0d6963f7d28e17f72");
    }
    {
        const char* s = "The quick brown fox jumps over the lazy dog";
        const auto d = opendepot::Md5(reinterpret_cast<const uint8_t*>(s), std::strlen(s));
        failures += Check("md5 fox", Hex(d.data(), d.size()),
                          "9e107d9d372bb6826bd81d3542a419d6");
    }
    {
        const auto d = opendepot::Sha256(nullptr, 0);
        failures += Check("sha256 empty", Hex(d.data(), d.size()),
                          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    }
    {
        const char* s = "abc";
        const auto d = opendepot::Sha256(reinterpret_cast<const uint8_t*>(s), 3);
        failures += Check("sha256 abc", Hex(d.data(), d.size()),
                          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        failures += Check("sha256hex abc",
                          opendepot::Sha256Hex(std::vector<uint8_t>{'a', 'b', 'c'}),
                          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    }

    // CryptoAPI-compatible RC4: key = MD5(seed)[:5] followed by 11 zero bytes.
    {
        const char seed_text[] = "A8Z6J9QP5";
        std::vector<uint8_t> seed(seed_text, seed_text + 9);
        const auto digest = opendepot::Md5(seed.data(), seed.size());
        std::vector<uint8_t> key(16, 0);
        for (int i = 0; i < 5; ++i) key[static_cast<size_t>(i)] = digest[static_cast<size_t>(i)];

        std::vector<uint8_t> stream(16, 0);
        opendepot::Rc4(key, stream);
        failures += Check("rc4 ms-vector", Hex(stream.data(), stream.size()),
                          "8b3a24de262adeedd31c5e445ec78a7f");

        std::vector<uint8_t> plain(32, 0);
        std::vector<uint8_t> cipher;
        std::vector<uint8_t> back;
        if (!opendepot::Rc4Encrypt(seed, plain, cipher) ||
            !opendepot::Rc4Decrypt(seed, cipher, back)) {
            std::printf("FAIL rc4 roundtrip call\n");
            ++failures;
        } else {
            failures += Check("rc4 cipher", Hex(cipher.data(), 16),
                              "8b3a24de262adeedd31c5e445ec78a7f");
            failures += Check("rc4 roundtrip", Hex(back.data(), back.size()), Hex(plain.data(), plain.size()).c_str());
        }

        // Too-short seeds are rejected.
        std::vector<uint8_t> short_seed(8, 0);
        std::vector<uint8_t> out;
        if (opendepot::Rc4Decrypt(short_seed, plain, out)) {
            std::printf("FAIL rc4 short seed accepted\n");
            ++failures;
        }
    }

    if (failures == 0) std::printf("crypto ok\n");
    return failures == 0 ? 0 : 1;
}
