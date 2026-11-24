// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include "opendepot/core/radio.h"
#include "opendepot/core/serial_link.h"

#include <windows.h>

#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace opendepot {
namespace {

class Win32SerialLink final : public SerialLink {
public:
    explicit Win32SerialLink(std::string port) : port_(std::move(port)) {}
    ~Win32SerialLink() override { Close(); }

    bool Open(uint32_t baud, std::string& err) override {
        Close();
        const std::wstring w(port_.begin(), port_.end());
        const std::wstring dev = L"\\\\.\\" + w;
        h_ = CreateFileW(dev.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0,
                         nullptr);
        if (h_ == INVALID_HANDLE_VALUE) {
            err = "cannot open " + port_;
            return false;
        }
        if (!SetBaud(baud, err)) {
            Close();
            return false;
        }
        COMMTIMEOUTS to{};
        to.ReadIntervalTimeout = 10;
        to.ReadTotalTimeoutConstant = 10;
        to.ReadTotalTimeoutMultiplier = 0;
        to.WriteTotalTimeoutConstant = 1000;
        to.WriteTotalTimeoutMultiplier = 0;
        SetCommTimeouts(h_, &to);
        Purge();
        return true;
    }

    bool Reconfigure(uint32_t baud, std::string& err) override {
        if (h_ == INVALID_HANDLE_VALUE) return Open(baud, err);
        if (!SetBaud(baud, err)) return false;
        Purge();
        return true;
    }

    void Purge() override {
        if (h_ != INVALID_HANDLE_VALUE) PurgeComm(h_, PURGE_RXCLEAR | PURGE_TXCLEAR);
    }

    size_t Read(uint8_t* data, size_t n, int timeout_ms) override {
        if (h_ == INVALID_HANDLE_VALUE) return 0;
        COMMTIMEOUTS to{};
        to.ReadIntervalTimeout = 10;
        to.ReadTotalTimeoutConstant = timeout_ms > 0 ? DWORD(timeout_ms) : 10;
        to.ReadTotalTimeoutMultiplier = 0;
        to.WriteTotalTimeoutConstant = 1000;
        to.WriteTotalTimeoutMultiplier = 0;
        SetCommTimeouts(h_, &to);
        DWORD got = 0;
        if (!ReadFile(h_, data, DWORD(n), &got, nullptr)) return 0;
        return got;
    }

    bool Write(const uint8_t* data, size_t n) override {
        if (h_ == INVALID_HANDLE_VALUE) return false;
        size_t offset = 0;
        while (offset < n) {
            DWORD written = 0;
            if (!WriteFile(h_, data + offset, DWORD(n - offset), &written, nullptr)) return false;
            if (written == 0) return false;
            offset += written;
        }
        return true;
    }

    void Close() override {
        if (h_ != INVALID_HANDLE_VALUE) {
            CloseHandle(h_);
            h_ = INVALID_HANDLE_VALUE;
        }
    }

private:
    bool SetBaud(uint32_t baud, std::string& err) {
        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(h_, &dcb)) {
            err = "GetCommState failed";
            return false;
        }
        dcb.BaudRate = baud;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = TRUE;
        dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        dcb.fDsrSensitivity = FALSE;
        if (!SetCommState(h_, &dcb)) {
            err = "SetCommState failed";
            return false;
        }
        return true;
    }

    std::string port_;
    HANDLE h_ = INVALID_HANDLE_VALUE;
};

}  // namespace

std::unique_ptr<SerialLink> CreateWin32SerialLink(const std::string& port) {
    return std::unique_ptr<SerialLink>(new Win32SerialLink(port));
}

std::unique_ptr<RadioSession> RadioSession::Open(const std::string& port, std::string& err) {
    return OpenWithLink(CreateWin32SerialLink(port), err);
}

}  // namespace opendepot

