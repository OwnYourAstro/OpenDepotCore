// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include "opendepot/core/radio.h"
#include "opendepot/core/serial_link.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace opendepot {
namespace {

constexpr int kHostPort = 8001;
constexpr int kRadioPort = 8002;
constexpr uint8_t kIpHost[4] = {192, 168, 128, 2};
constexpr uint8_t kIpRadio[4] = {192, 168, 128, 1};
constexpr uint16_t kTcpFin = 1, kTcpSyn = 2, kTcpRst = 4, kTcpPsh = 8, kTcpAck = 0x10;

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

uint16_t Be16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
uint32_t Be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
void PutBe16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
}
void PutBe32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24));
    v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
}

std::vector<uint8_t> Hex(const char* s) {
    std::vector<uint8_t> out;
    int hi = -1;
    for (const char* p = s; *p; ++p) {
        int v = -1;
        if (*p >= '0' && *p <= '9') v = *p - '0';
        else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') v = *p - 'A' + 10;
        else continue;
        if (hi < 0) hi = v;
        else {
            out.push_back(uint8_t((hi << 4) | v));
            hi = -1;
        }
    }
    return out;
}

uint16_t Fcs(const uint8_t* data, size_t n) {
    uint16_t f = 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
        f ^= data[i];
        for (int b = 0; b < 8; ++b) f = (f & 1) ? uint16_t((f >> 1) ^ 0x8408) : uint16_t(f >> 1);
    }
    return uint16_t(f ^ 0xFFFF);
}

std::vector<uint8_t> Unescape(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == 0x7D && i + 1 < in.size()) {
            out.push_back(uint8_t(in[++i] ^ 0x20));
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

std::vector<uint8_t> Frame(uint16_t proto, const std::vector<uint8_t>& data, bool pfc, bool addr) {
    std::vector<uint8_t> body;
    if (addr) {
        body.push_back(0xFF);
        body.push_back(0x03);
    }
    if (pfc) {
        body.push_back(uint8_t(proto));
    } else {
        PutBe16(body, proto);
    }
    body.insert(body.end(), data.begin(), data.end());
    const uint16_t f = Fcs(body.data(), body.size());
    body.push_back(uint8_t(f));
    body.push_back(uint8_t(f >> 8));

    std::vector<uint8_t> out;
    out.push_back(0x7E);
    for (uint8_t b : body) {
        if (b == 0x7E || b == 0x7D || b < 0x20) {
            out.push_back(0x7D);
            out.push_back(uint8_t(b ^ 0x20));
        } else {
            out.push_back(b);
        }
    }
    out.push_back(0x7E);
    return out;
}

uint16_t Checksum(const uint8_t* data, size_t n) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < n; i += 2) sum += (uint32_t(data[i]) << 8) | data[i + 1];
    if (n & 1) sum += uint32_t(data[n - 1]) << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return uint16_t(~sum & 0xFFFF);
}

struct TcpView {
    bool ok = false;
    uint16_t sport = 0, dport = 0;
    uint32_t seq = 0, ack = 0;
    uint16_t flags = 0;
    std::vector<uint8_t> data;
};

bool ParseTcp(const std::vector<uint8_t>& f, TcpView& out) {
    const uint8_t* ip = nullptr;
    size_t iplen = 0;
    if (!f.empty() && f[0] == 0x21) {
        ip = f.data() + 1;
        iplen = f.size() - 1;
    } else if (!f.empty() && f[0] == 0x00) {
        ip = f.data();
        iplen = f.size();
    } else {
        return false;
    }
    if (iplen < 20 || (ip[0] >> 4) != 4) return false;
    const size_t ihl = (ip[0] & 0x0F) * 4;
    size_t total = Be16(ip + 2);
    if (total > iplen) total = iplen;
    if (total < ihl + 20 || ip[9] != 6) return false;
    const uint8_t* th = ip + ihl;
    const size_t thlen = total - ihl;
    const uint16_t off = Be16(th + 12);
    const size_t hlen = (off >> 12) * 4;
    if (hlen < 20 || hlen > thlen) return false;
    out.ok = true;
    out.sport = Be16(th);
    out.dport = Be16(th + 2);
    out.seq = Be32(th + 4);
    out.ack = Be32(th + 8);
    out.flags = off & 0x1FF;
    out.data.assign(th + hlen, th + thlen);
    return true;
}

