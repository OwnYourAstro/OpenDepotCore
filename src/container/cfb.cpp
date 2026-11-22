// SPDX-FileCopyrightText: 2026 Sarah Rose
// SPDX-License-Identifier: MIT

#include "cfb.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>

namespace opendepot::cfb {
namespace {

constexpr uint32_t kNoStream = 0xFFFFFFFF;
constexpr uint32_t kMaxRegular = 0xFFFFFFFA;  // values below this are sectors
constexpr uint32_t kEndOfChain = 0xFFFFFFFE;
constexpr uint32_t kFreeSector = 0xFFFFFFFF;
constexpr uint32_t kFatSector = 0xFFFFFFFD;

uint16_t U16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
uint32_t U32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t U64(const uint8_t* p) {
    return static_cast<uint64_t>(U32(p)) | (static_cast<uint64_t>(U32(p + 4)) << 32);
}

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

int CompareName(const std::string& a, const std::string& b) {
    // Windows orders the directory tree by name length first, then
    // case-insensitively. The vendor-written trees follow this order.
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    const size_t n = a.size();
    for (size_t i = 0; i < n; ++i) {
        const char ca = Lower(a[i]);
        const char cb = Lower(b[i]);
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return 0;
}

bool ReadAll(const std::filesystem::path& path, std::vector<uint8_t>& out, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot open compound file";
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        err = "cannot size compound file";
        return false;
    }
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size && !in.read(reinterpret_cast<char*>(out.data()), size)) {
        err = "cannot read compound file";
        return false;
    }
    return true;
}

}  // namespace

bool File::ValidSector(uint32_t index) const {
    const size_t offset = (static_cast<size_t>(index) + 1) * sector_size_;
    return sector_size_ != 0 && offset + sector_size_ <= data_.size();
}

const uint8_t* File::Sector(uint32_t index) const {
    if (!ValidSector(index)) return nullptr;
    return data_.data() + (static_cast<size_t>(index) + 1) * sector_size_;
}

bool File::ReadFatChain(uint32_t start, size_t size, bool exact,
                        std::vector<uint8_t>& out) const {
    out.clear();
    if (size != 0) out.reserve(size);
    if (exact && size == 0) return true;

    uint32_t sector = start;
    size_t guard = 0;
    while (sector != kEndOfChain) {
        if (sector >= fat_.size()) return false;
        const uint8_t* p = Sector(sector);
        if (!p) return false;
        if (exact) {
            const size_t take = std::min<size_t>(sector_size_, size - out.size());
            out.insert(out.end(), p, p + take);
            if (out.size() >= size) break;
        } else {
            out.insert(out.end(), p, p + sector_size_);
        }
        sector = fat_[sector];
        if (++guard > fat_.size() + 1) return false;
    }
    return !exact || out.size() == size;
}

bool File::ReadMiniChain(uint32_t start, size_t size, std::vector<uint8_t>& out) const {
    out.clear();
    if (size != 0) out.reserve(size);
    if (size == 0) return true;

    uint32_t sector = start;
    size_t guard = 0;
    while (sector != kEndOfChain) {
        if (sector >= mini_fat_.size()) return false;
        const size_t offset = static_cast<size_t>(sector) * mini_sector_size_;
        if (offset + mini_sector_size_ > mini_stream_.size()) return false;
        const size_t take = std::min<size_t>(mini_sector_size_, size - out.size());
        out.insert(out.end(), mini_stream_.begin() + static_cast<std::ptrdiff_t>(offset),
                   mini_stream_.begin() + static_cast<std::ptrdiff_t>(offset + take));
        if (out.size() >= size) break;
        sector = mini_fat_[sector];
        if (++guard > mini_fat_.size() + 1) return false;
    }
    return out.size() == size;
}

