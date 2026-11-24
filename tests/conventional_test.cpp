// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include "opendepot/core/adk.h"
#include "opendepot/core/model.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace opendepot;
namespace fs = std::filesystem;

static bool SameRecords(const Codeplug& a, const Codeplug& b) {
    auto key = [](const Record& r) { return (uint64_t(r.type) << 32) | r.id; };
    std::map<uint64_t, std::vector<uint8_t>> am, bm;
    for (const auto& r : a.records) am[key(r)] = r.raw;
    for (const auto& r : b.records) bm[key(r)] = r.raw;
    return am == bm;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: conventional_test <h46-conv-clean.cpg>\n");
        return 2;
    }
    Codeplug cp;
    if (!LoadCodeplug(argv[1], cp)) {
        std::printf("load failed: %s\n", cp.error.c_str());
        return 1;
    }
    if (cp.records.size() != 99) {
        std::printf("expected 99 clean conventional records, got %zu\n", cp.records.size());
        return 1;
    }
    CodeplugModel model(cp);
    std::string err;
    if (!model.Reload(err)) {
        std::printf("model failed: %s\n", err.c_str());
        return 1;
    }
    if (model.Zones().size() != 1 || !model.Zone(0) || model.Zone(0)->channels.size() != 1) {
        std::printf("expected one clean zone with one channel\n");
        return 1;
    }
    if (!ValidateRadioCodeplug(cp, err)) {
        std::printf("clean fixture failed validation: %s\n", err.c_str());
        return 1;
    }

    const fs::path temp = fs::temp_directory_path() / "opendepot-conventional-test.cpg";
    if (!model.Save(temp, err)) {
        std::printf("save failed: %s\n", err.c_str());
        return 1;
    }
    Codeplug restored;
    if (!LoadCodeplug(temp, restored) || !SameRecords(cp, restored)) {
        std::printf("no-op save/reload changed records\n");
        return 1;
    }

    if (!model.SetChannelName(0, 0, "TESTCH") ||
        !model.SetChannelFrequency(0, 0, 851.0, 806.0)) {
        std::printf("channel edit failed\n");
        return 1;
    }
    if (!model.Save(temp, err)) {
        std::printf("edited save failed: %s\n", err.c_str());
        return 1;
    }
    Codeplug edited;
    if (!LoadCodeplug(temp, edited)) {
        std::printf("edited load failed: %s\n", edited.error.c_str());
        return 1;
    }
    CodeplugModel editedModel(edited);
    if (!editedModel.Reload(err)) {
        std::printf("edited model failed: %s\n", err.c_str());
        return 1;
    }
    const ChannelModel* ch = editedModel.Channel(0, 0);
    if (!ch || ch->name != "TESTCH" || std::fabs(ch->rxMHz - 851.0) > 1e-6 ||
        std::fabs(ch->txMHz - 806.0) > 1e-6) {
        std::printf("edited channel did not round-trip\n");
        return 1;
    }
    if (!ValidateRadioCodeplug(edited, err)) {
        std::printf("edited fixture failed validation: %s\n", err.c_str());
        return 1;
    }

    int added = -1;
    if (!editedModel.AddChannel(0, "TEST2", 852.0, 807.0, &added) || added != 1) {
        std::printf("add channel failed\n");
        return 1;
    }
    const ChannelModel* ch2 = editedModel.Channel(0, 1);
    if (!ch2 || ch2->name != "TEST2" || std::fabs(ch2->rxMHz - 852.0) > 1e-6 ||
        std::fabs(ch2->txMHz - 807.0) > 1e-6) {
        std::printf("added channel did not decode\n");
        return 1;
    }
    if (!editedModel.Save(temp, err)) {
        std::printf("added save failed: %s\n", err.c_str());
        return 1;
    }
    Codeplug addedCp;
    if (!LoadCodeplug(temp, addedCp)) {
        std::printf("added load failed: %s\n", addedCp.error.c_str());
        return 1;
    }
    CodeplugModel addedModel(addedCp);
    if (!addedModel.Reload(err)) {
        std::printf("added model failed: %s\n", err.c_str());
        return 1;
    }
    if (addedModel.Zones().size() != 1 || addedModel.Zone(0)->channels.size() != 2) {
        std::printf("added channel count wrong\n");
        return 1;
    }
    if (!ValidateRadioCodeplug(addedCp, err)) {
        std::printf("added fixture failed validation: %s\n", err.c_str());
        return 1;
    }

    for (int i = 2; i < 16; ++i) {
        char name[16];
        std::snprintf(name, sizeof(name), "BULK%02d", i);
        int idx = -1;
        if (!addedModel.AddChannel(0, name, 852.0 + i, 807.0 + i, &idx) || idx != i) {
            std::printf("bulk add failed at %d\n", i);
            return 1;
        }
    }
    if (addedModel.Zone(0)->channels.size() != 16) {
        std::printf("expected 16 channels after page overflow, got %zu\n",
                    addedModel.Zone(0)->channels.size());
        return 1;
    }
    if (!addedModel.Save(temp, err)) {
        std::printf("16-channel save failed: %s\n", err.c_str());
        return 1;
    }
    Codeplug overflowed;
    if (!LoadCodeplug(temp, overflowed)) {
        std::printf("16-channel load failed: %s\n", overflowed.error.c_str());
        return 1;
    }
    CodeplugModel overflowedModel(overflowed);
    if (!overflowedModel.Reload(err) || overflowedModel.Zone(0)->channels.size() != 16) {
        std::printf("16-channel decode failed: %s\n", err.c_str());
        return 1;
    }
    if (!ValidateRadioCodeplug(overflowed, err)) {
        std::printf("16-channel validation failed: %s\n", err.c_str());
        return 1;
    }
    if (overflowed.records.size() != 137) {
        std::printf("expected 137 records for the two-page 16-channel layout, got %zu\n",
                    overflowed.records.size());
        return 1;
    }
    int overflowIndex = -1;
    if (addedModel.AddChannel(0, "OVERFLOW", 852.0, 807.0, &overflowIndex)) {
        std::printf("model capacity was not enforced\n");
        return 1;
    }
    for (uint32_t id : {0x0FB40001u, 0x0FB50001u, 0x0FBE0001u, 0x0FCB0001u,
                        0x0FCC0001u, 0x0FF90001u, 0x0FFA0001u}) {
        bool found = false;
        for (const auto& r : overflowed.records)
            if (r.id == id) found = true;
        if (!found) {
            std::printf("missing page-scoped record 0x%08X\n", id);
            return 1;
        }
    }
    std::error_code ec;
    fs::remove(temp, ec);
    std::printf("conventional ok\n");
    return 0;
}