std::vector<uint8_t> IpPacket(uint8_t proto, const std::vector<uint8_t>& payload,
                              uint16_t ident) {
    std::vector<uint8_t> h(20, 0);
    const size_t total = 20 + payload.size();
    h[0] = 0x45;
    h[2] = uint8_t(total >> 8);
    h[3] = uint8_t(total);
    h[4] = uint8_t(ident >> 8);
    h[5] = uint8_t(ident);
    h[6] = 0x40;
    h[8] = 64;
    h[9] = proto;
    std::memcpy(h.data() + 12, kIpHost, 4);
    std::memcpy(h.data() + 16, kIpRadio, 4);
    const uint16_t chk = Checksum(h.data(), h.size());
    h[10] = uint8_t(chk >> 8);
    h[11] = uint8_t(chk);
    h.insert(h.end(), payload.begin(), payload.end());
    return h;
}

std::vector<uint8_t> TcpSegment(uint32_t seq, uint32_t ack, uint16_t flags,
                                const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> h(20, 0);
    h[0] = uint8_t(kHostPort >> 8);
    h[1] = uint8_t(kHostPort);
    h[2] = uint8_t(kRadioPort >> 8);
    h[3] = uint8_t(kRadioPort);
    h[4] = uint8_t(seq >> 24);
    h[5] = uint8_t(seq >> 16);
    h[6] = uint8_t(seq >> 8);
    h[7] = uint8_t(seq);
    h[8] = uint8_t(ack >> 24);
    h[9] = uint8_t(ack >> 16);
    h[10] = uint8_t(ack >> 8);
    h[11] = uint8_t(ack);
    h[12] = uint8_t((5 << 4) | (flags >> 8));
    h[13] = uint8_t(flags);
    h[14] = 0x16;
    h[15] = 0xD0;

    std::vector<uint8_t> pseudo;
    pseudo.insert(pseudo.end(), kIpHost, kIpHost + 4);
    pseudo.insert(pseudo.end(), kIpRadio, kIpRadio + 4);
    pseudo.push_back(0);
    pseudo.push_back(6);
    PutBe16(pseudo, uint16_t(h.size() + payload.size()));
    pseudo.insert(pseudo.end(), h.begin(), h.end());
    pseudo.insert(pseudo.end(), payload.begin(), payload.end());
    const uint16_t chk = Checksum(pseudo.data(), pseudo.size());
    h[16] = uint8_t(chk >> 8);
    h[17] = uint8_t(chk);
    h.insert(h.end(), payload.begin(), payload.end());
    return h;
}

std::vector<uint8_t> AuthResponse(const std::vector<uint8_t>& challenge) {
    if (challenge.size() < 32) return {};
    uint32_t state = 0x51E88D21;
    std::vector<uint8_t> out(32);
    for (int k = 0; k < 32; ++k) {
        uint8_t b = challenge[k];
        const uint8_t rev = challenge[31 - k];
        for (int bit = 7; bit >= 1; --bit) {
            const uint32_t fb = ((rev >> bit) & 1) ^ ((state >> 9) & 1);
            state = (state * 2 + fb) & 0xFFFFFFFF;
            b ^= uint8_t((fb << bit) & 0xFF);
        }
        out[k] = b;
    }
    return out;
}

std::string AsciiField(const std::vector<uint8_t>& r, size_t offset) {
    std::string out;
    for (size_t i = offset; i < r.size(); ++i) {
        if (r[i] == 0) break;
        if (r[i] >= 32 && r[i] < 127) out.push_back(char(r[i]));
    }
    return out;
}

std::string BannerField(const std::string& banner, const char* key) {
    const std::string tag = std::string(key) + ":";
    const size_t p = banner.find(tag);
    if (p == std::string::npos) return {};
    const size_t start = p + tag.size();
    const size_t end = banner.find_first_of("\r\n", start);
    return banner.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

}  // namespace

struct RadioSession::Impl {
    std::unique_ptr<SerialLink> link;
    bool link_open = false;
    std::string banner;
    std::vector<uint8_t> frame_buf;
    std::vector<uint8_t> rxbuf;
    uint32_t my_seq = 0;
    uint32_t their_seq = 0;
    uint16_t ip_id = 1;
    bool lcp_up = false;
    bool ipcp_sent = false;
    bool ipcp_radio_up = false;
    bool ipcp_host_up = false;

