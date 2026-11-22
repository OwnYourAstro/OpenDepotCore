// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include "opendepot/core/model.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace opendepot {
namespace {

uint16_t BE16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }

uint32_t BE32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

void PutBE16(std::vector<uint8_t>& v, size_t off, uint16_t x) {
    v[off] = uint8_t(x >> 8);
    v[off + 1] = uint8_t(x);
}

std::string AsciiField(const std::vector<uint8_t>& p, size_t off, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && off + n < p.size() && p[off + n] >= 0x20 && p[off + n] < 0x7F) ++n;
    while (n > 0 && p[off + n - 1] == ' ') --n;  // names are space padded
    return std::string(reinterpret_cast<const char*>(p.data() + off), n);
}

void WriteName(uint8_t* dst, size_t width, const std::string& name) {
    std::memset(dst, 0, width);
    std::memcpy(dst, name.data(), std::min<size_t>(name.size(), width - 1));
}

bool HasTag(const Codeplug& cp, uint16_t tag) {
    for (const auto& r : cp.records)
        if (uint16_t(r.id >> 16) == tag) return true;
    return false;
}

// A 17-byte name table: u16 entrySize, u16 capacity, u16 count, then entries.
bool ParseNameTable(const Codeplug& cp, uint16_t tag, std::vector<std::string>& out,
                    int& capacity) {
    out.clear();
    capacity = 0;
    const Record* r = FindRecord(cp, tag, 0);
    if (!r) return false;
    auto p = RecordPayload(*r);
    if (p.size() < 8) return false;
    const int entrySize = BE16(&p[2]);
    capacity = BE16(&p[4]);
    const int count = BE16(&p[6]);
    if (entrySize <= 0) return false;
    for (int i = 0; i < count; ++i) {
        const size_t off = 8 + size_t(i) * size_t(entrySize);
        if (off + size_t(entrySize) > p.size()) break;
        out.push_back(AsciiField(p, off, std::min<size_t>(16, size_t(entrySize) - 1)));
    }
    return true;
}

std::vector<std::string> StringsOf(const std::vector<uint8_t>& p, size_t minlen) {
    std::vector<std::string> out;
    std::string cur;
    for (uint8_t b : p) {
        if (b >= 0x20 && b < 0x7F) {
            cur.push_back(char(b));
        } else {
            if (cur.size() >= minlen) out.push_back(cur);
            cur.clear();
        }
    }
    if (cur.size() >= minlen) out.push_back(cur);
    return out;
}

struct SectionDef {
    const char* id;
    const char* group;
    const char* name;
    Confidence confidence;
    std::vector<uint16_t> tags;
};

