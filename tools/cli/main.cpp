// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include "opendepot/core/adk.h"
#include "opendepot/core/crypto.h"
#include "opendepot/core/model.h"
#include "opendepot/core/radio.h"

#include <windows.h>
#include <objbase.h>

#include <cstdio>
#include <cwchar>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace opendepot;

static std::string JsonStr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04X", static_cast<unsigned>(c));
                    o += b;
                } else {
                    o += c;
                }
        }
    }
    o += "\"";
    return o;
}

static void PrintRecordHex(const Record& r) {
    const size_t payloadLen = r.raw.size() >= 8 ? r.raw.size() - 8 : 0;
    printf("record 0x%08X  payload=%zu  type=0x%04X\n", r.id, payloadLen, r.type);
    for (size_t i = 0; i < payloadLen; i += 16) {
        printf("  %04zX  ", i);
        const size_t n = std::min<size_t>(16, payloadLen - i);
        for (size_t j = 0; j < n; ++j) printf("%02X ", r.raw[6 + i + j]);
        for (size_t j = n; j < 16; ++j) printf("   ");
        printf(" |");
        for (size_t j = 0; j < n; ++j) {
            const uint8_t c = r.raw[6 + i + j];
            putchar(c >= 0x20 && c < 0x7F ? c : '.');
        }
        printf("|\n");
    }
}

static void PrintRecordCsv(const Codeplug& cp) {
    printf("id,stream,size,type,checksum,verified\n");
    for (const auto& r : cp.records) {
        printf("0x%08X,%d,%zu,0x%04X,0x%04X,%d\n", r.id, r.streamNum, r.raw.size(),
               r.type, r.checksum, r.verified ? 1 : 0);
    }
}

static std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr,
                        nullptr);
    return s;
}

static std::wstring ExeDir() {
    wchar_t p[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    size_t i = s.find_last_of(L"\\/");
    return i == std::wstring::npos ? L"." : s.substr(0, i);
}

static std::wstring HelperPath() {
    wchar_t env[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"OPENDEPOT_HELPER", env, MAX_PATH);
    if (n > 0 && n < MAX_PATH && GetFileAttributesW(env) != INVALID_FILE_ATTRIBUTES) return env;
    const std::wstring d = ExeDir();
    const wchar_t* rel[] = {L"\\radio_helper.exe", L"\\..\\..\\..\\tools\\radio_helper.exe",
                            L"\\tools\\radio_helper.exe"};
    for (const wchar_t* r : rel) {
        std::wstring p = d + r;
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }
    return d + L"\\radio_helper.exe";
}

// Bundled per-model base used for template-free codeplug creation.
static std::wstring ModelBasePath(const std::string& model) {
    std::wstring m(model.begin(), model.end());
    const std::wstring d = ExeDir();
    const wchar_t* rel[] = {L"\\resources\\models\\", L"\\..\\..\\..\\resources\\models\\",
                            L"\\..\\resources\\models\\"};
    for (const wchar_t* r : rel) {
        std::wstring p = d + r + m + L".cpg";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }
    return L"";
}

static bool WriteTagListFile(const Codeplug& cp, const std::wstring& path) {
    std::vector<std::pair<uint16_t, uint16_t>> pairs;
    for (const auto& r : cp.records) {
        std::pair<uint16_t, uint16_t> p{static_cast<uint16_t>(r.type),
                                        static_cast<uint16_t>(r.id >> 16)};
        if (std::find(pairs.begin(), pairs.end(), p) == pairs.end()) pairs.push_back(p);
    }
    std::sort(pairs.begin(), pairs.end());
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    for (auto& p : pairs) {
        uint8_t b[4] = {uint8_t(p.first >> 8), uint8_t(p.first), uint8_t(p.second >> 8),
                        uint8_t(p.second)};
        fwrite(b, 1, 4, f);
    }
    fclose(f);
    return true;
}

static bool WriteTagListFileFiltered(const Codeplug& cp, const std::wstring& path, bool onlyFff) {
    std::vector<std::pair<uint16_t, uint16_t>> pairs;
    for (const auto& r : cp.records) {
        const uint16_t part = static_cast<uint16_t>(r.type);
        const uint16_t tag = static_cast<uint16_t>(r.id >> 16);
        const bool isFff = (part == 0x0080 && tag == 0x0FFF);
        if (isFff != onlyFff) continue;
        std::pair<uint16_t, uint16_t> p{part, tag};
        if (std::find(pairs.begin(), pairs.end(), p) == pairs.end()) pairs.push_back(p);
    }
    std::sort(pairs.begin(), pairs.end());
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    for (auto& p : pairs) {
        uint8_t b[4] = {uint8_t(p.first >> 8), uint8_t(p.first), uint8_t(p.second >> 8),
                        uint8_t(p.second)};
        fwrite(b, 1, 4, f);
    }
    fclose(f);
    return true;
}

static bool WriteIshListFile(const Codeplug& cp, const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    auto emit = [&](const Record& r) {
        if (r.raw.size() < 8) return;
        const uint16_t tag = uint16_t(r.id >> 16);
        const uint16_t inst = uint16_t(r.id & 0xFFFF);
        const uint16_t part = r.type;
        const uint16_t len = uint16_t(r.raw.size() - 6);
        uint8_t h[8] = {uint8_t(tag >> 8), uint8_t(tag), uint8_t(inst >> 8), uint8_t(inst),
                        uint8_t(part >> 8), uint8_t(part), uint8_t(len >> 8), uint8_t(len)};
        fwrite(h, 1, 8, f);
        fwrite(r.raw.data() + 6, 1, len, f);
    };
    // Match the depot write order: identity records first, then everything else,
    // and the 0x0047 directory/reorg record last.
    const uint32_t firstIds[] = {0x10050000u, 0x10040000u, 0x10060000u};
    const Record* dir = nullptr;
    std::vector<const Record*> rest;
    for (const auto& r : cp.records) {
        if (r.id == 0x00470000u) { dir = &r; continue; }
        bool isFirst = false;
        for (uint32_t id : firstIds) if (r.id == id) { isFirst = true; break; }
        if (!isFirst) rest.push_back(&r);
    }
    for (uint32_t id : firstIds)
        for (const auto& r : cp.records)
            if (r.id == id) emit(r);
    for (const Record* r : rest) emit(*r);
    if (dir) emit(*dir);
    uint8_t term[2] = {0, 0};
    fwrite(term, 1, 2, f);
    fclose(f);
    return true;
}

static bool ApplyRecBuffer(const std::vector<uint8_t>& buf, const Codeplug& tpl, Codeplug& out,
                           std::string& err) {
    out = tpl;
    out.records.clear();
    size_t o = 0;
    while (o + 6 <= buf.size()) {
        Record r;
        r.id = (uint32_t(buf[o]) << 24) | (uint32_t(buf[o + 1]) << 16) |
               (uint32_t(buf[o + 2]) << 8) | buf[o + 3];
        uint16_t len = uint16_t((buf[o + 4] << 8) | buf[o + 5]);
        if (o + 6 + len > buf.size()) break;
        r.raw.assign(buf.begin() + o, buf.begin() + o + 6 + len);
        r.checksum = uint16_t((r.raw[r.raw.size() - 2] << 8) | r.raw[r.raw.size() - 1]);
        r.verified = (Crc16Ccitt(r.raw.data() + 6, r.raw.size() - 8) == r.checksum);
        const uint16_t tg = uint16_t(r.id >> 16);
        r.type = (tg == 0x1004 || tg == 0x1005 || tg == 0x1006) ? 0x0081 : 0x0080;
        for (const auto& t : tpl.records)
            if ((t.id >> 16) == (r.id >> 16)) { r.type = t.type; break; }
        out.records.push_back(std::move(r));
        o += 6 + len;
    }
    if (out.records.empty()) { err = "no records in read data"; return false; }
    // keep only defined template records? No: the read is authoritative.
    return true;
}

static bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0) { out.resize((size_t)n); fread(out.data(), 1, (size_t)n, f); }
    fclose(f);
    return true;
}

static bool Sha256File(const std::wstring& path, std::string& hex) {
    std::vector<uint8_t> data;
    if (!ReadWholeFile(path, data)) return false;
    hex = opendepot::Sha256Hex(data);
    return true;
}

static bool RunHelper(const std::wstring& args, std::string& err, DWORD timeoutMs = 120000) {
    std::wstring cmd = L"\"" + HelperPath() + L"\" " + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si,
                        &pi)) {
        err = "Cannot start radio helper (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    const DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD code = 124;
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 124);
        WaitForSingleObject(pi.hProcess, 5000);
        err = "radio operation timed out";
    } else {
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (wait == WAIT_TIMEOUT) return false;
    if (code != 0) {
        err = "Radio helper failed (exit " + std::to_string(code) + ")";
        return false;
    }
    return true;
}