bool File::Open(const std::filesystem::path& path, std::string& err) {
    data_.clear();
    fat_.clear();
    mini_fat_.clear();
    mini_stream_.clear();
    dir_.clear();
    sector_size_ = 0;
    mini_sector_size_ = 0;
    mini_cutoff_ = 0;

    if (!ReadAll(path, data_, err)) return false;
    if (data_.size() < 512) {
        err = "compound file is too small";
        return false;
    }
    static const uint8_t kSignature[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    if (std::memcmp(data_.data(), kSignature, 8) != 0) {
        err = "not a compound file";
        return false;
    }

    const uint16_t sector_shift = U16(data_.data() + 0x1E);
    const uint16_t mini_shift = U16(data_.data() + 0x20);
    if (sector_shift != 9 && sector_shift != 12) {
        err = "unsupported sector size";
        return false;
    }
    if (mini_shift != 6) {
        err = "unsupported mini sector size";
        return false;
    }
    sector_size_ = 1u << sector_shift;
    mini_sector_size_ = 1u << mini_shift;
    mini_cutoff_ = U32(data_.data() + 0x38);

    const uint32_t first_dir = U32(data_.data() + 0x30);
    const uint32_t first_mini_fat = U32(data_.data() + 0x3C);
    const uint32_t num_mini_fat = U32(data_.data() + 0x40);
    const uint32_t first_difat = U32(data_.data() + 0x44);
    const uint32_t num_difat = U32(data_.data() + 0x48);

    std::vector<uint32_t> fat_sectors;
    for (int i = 0; i < 109; ++i) {
        const uint32_t v = U32(data_.data() + 0x4C + 4 * i);
        if (v < kMaxRegular) fat_sectors.push_back(v);
    }

    uint32_t difat = first_difat;
    size_t difat_guard = 0;
    while (difat < kMaxRegular) {
        const uint8_t* p = Sector(difat);
        if (!p) {
            err = "invalid DIFAT sector";
            return false;
        }
        const uint32_t entries = sector_size_ / 4;
        for (uint32_t i = 0; i + 1 < entries; ++i) {
            const uint32_t v = U32(p + 4 * i);
            if (v < kMaxRegular) fat_sectors.push_back(v);
        }
        difat = U32(p + 4 * (entries - 1));
        if (++difat_guard > num_difat) break;
    }

    for (uint32_t fs : fat_sectors) {
        const uint8_t* p = Sector(fs);
        if (!p) {
            err = "invalid FAT sector";
            return false;
        }
        for (uint32_t i = 0; i < sector_size_ / 4; ++i) fat_.push_back(U32(p + 4 * i));
    }
    if (fat_.empty()) {
        err = "empty FAT";
        return false;
    }

    std::vector<uint8_t> dir_data;
    if (!ReadFatChain(first_dir, 0, false, dir_data) || dir_data.size() < 128) {
        err = "invalid directory chain";
        return false;
    }

    const size_t count = dir_data.size() / 128;
    dir_.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* p = dir_data.data() + i * 128;
        Entry e;
        const uint16_t name_len = U16(p + 0x40);
        if (name_len >= 2 && name_len <= 64) {
            for (size_t k = 0; k + 2 <= name_len; k += 2) {
                const uint16_t c = U16(p + k);
                if (c == 0) break;
                e.name.push_back(c < 128 ? static_cast<char>(c) : '?');
            }
        }
        e.type = p[0x42];
        e.left = U32(p + 0x44);
        e.right = U32(p + 0x48);
        e.child = U32(p + 0x4C);
        e.start = U32(p + 0x74);
        e.size = U64(p + 0x78);
        dir_.push_back(std::move(e));
    }

    if (dir_.empty() || dir_[0].type != 5) {
        err = "missing root directory entry";
        return false;
    }

    if (dir_[0].size != 0) {
        if (!ReadFatChain(dir_[0].start, static_cast<size_t>(dir_[0].size), true, mini_stream_)) {
            err = "invalid mini stream";
            return false;
        }
    }

    if (num_mini_fat != 0 && first_mini_fat < kMaxRegular) {
        std::vector<uint8_t> mini_fat_data;
        if (!ReadFatChain(first_mini_fat, static_cast<size_t>(num_mini_fat) * sector_size_, true,
                          mini_fat_data)) {
            err = "invalid mini FAT";
            return false;
        }
        mini_fat_.reserve(mini_fat_data.size() / 4);
        for (size_t i = 0; i + 4 <= mini_fat_data.size(); i += 4)
            mini_fat_.push_back(U32(mini_fat_data.data() + i));
    }

    return true;
}