const std::vector<SectionDef>& SectionDefs() {
    static const std::vector<SectionDef> defs = {
        {"radio_information", "Digital", "Radio Information", Confidence::verified,
         {0x1004, 0x1005, 0x1006}},
        {"radio_wide", "Radio Configuration", "Radio Wide", Confidence::inferred,
         {0x000A, 0x000B, 0x000C, 0x0014, 0x0016, 0x0018, 0x0019, 0x0020, 0x0028, 0x002A,
          0x002C, 0x002D, 0x0048, 0x0058, 0x0070, 0x0071, 0x0FAB}},
        {"test_channel_list", "Radio Configuration", "Test Channel List", Confidence::unknown,
         {0x0FF0, 0x0FF1}},
        {"narrow_band_list", "Radio Configuration",
         "Narrow Bandwidth 12.5 kHz Only Frequency Range List", Confidence::inferred, {0x0029}},
        {"vrs_ep", "Radio Configuration", "Radio Wide VRS-EP Mode Steering", Confidence::unknown,
         {0x002F, 0x0030, 0x0031, 0x0032, 0x0033}},
        {"controls", "Radio Configuration", "Controls", Confidence::inferred, {0x001B}},
        {"display_menu", "Radio Configuration", "Display & Menu", Confidence::inferred,
         {0x0026, 0x0027}},
        {"phone", "Radio Configuration", "Phone", Confidence::inferred,
         {0x0049, 0x004A, 0x004B, 0x005A, 0x0FCE, 0x0FCF}},
        {"secure", "Radio Configuration", "Secure", Confidence::unknown, {0x0034, 0x0057}},
        {"trunking_config", "Trunking", "Trunking Configuration", Confidence::inferred,
         {0x0015, 0x0017, 0x001A, 0x0021, 0x0022, 0x0023, 0x0F9F, 0x0FA0}},
        {"trunking_system", "Trunking", "Trunking System", Confidence::inferred,
         {0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F}},
        {"trunking_personality", "Trunking", "Trunking Personality", Confidence::inferred,
         {0x0052, 0x0053, 0x0FA8, 0x0FA9, 0x0FC6, 0x0FD0, 0x0FD1}},
        {"talkgroup", "Trunking", "Talkgroup", Confidence::inferred,
         {0x0FD2, 0x0FD3, 0x0FD4, 0x0FD5, 0x0FD6, 0x0FD7, 0x0FD8, 0x0FD9, 0x0FDA, 0x0FDB,
          0x0FDC, 0x0FDD, 0x0FDE, 0x0FDF, 0x0FE0, 0x0FE1, 0x0FE2, 0x0FE3, 0x0FE4, 0x0FE5,
          0x0FE6, 0x0FE7, 0x0FE8, 0x0FE9, 0x0FEA, 0x0FEB, 0x0FEC, 0x0FED, 0x0FEE, 0x0FEF}},
        {"conventional_config", "Conventional", "Conventional Configuration",
         Confidence::inferred, {0x0040, 0x0050}},
        {"mpl", "Conventional", "MPL", Confidence::inferred,
         {0x0064, 0x0065, 0x0FBC, 0x0FBD}},
        {"conventional_personality", "Conventional", "Conventional Personality",
         Confidence::inferred, {0x0FBE, 0x0FCD, 0x0FFA, 0x0FFB}},
        {"message_alias", "Conventional", "Message Alias List", Confidence::verified,
         {0x005B, 0x0FAA}},
        {"status_alias", "Conventional", "Status Alias List", Confidence::verified, {0x0059}},
        {"mdc", "Conventional", "MDC", Confidence::inferred, {0x0FB4, 0x0FB5}},
        {"quik_call", "Conventional", "Quik-Call II", Confidence::inferred, {0x0FCB, 0x0FCC}},
        {"dtmf", "Conventional", "DTMF", Confidence::inferred,
         {0x0FC1, 0x0FC2, 0x0FC7, 0x0FC8, 0x0FC9, 0x0FCA}},
        {"singletone", "Conventional", "Singletone", Confidence::inferred, {0x0FB2, 0x0FB3}},
        {"astro_systems", "Conventional", "ASTRO Systems", Confidence::inferred,
         {0x0FAC, 0x0FAD, 0x0FAE, 0x0FAF, 0x0FB0, 0x0FB1, 0x0FF2, 0x0FF3, 0x0FF4, 0x0FF5,
          0x0FF6, 0x0FF7, 0x0FF8, 0x0FF9}},
        {"scan_config", "Scan", "Scan Configuration", Confidence::inferred,
         {0x0054, 0x005F, 0x0073, 0x0074, 0x0075, 0x0FA6}},
        {"scan_list", "Scan", "Scan List", Confidence::inferred,
         {0x004C, 0x004D, 0x004E, 0x004F, 0x006C, 0x006E, 0x0FA1}},
        {"zone_channel_assignment", "Zone Channel Assignment", "Zone Channel Assignment - 1",
         Confidence::verified, {0x000D, 0x0FFC, 0x0FFD, 0x0FFE, 0x0FFF}},
        {"codeplug_directory", "Expert", "Codeplug Directory", Confidence::verified, {0x0047}},
        {"passcode", "Expert", "Passcode", Confidence::verified, {0x0051}},
    };
    return defs;
}

}  // namespace

const char* ConfidenceName(Confidence c) {
    switch (c) {
        case Confidence::verified: return "verified";
        case Confidence::inferred: return "inferred";
        default: return "unknown";
    }
}

CodeplugModel::CodeplugModel(Codeplug& cp) : cp_(cp) {}

std::string CodeplugModel::Serial() const { return cp_.header.serial; }
std::string CodeplugModel::Model() const { return cp_.header.model; }
std::string CodeplugModel::Flashcode() const { return GetFlashcode(cp_); }
int CodeplugModel::RecordCount() const { return int(cp_.records.size()); }

const ZoneModel* CodeplugModel::Zone(int zone) const {
    if (zone < 0 || zone >= int(zones_.size())) return nullptr;
    return &zones_[zone];
}