static std::wstring RawScriptPath() {
    wchar_t env[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"OPENDEPOT_RADIO_SCRIPT", env, MAX_PATH);
    if (n > 0 && n < MAX_PATH && GetFileAttributesW(env) != INVALID_FILE_ATTRIBUTES) return env;
    const std::wstring d = ExeDir();
    const wchar_t* rel[] = {L"\\..\\..\\..\\tools\\radio_tcp.py", L"\\tools\\radio_tcp.py",
                            L"\\radio_tcp.py"};
    for (const wchar_t* r : rel) {
        std::wstring p = d + r;
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }
    return L"";
}

// Standalone flash: raw PPP/TCP transport, verified auth, full record set,
// 0x0047 directory, then reorg 0x01078000 and end 0x010601.
static bool RunRawWriter(const std::wstring& port, const std::wstring& cpg, std::string& err,
                         DWORD timeoutMs = 600000) {
    std::wstring script = RawScriptPath();
    if (script.empty()) {
        err = "radio_tcp.py not found";
        return false;
    }
    std::wstring cmd = L"python \"" + script + L"\" " + port + L" --write-cpg \"" + cpg +
                       L"\" --all";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si,
                        &pi)) {
        err = "Cannot start standalone writer (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    const DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD code = 124;
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 124);
        WaitForSingleObject(pi.hProcess, 5000);
        err = "standalone write timed out";
    } else {
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (wait == WAIT_TIMEOUT) return false;
    if (code != 0) {
        err = "standalone writer failed (exit " + std::to_string(code) + ")";
        return false;
    }
    return true;
}

// Native read over OpenDepot's own PPP/TCP transport (no vendor DLL).
static bool RunNativeReader(const std::wstring& port, const std::wstring& rec,
                            const std::wstring& reqs, std::string& err,
                            DWORD timeoutMs = 300000, const std::wstring& meta = L"") {
    std::wstring script = RawScriptPath();
    if (script.empty()) {
        err = "radio_tcp.py not found";
        return false;
    }
    std::wstring cmd = L"python \"" + script + L"\" " + port + L" --native-read \"" + rec +
                       L"\" --reqs \"" + reqs + L"\"";
    if (!meta.empty()) cmd += L" --meta \"" + meta + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si,
                        &pi)) {
        err = "Cannot start native reader (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    const DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD code = 124;
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 124);
        WaitForSingleObject(pi.hProcess, 5000);
        err = "native read timed out";
    } else {
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (wait == WAIT_TIMEOUT) return false;
    if (code != 0) {
        err = "native reader failed (exit " + std::to_string(code) + ")";
        return false;
    }
    return true;
}

static std::wstring TempPath(const wchar_t* name) {
    wchar_t dir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, dir);
    std::wstring base = dir;
    if (!base.empty() && base.back() != L'\\') base += L'\\';
    base += L"opendepot-radio";
    CreateDirectoryW(base.c_str(), nullptr);
    return base + L"\\" + name;
}


static std::string QueryBanner(const std::wstring& port) {
    std::wstring dev = L"\\\\.\\" + port;
    HANDLE h = CreateFileW(dev.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(h, &dcb);
    dcb.BaudRate = 9600;
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
    SetCommState(h, &dcb);
    COMMTIMEOUTS to{};
    to.ReadIntervalTimeout = 50;
    to.ReadTotalTimeoutConstant = 1500;
    to.ReadTotalTimeoutMultiplier = 10;
    to.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(h, &to);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    DWORD wrote = 0;
    WriteFile(h, "AT&V\r", 5, &wrote, nullptr);
    std::string out;
    char buf[512];
    for (int i = 0; i < 30; ++i) {
        DWORD n = 0;
        if (!ReadFile(h, buf, sizeof(buf), &n, nullptr) || n == 0) break;
        out.append(buf, n);
        if (out.find("OK") != std::string::npos) break;
    }
    CloseHandle(h);
    return out;
}

static std::string BannerField(const std::string& banner, const char* key) {
    const std::string tag = std::string(key) + ":";
    const size_t p = banner.find(tag);
    if (p == std::string::npos) return {};
    const size_t start = p + tag.size();
    const size_t stop = banner.find_first_of("\r\n", start);
    return banner.substr(start, stop == std::string::npos ? std::string::npos : stop - start);
}

static bool ReadRadioComplete(const std::wstring& port, const Codeplug& tpl, Codeplug& out,
                              std::string& err) {
    if (tpl.records.empty()) {
        err = "template has no records";
        return false;
    }

    // Read by partition/tag rather than tag+instance. The vendor ReadAll path
    // returns every instance of a tag, so records that a fixed per-instance
    // request list misses are still captured.
    std::vector<std::pair<uint16_t, uint16_t>> tags;
    for (const auto& r : tpl.records) {
        const std::pair<uint16_t, uint16_t> p{r.type, static_cast<uint16_t>(r.id >> 16)};
        if (std::find(tags.begin(), tags.end(), p) == tags.end()) tags.push_back(p);
    }
    std::sort(tags.begin(), tags.end());

    const std::wstring tagsPath = TempPath(L"rr.tags");
    FILE* tf = _wfopen(tagsPath.c_str(), L"wb");
    if (!tf) {
        err = "cannot stage tag list";
        return false;
    }
    for (const auto& p : tags) {
        const uint8_t b[4] = {uint8_t(p.first >> 8), uint8_t(p.first), uint8_t(p.second >> 8),
                              uint8_t(p.second)};
        fwrite(b, 1, 4, tf);
    }
    fclose(tf);

    // The vendor COM session can fail transiently right after a write or reset.
    // Retry once, then let the guard decide.
    std::string last_err;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const std::wstring rec = TempPath(L"rr.rec");
        DeleteFileW(rec.c_str());
        std::string herr;
        const bool ok =
            RunHelper(L"--read " + port + L" \"" + tagsPath + L"\" \"" + rec + L"\"", herr,
                      300000);
        if (!ok) {
            DeleteFileW(rec.c_str());
            last_err = "radio read failed: " + herr;
        } else {
            std::vector<uint8_t> buf;
            if (!ReadWholeFile(rec, buf)) {
                last_err = "read output missing";
            } else {
                Codeplug parsed;
                std::string perr;
                if (!ApplyRecBuffer(buf, tpl, parsed, perr)) {
                    last_err = "read parse failed: " + perr;
                } else {
                    std::string serial, modelName;
                    if (ReadIdentityFromCodeplug(parsed, serial, modelName))
                        UpdateIdentity(parsed, serial, modelName);
                    std::string verr;
                    if (ValidateRadioCodeplug(parsed, verr)) {
                        DeleteFileW(rec.c_str());
                        DeleteFileW(tagsPath.c_str());
                        out = std::move(parsed);
                        return true;
                    }
                    last_err = "radio read is incomplete: " + verr;
                }
            }
            DeleteFileW(rec.c_str());
        }
        if (attempt == 0) Sleep(1500);
    }
    DeleteFileW(tagsPath.c_str());
    err = last_err.empty() ? "radio read failed" : last_err;
    return false;
}

static bool ReadRadioToCodeplug(const std::wstring& port, Codeplug& out, std::string& err,
                                std::string* metaOut = nullptr) {
    const std::string banner = QueryBanner(port);
    std::string model = BannerField(banner, "MODEL");
    if (model.empty()) model = BuiltinFirstModel();

    Codeplug tpl;
    std::string berr;
    if (!BuiltinTemplate(model, tpl, berr) || tpl.records.empty()) {
        model = BuiltinFirstModel();
        if (!BuiltinTemplate(model, tpl, berr) || tpl.records.empty()) {
            err = "no built-in template for the connected model";
            return false;
        }
    }
    if (!ReadRadioComplete(port, tpl, out, err)) return false;
    if (metaOut) metaOut->clear();
    return true;
}

static bool WriteIdentityTagList(const std::wstring& path) {
    const uint16_t tags[3] = {0x1004, 0x1005, 0x1006};
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    for (uint16_t t : tags) {
        uint8_t b[4] = {0x00, 0x81, uint8_t(t >> 8), uint8_t(t)};
        fwrite(b, 1, 4, f);
    }
    fclose(f);
    return true;
}