    FILE* log = nullptr;
    ~Impl() {
        if (log) fclose(log);
    }

    void Log(const char* fmt, ...) {
        if (!log) return;
        va_list ap;
        va_start(ap, fmt);
        vfprintf(log, fmt, ap);
        va_end(ap);
        fputc('\n', log);
        fflush(log);
    }

    static std::string HexShort(const std::vector<uint8_t>& v, size_t max = 40) {
        static const char* kHex = "0123456789abcdef";
        std::string s;
        for (size_t i = 0; i < v.size() && i < max; ++i) {
            s.push_back(kHex[v[i] >> 4]);
            s.push_back(kHex[v[i] & 0x0F]);
        }
        if (v.size() > max) s += "...";
        return s;
    }

    bool OpenLink(uint32_t baud, std::string& err) {
        if (!link) {
            err = "no serial link";
            return false;
        }
        CloseLink();
        link_open = link->Open(baud, err);
        if (link_open) link->Purge();
        return link_open;
    }

    bool Reconfigure(uint32_t baud, std::string& err) {
        if (!link_open) return OpenLink(baud, err);
        if (link->Reconfigure(baud, err)) return true;
        return OpenLink(baud, err);
    }

    void CloseLink() {
        if (link && link_open) link->Close();
        link_open = false;
    }

    size_t ReadRaw(uint8_t* data, size_t n, int timeout_ms) {
        if (!link_open || !link) return 0;
        return link->Read(data, n, timeout_ms);
    }

    bool WriteRaw(const uint8_t* data, size_t n) {
        return link_open && link && link->Write(data, n);
    }

    void WriteStr(const char* s) {
        WriteRaw(reinterpret_cast<const uint8_t*>(s), std::strlen(s));
    }

    std::string ReadString(int total_ms = 1200) {
        std::string out;
        const int64_t end = NowMs() + total_ms;
        int64_t last_data = NowMs();
        while (NowMs() < end) {
            uint8_t tmp[512];
            const size_t got = ReadRaw(tmp, sizeof(tmp), 10);
            if (got) {
                out.append(reinterpret_cast<char*>(tmp), got);
                last_data = NowMs();
            } else if (NowMs() - last_data > 150) {
                break;
            }
        }
        return out;
    }

    std::vector<std::vector<uint8_t>> ReadFrames(int ms) {
        std::vector<std::vector<uint8_t>> out;
        const int64_t end = NowMs() + ms;
        while (NowMs() < end) {
            uint8_t tmp[4096];
            const size_t got = ReadRaw(tmp, sizeof(tmp), 10);
            if (got) {
                frame_buf.insert(frame_buf.end(), tmp, tmp + got);
                size_t i = 0;
                while (true) {
                    auto p = std::find(frame_buf.begin() + i, frame_buf.end(), 0x7E);
                    if (p == frame_buf.end()) break;
                    const size_t a = size_t(p - frame_buf.begin());
                    auto q = std::find(p + 1, frame_buf.end(), 0x7E);
                    if (q == frame_buf.end()) break;
                    const size_t b = size_t(q - frame_buf.begin());
                    std::vector<uint8_t> raw(frame_buf.begin() + a + 1, frame_buf.begin() + b);
                    frame_buf.erase(frame_buf.begin(), frame_buf.begin() + b + 1);
                    i = 0;
                    out.push_back(Unescape(raw));
                }
                if (!out.empty()) return out;
            } else {
                SleepMs(2);
            }
        }
        return out;
    }

    void WriteFrame(uint16_t proto, const std::vector<uint8_t>& data, bool pfc, bool addr) {
        const auto f = Frame(proto, data, pfc, addr);
        WriteRaw(f.data(), f.size());
    }

    void SendIp(const std::vector<uint8_t>& payload, uint8_t proto = 6) {
        const auto pkt = IpPacket(proto, payload, ip_id++);
        const auto f = Frame(0x21, pkt, true, false);
        WriteRaw(f.data(), f.size());
    }

    void Ack() { SendIp(TcpSegment(my_seq, their_seq, kTcpAck, {})); }

