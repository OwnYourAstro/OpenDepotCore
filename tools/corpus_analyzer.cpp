// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include "opendepot/core/adk.h"
#include "opendepot/core/block_map.h"
#include "opendepot/core/field_catalog.h"
#include "opendepot/core/model.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace opendepot;

namespace {

struct TagAgg {
    std::set<uint16_t> instances;
    uint16_t minLen = 0xFFFF;
    uint16_t maxLen = 0;
    std::set<uint16_t> types;
    std::set<std::string> sections;
};

struct OptionAgg {
    int files = 0;
    std::set<uint16_t> tags;
    std::map<uint16_t, TagAgg> byTag;
};

struct ModelAgg {
    std::map<std::string, OptionAgg> options;
};

std::string HexTag(uint16_t tag) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%04X", tag);
    return b;
}

std::string Json(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default: o += c;
        }
    }
    o += "\"";
    return o;
}

std::vector<std::string> JoinTags(const std::set<uint16_t>& tags) {
    std::vector<std::string> out;
    for (uint16_t t : tags) out.push_back(Json(HexTag(t)));
    return out;
}

void WriteStringArray(std::ostream& out, const std::set<uint16_t>& tags) {
    out << "[";
    bool first = true;
    for (uint16_t tag : tags) {
        if (!first) out << ",";
        first = false;
        out << Json(HexTag(tag));
    }
    out << "]";
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::wcerr << L"usage: corpus-analyzer <codeplug-dir> <out.json>\n";
        return 2;
    }
    const fs::path root(argv[1]);
    const fs::path outPath(argv[2]);

    std::map<uint16_t, size_t> fieldCountByTag;
    size_t fieldCount = 0;
    const FieldDef* fields = FieldCatalog(fieldCount);
    for (size_t i = 0; i < fieldCount; ++i) {
        const auto& f = fields[i];
        if (!f.name || !f.recordTag || f.bits == 0 || f.bits > 32 || f.msb > 7) continue;
        const std::string tagText = f.recordTag;
        if (tagText.size() != 6 || tagText[0] != '0' || tagText[1] != 'x') continue;
        char* end = nullptr;
        const unsigned long value = std::strtoul(tagText.c_str() + 2, &end, 16);
        if (end && *end == '\0' && value <= 0xFFFF)
            ++fieldCountByTag[static_cast<uint16_t>(value)];
    }

    std::map<std::string, ModelAgg> models;
    std::vector<std::string> failures;
    size_t files = 0;
    size_t roundtripOk = 0;
    size_t roundtripFailed = 0;

    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const fs::path p = entry.path();
        if (p.extension() != L".cpg") continue;
        ++files;
        const std::string stem = p.stem().string();
        std::string model = stem;
        std::string option;
        const size_t dash = stem.find('-');
        if (dash != std::string::npos) {
            model = stem.substr(0, dash);
            option = stem.substr(dash + 1);
        }

        Codeplug cp;
        if (!LoadCodeplug(p, cp)) {
            failures.push_back(p.string() + ": " + cp.error);
            continue;
        }
        CodeplugModel decoded(cp);
        std::string err;
        if (!decoded.Reload(err)) {
            failures.push_back(p.string() + ": " + err);
            continue;
        }

        {
            const fs::path temp = fs::temp_directory_path() / "opendepot-corpus-roundtrip.cpg";
            std::string saveErr;
            if (!SaveCodeplug(temp, cp, saveErr)) {
                ++roundtripFailed;
                failures.push_back(p.string() + ": roundtrip save: " + saveErr);
            } else {
                Codeplug rt;
                if (!LoadCodeplug(temp, rt)) {
                    ++roundtripFailed;
                    failures.push_back(p.string() + ": roundtrip load: " + rt.error);
                } else {
                    auto key = [](const Record& r) {
                        return (uint64_t(r.type) << 32) | r.id;
                    };
                    std::map<uint64_t, std::vector<uint8_t>> before, after;
                    for (const auto& r : cp.records) before[key(r)] = r.raw;
                    for (const auto& r : rt.records) after[key(r)] = r.raw;
                    if (before == after) {
                        ++roundtripOk;
                    } else {
                        ++roundtripFailed;
                        failures.push_back(p.string() + ": roundtrip record mismatch");
                    }
                }
            }
            std::error_code ec;
            fs::remove(temp, ec);
        }

        auto& opt = models[model].options[option];
        ++opt.files;
        std::map<uint16_t, const CodeplugSection*> sectionByTag;
        for (const auto& s : decoded.Sections())
            for (uint32_t id : s.records) sectionByTag[uint16_t(id >> 16)] = &s;

        for (const auto& node : decoded.RecordNodes()) {
            opt.tags.insert(node.tag);
            auto& agg = opt.byTag[node.tag];
            agg.instances.insert(node.inst);
            const uint16_t len = static_cast<uint16_t>(node.payload.size());
            if (len < agg.minLen) agg.minLen = len;
            if (len > agg.maxLen) agg.maxLen = len;
            agg.types.insert(node.type);
            auto sit = sectionByTag.find(node.tag);
            if (sit != sectionByTag.end()) agg.sections.insert(sit->second->id);
        }
    }

    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        std::cerr << "cannot write " << outPath.string() << "\n";
        return 2;
    }
    out << "{\n";
    out << "  \"root\":" << Json(fs::absolute(root).string()) << ",\n";
    out << "  \"file_count\":" << files << ",\n";
    out << "  \"roundtrip_ok\":" << roundtripOk << ",\n";
    out << "  \"roundtrip_failed\":" << roundtripFailed << ",\n";
    out << "  \"failures\":[";
    for (size_t i = 0; i < failures.size(); ++i) {
        if (i) out << ",";
        out << Json(failures[i]);
    }
    out << "],\n";
    out << "  \"models\":{\n";
    bool firstModel = true;
    for (const auto& mit : models) {
        if (!firstModel) out << ",\n";
        firstModel = false;
        out << "    " << Json(mit.first) << ":{\n";
        std::vector<ReadRequest> readSet;
        ModelReadRequests(mit.first, readSet);
        out << "      \"readSet\":[";
        for (size_t ri = 0; ri < readSet.size(); ++ri) {
            if (ri) out << ",";
            out << "{\"part\":" << readSet[ri].part << ",\"tag\":" << Json(HexTag(readSet[ri].tag))
                << ",\"inst\":" << readSet[ri].inst << ",\"len\":" << readSet[ri].len << "}";
        }
        out << "],\n";
        out << "      \"options\":{\n";
        bool firstOpt = true;
        std::set<uint16_t> intersection;
        std::set<uint16_t> unionTags;
        bool firstSet = true;
        for (const auto& oit : mit.second.options) {
            if (!firstOpt) out << ",\n";
            firstOpt = false;
            out << "        " << Json(oit.first) << ":{\n";
            out << "          \"files\":" << oit.second.files << ",\n";
            out << "          \"tags\":";
            WriteStringArray(out, oit.second.tags);
            out << ",\n          \"records\":{\n";
            bool firstTag = true;
            for (const auto& tit : oit.second.byTag) {
                if (!firstTag) out << ",\n";
                firstTag = false;
                const TagAgg& a = tit.second;
                out << "            " << Json(HexTag(tit.first)) << ":{";
                out << "\"instances\":";
                out << "[";
                bool firstInst = true;
                for (uint16_t inst : a.instances) {
                    if (!firstInst) out << ",";
                    firstInst = false;
                    out << inst;
                }
                out << "],\"minLen\":" << a.minLen << ",\"maxLen\":" << a.maxLen;
                out << ",\"types\":[";
                bool firstType = true;
                for (uint16_t t : a.types) {
                    if (!firstType) out << ",";
                    firstType = false;
                    out << t;
                }
                out << "],\"sections\":[";
                bool firstSec = true;
                for (const auto& s : a.sections) {
                    if (!firstSec) out << ",";
                    firstSec = false;
                    out << Json(s);
                }
                out << "],\"fieldCount\":" << fieldCountByTag[tit.first];
                out << ",\"block\":" << Json(BlockNameForTag(tit.first));
                out << "}";
            }
            out << "\n          }\n";
            out << "        }";

            if (firstSet) {
                intersection = oit.second.tags;
                firstSet = false;
            } else {
                std::set<uint16_t> next;
                std::set_intersection(intersection.begin(), intersection.end(),
                                      oit.second.tags.begin(), oit.second.tags.end(),
                                      std::inserter(next, next.end()));
                intersection = std::move(next);
            }
            unionTags.insert(oit.second.tags.begin(), oit.second.tags.end());
        }
        out << "\n      },\n";
        out << "      \"baseTags\":";
        WriteStringArray(out, intersection);
        out << ",\n      \"unionTags\":";
        WriteStringArray(out, unionTags);
        out << "\n    }";
    }
    out << "\n  }\n}\n";
    std::cout << "files=" << files << " models=" << models.size()
              << " failures=" << failures.size() << " roundtrip_ok=" << roundtripOk
              << " roundtrip_failed=" << roundtripFailed << "\n";
    return 0;
}





