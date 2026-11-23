// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include "opendepot/core/block_map.h"

namespace opendepot {
namespace {

// Provenance: live x32dbg dump of ASTRO 25 Portable Depot (model
// H46UCF9PW6BN), read through CcgIshBlkLayout::GetBlkID()/GetIshType().
// The block "name" is the CPS block-map label cross-referenced from
// docs/codeplug_block_map.json by block type. See
// docs/CODEPLUG_BLOCK_TYPES_VERIFIED.md.
const BlockTagInfo kBlocks[] = {
    {0x0031, 0x000A, "Radio Wide"},
    {0x0002, 0x000B, "Display & Menu"},
    {0x0012, 0x000C, "Dynamic Radio Block"},
    {0x00B0, 0x0014, "ISH Conventional Switch Block"},
    {0x0011, 0x0016, "Cnv Menu Item Blk"},
    {0x00BA, 0x0018, "Global Button Config List"},
    {0x00BB, 0x0019, "Conventional Button Config List"},
    {0x0038, 0x001B, "Hardware Button Configuration Block"},
    {0x00AE, 0x0020, "Ish Portable Conventional Button Block"},
    {0x00B4, 0x0021, "HHCH Conventional Block"},
    {0x0040, 0x0026, "Conventional Configuration"},
    {0x0029, 0x0027, "Test Mode Personality Block"},
    {0x00C7, 0x0028, "Test Mode Personalitty Float Data Block"},
    {0x002A, 0x0029, "Test Channel Table Block"},
    {0x00A6, 0x002A, "Astro Peripheral Data Block"},
    {0x00A1, 0x002B, "ASTRO OTAR Radio Block"},
    {0x007B, 0x002C, "Astro Radio Block"},
    {0x0084, 0x002D, "Astro dynamic Block"},
    {0x0082, 0x002E, "OTACR block vector"},
    {0x0095, 0x002F, "Factory Overrides DSP Clock Shifts Block Pointer"},
    {0x0094, 0x0030, "Factory Overrides Host Clock Shifts Block Pointer"},
    {0x00AB, 0x0031, "FGU RxSynthesizer Block Pointer"},
    {0x0096, 0x0032, "Factory Overrides Secure Clock Shifts Block Pointer"},
    {0x00A3, 0x0033, "FGU Tx Synthesizer Block Pointer"},
    {0x00A0, 0x0037, "CKR Key Number Block"},
    {0x0085, 0x0038, "Hardware Key Name Block"},
    {0x00E5, 0x0040, "Test Personality Cnv Channel Data"},
    {0x0030, 0x0047, "External Radio Blk"},
    {0x0007, 0x0048, "ID Disp List Vector"},
    {0x00CF, 0x0049, "Phone Number List Distributed Header"},
    {0x00CE, 0x004A, "Phone Number Text List Distributed Header"},
    {0x00D1, 0x004B, "Second LO Injection Default Freq Blk"},
    {0x00D2, 0x004C, "Astro System Call List Alias List"},
    {0x00DA, 0x0052, "Rx Frequency Split Block"},
    {0x00DB, 0x0053, "Tx Frequency Split Block"},
    {0x00DC, 0x0054, "NAT Entry"},
    {0x0066, 0x0055, "OTAR Radio Block"},
    {0x0034, 0x0057, "DTMF Codes List Block"},
    {0x00DE, 0x0058, "Mdc Status Alias Distributed List Header"},
    {0x0006, 0x0059, "Sts Alias Text Block"},
    {0x00DF, 0x005A, "Mdc Message Alias Distributed List Header"},
    {0x0009, 0x005B, "Msg Alias Text blk"},
    {0x00F1, 0x005E, ""},
    {0x0119, 0x0070, "Data User List Distributed Header"},
    {0x011A, 0x0071, "Quick Text Message Distributed Header"},
    {0x012A, 0x0075, "Trunking APCO Inter WACN Dyn Chan Id Distributed List Header"},
    {0x012B, 0x0F9F, "APCO Trunking Call WACN System ID List Block"},
    {0x012C, 0x0FA0, "APCO Trunking Call WACN System ID List Distributed Header"},
    {0x0128, 0x0FA3, "Conventional Personality MVS Channel Alias Text Header Block"},
    {0x0126, 0x0FA5, "Conventional Personality MVS Channel Options Header Block"},
    {0x011C, 0x0FAA, "Quick Text Message Entry"},
    {0x011B, 0x0FAB, "Data User List Entry"},
    {0x00F0, 0x0FBF, ""},
    {0x0008, 0x0FC1, "Message Alias Table"},
    {0x0005, 0x0FC2, "Status Alias Table"},
    {0x00EB, 0x0FC4, "Secure KMF Profile List LED"},
    {0x00EA, 0x0FC5, "Secure KMF Profile List Header"},
    {0x000C, 0x0FCC, "MDC System"},
    {0x00D5, 0x0FCD, "Conventional Personality Floating Block"},
    {0x0039, 0x0FCE, "Phone List Text Block"},
    {0x0035, 0x0FCF, "Phone Number Table"},
    {0x0091, 0x0FF2, "Astro Talkgroup ID Block"},
    {0x00C6, 0x0FF3, "Astro Talkgroup ID List Distributed Header"},
    {0x0080, 0x0FF4, "Astro Call ID Text Block"},
    {0x00C2, 0x0FF5, "Astro System Call ID Text List Distributed Header"},
    {0x007F, 0x0FF6, "List"},
    {0x00C1, 0x0FF7, "Astro System Call ID List Distributed Header"},
    {0x0092, 0x0FF8, "Astro Talkgroup Alias Text Block"},
    {0x0081, 0x0FF9, "Astro Emergency Block"},
    {0x007D, 0x0FFA, "Astro Systems Block"},
    {0x0004, 0x0FFB, "Conventional Personality"},
    {0x00B1, 0x0FFC, "Zone Channel Assignment Dist Header Block"},
    {0x002D, 0x0FFD, "Zone Channel Assignment"},
    {0x00B2, 0x0FFE, "Channel Text Header Block"},
    {0x00AD, 0x0FFF, "Channel Text Block"},
    {0x00EF, 0x12BE, "Voice File Data Distributed List Block"},
    {0x00EE, 0x12BF, "Voice File Data Distributed List Header Block"},
};

}  // namespace

const BlockTagInfo* BlockTagCatalog(std::size_t& count) {
    count = sizeof(kBlocks) / sizeof(kBlocks[0]);
    return kBlocks;
}

const BlockTagInfo* FindBlockByTag(uint16_t tag) {
    for (const auto& b : kBlocks)
        if (b.tag == tag) return &b;
    return nullptr;
}

const BlockTagInfo* FindBlockByType(uint16_t type) {
    for (const auto& b : kBlocks)
        if (b.type == type) return &b;
    return nullptr;
}

const char* BlockNameForTag(uint16_t tag) {
    const BlockTagInfo* b = FindBlockByTag(tag);
    return b ? b->name : "";
}

}  // namespace opendepot