static bool VerifyIdentityAfterWrite(const std::wstring& port, const Codeplug& cp,
                                     std::string& err) {
    std::wstring tags = TempPath(L"id-tags.bin");
    std::wstring rec = TempPath(L"id-read.rec");
    if (!WriteIdentityTagList(tags)) {
        err = "cannot stage identity tag list";
        return false;
    }
    std::string rerr;
    std::wstring args = L"--read " + port + L" \"" + tags + L"\" \"" + rec + L"\"";
    if (!RunHelper(args, rerr, 60000)) {
        DeleteFileW(tags.c_str());
        DeleteFileW(rec.c_str());
        err = "post-write identity read failed: " + rerr;
        return false;
    }
    std::vector<uint8_t> buf;
    if (!ReadWholeFile(rec, buf)) {
        DeleteFileW(tags.c_str());
        DeleteFileW(rec.c_str());
        err = "post-write read produced no data";
        return false;
    }
    DeleteFileW(tags.c_str());
    DeleteFileW(rec.c_str());
    bool found = false;
    size_t o = 0;
    while (o + 6 <= buf.size()) {
        uint32_t id = (uint32_t(buf[o]) << 24) | (uint32_t(buf[o + 1]) << 16) |
                      (uint32_t(buf[o + 2]) << 8) | buf[o + 3];
        uint16_t len = uint16_t((buf[o + 4] << 8) | buf[o + 5]);
        if (o + 6 + len > buf.size()) break;
        if (id == 0x10040000) {
            std::string payload(reinterpret_cast<const char*>(buf.data() + o + 6), len);
            found = payload.find(cp.header.serial) != std::string::npos &&
                    payload.find(cp.header.model) != std::string::npos;
        }
        o += 6 + len;
    }
    if (!found) {
        err = "identity readback does not contain the written serial/model";
        return false;
    }
    return true;
}