const ChannelModel* CodeplugModel::Channel(int zone, int channel) const {
    const ZoneModel* z = Zone(zone);
    if (!z || channel < 0 || channel >= int(z->channels.size())) return nullptr;
    return &z->channels[channel];
}

bool CodeplugModel::Reload(std::string& err) {
    err.clear();
    if (cp_.records.empty()) {
        err = "codeplug has no records";
        return false;
    }
    Decode();
    return true;
}

void CodeplugModel::Decode() {
    zones_.clear();
    aliasLists_.clear();
    namedLists_.clear();
    scanLists_.clear();
    sections_.clear();
    recordNodes_.clear();

    // ---- Every record ------------------------------------------------------
    for (const auto& r : cp_.records) {
        RecordNode n;
        n.id = r.id;
        n.tag = uint16_t(r.id >> 16);
        n.inst = uint16_t(r.id & 0xFFFF);
        n.type = r.type;
        n.payload = RecordPayload(r);
        n.length = int(n.payload.size());
        const auto& p = n.payload;

        if (n.tag == 0x1004 || n.tag == 0x1005 || n.tag == 0x1006) n.family = "identity";
        if (p.size() >= 8 && BE16(&p[2]) == 17) {
            const int count = BE16(&p[6]);
            if (count >= 0 && count <= 256 && 8 + size_t(count) * 17 <= p.size()) {
                n.family = "name_table";
                for (int i = 0; i < count; ++i)
                    n.names.push_back(AsciiField(p, 8 + size_t(i) * 17, 16));
                if (!n.names.empty()) n.name = n.names[0];
            }
        }
        if (n.family.empty() && n.tag == 0x0FFF && p.size() >= 6) {
            const int count = BE16(&p[4]);
            if (count >= 0 && 6 + size_t(count) * 17 <= p.size()) {
                n.family = "name_table";
                for (int i = 0; i < count; ++i)
                    n.names.push_back(AsciiField(p, 6 + size_t(i) * 17, 16));
                if (!n.names.empty()) n.name = n.names[0];
            }
        }
        if (n.family.empty() && p.size() >= 7 && p[0] == 0 && p[1] == 0 && p[2] == 0 &&
            p[3] == 0 && p[4] == 0 && p[5] == 1) {
            const std::string s = AsciiField(p, 6, p.size() - 6);
            if (!s.empty()) {
                n.family = "name";
                n.name = s;
            }
        }
        if (n.family.empty() && p.size() >= 6) {
            const int count = BE16(&p[4]);
            if (count > 0 && count <= 256) {
                const size_t body = p.size() - 6;
                if (body % size_t(count) == 0) {
                    const size_t entry = body / size_t(count);
                    const bool selfHeader = BE16(&p[0]) == n.tag;
                    const bool channelPage = n.tag == 0x0FFD && entry == 35;
                    const bool scanPage = n.tag == 0x0FA1 && entry == 15;
                    if ((selfHeader || channelPage || scanPage) && entry >= 8 && entry <= 64)
                        n.family = "list_page";
                }
            }
        }
        n.strings = StringsOf(p, 3);
        if (n.family != "name_table" && n.family != "name") {
            std::vector<RecordRef> refs;
            for (size_t off = 0; off + 3 < p.size() && refs.size() < 64; ++off) {
                const uint16_t tag = BE16(&p[off]);
                const uint16_t inst = BE16(&p[off + 2]);
                if (tag == n.tag && inst == n.inst) continue;
                const bool exact = FindRecord(cp_, tag, inst) != nullptr;
                const bool wildcard =
                    inst == 0xFFFF && FindRecord(cp_, tag, 0) != nullptr;
                if (!exact && !wildcard) continue;
                bool dup = false;
                for (const auto& x : refs)
                    if (x.offset == int(off)) dup = true;
                if (!dup) refs.push_back({tag, inst, int(off)});
            }
            if (!refs.empty()) {
                n.references = std::move(refs);
                if (n.family.empty()) n.family = "references";
            }
        }
        if (n.family.empty()) n.family = "blob";
        recordNodes_.push_back(std::move(n));
    }

    // ---- Zone Channel Assignment -------------------------------------------
    const Record* znames = FindRecord(cp_, 0x000D, 0);
    if (znames) {
        auto zn = RecordPayload(*znames);
        if (zn.size() >= 8) {
            const int zoneCount = BE16(&zn[6]);
            for (int z = 0; z < zoneCount; ++z) {
                ZoneModel zm;
                zm.index = z;
                const size_t nameOff = 8 + size_t(z) * 17;
                if (nameOff + 16 <= zn.size()) zm.name = AsciiField(zn, nameOff, 16);

                std::vector<std::string> chanNames;
                const Record* zref = FindRecord(cp_, 0x0FFE, uint16_t(z));
                if (zref) {
                    auto rp = RecordPayload(*zref);
                    if (rp.size() >= 12) {
                        const uint16_t tag = BE16(&rp[8]);
                        const uint16_t inst = BE16(&rp[10]);
                        const Record* names = FindRecord(cp_, tag, inst);
                        if (names) {
                            auto np = RecordPayload(*names);
                            if (np.size() >= 6) {
                                const int n = BE16(&np[4]);
                                for (int i = 0; i < n; ++i) {
                                    const size_t o = 6 + size_t(i) * 17;
                                    if (o + 16 > np.size()) break;
                                    chanNames.push_back(AsciiField(np, o, 16));
                                }
                            }
                        }
                    }
                }

                const Record* cref = FindRecord(cp_, 0x0FFC, uint16_t(z));
                if (cref) {
                    auto rp = RecordPayload(*cref);
                    if (rp.size() >= 12) {
                        const uint16_t tag = BE16(&rp[8]);
                        const uint16_t sub = BE16(&rp[10]);
                        for (uint16_t page = sub; page <= uint16_t(sub + 1); ++page) {
                            const Record* pr = FindRecord(cp_, tag, page);
                            if (!pr) continue;
                            auto pp = RecordPayload(*pr);
                            if (pp.size() < 6) continue;
                            const int n = BE16(&pp[4]);
                            for (int j = 0; j < n; ++j) {
                                const size_t o = 6 + size_t(j) * 35;
                                if (o + 35 > pp.size()) break;
                                if (BE16(&pp[o]) != 0x0FFB) continue;
                                ChannelModel ch;
                                ch.zone = z;
                                ch.index = int(zm.channels.size());
                                ch.entryType = BE16(&pp[o]);
                                ch.channelId = BE16(&pp[o + 2]);
                                std::copy(pp.begin() + static_cast<std::ptrdiff_t>(o),
                                          pp.begin() + static_cast<std::ptrdiff_t>(o + 35),
                                          ch.entry.begin());
                                ch.rxWord = BE32(&pp[o + 12]);
                                ch.txWord = BE32(&pp[o + 16]);
                                ch.altWord = BE32(&pp[o + 20]);
                                ch.rxMHz = WordToMHz(ch.rxWord);
                                ch.txMHz = WordToMHz(ch.txWord);
                                ch.squelchRx = pp[o + 24];
                                ch.squelchTx = pp[o + 26];
                                ch.flags = pp[o + 27];
                                ch.simplex = (ch.flags & 0x80) != 0;

                                const Record* pers = FindRecord(cp_, 0x0FFB, ch.channelId);
                                if (pers) {
                                    auto rp2 = RecordPayload(*pers);
                                    ch.hasPersonality = true;
                                    ch.personalitySize = int(std::min<size_t>(rp2.size(), 75));
                                    std::copy(rp2.begin(), rp2.begin() + ch.personalitySize,
                                              ch.personality.begin());
                                }
                                const Record* astro = FindRecord(cp_, 0x0FCD, ch.channelId);
                                if (astro) {
                                    auto rp2 = RecordPayload(*astro);
                                    ch.hasAstro = true;
                                    ch.astroSize = int(std::min<size_t>(rp2.size(), 10));
                                    std::copy(rp2.begin(), rp2.begin() + ch.astroSize,
                                              ch.astro.begin());
                                }
                                zm.channels.push_back(std::move(ch));
                            }
                        }
                    }
                }

                for (size_t i = 0; i < zm.channels.size() && i < chanNames.size(); ++i)
                    zm.channels[i].name = chanNames[i];
                zones_.push_back(std::move(zm));
            }
        }
    }

    // ---- Name tables (aliases, lists) --------------------------------------
    struct AliasDef {
        const char* name;
        uint16_t tag;
    };
    static const AliasDef aliases[] = {
        {"Status Alias List", 0x0059},
        {"Message Alias List", 0x005B},
    };
    for (const auto& a : aliases) {
        AliasListModel m;
        m.name = a.name;
        m.tag = a.tag;
        if (ParseNameTable(cp_, a.tag, m.entries, m.capacity)) {
            m.confidence = Confidence::verified;
            aliasLists_.push_back(std::move(m));
        }
    }

    static const uint16_t listTags[] = {0x004C, 0x004D, 0x004E, 0x004F, 0x006C, 0x006E};
    for (uint16_t tag : listTags) {
        NamedListModel m;
        m.tag = tag;
        char label[32];
        std::snprintf(label, sizeof(label), "List 0x%04X", tag);
        m.label = label;
        int capacity = 0;
        if (ParseNameTable(cp_, tag, m.entries, capacity)) {
            m.confidence = Confidence::inferred;
            namedLists_.push_back(std::move(m));
        }
    }

    for (const auto& r : cp_.records) {
        if (uint16_t(r.id >> 16) != 0x0FA1) continue;
        ScanListModel m;
        m.index = int(r.id & 0xFFFF);
        m.raw = RecordPayload(r);
        m.confidence = Confidence::inferred;
        scanLists_.push_back(std::move(m));
    }

    // ---- CPS section tree --------------------------------------------------
    for (const auto& def : SectionDefs()) {
        CodeplugSection s;
        s.id = def.id;
        s.group = def.group;
        s.name = def.name;
        s.confidence = def.confidence;
        for (auto& n : recordNodes_) {
            if (std::find(def.tags.begin(), def.tags.end(), n.tag) != def.tags.end()) {
                n.section = def.id;
                s.records.push_back(n.id);
            }
        }
        if (!s.records.empty()) sections_.push_back(std::move(s));
    }
    std::vector<uint32_t> other;
    for (auto& n : recordNodes_) {
        if (n.section.empty()) {
            n.section = "other";
            other.push_back(n.id);
        }
    }
    if (!other.empty()) {
        CodeplugSection s;
        s.id = "other";
        s.group = "Expert";
        s.name = "Other Records";
        s.confidence = Confidence::unknown;
        s.records = std::move(other);
        sections_.push_back(std::move(s));
    }
}

