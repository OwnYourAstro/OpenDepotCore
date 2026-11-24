// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace opendepot {

// Byte-stream transport used by the portable PPP/RCMP session. Implementations
// provide the platform-specific serial port (Win32, Android USB, tests).
class SerialLink {
public:
    virtual ~SerialLink() = default;

    // Opens the port at the given baud rate. May be called again after Close()
    // to emulate Depot's close/reopen sequence between AT commands.
    virtual bool Open(uint32_t baud, std::string& err) = 0;

    // Changes the baud rate without closing the port when the backend supports
    // it. Callers fall back to Close()/Open() when this returns false.
    virtual bool Reconfigure(uint32_t baud, std::string& err) = 0;

    virtual void Purge() = 0;

    // Reads up to n bytes. Returns the number of bytes read (0 on timeout).
    // timeout_ms bounds the wait for at least one byte.
    virtual size_t Read(uint8_t* data, size_t n, int timeout_ms) = 0;

    virtual bool Write(const uint8_t* data, size_t n) = 0;

    virtual void Close() = 0;
};

}  // namespace opendepot
