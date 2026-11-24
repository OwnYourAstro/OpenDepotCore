// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "opendepot/core/adk.h"

namespace opendepot {

// How strongly a decoded field is established. "verified" means it round-trips
// through the hardware-tested radio; "inferred" means the record was mapped to
// a CPS section from structure/correlation but the individual fields are not
// proven; "unknown" means the bytes are exposed raw only.
enum class Confidence { verified, inferred, unknown };
const char* ConfidenceName(Confidence c);

// A single conventional channel in a zone, decoded from a 35-byte entry in a
// 0x0FFD page. The entry layout is verified for name/RX/TX/squelch/flags; the
// remaining bytes are exposed raw.
struct ChannelModel {
    int zone = 0;                // 0-based zone index
    int index = 0;               // 0-based index within the zone
    uint16_t channelId = 0;      // entry[2..3] == 0x0FFB/0x0FCD instance
    uint16_t entryType = 0x0FFB; // entry[0..1]

    std::string name;
    uint32_t rxWord = 0;
    uint32_t txWord = 0;
    uint32_t altWord = 0;
    double rxMHz = 0.0;
    double txMHz = 0.0;
    uint8_t squelchRx = 0;
    uint8_t squelchTx = 0;
    uint8_t flags = 0;
    bool simplex = false;
    std::array<uint8_t, 35> entry{};

    // 0x0FFB conventional-personality record for this channel.
    bool hasPersonality = false;
    std::array<uint8_t, 75> personality{};
    int personalitySize = 0;

    // 0x0FCD decoded ASTRO data record for this channel.
    bool hasAstro = false;
    std::array<uint8_t, 10> astro{};
    int astroSize = 0;
};

struct ZoneModel {
    int index = 0;
    std::string name;
    std::vector<ChannelModel> channels;
};

// A name table such as the Status or Message alias list (0x0059 / 0x005B).
struct AliasListModel {
    std::string name;
    uint16_t tag = 0;
    std::vector<std::string> entries;
    int capacity = 0;
    Confidence confidence = Confidence::verified;
};

// A named list record (0x004C/0x004D/0x004E/0x004F/0x006C/0x006E).
struct NamedListModel {
    std::string label;
    uint16_t tag = 0;
    std::vector<std::string> entries;
    Confidence confidence = Confidence::inferred;
};

// A scan list membership table (0x0FA1 instances).
struct ScanListModel {
    int index = 0;
    std::vector<uint8_t> raw;
    Confidence confidence = Confidence::inferred;
};

// Trunking System / General (0x0FE6).
struct TrunkingSystemInfo {
    bool present = false;
    uint32_t wacn = 0;           // payload 0x1A, 24-bit big-endian
    uint16_t homeSystemId = 0;   // payload 0x1E, big-endian
};

// One node of the CPS-style tree. Records lists the codeplug records that back
// this section, so an expert view can open them directly.
struct CodeplugSection {
    std::string id;
    std::string group;
    std::string name;
    Confidence confidence = Confidence::unknown;
    std::vector<uint32_t> records;
};

struct RecordRef {
    uint16_t tag = 0;
    uint16_t inst = 0;   // 0xFFFF = tag reference with no specific instance
    int offset = -1;     // byte offset of the tag in the payload
};

// Every record in the codeplug, represented generically. Fields that are not
// decoded are still available as raw payload bytes.
struct RecordNode {
    uint32_t id = 0;
    uint16_t tag = 0;
    uint16_t inst = 0;
    uint16_t type = 0;
    int length = 0;
    std::string section;   // CodeplugSection::id
    std::string family;    // identity, name_table, name, list_page, references, blob
    std::string name;      // decoded name when the record carries one
    std::vector<std::string> names;
    std::vector<RecordRef> references;
    std::vector<std::string> strings;
    std::vector<uint8_t> payload;
};

class CodeplugModel {
public:
    explicit CodeplugModel(Codeplug& cp);

    // Decode the codeplug into zones/channels/lists/sections.
    bool Reload(std::string& err);

    Codeplug& codeplug() { return cp_; }
    const Codeplug& codeplug() const { return cp_; }

    std::string Serial() const;
    std::string Model() const;
    std::string Flashcode() const;
    int RecordCount() const;

    const std::vector<ZoneModel>& Zones() const { return zones_; }
    const std::vector<AliasListModel>& AliasLists() const { return aliasLists_; }
    const std::vector<NamedListModel>& NamedLists() const { return namedLists_; }
    const std::vector<ScanListModel>& ScanLists() const { return scanLists_; }
    const TrunkingSystemInfo& TrunkingSystem() const { return trunking_; }
    const std::vector<CodeplugSection>& Sections() const { return sections_; }
    const std::vector<RecordNode>& RecordNodes() const { return recordNodes_; }
    std::string SectionForTag(uint16_t tag) const;

    const ZoneModel* Zone(int zone) const;
    const ChannelModel* Channel(int zone, int channel) const;

    // Typed edits. Each one updates the underlying record and refreshes the
    // decoded model on success.
    bool SetZoneName(int zone, const std::string& name);
    bool SetChannelName(int zone, int channel, const std::string& name);
    // Adds a channel to an existing zone, cloning the last channel/personality
    // defaults and updating the zone page and channel-name table.
    bool AddChannel(int zone, const std::string& name, double rxMHz, double txMHz,
                    int* newIndex = nullptr);
    // Removes a channel and its personality/astro records, preserving the
    // remaining channel/page/name graph. The last channel in a zone is kept.
    bool RemoveChannel(int zone, int channel);
    bool SetChannelFrequency(int zone, int channel, double rxMHz, double txMHz);
    bool SetChannelSquelch(int zone, int channel, int rx, int tx);
    bool SetChannelFlags(int zone, int channel, int flags);
    bool SetChannelEntryByte(int zone, int channel, int offset, uint8_t value);
    bool SetPersonalityByte(int zone, int channel, int offset, uint8_t value);
    bool SetAstroByte(int zone, int channel, int offset, uint8_t value);
    bool SetAliasEntry(int list, int index, const std::string& name);
    bool SetNamedListEntry(int list, int index, const std::string& name);
    bool SetWacn(uint32_t wacn);
    bool SetHomeSystemId(uint16_t id);

    // Expert escape hatch: replace a whole record payload (CRC is recomputed on
    // save and the decoded model is refreshed).
    bool SetRecordPayload(uint16_t tag, uint16_t inst, const std::vector<uint8_t>& payload);

    bool Save(const std::filesystem::path& path, std::string& err) const;

private:
    bool LocateChannel(int zone, int channel, uint16_t& pageTag, uint16_t& pageInst,
                       size_t& entryOffset);
    void Decode();

    Codeplug& cp_;
    std::vector<ZoneModel> zones_;
    std::vector<AliasListModel> aliasLists_;
    std::vector<NamedListModel> namedLists_;
    std::vector<ScanListModel> scanLists_;
    TrunkingSystemInfo trunking_;
    std::vector<CodeplugSection> sections_;
    std::vector<RecordNode> recordNodes_;
};

}  // namespace opendepot