std::string CodeplugModel::SectionForTag(uint16_t tag) const {
    for (const auto& def : SectionDefs())
        if (std::find(def.tags.begin(), def.tags.end(), tag) != def.tags.end()) return def.id;
    return "other";
}

bool CodeplugModel::LocateChannel(int zone, int channel, uint16_t& pageTag, uint16_t& pageInst,
                                  size_t& entryOffset) {
    const Record* cref = FindRecord(cp_, 0x0FFC, uint16_t(zone));
    if (!cref) return false;
    auto rp = RecordPayload(*cref);
    if (rp.size() < 12) return false;
    pageTag = BE16(&rp[8]);
    const uint16_t sub = BE16(&rp[10]);
    int remaining = channel;
    for (uint16_t page = sub; page <= uint16_t(sub + 1); ++page) {
        const Record* pr = FindRecord(cp_, pageTag, page);
        if (!pr) continue;
        auto pp = RecordPayload(*pr);
        if (pp.size() < 6) continue;
        const int n = BE16(&pp[4]);
        for (int j = 0; j < n; ++j) {
            const size_t o = 6 + size_t(j) * 35;
            if (o + 35 > pp.size()) break;
            if (BE16(&pp[o]) != 0x0FFB) continue;
            if (remaining-- == 0) {
                pageInst = page;
                entryOffset = o;
                return true;
            }
        }
    }
    return false;
}