int File::FindChild(uint32_t parent, const std::string& name) const {
    if (parent >= dir_.size()) return -1;
    // Vendor-written directory trees are not reliably name-ordered, so scan the
    // whole sibling tree instead of walking it as a binary search tree.
    std::vector<uint32_t> ids;
    size_t guard = 0;
    Collect(dir_[parent].child, ids, guard);
    for (uint32_t id : ids) {
        if (id < dir_.size() && CompareName(dir_[id].name, name) == 0)
            return static_cast<int>(id);
    }
    return -1;
}

void File::Collect(uint32_t id, std::vector<uint32_t>& out, size_t& guard) const {
    while (id < dir_.size()) {
        Collect(dir_[id].left, out, guard);
        out.push_back(id);
        id = dir_[id].right;
        if (++guard > dir_.size() * 2 + 2) return;
    }
}

bool File::ReadEntry(uint32_t id, std::vector<uint8_t>& out, std::string& err) const {
    if (id >= dir_.size()) {
        err = "invalid directory entry";
        return false;
    }
    const Entry& e = dir_[id];
    if (e.type != 2) {
        err = "entry is not a stream";
        return false;
    }
    if (e.size == 0) {
        out.clear();
        return true;
    }
    const bool ok = e.size < mini_cutoff_
                        ? ReadMiniChain(e.start, static_cast<size_t>(e.size), out)
                        : ReadFatChain(e.start, static_cast<size_t>(e.size), true, out);
    if (!ok) err = "invalid stream chain";
    return ok;
}

bool File::ReadRoot(const std::string& name, std::vector<uint8_t>& out,
                    std::string& err) const {
    const int id = FindChild(0, name);
    if (id < 0) {
        err = "missing stream: " + name;
        return false;
    }
    return ReadEntry(static_cast<uint32_t>(id), out, err);
}

bool File::ReadStorage(const std::string& storage, const std::string& name,
                       std::vector<uint8_t>& out, std::string& err) const {
    const int sid = FindChild(0, storage);
    if (sid < 0 || dir_[static_cast<size_t>(sid)].type != 1) {
        err = "missing storage: " + storage;
        return false;
    }
    const int id = FindChild(static_cast<uint32_t>(sid), name);
    if (id < 0) {
        err = "missing stream: " + storage + "/" + name;
        return false;
    }
    return ReadEntry(static_cast<uint32_t>(id), out, err);
}

bool File::ListStreams(const std::string& storage, std::vector<std::string>& names,
                       std::string& err) const {
    names.clear();
    const int sid = FindChild(0, storage);
    if (sid < 0 || dir_[static_cast<size_t>(sid)].type != 1) {
        err = "missing storage: " + storage;
        return false;
    }
    std::vector<uint32_t> ids;
    size_t guard = 0;
    Collect(dir_[static_cast<size_t>(sid)].child, ids, guard);
    for (uint32_t id : ids) {
        if (id < dir_.size() && dir_[id].type == 2) names.push_back(dir_[id].name);
    }
    return true;
}

namespace {

struct OutEntry {
    std::string name;
    uint8_t type = 0;
    uint32_t left = kNoStream;
    uint32_t right = kNoStream;
    uint32_t child = kNoStream;
    uint32_t start = kEndOfChain;
    uint64_t size = 0;
    bool red = false;
};

struct LargeStream {
    int dir_index = 0;
    std::vector<uint8_t> data;
    uint32_t sectors = 0;
    uint32_t start = 0;
};

void Put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
void Put32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}
void Put64(uint8_t* p, uint64_t v) {
    Put32(p, static_cast<uint32_t>(v));
    Put32(p + 4, static_cast<uint32_t>(v >> 32));
}

