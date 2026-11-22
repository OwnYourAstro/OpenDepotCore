// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "opendepot/core/adk.h"
#include "opendepot/core/model.h"

namespace {

int Fail(const char* what) {
    std::printf("model test failed: %s\n", what);
    return 1;
}

bool Near(double a, double b) { return std::fabs(a - b) < 1e-6; }

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: model_test <fixture.cpg>\n");
        return 2;
    }
    opendepot::Codeplug cp;
    if (!opendepot::LoadCodeplug(argv[1], cp)) {
        std::printf("load failed: %s\n", cp.error.c_str());
        return 2;
    }
    opendepot::CodeplugModel model(cp);
    std::string err;
    if (!model.Reload(err)) {
        std::printf("model reload failed: %s\n", err.c_str());
        return 1;
    }
    if (model.Zones().empty()) return Fail("no zones decoded");
    if (model.Zones()[0].channels.empty()) return Fail("no channels decoded");
    if (model.AliasLists().size() < 2) return Fail("alias lists not decoded");
    if (model.RecordNodes().size() != cp.records.size())
        return Fail("record nodes do not cover every record");
    bool sawStatus = false, sawScan = false, sawMessage = false;
    for (const auto& n : model.RecordNodes()) {
        if (n.section.empty()) return Fail("record node has no section");
        if (n.tag == 0x0059 && n.family == "name_table" && n.names.size() == 16) sawStatus = true;
        if (n.tag == 0x0FA1 && n.family == "list_page") sawScan = true;
        if (n.tag == 0x0FAA && n.family == "name" && n.name == "MESSAGE 1") sawMessage = true;
    }
    if (!sawStatus || !sawScan || !sawMessage) return Fail("record family decoding incomplete");

    const std::string originalZone = model.Zones()[0].name;
    if (!model.SetZoneName(0, "ZONE TEST")) return Fail("SetZoneName");
    if (!model.SetChannelName(0, 0, "E2E CHAN")) return Fail("SetChannelName");
    if (!model.SetChannelFrequency(0, 0, 810.0, 855.0)) return Fail("SetChannelFrequency");
    if (!model.SetChannelSquelch(0, 0, 3, 5)) return Fail("SetChannelSquelch");
    if (!model.SetChannelFlags(0, 0, 0x40)) return Fail("SetChannelFlags");
    if (!model.SetAliasEntry(0, 0, "STATUS OK")) return Fail("SetAliasEntry");
    if (!model.SetNamedListEntry(0, 0, "LIST OK")) return Fail("SetNamedListEntry");
    if (!model.SetPersonalityByte(0, 0, 0x40, 0x27)) return Fail("SetPersonalityByte");

    const auto out = std::filesystem::temp_directory_path() / "opendepot-model-test.cpg";
    std::error_code ec;
    std::filesystem::remove(out, ec);
    if (!model.Save(out, err)) {
        std::printf("save failed: %s\n", err.c_str());
        return 1;
    }

    opendepot::Codeplug saved;
    if (!opendepot::LoadCodeplug(out, saved)) {
        std::printf("reload failed: %s\n", saved.error.c_str());
        return 1;
    }
    for (const auto& r : saved.records)
        if (!r.verified) return Fail("a saved record did not verify");

    opendepot::CodeplugModel check(saved);
    if (!check.Reload(err)) return Fail("check reload");
    const auto* z = check.Zone(0);
    const auto* ch = check.Channel(0, 0);
    if (!z || !ch) return Fail("zone/channel missing after save");
    if (z->name != "ZONE TEST") return Fail("zone name did not persist");
    if (ch->name != "E2E CHAN") return Fail("channel name did not persist");
    if (!Near(ch->rxMHz, 810.0) || !Near(ch->txMHz, 855.0))
        return Fail("frequency did not persist");
    if (ch->squelchRx != 3 || ch->squelchTx != 5) return Fail("squelch did not persist");
    if (ch->flags != 0x40) return Fail("flags did not persist");
    if (check.AliasLists()[0].entries[0] != "STATUS OK") return Fail("alias did not persist");
    if (check.NamedLists()[0].entries[0] != "LIST OK") return Fail("named list did not persist");
    if (ch->personality[0x40] != 0x27) return Fail("personality byte did not persist");

    std::filesystem::remove(out, ec);
    std::printf("model ok (original zone '%s')\n", originalZone.c_str());
    return 0;
}