bool CodeplugModel::SetZoneName(int zone, const std::string& name) {
    if (zone < 0 || zone >= int(zones_.size())) return false;
    if (!opendepot::SetZoneName(cp_, zone + 1, name)) return false;
    std::string err;
    return Reload(err);
}

bool CodeplugModel::SetChannelName(int zone, int channel, const std::string& name) {
    if (!Channel(zone, channel)) return false;
    if (!opendepot::SetChannelName(cp_, zone + 1, channel + 1, name)) return false;
    std::string err;
    return Reload(err);
}

bool CodeplugModel::SetChannelFrequency(int zone, int channel, double rxMHz, double txMHz) {
    if (!Channel(zone, channel)) return false;
    if (!opendepot::SetChannelFreq(cp_, zone + 1, channel + 1, rxMHz, txMHz)) return false;
    std::string err;
    return Reload(err);
}

bool CodeplugModel::SetChannelEntryByte(int zone, int channel, int offset, uint8_t value) {
    if (offset < 0 || offset >= 35 || !Channel(zone, channel)) return false;
    uint16_t pageTag = 0, pageInst = 0;
    size_t entryOffset = 0;
    if (!LocateChannel(zone, channel, pageTag, pageInst, entryOffset)) return false;
    Record* pr = FindRecord(cp_, pageTag, pageInst);
    if (!pr) return false;
    auto p = RecordPayload(*pr);
    p[entryOffset + size_t(offset)] = value;
    if (!SetItemPayload(cp_, pageTag, pageInst, p)) return false;
    std::string err;
    return Reload(err);
}