static std::vector<int> ParseIntList(const wchar_t* s) {
    std::vector<int> out;
    std::wstring w(s ? s : L""), cur;
    for (wchar_t c : w) {
        if (c == L',') {
            if (!cur.empty()) {
                out.push_back(_wtoi(cur.c_str()));
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(_wtoi(cur.c_str()));
    return out;
}

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (argc < 2) {
        printf("WARNING: experimental; a write can BRICK your radio. Keep a verified backup.\n");
        printf("         Only tested on one XTS 2500. Firmware flashing is not supported.\n");
        printf("usage:\n");
        printf("  opendepot-cli --verify <in.cpg>\n");
        printf("  opendepot-cli --dump <in.cpg>\n");
        printf("  opendepot-cli --records <in.cpg>\n");
        printf("  opendepot-cli --model <in.cpg>\n");
        printf("  opendepot-cli --schema <in.cpg>\n");
        printf("  opendepot-cli --hex <in.cpg> <tagHex> [instHex]\n");
        printf("  opendepot-cli --hexall <in.cpg>\n");
        printf("  opendepot-cli --clone <in.cpg> <out.cpg>\n");
        printf("  opendepot-cli --models\n");
        printf("  opendepot-cli --zones <in.cpg>\n");
        printf("  opendepot-cli --set-zone-name <in.cpg> <out.cpg> <zoneIndex> <name>\n");
        printf("  opendepot-cli --set-channel-name <in.cpg> <out.cpg> <zoneIndex> <chanIndex> <name>\n");
        printf("  opendepot-cli --set-channel-freq <in.cpg> <out.cpg> <zoneIndex> <chanIndex> <rxMHz> <txMHz>\n");
        printf("  opendepot-cli --set-identity <in.cpg> <out.cpg> <serial> <model>\n");
        printf("  opendepot-cli --password <in.cpg>\n");
        printf("  opendepot-cli --set-password <in.cpg> <out.cpg> <password>\n");
        printf("  opendepot-cli --clear-password <in.cpg> <out.cpg>\n");
        printf("  opendepot-cli --radio-password <port>\n");
        printf("  opendepot-cli --backup-radio <port> <outdir>\n");
        printf("  opendepot-cli --report <in.cpg> [--json]\n");
        printf("  opendepot-cli --radio-report <port> [--json]\n");
        printf("  opendepot-cli --doctor <in.cpg> [<out.cpg>]\n");
        printf("  opendepot-cli --diff <a.cpg> <b.cpg> [--json]\n");
        printf("  opendepot-cli --copy-zone <src.cpg> <srcZone> <dst.cpg> <dstZone> <out.cpg>\n");
        printf("  opendepot-cli --copy-zones <src.cpg> <srcZones> <dst.cpg> <dstZones> <out.cpg> [--trunking]\n");
        printf("  opendepot-cli --unlock <port>\n");
        printf("  opendepot-cli --security <port>\n");
        printf("  opendepot-cli --flashcode <in.cpg> [radio|platform]\n");
        printf("  opendepot-cli --set-flashcode <in.cpg> <out.cpg> <flashcode>\n");
        printf("  opendepot-cli --flashcode-options <radio|platform>\n");
        printf("  opendepot-cli --build-flashcode <radio|platform> <base> <code,code,...>\n");
        printf("  opendepot-cli --create <template.cpg> <out.cpg> <serial> <model> <flashcode>\n");
        printf("  opendepot-cli --create-new <out.cpg> <model> <serial> [flashcode]\n");
        printf("  opendepot-cli --conventional <in.cpg> <out.cpg>\n");
        printf("  opendepot-cli --conventional-ref <ref.cpg> <src.cpg> <out.cpg>\n");
        printf("  opendepot-cli --create-conventional <template.cpg> <out.cpg> <serial> <model>\n");
        printf("  opendepot-cli --rec <in.cpg> <out.rec>\n");
        printf("  opendepot-cli --from-rec <template.cpg> <in.rec> <out.cpg>\n");
        printf("  opendepot-cli --taglist <in.cpg> <out.bin>\n");
        printf("  opendepot-cli --ishlist <in.cpg> <out.bin>\n");
        printf("  opendepot-cli --read-radio <port> <out.cpg>\n");
        printf("  opendepot-cli --read-radio <port> <template.cpg> <out.cpg>\n");
        printf("  opendepot-cli --check-write <in.cpg>\n");
        printf("  opendepot-cli --write-radio <port> <in.cpg>\n");
        printf("  opendepot-cli --erase-radio <port> [--yes-erase]\n");
        printf("  opendepot-cli --replace-radio <port> <old.cpg> <new.cpg>\n");
        return 1;
    }
    const std::wstring cmd = argv[1];
    bool wantJson = false;
    for (int i = 1; i < argc; ++i)
        if (std::wstring(argv[i]) == L"--json") wantJson = true;
    if (cmd == L"--verify" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        printf("%s", VerifyCodeplug(cp).c_str());
        size_t bad = 0;
        for (auto& r : cp.records) if (!r.verified) ++bad;
        return bad ? 3 : 0;
    }
    if (cmd == L"--dump" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        printf("%s", DumpCodeplug(cp).c_str());
        return 0;
    }
    if (cmd == L"--records" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        PrintRecordCsv(cp);
        return 0;
    }
    if (cmd == L"--model" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        CodeplugModel m(cp);
        std::string err;
        if (!m.Reload(err)) {
            printf("MODEL FAILED: %s\n", err.c_str());
            return 2;
        }
        printf("{\n");
        printf("  \"model\": %s,\n", JsonStr(m.Model()).c_str());
        printf("  \"serial\": %s,\n", JsonStr(m.Serial()).c_str());
        printf("  \"flashcode\": %s,\n", JsonStr(m.Flashcode()).c_str());
        printf("  \"records\": %d,\n", m.RecordCount());
        printf("  \"sections\": [\n");
        for (size_t i = 0; i < m.Sections().size(); ++i) {
            const auto& s = m.Sections()[i];
            printf("    {\"id\": %s, \"group\": %s, \"name\": %s, \"confidence\": %s, \"records\": [",
                   JsonStr(s.id).c_str(), JsonStr(s.group).c_str(), JsonStr(s.name).c_str(),
                   JsonStr(ConfidenceName(s.confidence)).c_str());
            for (size_t j = 0; j < s.records.size(); ++j)
                printf("%s\"0x%08X\"", j ? ", " : "", s.records[j]);
            printf("]}%s\n", i + 1 == m.Sections().size() ? "" : ",");
        }
        printf("  ],\n");
        printf("  \"zones\": [\n");
        for (size_t z = 0; z < m.Zones().size(); ++z) {
            const auto& zm = m.Zones()[z];
            printf("    {\"index\": %d, \"name\": %s, \"channels\": [\n", zm.index,
                   JsonStr(zm.name).c_str());
            for (size_t c = 0; c < zm.channels.size(); ++c) {
                const auto& ch = zm.channels[c];
                printf("      {\"index\": %d, \"channelId\": %u, \"name\": %s, "
                       "\"rxMHz\": %.5f, \"txMHz\": %.5f, \"rxWord\": %u, \"txWord\": %u, "
                       "\"squelchRx\": %u, \"squelchTx\": %u, \"flags\": %u, \"simplex\": %s, "
                       "\"hasPersonality\": %s, \"hasAstro\": %s}%s\n",
                       ch.index, unsigned(ch.channelId), JsonStr(ch.name).c_str(), ch.rxMHz,
                       ch.txMHz, ch.rxWord, ch.txWord, unsigned(ch.squelchRx),
                       unsigned(ch.squelchTx), unsigned(ch.flags),
                       ch.simplex ? "true" : "false", ch.hasPersonality ? "true" : "false",
                       ch.hasAstro ? "true" : "false",
                       c + 1 == zm.channels.size() ? "" : ",");
            }
            printf("    ]}%s\n", z + 1 == m.Zones().size() ? "" : ",");
        }
        printf("  ],\n");
        printf("  \"aliasLists\": [\n");
        for (size_t i = 0; i < m.AliasLists().size(); ++i) {
            const auto& a = m.AliasLists()[i];
            printf("    {\"name\": %s, \"tag\": \"0x%04X\", \"capacity\": %d, "
                   "\"confidence\": %s, \"entries\": [",
                   JsonStr(a.name).c_str(), a.tag, a.capacity,
                   JsonStr(ConfidenceName(a.confidence)).c_str());
            for (size_t j = 0; j < a.entries.size(); ++j)
                printf("%s%s", j ? ", " : "", JsonStr(a.entries[j]).c_str());
            printf("]}%s\n", i + 1 == m.AliasLists().size() ? "" : ",");
        }
        printf("  ],\n");
        printf("  \"namedLists\": [\n");
        for (size_t i = 0; i < m.NamedLists().size(); ++i) {
            const auto& l = m.NamedLists()[i];
            printf("    {\"label\": %s, \"tag\": \"0x%04X\", \"confidence\": %s, \"entries\": [",
                   JsonStr(l.label).c_str(), l.tag,
                   JsonStr(ConfidenceName(l.confidence)).c_str());
            for (size_t j = 0; j < l.entries.size(); ++j)
                printf("%s%s", j ? ", " : "", JsonStr(l.entries[j]).c_str());
            printf("]}%s\n", i + 1 == m.NamedLists().size() ? "" : ",");
        }
        printf("  ],\n");
        printf("  \"scanLists\": [\n");
        for (size_t i = 0; i < m.ScanLists().size(); ++i) {
            const auto& s = m.ScanLists()[i];
            printf("    {\"index\": %d, \"payloadBytes\": %zu, \"confidence\": %s}%s\n", s.index,
                   s.raw.size(), JsonStr(ConfidenceName(s.confidence)).c_str(),
                   i + 1 == m.ScanLists().size() ? "" : ",");
        }
        printf("  ],\n");
        printf("  \"trunkingSystem\": {\"present\": %s, \"wacn\": %u, \"homeSystemId\": %u},\n",
               m.TrunkingSystem().present ? "true" : "false",
               m.TrunkingSystem().wacn,
               static_cast<unsigned>(m.TrunkingSystem().homeSystemId));
        printf("  \"recordNodes\": [\n");
        for (size_t i = 0; i < m.RecordNodes().size(); ++i) {
            const auto& n = m.RecordNodes()[i];
            printf("    {\"id\": \"0x%08X\", \"tag\": \"0x%04X\", \"inst\": %u, "
                   "\"length\": %d, \"section\": %s, \"family\": %s, \"name\": %s, "
                   "\"names\": [",
                   n.id, n.tag, unsigned(n.inst), n.length, JsonStr(n.section).c_str(),
                   JsonStr(n.family).c_str(), JsonStr(n.name).c_str());
            for (size_t j = 0; j < n.names.size(); ++j)
                printf("%s%s", j ? ", " : "", JsonStr(n.names[j]).c_str());
            printf("], \"refs\": [");
            for (size_t j = 0; j < n.references.size(); ++j)
                printf("%s[\"0x%04X\", %u, %d]", j ? ", " : "", n.references[j].tag,
                       unsigned(n.references[j].inst), n.references[j].offset);
            printf("], \"strings\": [");
            for (size_t j = 0; j < n.strings.size(); ++j)
                printf("%s%s", j ? ", " : "", JsonStr(n.strings[j]).c_str());
            printf("], \"payload\": \"");
            for (uint8_t b : n.payload) printf("%02X", b);
            printf("\"}%s\n", i + 1 == m.RecordNodes().size() ? "" : ",");
        }
        printf("  ]\n}\n");
        return 0;
    }
    if (cmd == L"--schema" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        CodeplugModel m(cp);
        std::string err;
        if (!m.Reload(err)) {
            printf("MODEL FAILED: %s\n", err.c_str());
            return 2;
        }
        struct TagAgg {
            int count = 0;
            std::vector<int> lengths;
            std::string section;
            std::string family;
            std::vector<std::string> names;
            std::vector<std::string> strings;
        };
        std::map<uint16_t, TagAgg> agg;
        for (const auto& n : m.RecordNodes()) {
            auto& a = agg[n.tag];
            ++a.count;
            if (std::find(a.lengths.begin(), a.lengths.end(), n.length) == a.lengths.end())
                a.lengths.push_back(n.length);
            a.section = n.section;
            if (a.family.empty()) a.family = n.family;
            for (const auto& s : n.names)
                if (std::find(a.names.begin(), a.names.end(), s) == a.names.end())
                    a.names.push_back(s);
            for (const auto& s : n.strings)
                if (std::find(a.strings.begin(), a.strings.end(), s) == a.strings.end())
                    a.strings.push_back(s);
        }
        printf("{\n  \"recordCount\": %d,\n  \"tags\": [\n", m.RecordCount());
        size_t i = 0;
        for (const auto& kv : agg) {
            const auto& a = kv.second;
            printf("    {\"tag\": \"0x%04X\", \"count\": %d, \"lengths\": [", kv.first, a.count);
            for (size_t j = 0; j < a.lengths.size(); ++j)
                printf("%s%d", j ? ", " : "", a.lengths[j]);
            printf("], \"section\": %s, \"family\": %s, \"names\": [",
                   JsonStr(a.section).c_str(), JsonStr(a.family).c_str());
            for (size_t j = 0; j < a.names.size(); ++j)
                printf("%s%s", j ? ", " : "", JsonStr(a.names[j]).c_str());
            printf("], \"strings\": [");
            for (size_t j = 0; j < a.strings.size(); ++j)
                printf("%s%s", j ? ", " : "", JsonStr(a.strings[j]).c_str());
            printf("]}%s\n", ++i == agg.size() ? "" : ",");
        }
        printf("  ],\n  \"sections\": [\n");
        for (size_t k = 0; k < m.Sections().size(); ++k) {
            const auto& s = m.Sections()[k];
            printf("    {\"id\": %s, \"group\": %s, \"name\": %s, \"confidence\": %s, "
                   "\"records\": %zu}%s\n",
                   JsonStr(s.id).c_str(), JsonStr(s.group).c_str(), JsonStr(s.name).c_str(),
                   JsonStr(ConfidenceName(s.confidence)).c_str(), s.records.size(),
                   k + 1 == m.Sections().size() ? "" : ",");
        }
        printf("  ]\n}\n");
        return 0;
    }
    if (cmd == L"--hex" && argc >= 4) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        const uint16_t tag = static_cast<uint16_t>(wcstoul(argv[3], nullptr, 16));
        const bool anyInst = (argc < 5);
        const uint16_t inst = anyInst ? 0 : static_cast<uint16_t>(wcstoul(argv[4], nullptr, 16));
        int shown = 0;
        for (const auto& r : cp.records) {
            if (static_cast<uint16_t>(r.id >> 16) != tag) continue;
            if (!anyInst && static_cast<uint16_t>(r.id & 0xFFFF) != inst) continue;
            ++shown;
            PrintRecordHex(r);
        }
        if (!shown) printf("no record with tag 0x%04X\n", tag);
        return shown ? 0 : 3;
    }
    if (cmd == L"--hexall" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        for (const auto& r : cp.records) PrintRecordHex(r);
        return 0;
    }
    if (cmd == L"--clone" && argc >= 4) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        std::string err;
        if (!SaveCodeplug(argv[3], cp, err)) {
            printf("SAVE FAILED: %s\n", err.c_str());
            return 2;
        }
        printf("wrote %ls\n", argv[3]);
        return 0;
    }
    if (cmd == L"--models" && argc >= 2) {
        printf("Bands:  ");
        for (const auto& s : BandOptions()) printf("%s  ", s.c_str());
        printf("\nRadios: ");
        for (const auto& s : RadioOptions()) printf("%s  ", s.c_str());
        printf("\nTypes:  ");
        for (const auto& s : TypeOptions()) printf("%s  ", s.c_str());
        printf("\nModels:\n");
        for (const auto& m : ModelCatalog())
            printf("  %-8s %-10s %-10s %s\n", m.band, m.radio, m.type, m.model);
        printf("\nPackages (XTS5000):\n");
        for (const auto& s : PackagesForModel("H46UCF9PW6BN")) printf("  %s\n", s.c_str());
        printf("Add-ons (XTS5000):\n");
        for (const auto& s : AddonsForModel("H46UCF9PW6BN")) printf("  %s\n", s.c_str());
        return 0;
    }
    if (cmd == L"--create" && argc >= 7) {
        char serial[64] = {}, model[64] = {}, flash[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[4], -1, serial, sizeof(serial), nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, argv[5], -1, model, sizeof(model), nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, argv[6], -1, flash, sizeof(flash), nullptr, nullptr);
        Codeplug tpl;
        if (!LoadCodeplug(argv[2], tpl)) {
            printf("LOAD FAILED: %s\n", tpl.error.c_str());
            return 2;
        }
        CreateOptions opt;
        opt.serial = serial;
        opt.model = model;
        opt.flashcode = flash;
        Codeplug created;
        std::string err;
        if (!CreateCodeplug(tpl, opt, created, err)) {
            printf("CREATE FAILED: %s\n", err.c_str());
            return 2;
        }
        if (!SaveCodeplug(argv[3], created, err)) {
            printf("SAVE FAILED: %s\n", err.c_str());
            return 2;
        }
        printf("wrote %ls  model=%s serial=%s flashcode=%s\n", argv[3], model, serial,
               GetFlashcode(created).c_str());
        return 0;
    }
    if (cmd == L"--create-new" && argc >= 5) {
        // Template-free: --create-new <out.cpg> <model> <serial> [flashcode]
        char model[64] = {}, serial[64] = {}, flash[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[3], -1, model, sizeof(model), nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, argv[4], -1, serial, sizeof(serial), nullptr, nullptr);
        if (argc >= 6)
            WideCharToMultiByte(CP_UTF8, 0, argv[5], -1, flash, sizeof(flash), nullptr, nullptr);
        Codeplug tpl;
        std::string berr;
        if (!BuiltinTemplate(model, tpl, berr)) {
            printf("NO BUILT-IN SCHEMA for model %s: %s\n", model, berr.c_str());
            return 2;
        }
        CreateOptions opt;
        opt.serial = serial;
        opt.model = model;
        opt.flashcode = flash;
        Codeplug created;
        std::string err;
        if (!CreateCodeplug(tpl, opt, created, err)) {
            printf("CREATE FAILED: %s\n", err.c_str());
            return 2;
        }
        if (!SaveCodeplug(argv[2], created, err)) {
            printf("SAVE FAILED: %s\n", err.c_str());
            return 2;
        }
        printf("wrote %ls  model=%s serial=%s flashcode=%s\n", argv[2], model, serial,
               GetFlashcode(created).c_str());
        return 0;
    }
    if (cmd == L"--rec" && argc >= 4) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        FILE* f = _wfopen(argv[3], L"wb");
        if (!f) { printf("open failed\n"); return 2; }
        for (const auto& r : cp.records) fwrite(r.raw.data(), 1, r.raw.size(), f);
        fclose(f);
        printf("wrote %ls (%zu records)\n", argv[3], cp.records.size());
        return 0;
    }
    if (cmd == L"--taglist" && argc >= 4) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        std::vector<std::pair<uint16_t, uint16_t>> pairs;
        for (const auto& r : cp.records) {
            std::pair<uint16_t, uint16_t> p{static_cast<uint16_t>(r.type),
                                            static_cast<uint16_t>(r.id >> 16)};
            if (std::find(pairs.begin(), pairs.end(), p) == pairs.end()) pairs.push_back(p);
        }
        std::sort(pairs.begin(), pairs.end());
        FILE* f = _wfopen(argv[3], L"wb");
        if (!f) { printf("open failed\n"); return 2; }
        for (auto& p : pairs) {
            uint8_t b[4] = {uint8_t(p.first >> 8), uint8_t(p.first),
                            uint8_t(p.second >> 8), uint8_t(p.second)};
            fwrite(b, 1, 4, f);
        }
        fclose(f);
        printf("wrote %ls (%zu partition/tag pairs)\n", argv[3], pairs.size());
        return 0;
    }
    if (cmd == L"--ishlist" && argc >= 4) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        FILE* f = _wfopen(argv[3], L"wb");
        if (!f) { printf("open failed\n"); return 2; }
        size_t total = 0;
        for (const auto& r : cp.records) {
            if (r.raw.size() < 8) continue;
            const uint16_t tag = uint16_t(r.id >> 16);
            const uint16_t inst = uint16_t(r.id & 0xFFFF);
            const uint16_t part = r.type;
            const uint16_t len = uint16_t(r.raw.size() - 6);
            uint8_t h[8] = {uint8_t(tag >> 8), uint8_t(tag), uint8_t(inst >> 8), uint8_t(inst),
                            uint8_t(part >> 8), uint8_t(part), uint8_t(len >> 8), uint8_t(len)};
            fwrite(h, 1, 8, f);
            fwrite(r.raw.data() + 6, 1, len, f);
            total += 8 + len;
        }
        uint8_t term[2] = {0, 0};
        fwrite(term, 1, 2, f);
        fclose(f);
        printf("wrote %ls (%zu records, %zu bytes)\n", argv[3], cp.records.size(), total + 2);
        return 0;
    }
    if (cmd == L"--from-rec" && argc >= 5) {
        Codeplug tpl;
        if (!LoadCodeplug(argv[2], tpl)) { printf("LOAD FAILED: %s\n", tpl.error.c_str()); return 2; }
        FILE* f = _wfopen(argv[3], L"rb");
        if (!f) { printf("open failed\n"); return 2; }
        std::vector<uint8_t> buf;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 0) { buf.resize((size_t)n); fread(buf.data(), 1, (size_t)n, f); }
        fclose(f);
        Codeplug out = tpl;
        out.records.clear();
        size_t o = 0;
        while (o + 6 <= buf.size()) {
            Record r;
            r.id = (uint32_t(buf[o]) << 24) | (uint32_t(buf[o + 1]) << 16) |
                   (uint32_t(buf[o + 2]) << 8) | buf[o + 3];
            uint16_t len = uint16_t((buf[o + 4] << 8) | buf[o + 5]);
            if (o + 6 + len > buf.size()) break;
            r.raw.assign(buf.begin() + o, buf.begin() + o + 6 + len);
            r.checksum = uint16_t((r.raw[r.raw.size() - 2] << 8) | r.raw[r.raw.size() - 1]);
            r.verified = (Crc16Ccitt(r.raw.data() + 6, r.raw.size() - 8) == r.checksum);
            r.type = 0x0080;
            for (const auto& t : tpl.records)
                if ((t.id >> 16) == (r.id >> 16)) { r.type = t.type; break; }
            out.records.push_back(std::move(r));
            o += 6 + len;
        }
        std::string err;
        if (!SaveCodeplug(argv[4], out, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        size_t good = 0;
        for (auto& r : out.records) if (r.verified) ++good;
        printf("wrote %ls (%zu records, %zu CRC-ok)\n", argv[4], out.records.size(), good);
        return 0;
    }
    if (cmd == L"--read-radio" && argc >= 4) {
        // --read-radio <port> <template.cpg> <out.cpg>
        // --read-radio <port> <out.cpg>
        const bool useTemplate = (argc >= 5);
        Codeplug out;
        std::string err;
        std::wstring outPath;
        if (useTemplate) {
            Codeplug tpl;
            if (!LoadCodeplug(argv[3], tpl)) {
                printf("LOAD FAILED: %s\n", tpl.error.c_str());
                return 2;
            }
            outPath = argv[4];
            if (!ReadRadioComplete(std::wstring(argv[2]), tpl, out, err)) {
                printf("READ FAILED: %s\n", err.c_str());
                return 2;
            }
        } else {
            outPath = argv[3];
            if (!ReadRadioToCodeplug(std::wstring(argv[2]), out, err)) {
                printf("READ FAILED: %s\n", err.c_str());
                return 2;
            }
        }
        if (!SaveCodeplug(outPath, out, err)) {
            printf("SAVE FAILED: %s\n", err.c_str());
            return 2;
        }
        size_t good = 0;
        for (const auto& r : out.records)
            if (r.verified) ++good;
        printf("read %zu records (%zu CRC-ok, validated) -> %ls\n", out.records.size(), good,
               outPath.c_str());
        return 0;
    }
    if (cmd == L"--read-radio-native" && argc >= 4) {
        const bool haveTemplate = (argc >= 5);
        std::wstring outPath;
        Codeplug tpl;
        if (haveTemplate) {
            if (!LoadCodeplug(argv[3], tpl)) {
                printf("LOAD FAILED: %s\n", tpl.error.c_str());
                return 2;
            }
            outPath = argv[4];
        }
        std::string err;
        auto session = opendepot::RadioSession::Open(ToUtf8(argv[2]), err);
        if (!session) {
            printf("OPEN FAILED: %s\n", err.c_str());
            return 2;
        }
        std::string model;
        if (!haveTemplate) {
            opendepot::RadioInfo info;
            session->Info(info, err);
            model = info.model.empty() ? BuiltinFirstModel() : info.model;
            std::string berr;
            if (!BuiltinTemplate(model, tpl, berr) || tpl.records.empty()) {
                printf("NO TEMPLATE: %s\n", berr.c_str());
                session->Close();
                return 2;
            }
            outPath = argv[3];
        }
        std::vector<opendepot::RadioReadRequest> reqs;
        std::vector<opendepot::ReadRequest> modelReqs;
        if (!haveTemplate) ModelReadRequests(model, modelReqs);
        if (!modelReqs.empty()) {
            reqs.reserve(modelReqs.size());
            for (const auto& r : modelReqs) reqs.push_back({r.part, r.tag, r.inst, r.len});
        } else {
            reqs.reserve(tpl.records.size());
            for (const auto& r : tpl.records)
                reqs.push_back({r.type, static_cast<uint16_t>(r.id >> 16),
                                static_cast<uint16_t>(r.id & 0xFFFF),
                                static_cast<uint16_t>(r.raw.size() - 6)});
        }
        std::vector<opendepot::Record> recs;
        if (!session->ReadRequests(reqs, recs, err)) {
            printf("NATIVE READ FAILED: %s\n", err.c_str());
            session->Close();
            return 2;
        }
        session->Close();
        Codeplug out = tpl;
        out.records = std::move(recs);
        std::string serial, modelName;
        if (ReadIdentityFromCodeplug(out, serial, modelName)) UpdateIdentity(out, serial, modelName);
        std::string verr;
        if (!ValidateRadioCodeplug(out, verr)) {
            printf("NATIVE READ INCOMPLETE: %s\n", verr.c_str());
            return 3;
        }
        if (!SaveCodeplug(outPath, out, err)) {
            printf("SAVE FAILED: %s\n", err.c_str());
            return 2;
        }
        size_t good = 0;
        for (const auto& r : out.records)
            if (r.verified) ++good;
        printf("native read %zu records (%zu CRC-ok) -> %ls\n", out.records.size(), good,
               outPath.c_str());
        return 0;
    }
    if (cmd == L"--write-radio-native" && argc >= 4) {
        Codeplug cp;
        if (!LoadCodeplug(argv[3], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        std::string verr;
        if (!ValidateRadioCodeplug(cp, verr)) {
            printf("REFUSED: %s\n", verr.c_str());
            return 3;
        }
        std::string err;
        auto session = opendepot::RadioSession::Open(ToUtf8(argv[2]), err);
        if (!session) {
            printf("OPEN FAILED: %s\n", err.c_str());
            return 2;
        }
        const bool ok = session->WriteRecords(cp.records, err);
        session->Close();
        if (!ok) {
            printf("NATIVE WRITE FAILED: %s\n", err.c_str());
            return 2;
        }
        printf("native write: %zu records -> %s\n", cp.records.size(), ToUtf8(argv[2]).c_str());
        return 0;
    }
    if (cmd == L"--erase-radio" && argc >= 3) {
        bool confirmed = false;
        for (int i = 1; i < argc; ++i)
            if (std::wstring(argv[i]) == L"--yes-erase") confirmed = true;
        if (!confirmed) {
            printf("ERASE REFUSED: erasing deletes the identity block and causes FAIL 01/92.\n");
            printf("Re-run with --yes-erase only if you have a verified backup.\n");
            return 2;
        }
        std::string err;
        std::wstring args = L"--erase " + std::wstring(argv[2]);
        if (!RunHelper(args, err, 120000)) { printf("ERASE FAILED: %s\n", err.c_str()); return 2; }
        printf("erased codeplug on %ls\n", argv[2]);
        return 0;
    }
    if (cmd == L"--replace-radio" && argc >= 5) {
        Codeplug oldcp, newcp;
        if (!LoadCodeplug(argv[3], oldcp)) { printf("LOAD OLD FAILED: %s\n", oldcp.error.c_str()); return 2; }
        if (!LoadCodeplug(argv[4], newcp)) { printf("LOAD NEW FAILED: %s\n", newcp.error.c_str()); return 2; }
        std::string verr;
        if (!ValidateRadioCodeplug(newcp, verr)) {
            printf("REPLACE REFUSED: %s\n", verr.c_str());
            return 2;
        }
        std::wstring oldrec = TempPath(L"old.rec");
        FILE* f = _wfopen(oldrec.c_str(), L"wb");
        if (!f) { printf("temp open failed\n"); return 2; }
        for (const auto& r : oldcp.records) fwrite(r.raw.data(), 1, r.raw.size(), f);
        fclose(f);
        std::wstring ish = TempPath(L"new.ish");
        if (!WriteIshListFile(newcp, ish)) { printf("ishlist write failed\n"); DeleteFileW(oldrec.c_str()); return 2; }
        std::string err;
        std::wstring args = L"--replace " + std::wstring(argv[2]) + L" \"" + oldrec + L"\" \"" + ish + L"\"";
        bool ok = RunHelper(args, err, 300000);
        DeleteFileW(oldrec.c_str());
        DeleteFileW(ish.c_str());
        if (!ok) { printf("REPLACE FAILED: %s\n", err.c_str()); return 2; }
        printf("replaced radio codeplug on %ls (old=%zu new=%zu records)\n", argv[2], oldcp.records.size(), newcp.records.size());
        return 0;
    }
    if (cmd == L"--check-write" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        std::string err;
        if (!ValidateRadioCodeplug(cp, err)) {
            printf("REFUSED: %s\n", err.c_str());
            return 3;
        }
        printf("OK: safe to write (identity %s/%s, %zu records)\n", cp.header.serial.c_str(),
               cp.header.model.c_str(), cp.records.size());
        return 0;
    }
    if (cmd == L"--password" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        CodeplugPassword pw;
        if (!GetCodeplugPassword(cp, pw)) { printf("no passcode record (0x00510000)\n"); return 2; }
        printf("passcode: enabled=%d password='%s'\n", pw.enabled ? 1 : 0, pw.password.c_str());
        return 0;
    }
    if (cmd == L"--set-password" && argc >= 5) {
        char pw[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[4], -1, pw, sizeof(pw), nullptr, nullptr);
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        if (!SetCodeplugPassword(cp, true, pw)) { printf("no passcode record (0x00510000)\n"); return 2; }
        std::string err;
        if (!SaveCodeplug(argv[3], cp, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        printf("wrote %ls with passcode set\n", argv[3]);
        return 0;
    }
    if (cmd == L"--clear-password" && argc >= 4) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        if (!SetCodeplugPassword(cp, false, "")) { printf("no passcode record (0x00510000)\n"); return 2; }
        std::string err;
        if (!SaveCodeplug(argv[3], cp, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        printf("wrote %ls with passcode cleared\n", argv[3]);
        return 0;
    }
    if (cmd == L"--radio-password" && argc >= 3) {
        // Native: full read, then parse the passcode record (no vendor DLL).
        Codeplug tpl;
        std::string berr;
        std::string first = BuiltinFirstModel();
        if (!first.empty()) BuiltinTemplate(first, tpl, berr);
        std::vector<ReadRequest> reqs;
        BuiltinReadRequests(reqs);
        std::wstring reqsPath = TempPath(L"pw.reqs");
        FILE* rf = _wfopen(reqsPath.c_str(), L"wb");
        if (!rf) { printf("reqs write failed\n"); return 2; }
        for (auto& r : reqs) {
            uint8_t b[8] = {uint8_t(r.part >> 8), uint8_t(r.part), uint8_t(r.tag >> 8),
                            uint8_t(r.tag),      uint8_t(r.inst >> 8), uint8_t(r.inst),
                            uint8_t(r.len >> 8), uint8_t(r.len)};
            fwrite(b, 1, 8, rf);
        }
        fclose(rf);
        std::wstring rec = TempPath(L"pw.rec");
        std::string err;
        if (!RunNativeReader(std::wstring(argv[2]), rec, reqsPath, err)) {
            printf("READ FAILED: %s\n", err.c_str());
            DeleteFileW(reqsPath.c_str());
            return 2;
        }
        std::vector<uint8_t> buf;
        if (!ReadWholeFile(rec, buf)) { printf("read output missing\n"); return 2; }
        Codeplug out;
        if (!ApplyRecBuffer(buf, tpl, out, err)) { printf("READ PARSE FAILED: %s\n", err.c_str()); return 2; }
        CodeplugPassword pw;
        if (!GetCodeplugPassword(out, pw)) { printf("no passcode record returned\n"); return 2; }
        printf("passcode: enabled=%d password='%s'\n", pw.enabled ? 1 : 0, pw.password.c_str());
        DeleteFileW(reqsPath.c_str());
        DeleteFileW(rec.c_str());
        return 0;
    }
    if (cmd == L"--report" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        printf("%s", (wantJson ? ServiceReportJson(cp, "") : ServiceReportText(cp, "")).c_str());
        return 0;
    }
    if (cmd == L"--radio-report" && argc >= 3) {
        const std::wstring port = argv[2];
        std::string banner = QueryBanner(port);
        Codeplug cp;
        std::string err;
        std::string meta;
        if (!ReadRadioToCodeplug(port, cp, err, &meta)) {
            printf("READ FAILED: %s\n", err.c_str());
            return 2;
        }
        if (!meta.empty()) banner += (banner.empty() ? "" : "\n") + meta;
        printf("%s", (wantJson ? ServiceReportJson(cp, banner) : ServiceReportText(cp, banner)).c_str());
        return 0;
    }
    if (cmd == L"--doctor" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        const bool repair = (argc >= 4) && argv[3][0] != L'-';
        DoctorResult res;
        const bool ok = DoctorCodeplug(cp, res, repair);
        printf("%s\n", res.issues.empty() ? "Codeplug doctor: healthy" : "Codeplug doctor: issues found");
        for (const auto& s : res.issues) printf("  issue: %s\n", s.c_str());
        for (const auto& s : res.fixes) printf("  fix:   %s\n", s.c_str());
        if (!ok) { printf("  fatal: no identity record; cannot repair\n"); return 3; }
        if (repair) {
            std::string err;
            if (!SaveCodeplug(argv[3], cp, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
            printf("wrote repaired %ls\n", argv[3]);
            return 0;
        }
        return res.issues.empty() ? 0 : 3;
    }
    if (cmd == L"--diff" && argc >= 4) {
        Codeplug a, b;
        if (!LoadCodeplug(argv[2], a)) { printf("LOAD A FAILED: %s\n", a.error.c_str()); return 2; }
        if (!LoadCodeplug(argv[3], b)) { printf("LOAD B FAILED: %s\n", b.error.c_str()); return 2; }
        printf("%s", (wantJson ? DiffCodeplugsJson(a, b) : DiffCodeplugsText(a, b)).c_str());
        return 0;
    }
    if (cmd == L"--copy-zone" && argc >= 7) {
        Codeplug src, dst;
        if (!LoadCodeplug(argv[2], src)) { printf("LOAD SRC FAILED: %s\n", src.error.c_str()); return 2; }
        if (!LoadCodeplug(argv[4], dst)) { printf("LOAD DST FAILED: %s\n", dst.error.c_str()); return 2; }
        std::string err;
        if (!CopyZone(dst, src, _wtoi(argv[3]), _wtoi(argv[5]), err)) {
            printf("COPY FAILED: %s\n", err.c_str());
            return 2;
        }
        if (!SaveCodeplug(argv[6], dst, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        printf("copied source zone %d into destination zone %d -> %ls\n", _wtoi(argv[3]),
               _wtoi(argv[5]), argv[6]);
        return 0;
    }
    if (cmd == L"--copy-zones" && argc >= 7) {
        Codeplug src, dst;
        if (!LoadCodeplug(argv[2], src)) { printf("LOAD SRC FAILED: %s\n", src.error.c_str()); return 2; }
        if (!LoadCodeplug(argv[4], dst)) { printf("LOAD DST FAILED: %s\n", dst.error.c_str()); return 2; }
        std::vector<int> sz = ParseIntList(argv[3]);
        std::vector<int> dz = ParseIntList(argv[5]);
        if (sz.empty() || sz.size() != dz.size()) { printf("zone list mismatch\n"); return 2; }
        std::string err;
        for (size_t i = 0; i < sz.size(); ++i)
            if (!CopyZone(dst, src, sz[i], dz[i], err, true)) {
                printf("COPY FAILED (zone %d->%d): %s\n", sz[i], dz[i], err.c_str());
                return 2;
            }
        bool trunk = false;
        for (int i = 1; i < argc; ++i)
            if (std::wstring(argv[i]) == L"--trunking") trunk = true;
        if (trunk && !CopyTrunking(dst, src, err)) { printf("TRUNKING FAILED: %s\n", err.c_str()); return 2; }
        if (!SaveCodeplug(argv[6], dst, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        printf("copied %zu zone(s)%s -> %ls\n", sz.size(), trunk ? " + trunking" : "", argv[6]);
        return 0;
    }
    if (cmd == L"--backup-radio" && argc >= 4) {
        const std::wstring port = argv[2];
        const std::wstring outdir = argv[3];
        CreateDirectoryW(outdir.c_str(), nullptr);
        // 1) native codeplug read
        Codeplug tpl;
        std::string berr;
        std::string first = BuiltinFirstModel();
        if (!first.empty()) BuiltinTemplate(first, tpl, berr);
        std::vector<ReadRequest> reqs;
        BuiltinReadRequests(reqs);
        std::wstring reqsPath = TempPath(L"bk.reqs");
        FILE* rf = _wfopen(reqsPath.c_str(), L"wb");
        if (!rf) { printf("reqs write failed\n"); return 2; }
        for (auto& r : reqs) {
            uint8_t b[8] = {uint8_t(r.part >> 8), uint8_t(r.part), uint8_t(r.tag >> 8),
                            uint8_t(r.tag),      uint8_t(r.inst >> 8), uint8_t(r.inst),
                            uint8_t(r.len >> 8), uint8_t(r.len)};
            fwrite(b, 1, 8, rf);
        }
        fclose(rf);
        std::wstring rec = TempPath(L"bk.rec");
        std::string err;
        if (!RunNativeReader(port, rec, reqsPath, err)) {
            printf("BACKUP READ FAILED: %s\n", err.c_str());
            DeleteFileW(reqsPath.c_str());
            return 2;
        }
        std::vector<uint8_t> buf;
        if (!ReadWholeFile(rec, buf)) { printf("read output missing\n"); return 2; }
        Codeplug out;
        if (!ApplyRecBuffer(buf, tpl, out, err)) { printf("READ PARSE FAILED: %s\n", err.c_str()); return 2; }
        std::string s, m;
        if (ReadIdentityFromCodeplug(out, s, m)) UpdateIdentity(out, s, m);
        std::wstring cpg = outdir + L"\\codeplug.cpg";
        if (!SaveCodeplug(cpg, out, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        DeleteFileW(reqsPath.c_str());
        DeleteFileW(rec.c_str());
        // 2) vendor helper: softpots, security attempt, info.txt (read-only)
        if (!RunHelper(L"--backup " + port + L" \"" + outdir + L"\"", err, 300000))
            printf("helper backup issue: %s\n", err.c_str());
        // 3) manifest with SHA-256
        std::wstring manifest = outdir + L"\\manifest.json";
        FILE* mf = _wfopen(manifest.c_str(), L"wb");
        if (mf) {
            fprintf(mf, "{\n  \"tool\": \"OpenDepot\",\n  \"model\": \"%s\",\n  \"serial\": \"%s\",\n  \"artifacts\": [\n",
                    out.header.model.c_str(), out.header.serial.c_str());
            const wchar_t* names[] = {L"codeplug.cpg", L"softpots.bin", L"security.bin", L"info.txt"};
            bool firstArt = true;
            for (auto n : names) {
                std::wstring p = outdir + L"\\" + n;
                std::vector<uint8_t> d;
                if (!ReadWholeFile(p, d)) continue;
                std::string hx;
                Sha256File(p, hx);
                if (!firstArt) fprintf(mf, ",\n");
                firstArt = false;
                fprintf(mf, "    {\"path\": \"%ls\", \"bytes\": %zu, \"sha256\": \"%s\"}", n, d.size(),
                        hx.c_str());
            }
            fprintf(mf, "\n  ]\n}\n");
            fclose(mf);
        }
        printf("backup written to %ls (model %s serial %s)\n", outdir.c_str(),
               out.header.model.c_str(), out.header.serial.c_str());
        return 0;
    }
    if (cmd == L"--unlock" && argc >= 3) {
        std::string err;
        if (!RunHelper(L"--unlock " + std::wstring(argv[2]), err)) {
            printf("UNLOCK FAILED: %s\n", err.c_str());
            return 2;
        }
        return 0;
    }
    if (cmd == L"--security" && argc >= 3) {
        std::string err;
        if (!RunHelper(L"--security " + std::wstring(argv[2]), err)) {
            printf("SECURITY FAILED: %s\n", err.c_str());
            return 2;
        }
        return 0;
    }
    if (cmd == L"--write-radio" && argc >= 4) {
        Codeplug cp;
        if (!LoadCodeplug(argv[3], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        std::string verr;
        if (!ValidateRadioCodeplug(cp, verr)) {
            printf("WRITE REFUSED: %s\n", verr.c_str());
            return 2;
        }
        std::string err;
        if (!RunRawWriter(std::wstring(argv[2]), std::wstring(argv[3]), err)) {
            printf("WRITE FAILED: %s\n", err.c_str());
            return 2;
        }
        printf("wrote %zu records to the radio on %ls (identity %s/%s)\n", cp.records.size(),
               argv[2], cp.header.serial.c_str(), cp.header.model.c_str());
        std::string verr2;
        Sleep(8000);
        if (!VerifyIdentityAfterWrite(std::wstring(argv[2]), cp, verr2)) {
            Sleep(4000);
            if (!VerifyIdentityAfterWrite(std::wstring(argv[2]), cp, verr2)) {
                printf("WRITE FAILED: post-write verification failed: %s\n", verr2.c_str());
                return 3;
            }
            printf("verified identity on the radio\n");
        } else {
            printf("verified identity on the radio\n");
        }
        return 0;
    }
    if (cmd == L"--conventional" && argc >= 4) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        std::string err;
        if (!MakeConventional(cp, err)) { printf("CONVENTIONAL FAILED: %s\n", err.c_str()); return 2; }
        if (!SaveCodeplug(argv[3], cp, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        printf("wrote %ls (%zu records)\n", argv[3], cp.records.size());
        return 0;
    }
    if (cmd == L"--conventional-ref" && argc >= 5) {
        Codeplug ref, src, out;
        if (!LoadCodeplug(argv[2], ref)) { printf("LOAD REF FAILED: %s\n", ref.error.c_str()); return 2; }
        if (!LoadCodeplug(argv[3], src)) { printf("LOAD SRC FAILED: %s\n", src.error.c_str()); return 2; }
        std::string err;
        if (!MakeConventionalFromRef(out, ref, src, err)) { printf("CONVENTIONAL REF FAILED: %s\n", err.c_str()); return 2; }
        if (!SaveCodeplug(argv[4], out, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        printf("wrote %ls (%zu records)\n", argv[4], out.records.size());
        return 0;
    }
    if (cmd == L"--create-conventional" && argc >= 6) {
        char serial[64] = {}, model[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[4], -1, serial, sizeof(serial), nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, argv[5], -1, model, sizeof(model), nullptr, nullptr);
        Codeplug tpl;
        if (!LoadCodeplug(argv[2], tpl)) { printf("LOAD FAILED: %s\n", tpl.error.c_str()); return 2; }
        CreateOptions opt;
        opt.serial = serial;
        opt.model = model;
        opt.flashcode = "600022150000";
        Codeplug out;
        std::string err;
        if (!CreateCodeplug(tpl, opt, out, err)) { printf("CREATE FAILED: %s\n", err.c_str()); return 2; }
        if (!SaveCodeplug(argv[3], out, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        printf("wrote %ls (%zu records) model=%s serial=%s flashcode=%s\n", argv[3],
               out.records.size(), model, serial, GetFlashcode(out).c_str());
        return 0;
    }
    if (cmd == L"--zones" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        std::vector<ZoneView> zones;
        if (!ParseZones(cp, zones)) {
            printf("no zone table\n");
            return 2;
        }
        for (size_t z = 0; z < zones.size(); ++z) {
            printf("Zone %zu: %s  (%zu channels)\n", z + 1, zones[z].name.c_str(), zones[z].channels.size());
            for (size_t c = 0; c < zones[z].channels.size(); ++c) {
                const auto& ch = zones[z].channels[c];
                printf("   %2zu. %-16s  rx=%.5f tx=%.5f  flags=0x%02X\n", c + 1,
                       ch.name.c_str(), WordToMHz(ch.rxWord), WordToMHz(ch.txWord), ch.flags);
            }
        }
        return 0;
    }
    if (cmd == L"--set-zone-name" && argc >= 6) {
        char name[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[5], -1, name, sizeof(name), nullptr, nullptr);
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        if (!SetZoneName(cp, _wtoi(argv[4]), name)) { printf("zone set failed\n"); return 2; }
        std::string err;
        if (!SaveCodeplug(argv[3], cp, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        printf("wrote %ls\n", argv[3]);
        return 0;
    }
    if (cmd == L"--set-channel-name" && argc >= 7) {
        char name[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[6], -1, name, sizeof(name), nullptr, nullptr);
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        if (!SetChannelName(cp, _wtoi(argv[4]), _wtoi(argv[5]), name)) { printf("channel set failed\n"); return 2; }
        std::string err;
        if (!SaveCodeplug(argv[3], cp, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        printf("wrote %ls\n", argv[3]);
        return 0;
    }
    if (cmd == L"--set-channel-freq" && argc >= 8) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) { printf("LOAD FAILED: %s\n", cp.error.c_str()); return 2; }
        if (!SetChannelFreq(cp, _wtoi(argv[4]), _wtoi(argv[5]), _wtof(argv[6]), _wtof(argv[7]))) {
            printf("channel set failed\n");
            return 2;
        }
        std::string err;
        if (!SaveCodeplug(argv[3], cp, err)) { printf("SAVE FAILED: %s\n", err.c_str()); return 2; }
        printf("wrote %ls\n", argv[3]);
        return 0;
    }
    if (cmd == L"--flashcode-options" && argc >= 3) {
        char name[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[2], -1, name, sizeof(name), nullptr, nullptr);
        const FlashcodePlatform* p = FindFlashcodePlatform(name);
        if (!p) p = FindFlashcodePlatform(PlatformForRadio(name));
        if (!p) { printf("no platform table for %s\n", name); return 2; }
        printf("platform %s (%zu options)\n", p->name.c_str(), p->options.size());
        for (const auto& o : p->options)
            printf("  d%db%d  %-5s %s\n", o.nibble, o.bit, o.code.c_str(), o.label.c_str());
        return 0;
    }
    if (cmd == L"--build-flashcode" && argc >= 5) {
        char name[64] = {}, base[64] = {}, codes[512] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[2], -1, name, sizeof(name), nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, argv[3], -1, base, sizeof(base), nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, argv[4], -1, codes, sizeof(codes), nullptr, nullptr);
        const FlashcodePlatform* p = FindFlashcodePlatform(name);
        if (!p) p = FindFlashcodePlatform(PlatformForRadio(name));
        if (!p) { printf("no platform table for %s\n", name); return 2; }
        std::vector<std::string> sel;
        std::string cur;
        for (const char* c = codes;; ++c) {
            if (*c == ',' || *c == 0) {
                if (!cur.empty()) { sel.push_back(cur); cur.clear(); }
                if (*c == 0) break;
            } else {
                cur.push_back(*c);
            }
        }
        std::string fc = BuildFlashcode(p->name, base, sel);
        printf("%s\n", fc.empty() ? "FAILED" : fc.c_str());
        return fc.empty() ? 2 : 0;
    }
    if (cmd == L"--flashcode" && argc >= 3) {
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        std::string platform = "XTS2500";
        if (argc >= 4) {
            char b[64] = {};
            WideCharToMultiByte(CP_UTF8, 0, argv[3], -1, b, sizeof(b), nullptr, nullptr);
            platform = b;
        }
        std::string platName =
            FindFlashcodePlatform(platform) ? platform : PlatformForRadio(platform);
        std::string fc = GetFlashcode(cp);
        printf("FLASHcode: %s\n", fc.c_str());
        const FlashcodePlatform* p = platName.empty() ? nullptr : FindFlashcodePlatform(platName);
        printf("platform: %s\n", p ? p->name.c_str() : "(unknown)");
        for (const auto& f : DecodeFlashcode(platName, fc)) printf("  - %s\n", f.c_str());
        return 0;
    }
    if (cmd == L"--set-flashcode" && argc >= 5) {
        char fc[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[4], -1, fc, sizeof(fc), nullptr, nullptr);
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        std::string canonical, err;
        if (!SetFlashcodeMap(cp, fc, canonical, err)) {
            printf("FLASHCODE FAILED: %s\n", err.c_str());
            return 2;
        }
        if (!SaveCodeplug(argv[3], cp, err)) {
            printf("SAVE FAILED: %s\n", err.c_str());
            return 2;
        }
        printf("wrote %ls with FLASHcode %s\n", argv[3], canonical.c_str());
        return 0;
    }
    if (cmd == L"--set-identity" && argc >= 6) {
        char serial[64] = {}, model[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, argv[4], -1, serial, sizeof(serial), nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, argv[5], -1, model, sizeof(model), nullptr, nullptr);
        Codeplug cp;
        if (!LoadCodeplug(argv[2], cp)) {
            printf("LOAD FAILED: %s\n", cp.error.c_str());
            return 2;
        }
        if (!UpdateIdentity(cp, serial, model)) {
            printf("IDENTITY REJECTED (new values must fit existing fields)\n");
            return 2;
        }
        std::string err;
        if (!SaveCodeplug(argv[3], cp, err)) {
            printf("SAVE FAILED: %s\n", err.c_str());
            return 2;
        }
        printf("wrote %ls\n", argv[3]);
        return 0;
    }
    printf("unknown or incomplete command\n");
    return 1;
}