    void HandleLink(const std::vector<uint8_t>& f) {
        if (f.size() < 8 || f[0] != 0xFF || f[1] != 0x03) return;
        const uint16_t proto = Be16(f.data() + 2);
        const std::vector<uint8_t> data(f.begin() + 6, f.end() - 2);
        if (proto == 0xC021) {
            const uint8_t code = f[4];
            Log("LCP code=%u id=%u len=%zu", code, f[5], data.size());
            if (code == 1) {
                std::vector<uint8_t> body{2, f[5]};
                body.insert(body.end(), data.begin(), data.end());
                WriteFrame(0xC021, body, false, true);
            } else if (code == 2) {
                lcp_up = true;
                Log("LCP up");
            } else if (code == 9) {
                Log("LCP terminate");
                std::vector<uint8_t> body{0x0A, f[5]};
                body.insert(body.end(), data.begin(), data.end());
                WriteFrame(0xC021, body, false, true);
            }
        } else if (proto == 0x8021) {
            const uint8_t code = f[4];
            if (code == 1) {
                std::vector<uint8_t> vj;
                bool has_vj = false;
                for (size_t i = 2; i + 2 <= data.size();) {
                    const uint8_t t = data[i];
                    const uint8_t l = data[i + 1];
                    if (l < 2 || i + l > data.size()) break;
                    if (t == 2) {
                        has_vj = true;
                        vj.insert(vj.end(), data.begin() + i, data.begin() + i + l);
                    }
                    i += l;
                }
                Log("IPCP req id=%u has_vj=%d opts=%s", f[5], has_vj ? 1 : 0,
                    HexShort(data).c_str());
                if (has_vj) {
                    std::vector<uint8_t> body{4, f[5]};
                    PutBe16(body, static_cast<uint16_t>(4 + vj.size()));
                    body.insert(body.end(), vj.begin(), vj.end());
                    WriteFrame(0x8021, body, false, true);
                    Log("IPCP sent reject vj=%s", HexShort(vj).c_str());
                } else {
                    std::vector<uint8_t> body{2, f[5]};
                    body.insert(body.end(), data.begin(), data.end());
                    WriteFrame(0x8021, body, false, true);
                    ipcp_radio_up = true;
                    Log("IPCP sent ack");
                }
            } else if (code == 2) {
                ipcp_host_up = true;
                Log("IPCP ack id=%u", f[5]);
            } else if (code == 4) {
                Log("IPCP reject id=%u", f[5]);
            } else if (code == 3) {
                Log("IPCP nak id=%u", f[5]);
            }
        }
    }

    void Pump(int ms) {
        const int64_t end = NowMs() + ms;
        while (NowMs() < end) {
            for (const auto& f : ReadFrames(10)) {
                if (f.size() >= 2 && f[0] == 0xFF) {
                    HandleLink(f);
                    continue;
                }
                TcpView v;
                if (!ParseTcp(f, v) || v.data.empty()) continue;
                const uint32_t diff = v.seq - their_seq;
                if (diff == 0) {
                    rxbuf.insert(rxbuf.end(), v.data.begin(), v.data.end());
                    their_seq += uint32_t(v.data.size());
                } else if (diff & 0x80000000u) {
                    const uint32_t skipped = their_seq - v.seq;
                    if (skipped < v.data.size()) {
                        rxbuf.insert(rxbuf.end(), v.data.begin() + skipped, v.data.end());
                        their_seq += uint32_t(v.data.size() - skipped);
                    }
                }
                Ack();
            }
        }
    }

    void SendMsg(const std::vector<uint8_t>& body) {
        std::vector<uint8_t> wire;
        PutBe16(wire, static_cast<uint16_t>(body.size()));
        wire.insert(wire.end(), body.begin(), body.end());
        SendIp(TcpSegment(my_seq, their_seq, kTcpAck | kTcpPsh, wire));
        my_seq += uint32_t(wire.size());
    }