std::u16string Utf16(const std::string& s) {
    std::u16string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
    return out;
}

int BuildTree(std::vector<OutEntry>& dir, const std::vector<int>& ids, int lo, int hi) {
    if (lo > hi) return -1;
    const int mid = (lo + hi) / 2;
    dir[static_cast<size_t>(ids[static_cast<size_t>(mid)])].left =
        static_cast<uint32_t>(BuildTree(dir, ids, lo, mid - 1));
    dir[static_cast<size_t>(ids[static_cast<size_t>(mid)])].right =
        static_cast<uint32_t>(BuildTree(dir, ids, mid + 1, hi));
    return ids[static_cast<size_t>(mid)];
}

void SortIds(const std::vector<OutEntry>& dir, std::vector<int>& ids) {
    std::sort(ids.begin(), ids.end(), [&](int a, int b) {
        return CompareName(dir[static_cast<size_t>(a)].name, dir[static_cast<size_t>(b)].name) < 0;
    });
}

struct RbNode {
    int left = -1;
    int right = -1;
    int parent = -1;
    bool red = false;
};

bool IsRed(const std::vector<RbNode>& nodes, int id) {
    return id >= 0 && nodes[static_cast<size_t>(id)].red;
}

void RotateLeft(std::vector<RbNode>& n, int& root, int x) {
    const int y = n[static_cast<size_t>(x)].right;
    if (y < 0) return;
    n[static_cast<size_t>(x)].right = n[static_cast<size_t>(y)].left;
    if (n[static_cast<size_t>(y)].left >= 0)
        n[static_cast<size_t>(n[static_cast<size_t>(y)].left)].parent = x;
    n[static_cast<size_t>(y)].parent = n[static_cast<size_t>(x)].parent;
    if (n[static_cast<size_t>(x)].parent < 0)
        root = y;
    else if (n[static_cast<size_t>(n[static_cast<size_t>(x)].parent)].left == x)
        n[static_cast<size_t>(n[static_cast<size_t>(x)].parent)].left = y;
    else
        n[static_cast<size_t>(n[static_cast<size_t>(x)].parent)].right = y;
    n[static_cast<size_t>(y)].left = x;
    n[static_cast<size_t>(x)].parent = y;
}

void RotateRight(std::vector<RbNode>& n, int& root, int x) {
    const int y = n[static_cast<size_t>(x)].left;
    if (y < 0) return;
    n[static_cast<size_t>(x)].left = n[static_cast<size_t>(y)].right;
    if (n[static_cast<size_t>(y)].right >= 0)
        n[static_cast<size_t>(n[static_cast<size_t>(y)].right)].parent = x;
    n[static_cast<size_t>(y)].parent = n[static_cast<size_t>(x)].parent;
    if (n[static_cast<size_t>(x)].parent < 0)
        root = y;
    else if (n[static_cast<size_t>(n[static_cast<size_t>(x)].parent)].right == x)
        n[static_cast<size_t>(n[static_cast<size_t>(x)].parent)].right = y;
    else
        n[static_cast<size_t>(n[static_cast<size_t>(x)].parent)].left = y;
    n[static_cast<size_t>(y)].right = x;
    n[static_cast<size_t>(x)].parent = y;
}

