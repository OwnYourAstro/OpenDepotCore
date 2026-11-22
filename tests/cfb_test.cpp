// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "container/cfb.h"
#include "opendepot/core/adk.h"

#ifdef _WIN32
#include <objbase.h>
#include <windows.h>
#endif

namespace {

int Fail(const std::string& what) {
    std::printf("FAIL %s\n", what.c_str());
    return 1;
}

std::filesystem::path TempPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

#ifdef _WIN32
bool OleCanRead(const std::filesystem::path& path) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IStorage* root = nullptr;
    HRESULT hr = StgOpenStorage(path.wstring().c_str(), nullptr,
                                STGM_READ | STGM_SHARE_EXCLUSIVE, nullptr, 0, &root);
    std::printf("OLE open %ls -> 0x%08lX\n", path.filename().c_str(),
                static_cast<unsigned long>(hr));
    bool ok = SUCCEEDED(hr);
    if (ok) {
        IStorage* data = nullptr;
        HRESULT h1 = root->OpenStorage(L"DataStg", nullptr,
                                       STGM_READ | STGM_SHARE_EXCLUSIVE, nullptr, 0, &data);
        std::printf("  OpenStorage DataStg -> 0x%08lX\n", static_cast<unsigned long>(h1));
        if (data) data->Release();
        IStream* header = nullptr;
        HRESULT h2 = root->OpenStream(L"AdkFileHeader", nullptr,
                                      STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &header);
        std::printf("  OpenStream AdkFileHeader -> 0x%08lX\n", static_cast<unsigned long>(h2));
        if (header) header->Release();
        ok = SUCCEEDED(h1) && SUCCEEDED(h2);
        root->Release();
    }
    CoUninitialize();
    return ok;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: cfb_test <fixture.cpg>\n");
        return 2;
    }
    const std::filesystem::path fixture = argv[1];
    const std::string stem = fixture.stem().string();

    std::string err;
    opendepot::cfb::File original;
    if (!original.Open(fixture, err)) return Fail("open fixture: " + err);

    std::vector<uint8_t> header;
    if (!original.ReadRoot("AdkFileHeader", header, err)) return Fail("read header: " + err);
    if (header.size() != 800) return Fail("header size");

    std::vector<std::string> names;
    if (!original.ListStreams("DataStg", names, err)) return Fail("list streams: " + err);
    if (names.empty()) return Fail("no streams");

    std::vector<opendepot::cfb::Stream> streams;
    streams.reserve(names.size());
    for (const auto& name : names) {
        std::vector<uint8_t> data;
        if (!original.ReadStorage("DataStg", name, data, err))
            return Fail("read stream " + name + ": " + err);
        streams.push_back({name, std::move(data)});
    }

    const std::filesystem::path rewritten = TempPath("opendepot-cfb-" + stem + ".cpg");
    std::vector<opendepot::cfb::Stream> root{{"AdkFileHeader", header}};
    if (!opendepot::cfb::Write(rewritten, root, "DataStg", streams, err))
        return Fail("write: " + err);

    opendepot::cfb::File round;
    if (!round.Open(rewritten, err)) return Fail("reopen: " + err);

    std::vector<uint8_t> header2;
    if (!round.ReadRoot("AdkFileHeader", header2, err)) return Fail("read header 2: " + err);
    if (header2 != header) return Fail("header mismatch");

    std::vector<std::string> names2;
    if (!round.ListStreams("DataStg", names2, err)) return Fail("list streams 2: " + err);
    std::vector<std::string> sorted_a = names;
    std::vector<std::string> sorted_b = names2;
    std::sort(sorted_a.begin(), sorted_a.end());
    std::sort(sorted_b.begin(), sorted_b.end());
    if (sorted_a != sorted_b) return Fail("stream name set mismatch");

    for (const auto& s : streams) {
        std::vector<uint8_t> back;
        if (!round.ReadStorage("DataStg", s.name, back, err))
            return Fail("read back " + s.name + ": " + err);
        if (back != s.data) return Fail("stream mismatch: " + s.name);
    }

    opendepot::Codeplug codeplug;
    if (!opendepot::LoadCodeplug(fixture, codeplug))
        return Fail("LoadCodeplug: " + codeplug.error);

    const std::filesystem::path saved = TempPath("opendepot-roundtrip-" + stem + ".cpg");
    if (!opendepot::SaveCodeplug(saved, codeplug, err)) return Fail("SaveCodeplug: " + err);

    opendepot::Codeplug back;
    if (!opendepot::LoadCodeplug(saved, back)) return Fail("reload: " + back.error);
    if (back.records.size() != codeplug.records.size()) return Fail("record count mismatch");
    for (size_t i = 0; i < back.records.size(); ++i) {
        if (back.records[i].id != codeplug.records[i].id) return Fail("record id mismatch");
        if (back.records[i].raw != codeplug.records[i].raw) return Fail("record bytes mismatch");
        if (!back.records[i].verified) return Fail("record not verified after roundtrip");
    }

#ifdef _WIN32
    if (!OleCanRead(fixture)) return Fail("OLE reader rejected the source fixture");
    if (!OleCanRead(rewritten)) return Fail("OLE reader rejected the written compound file");
    if (!OleCanRead(saved)) return Fail("OLE reader rejected the saved codeplug");
#endif

    std::printf("cfb ok: %zu streams, %zu records\n", streams.size(), back.records.size());
    return 0;
}