    std::vector<uint8_t> RecvMsg(int ms) {
        const int64_t end = NowMs() + ms;
        while (true) {
            if (rxbuf.size() >= 2) {
                const uint16_t len = Be16(rxbuf.data());
                if (len <= 0xFFFF && rxbuf.size() >= size_t(2) + len) {
                    std::vector<uint8_t> out(rxbuf.begin() + 2, rxbuf.begin() + 2 + len);
                    rxbuf.erase(rxbuf.begin(), rxbuf.begin() + 2 + len);
                    return out;
                }
            }
            if (NowMs() > end) return {};
            Pump(10);
        }
    }

    std::vector<uint8_t> Req(const std::vector<uint8_t>& body, int ms) {
        SendMsg(body);
        return RecvMsg(ms);
    }

    bool Init(std::string& err) {
        if (const char* log_path = std::getenv("OPENDEPOT_NATIVE_LOG"))
            if (*log_path) log = std::fopen(log_path, "w");
        Log("native init");

        auto at_response = [](const std::string& r) {
            return r.find("CARRIER") != std::string::npos ||
                   r.find("RADIO") != std::string::npos ||
                   r.find("OK") != std::string::npos ||
                   r.find("ERROR") != std::string::npos;
        };
        auto probe = [&](int baud, std::string& out) {
            std::string open_err;
            Log("probe open %d", baud);
            if (!OpenLink(baud, open_err)) {
                Log("probe open failed %d: %s", baud, open_err.c_str());
                return false;
            }
            Log("probe write AT&V %d", baud);
            WriteStr("AT&V\r");
            Log("probe read %d", baud);
            out = ReadString(3000);
            Log("AT&V @%d len=%zu: %s", baud, out.size(), out.c_str());
            CloseLink();
            return at_response(out);
        };

        std::string r9600, r115200;
        bool at9600 = probe(9600, r9600);
        if (!at9600 && probe(115200, r115200)) {
            // The radio was left at 115200. Normalize back to 9600 so the
            // Depot DATLCL/IPR sequence is used exactly as on Windows.
            std::string e;
            Log("normalize to 9600");
            if (OpenLink(115200, e)) {
                WriteStr("AT+IPR=9600\r");
                const std::string nr = ReadString(900);
                Log("normalize resp: %s", nr.c_str());
                CloseLink();
            } else {
                Log("normalize open failed: %s", e.c_str());
            }
            SleepMs(600);
            at9600 = probe(9600, r9600);
        }

        if (at9600) {
            std::string e;
            if (!OpenLink(9600, e)) {
                err = e;
                return false;
            }
            Log("DATLCL at 9600");
            WriteStr("AT#DATLCL=1\r");
            const std::string dcl = ReadString(900);
            Log("DATLCL resp: %s", dcl.c_str());
            Log("IPR=115200 at 9600");
            WriteStr("AT+IPR=115200\r");
            const std::string ipr = ReadString(900);
            Log("IPR resp: %s", ipr.c_str());
            banner = r9600;
            CloseLink();
            SleepMs(500);
        } else {
            // Last resort: use 115200 AT mode directly.
            std::string e;
            if (!OpenLink(115200, e)) {
                err = r115200.empty() ? "no AT response at 9600 or 115200" : e;
                return false;
            }
            WriteStr("AT&V\r");
            banner = ReadString(1500);
            WriteStr("AT#DATLCL=1\r");
            ReadString(600);
            CloseLink();
            SleepMs(500);
        }

        if (!OpenLink(115200, err)) return false;
        WriteStr("CLIENT");
        Log("CLIENT sent");
        // Keep reading continuously; sleeping here overflows the FTDI buffer and
        // can lose the radio's first LCP frames.
        int64_t client_end = NowMs() + 1500;
        while (NowMs() < client_end) {
            for (const auto& f : ReadFrames(100)) {
                Log("rx post-client %s", HexShort(f).c_str());
                HandleLink(f);
            }
        }

        Log("LCP tx");
        WriteFrame(0xC021, Hex("010100140206000000000506000148b807020802"), false, true);
        int64_t end = NowMs() + 8000;
        int64_t last_ipcp_tx = 0;
        while (NowMs() < end) {
            for (const auto& f : ReadFrames(500)) {
                Log("rx link %s", HexShort(f).c_str());
                HandleLink(f);
            }
            if (lcp_up && !ipcp_sent) {
                Log("IPCP tx (LCP up)");
                WriteFrame(0x8021, Hex("0101000a0306c0a88002"), false, true);
                ipcp_sent = true;
                last_ipcp_tx = NowMs();
            } else if (ipcp_sent && !ipcp_host_up && NowMs() - last_ipcp_tx > 1500) {
                Log("IPCP retransmit");
                WriteFrame(0x8021, Hex("0101000a0306c0a88002"), false, true);
                last_ipcp_tx = NowMs();
            }
            if (ipcp_host_up && ipcp_radio_up) break;
        }
        Log("IPCP state radio=%d host=%d", ipcp_radio_up ? 1 : 0, ipcp_host_up ? 1 : 0);
        if (!(ipcp_host_up && ipcp_radio_up)) {
            err = "PPP IPCP did not come up";
            EmergencyEnd();
            return false;
        }

        my_seq = 0x12345678;
        their_seq = 0;
        Log("TCP SYN");
        SendIp(TcpSegment(my_seq, 0, kTcpSyn, {}));
        uint32_t radio_seq = 0;
        bool synack = false;
        const int64_t end2 = NowMs() + 4000;
        while (NowMs() < end2 && !synack) {
            for (const auto& f : ReadFrames(500)) {
                if (f.size() >= 2 && f[0] == 0xFF) {
                    HandleLink(f);
                    continue;
                }
                TcpView v;
                if (ParseTcp(f, v) && (v.flags & kTcpSyn) && (v.flags & kTcpAck)) {
                    radio_seq = v.seq;
                    synack = true;
                    Log("SYN-ACK seq=%08x", radio_seq);
                    break;
                }
            }
        }
        if (!synack) {
            err = "no TCP SYN-ACK from radio";
            EmergencyEnd();
            return false;
        }
        my_seq = 0x12345679;
        their_seq = radio_seq + 1;
        SendIp(TcpSegment(my_seq, their_seq, kTcpAck, {}));
        return true;
    }