void InsertFixup(std::vector<RbNode>& n, int& root, int z) {
    while (z != root && IsRed(n, n[static_cast<size_t>(z)].parent)) {
        const int p = n[static_cast<size_t>(z)].parent;
        const int g = n[static_cast<size_t>(p)].parent;
        if (p == n[static_cast<size_t>(g)].left) {
            const int u = n[static_cast<size_t>(g)].right;
            if (IsRed(n, u)) {
                n[static_cast<size_t>(p)].red = false;
                n[static_cast<size_t>(u)].red = false;
                n[static_cast<size_t>(g)].red = true;
                z = g;
            } else {
                if (z == n[static_cast<size_t>(p)].right) {
                    z = p;
                    RotateLeft(n, root, z);
                }
                const int p2 = n[static_cast<size_t>(z)].parent;
                const int g2 = n[static_cast<size_t>(p2)].parent;
                n[static_cast<size_t>(p2)].red = false;
                n[static_cast<size_t>(g2)].red = true;
                RotateRight(n, root, g2);
            }
        } else {
            const int u = n[static_cast<size_t>(g)].left;
            if (IsRed(n, u)) {
                n[static_cast<size_t>(p)].red = false;
                n[static_cast<size_t>(u)].red = false;
                n[static_cast<size_t>(g)].red = true;
                z = g;
            } else {
                if (z == n[static_cast<size_t>(p)].left) {
                    z = p;
                    RotateRight(n, root, z);
                }
                const int p2 = n[static_cast<size_t>(z)].parent;
                const int g2 = n[static_cast<size_t>(p2)].parent;
                n[static_cast<size_t>(p2)].red = false;
                n[static_cast<size_t>(g2)].red = true;
                RotateLeft(n, root, g2);
            }
        }
    }
    if (root >= 0) n[static_cast<size_t>(root)].red = false;
}

int BuildRbTree(std::vector<OutEntry>& dir, const std::vector<int>& ids) {
    std::vector<RbNode> nodes(dir.size());
    int root = -1;
    for (int id : ids) {
        int y = -1;
        int x = root;
        while (x >= 0) {
            y = x;
            if (CompareName(dir[static_cast<size_t>(id)].name, dir[static_cast<size_t>(x)].name) < 0)
                x = nodes[static_cast<size_t>(x)].left;
            else
                x = nodes[static_cast<size_t>(x)].right;
        }
        nodes[static_cast<size_t>(id)].parent = y;
        nodes[static_cast<size_t>(id)].left = -1;
        nodes[static_cast<size_t>(id)].right = -1;
        nodes[static_cast<size_t>(id)].red = true;
        if (y < 0)
            root = id;
        else if (CompareName(dir[static_cast<size_t>(id)].name,
                             dir[static_cast<size_t>(y)].name) < 0)
            nodes[static_cast<size_t>(y)].left = id;
        else
            nodes[static_cast<size_t>(y)].right = id;
        InsertFixup(nodes, root, id);
    }
    for (int id : ids) {
        dir[static_cast<size_t>(id)].left =
            nodes[static_cast<size_t>(id)].left < 0
                ? kNoStream
                : static_cast<uint32_t>(nodes[static_cast<size_t>(id)].left);
        dir[static_cast<size_t>(id)].right =
            nodes[static_cast<size_t>(id)].right < 0
                ? kNoStream
                : static_cast<uint32_t>(nodes[static_cast<size_t>(id)].right);
        dir[static_cast<size_t>(id)].red = nodes[static_cast<size_t>(id)].red;
    }
    return root;
}

void SetChain(std::vector<uint32_t>& fat, uint32_t start, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i)
        fat[start + i] = (i + 1 == count) ? kEndOfChain : start + i + 1;
}

}  // namespace