bool CodeplugModel::SetChannelSquelch(int zone, int channel, int rx, int tx) {
    const ChannelModel* ch = Channel(zone, channel);
    if (!ch) return false;
    if (!SetChannelEntryByte(zone, channel, 24, uint8_t(rx))) return false;
    return SetChannelEntryByte(zone, channel, 26, uint8_t(tx));
}

bool CodeplugModel::SetChannelFlags(int zone, int channel, int flags) {
    return SetChannelEntryByte(zone, channel, 27, uint8_t(flags));
}

bool CodeplugModel::SetPersonalityByte(int zone, int channel, int offset, uint8_t value) {
    const ChannelModel* ch = Channel(zone, channel);
    if (!ch || !ch->hasPersonality || offset < 0 || offset >= ch->personalitySize) return false;
    Record* r = FindRecord(cp_, 0x0FFB, ch->channelId);
    if (!r) return false;
    auto p = RecordPayload(*r);
    p[size_t(offset)] = value;
    if (!SetItemPayload(cp_, 0x0FFB, ch->channelId, p)) return false;
    std::string err;
    return Reload(err);
}

bool CodeplugModel::SetAstroByte(int zone, int channel, int offset, uint8_t value) {
    const ChannelModel* ch = Channel(zone, channel);
    if (!ch || !ch->hasAstro || offset < 0 || offset >= ch->astroSize) return false;
    Record* r = FindRecord(cp_, 0x0FCD, ch->channelId);
    if (!r) return false;
    auto p = RecordPayload(*r);
    p[size_t(offset)] = value;
    if (!SetItemPayload(cp_, 0x0FCD, ch->channelId, p)) return false;
    std::string err;
    return Reload(err);
}

bool CodeplugModel::SetAliasEntry(int list, int index, const std::string& name) {
    if (list < 0 || list >= int(aliasLists_.size())) return false;
    const uint16_t tag = aliasLists_[list].tag;
    Record* r = FindRecord(cp_, tag, 0);
    if (!r) return false;
    auto p = RecordPayload(*r);
    if (p.size() < 8) return false;
    const int entrySize = BE16(&p[2]);
    const int count = BE16(&p[6]);
    if (entrySize < 2 || index < 0 || index >= count) return false;
    const size_t off = 8 + size_t(index) * size_t(entrySize);
    if (off + size_t(entrySize) > p.size()) return false;
    WriteName(p.data() + off, size_t(entrySize), name);
    if (!SetItemPayload(cp_, tag, 0, p)) return false;
    std::string err;
    return Reload(err);
}

bool CodeplugModel::SetNamedListEntry(int list, int index, const std::string& name) {
    if (list < 0 || list >= int(namedLists_.size())) return false;
    const uint16_t tag = namedLists_[list].tag;
    Record* r = FindRecord(cp_, tag, 0);
    if (!r) return false;
    auto p = RecordPayload(*r);
    if (p.size() < 8) return false;
    const int entrySize = BE16(&p[2]);
    const int count = BE16(&p[6]);
    if (entrySize < 2 || index < 0 || index >= count) return false;
    const size_t off = 8 + size_t(index) * size_t(entrySize);
    if (off + size_t(entrySize) > p.size()) return false;
    WriteName(p.data() + off, size_t(entrySize), name);
    if (!SetItemPayload(cp_, tag, 0, p)) return false;
    std::string err;
    return Reload(err);
}

bool CodeplugModel::SetRecordPayload(uint16_t tag, uint16_t inst,
                                     const std::vector<uint8_t>& payload) {
    if (!SetItemPayload(cp_, tag, inst, payload)) return false;
    std::string err;
    return Reload(err);
}

bool CodeplugModel::Save(const std::filesystem::path& path, std::string& err) const {
    return SaveCodeplug(path, cp_, err);
}

}  // namespace opendepot