    // Called on every failure path and on Close. Ends program mode if possible,
    // then returns the radio to AT mode at whichever baud it is using.
    void ResetToAt() {
        CloseLink();
        SleepMs(1500);  // let the radio notice that PPP carrier is gone
        for (int baud : {115200, 9600}) {
            std::string e;
            if (!OpenLink(baud, e)) continue;
            WriteStr("ATZ\r");
            ReadString(800);
            WriteStr("ATZ\r");
            ReadString(800);
            WriteStr("AT+IPR=9600\r");
            ReadString(500);
            WriteStr("AT&V\r");
            const std::string r = ReadString(3000);
            CloseLink();
            if (r.find("RADIO:") != std::string::npos || r.find("OK") != std::string::npos) break;
        }
    }

    void EmergencyEnd() {
        if (link_open) {
            Req(Hex("010601"), 1000);
            CloseLink();
        }
        ResetToAt();
    }

    // Read-only sessions must NOT send the program-mode config or the
    // SendReOrg command; doing so can rebuild an inconsistent ISH directory.
    void ReadPreamble() {
        Req(Hex("010600"), 2000);
        Req(Hex("000f00"), 2000);
        Req(Hex("001100"), 2000);
        Req(Hex("0012"), 2000);
        Req(Hex("0300"), 3000);
    }

    bool ProgramAndAuth(std::string& err) {
        Req(Hex("010600"), 2000);
        Req(Hex("01048512bf00000000"), 2000);
        Req(Hex("000f00"), 2000);
        auto r = Req(Hex("0300"), 3000);
        Log("auth challenge size=%zu", r.size());
        if (!(r.size() >= 35 && r[0] == 0x83 && r[1] == 0x00)) {
            err = "auth challenge failed";
            EmergencyEnd();
            return false;
        }
        const std::vector<uint8_t> ch(r.begin() + 3, r.begin() + 35);
        std::vector<uint8_t> body{0x03, 0x01};
        const auto a2 = AuthResponse(ch);
        body.insert(body.end(), a2.begin(), a2.end());
        r = Req(body, 3000);
        Log("auth reply %s", HexShort(r).c_str());
        if (!(r.size() >= 2 && r[0] == 0x83 && r[1] == 0x01)) {
            err = "auth rejected";
            EmergencyEnd();
            return false;
        }
        Req(Hex("010881"), 2000);
        Req(Hex("010280004700000001"), 2000);
        Req(Hex("010380000201f4"), 2000);
        Req(Hex("0103800e0b0fff"), 2000);
        Req(Hex("01078000"), 2000);
        return true;
    }
};

