// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "opendepot/core/adk.h"

namespace opendepot {

struct RadioInfo {
    std::string banner;
    std::string host;
    std::string serial;
    std::string model;
};

struct RadioReadRequest {
    uint16_t part = 0;
    uint16_t tag = 0;
    uint16_t inst = 0;
    uint16_t len = 0;
};

// Native PPP/RCMP transport. The protocol is portable; the serial byte-stream
// backend is currently Windows-only.
class RadioSession {
public:
    ~RadioSession();

    static std::unique_ptr<RadioSession> Open(const std::string& port, std::string& err);

    bool Info(RadioInfo& out, std::string& err);

    // Reads each requested (partition, tag, instance). Empty records are
    // omitted. Returns false only for transport/auth failures.
    bool ReadRequests(const std::vector<RadioReadRequest>& reqs, std::vector<Record>& out,
                      std::string& err);

    // Writes the records (identity first, 0x0047 last), then reorg + end.
    bool WriteRecords(const std::vector<Record>& recs, std::string& err);

    void Close();

private:
    RadioSession();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace opendepot