bool Write(const std::filesystem::path& path, const std::vector<Stream>& root_streams,
           const std::string& storage_name, const std::vector<Stream>& storage_streams,
           std::string& err) {
    constexpr uint32_t kSector = 512;
    constexpr uint32_t kMini = 64;
    constexpr uint32_t kCutoff = 4096;

    std::vector<OutEntry> dir;
    dir.push_back(OutEntry{"Root Entry", 5, kNoStream, kNoStream, kNoStream, 0, 0});

    const size_t root_count = root_streams.size();
    for (const auto& s : root_streams) dir.push_back(OutEntry{s.name, 2});

    const int storage_index = static_cast<int>(dir.size());
    dir.push_back(OutEntry{storage_name, 1, kNoStream, kNoStream, kNoStream, 0, 0});

    const int first_stream = static_cast<int>(dir.size());
    for (const auto& s : storage_streams) dir.push_back(OutEntry{s.name, 2});

    std::vector<uint8_t> mini_stream;
    std::vector<uint32_t> mini_fat;
    std::vector<LargeStream> large;

    auto add_stream = [&](int index, const std::vector<uint8_t>& data) {
        OutEntry& e = dir[static_cast<size_t>(index)];
        e.size = data.size();
        if (data.empty()) {
            e.start = kEndOfChain;
            return;
        }
        if (data.size() < kCutoff) {
            const uint32_t sectors = (static_cast<uint32_t>(data.size()) + kMini - 1) / kMini;
            e.start = static_cast<uint32_t>(mini_fat.size());
            for (uint32_t j = 0; j < sectors; ++j)
                mini_fat.push_back(j + 1 == sectors ? kEndOfChain : e.start + j + 1);
            mini_stream.insert(mini_stream.end(), data.begin(), data.end());
            mini_stream.resize(mini_stream.size() + static_cast<size_t>(sectors) * kMini -
                               data.size(),
                           0);
        } else {
            LargeStream l;
            l.dir_index = index;
            l.sectors = (static_cast<uint32_t>(data.size()) + kSector - 1) / kSector;
            l.data = data;
            l.data.resize(static_cast<size_t>(l.sectors) * kSector, 0);
            large.push_back(std::move(l));
        }
    };

    for (size_t i = 0; i < root_count; ++i) add_stream(1 + static_cast<int>(i), root_streams[i].data);
    for (size_t i = 0; i < storage_streams.size(); ++i)
        add_stream(first_stream + static_cast<int>(i), storage_streams[i].data);

    const uint32_t dir_sectors =
        (static_cast<uint32_t>(dir.size() * 128) + kSector - 1) / kSector;
    const uint32_t mini_fat_sectors =
        mini_fat.empty()
            ? 0
            : (static_cast<uint32_t>(mini_fat.size() * 4) + kSector - 1) / kSector;
    const uint32_t mini_stream_sectors =
        (static_cast<uint32_t>(mini_stream.size()) + kSector - 1) / kSector;

    uint32_t next = 0;
    const uint32_t dir_start = next;
    next += dir_sectors;
    for (auto& l : large) {
        l.start = next;
        next += l.sectors;
    }
    const uint32_t mini_stream_start = mini_stream.empty() ? kEndOfChain : next;
    next += mini_stream_sectors;
    const uint32_t mini_fat_start = mini_fat.empty() ? kEndOfChain : next;
    next += mini_fat_sectors;
    const uint32_t fat_start = next;

    const uint32_t data_sectors = next;
    uint32_t fat_sectors = 1;
    for (;;) {
        const uint32_t need = (data_sectors + fat_sectors + 127) / 128;
        if (need == fat_sectors) break;
        fat_sectors = need;
    }
    if (fat_sectors > 109) {
        err = "compound file needs DIFAT sectors (not supported)";
        return false;
    }

    std::vector<uint32_t> fat(static_cast<size_t>(fat_sectors) * 128, kFreeSector);
    for (uint32_t i = 0; i < fat_sectors; ++i) fat[fat_start + i] = kFatSector;
    SetChain(fat, dir_start, dir_sectors);
    for (const auto& l : large) SetChain(fat, l.start, l.sectors);
    if (mini_stream_sectors) SetChain(fat, mini_stream_start, mini_stream_sectors);
    if (mini_fat_sectors) SetChain(fat, mini_fat_start, mini_fat_sectors);
    for (const auto& l : large) dir[static_cast<size_t>(l.dir_index)].start = l.start;

    dir[0].start = mini_stream.empty() ? kEndOfChain : mini_stream_start;
    dir[0].size = mini_stream.size();

    std::vector<int> root_ids;
    for (size_t i = 0; i < root_count; ++i) root_ids.push_back(1 + static_cast<int>(i));
    root_ids.push_back(storage_index);
    std::vector<int> storage_ids;
    for (size_t i = 0; i < storage_streams.size(); ++i)
        storage_ids.push_back(first_stream + static_cast<int>(i));

    std::sort(root_ids.begin(), root_ids.end(), [&](int a, int b) {
        return CompareName(dir[static_cast<size_t>(a)].name, dir[static_cast<size_t>(b)].name) < 0;
    });
    std::sort(storage_ids.begin(), storage_ids.end(), [&](int a, int b) {
        return CompareName(dir[static_cast<size_t>(a)].name, dir[static_cast<size_t>(b)].name) < 0;
    });
    const int root_tree = BuildRbTree(dir, root_ids);
    const int storage_tree = BuildRbTree(dir, storage_ids);
    dir[0].child = root_tree < 0 ? kNoStream : static_cast<uint32_t>(root_tree);
    dir[static_cast<size_t>(storage_index)].child =
        storage_tree < 0 ? kNoStream : static_cast<uint32_t>(storage_tree);

    std::vector<uint8_t> dir_bytes(static_cast<size_t>(dir_sectors) * kSector, 0);
    for (size_t i = 0; i < dir.size(); ++i) {
        uint8_t* p = dir_bytes.data() + i * 128;
        const std::u16string name = Utf16(dir[i].name);
        const size_t n = std::min<size_t>(name.size(), 31);
        for (size_t k = 0; k < n; ++k) Put16(p + 2 * k, static_cast<uint16_t>(name[k]));
        Put16(p + 0x40, static_cast<uint16_t>((n + 1) * 2));
        p[0x42] = dir[i].type;
        p[0x43] = dir[i].red ? 0 : 1;
        Put32(p + 0x44, dir[i].left);
        Put32(p + 0x48, dir[i].right);
        Put32(p + 0x4C, dir[i].child);
        Put32(p + 0x74, dir[i].start);
        Put64(p + 0x78, dir[i].size);
    }

    std::vector<uint8_t> mini_fat_bytes(static_cast<size_t>(mini_fat_sectors) * kSector, 0xFF);
    for (size_t i = 0; i < mini_fat.size(); ++i) Put32(mini_fat_bytes.data() + 4 * i, mini_fat[i]);

    std::vector<uint8_t> fat_bytes(static_cast<size_t>(fat_sectors) * kSector, 0);
    for (size_t i = 0; i < fat.size(); ++i) Put32(fat_bytes.data() + 4 * i, fat[i]);

    std::vector<uint8_t> header(kSector, 0);
    static const uint8_t kSignature[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    std::memcpy(header.data(), kSignature, 8);
    Put16(header.data() + 0x18, 0x003E);
    Put16(header.data() + 0x1A, 3);
    Put16(header.data() + 0x1C, 0xFFFE);
    Put16(header.data() + 0x1E, 9);
    Put16(header.data() + 0x20, 6);
    Put32(header.data() + 0x2C, fat_sectors);
    Put32(header.data() + 0x30, dir_start);
    Put32(header.data() + 0x38, kCutoff);
    Put32(header.data() + 0x3C, mini_fat_start);
    Put32(header.data() + 0x40, mini_fat_sectors);
    Put32(header.data() + 0x44, kEndOfChain);
    Put32(header.data() + 0x48, 0);
    for (uint32_t i = 0; i < 109; ++i)
        Put32(header.data() + 0x4C + 4 * i, i < fat_sectors ? fat_start + i : kFreeSector);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        err = "cannot create compound file";
        return false;
    }
    auto write_bytes = [&](const std::vector<uint8_t>& bytes) {
        if (!bytes.empty())
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
    };
    write_bytes(header);
    write_bytes(dir_bytes);
    for (const auto& l : large) write_bytes(l.data);
    if (mini_stream_sectors) {
        std::vector<uint8_t> padded = mini_stream;
        padded.resize(static_cast<size_t>(mini_stream_sectors) * kSector, 0);
        write_bytes(padded);
    }
    write_bytes(mini_fat_bytes);
    write_bytes(fat_bytes);
    if (!out) {
        err = "failed writing compound file";
        return false;
    }
    return true;
}

}  // namespace opendepot::cfb