RadioSession::RadioSession() : impl_(new Impl()) {}
RadioSession::~RadioSession() = default;

std::unique_ptr<RadioSession> RadioSession::OpenWithLink(std::unique_ptr<SerialLink> link,
                                                         std::string& err) {
    if (!link) {
        err = "no serial link";
        return nullptr;
    }
    std::unique_ptr<RadioSession> session(new RadioSession());
    session->impl_->link = std::move(link);
    if (!session->impl_->Init(err)) return nullptr;
    return session;
}

void RadioSession::Close() {
    if (impl_->link_open) impl_->EmergencyEnd();
}

bool RadioSession::Info(RadioInfo& out, std::string& err) {
    impl_->Req(Hex("010600"), 2000);
    const auto rv = impl_->Req(Hex("000f00"), 2000);
    const auto rs = impl_->Req(Hex("001100"), 2000);
    out.banner = impl_->banner;
    out.host = AsciiField(rv, 3);
    out.serial = AsciiField(rs, 3);
    out.model = BannerField(impl_->banner, "MODEL");
    (void)err;
    return true;
}

bool RadioSession::ReadRequests(const std::vector<RadioReadRequest>& reqs,
                                std::vector<Record>& out, std::string& err) {
    impl_->ReadPreamble();
    std::set<uint32_t> seen;
    for (const auto& q : reqs) {
        auto attempt = [&](uint16_t part) {
            std::vector<uint8_t> body{0x01, 0x00, uint8_t(part)};
            PutBe16(body, q.tag);
            PutBe16(body, q.inst);
            PutBe16(body, q.len);
            PutBe16(body, 0);
            return impl_->Req(body, 3000);
        };
        uint16_t used = q.part;
        auto r = attempt(used);
        if (!(r.size() >= 3 && r[0] == 0x81 && r[1] == 0x00 && r[2] == 0x00)) {
            const uint16_t alt = static_cast<uint16_t>(q.part ^ 0x0001);
            auto r2 = attempt(alt);
            if (r2.size() >= 3 && r2[0] == 0x81 && r2[1] == 0x00 && r2[2] == 0x00) {
                r = std::move(r2);
                used = alt;
            }
        }
        impl_->Log("read %02x/%04x/%04x len=%u -> %s", used, q.tag, q.inst, q.len,
                   Impl::HexShort(r).c_str());
        if (!(r.size() >= 14 && r[0] == 0x81 && r[1] == 0x00 && r[2] == 0x00)) continue;
        const uint16_t len = Be16(r.data() + 12);
        if (len == 0 || r.size() < size_t(14) + len) continue;

        const uint32_t id = (uint32_t(q.tag) << 16) | q.inst;
        if (!seen.insert(id).second) continue;

        Record rec;
        rec.id = id;
        rec.type = used;
        PutBe32(rec.raw, rec.id);
        PutBe16(rec.raw, len);
        rec.raw.insert(rec.raw.end(), r.begin() + 14, r.begin() + 14 + len);
        rec.checksum = Be16(rec.raw.data() + rec.raw.size() - 2);
        rec.verified = Crc16Ccitt(rec.raw.data() + 6, rec.raw.size() - 8) == rec.checksum;
        out.push_back(std::move(rec));
    }
    impl_->Req(Hex("010601"), 3000);
    impl_->ResetToAt();
    return true;
}

bool RadioSession::ReadAllTypeInstances(const std::vector<RadioReadRequest>& types,
                                        std::vector<Record>& out, std::string& err) {
    impl_->ReadPreamble();
    std::set<uint64_t> seen;
    for (const auto& t : types) {
        uint16_t used = t.part;
        uint16_t read_len = t.len > 0x370 ? t.len : 0x370;
        const int probe_timeout = t.len == 0 ? 600 : 3000;
        auto read_one = [&](uint16_t part, uint16_t inst, int timeout) {
            std::vector<uint8_t> body{0x01, 0x00, uint8_t(part)};
            PutBe16(body, t.tag);
            PutBe16(body, inst);
            PutBe16(body, read_len);
            PutBe16(body, 0);
            return impl_->Req(body, timeout);
        };
        auto read_first = [&](uint16_t inst) {
            auto r = read_one(used, inst, probe_timeout);
            if (r.size() >= 3 && r[0] == 0x81 && r[1] == 0x00 && r[2] == 0x00) return r;
            if (t.len == 0) return r;  // scan probe: the other partition is probed too
            const uint16_t alt = static_cast<uint16_t>(t.part ^ 0x0001);
            auto r2 = read_one(alt, inst, probe_timeout);
            if (r2.size() >= 3 && r2[0] == 0x81 && r2[1] == 0x00 && r2[2] == 0x00) {
                used = alt;
                return r2;
            }
            return r;
        };
        uint32_t start = 0;
        auto r = read_first(0);
        if (!(r.size() >= 3 && r[0] == 0x81 && r[1] == 0x00 && r[2] == 0x00)) {
            if (t.len == 0) continue;  // scan probe: tag does not exist
            start = t.inst ? t.inst : 1;
            r = read_first(static_cast<uint16_t>(start));
        }
        size_t count = 0;
        for (uint32_t inst = start; inst <= 0xFFFF; ++inst) {
            if (inst != start) r = read_one(used, static_cast<uint16_t>(inst), 3000);
            if (!(r.size() >= 14 && r[0] == 0x81 && r[1] == 0x00 && r[2] == 0x00)) break;
            const uint16_t len = Be16(r.data() + 12);
            if (len == 0 || r.size() < size_t(14) + len) break;
            const uint32_t id = (uint32_t(t.tag) << 16) | uint16_t(inst);
            const uint64_t key = (uint64_t(used) << 32) | id;
            if (seen.insert(key).second) {
                Record rec;
                rec.id = id;
                rec.type = used;
                PutBe32(rec.raw, rec.id);
                PutBe16(rec.raw, len);
                rec.raw.insert(rec.raw.end(), r.begin() + 14, r.begin() + 14 + len);
                rec.checksum = Be16(rec.raw.data() + rec.raw.size() - 2);
                rec.verified = Crc16Ccitt(rec.raw.data() + 6, rec.raw.size() - 8) == rec.checksum;
                out.push_back(std::move(rec));
                ++count;
            }
        }
        impl_->Log("read-all %02x/%04x: %zu instance(s)", used, t.tag, count);
    }
    impl_->Req(Hex("010601"), 3000);
    impl_->ResetToAt();
    (void)err;
    return true;
}

bool RadioSession::WriteRecords(const std::vector<Record>& recs, std::string& err) {
    if (!impl_->ProgramAndAuth(err)) return false;

    const uint32_t identity[] = {0x10050000u, 0x10040000u, 0x10060000u};
    std::vector<const Record*> order;
    for (uint32_t id : identity) {
        for (const auto& r : recs)
            if (r.id == id) order.push_back(&r);
    }
    for (const auto& r : recs) {
        bool skip = r.id == 0x00470000u;
        for (uint32_t id : identity)
            if (r.id == id) skip = true;
        if (!skip) order.push_back(&r);
    }
    for (const auto& r : recs)
        if (r.id == 0x00470000u) order.push_back(&r);

    int failed = 0;
    for (const Record* r : order) {
        if (!r || r->raw.size() < 8) {
            ++failed;
            continue;
        }
        const uint16_t len = static_cast<uint16_t>(r->raw.size() - 6);
        std::vector<uint8_t> body{0x01, 0x01, uint8_t(r->type)};
        PutBe16(body, static_cast<uint16_t>(r->id >> 16));
        PutBe16(body, static_cast<uint16_t>(r->id & 0xFFFF));
        PutBe16(body, len);
        PutBe32(body, len);
        body.insert(body.end(), r->raw.begin() + 6, r->raw.end());
        const auto reply = impl_->Req(body, 5000);
        if (!(reply.size() >= 3 && reply[0] == 0x81 && reply[1] == 0x01 && reply[2] == 0x00))
            ++failed;
    }
    impl_->Req(Hex("01078000"), 5000);
    impl_->Req(Hex("010601"), 5000);
    impl_->ResetToAt();
    if (failed) {
        err = std::to_string(failed) + " record(s) were not accepted";
        return false;
    }
    return true;
}

}  // namespace opendepot






