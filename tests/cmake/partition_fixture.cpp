#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    const std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root);
    struct Entry { const char* name; unsigned type, subtype, address, size; };
    constexpr std::array entries{
        Entry{"nvs",1,2,0x9000,0x5000}, Entry{"otadata",1,0,0xe000,0x2000},
        Entry{"app0",0,0x10,0x10000,0x300000}, Entry{"app1",0,0x11,0x310000,0x300000},
        Entry{"ffat",1,0x81,0x610000,0x9c0000}, Entry{"history",1,0x40,0xfd0000,0x20000},
        Entry{"coredump",1,3,0xff0000,0x10000}};
    std::vector<unsigned char> bytes(0xc00, 0xff);
    auto put32 = [&](std::size_t at, unsigned value) {
        for (unsigned i = 0; i < 4; ++i) bytes[at+i] = (value >> (8*i)) & 255;
    };
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i]; const auto at = i*32;
        bytes[at] = 0xaa; bytes[at+1] = 0x50;
        bytes[at+2] = entry.type; bytes[at+3] = entry.subtype;
        put32(at+4, entry.address); put32(at+8, entry.size);
        for (unsigned j = 0; j < 16; ++j) bytes[at+12+j] = 0;
        for (std::size_t j = 0; entry.name[j]; ++j) bytes[at+12+j] = entry.name[j];
        put32(at+28, 0);
    }
    bytes[224] = 0xeb; bytes[225] = 0xeb;
    // MD5 of the canonical seven serialized entries above.
    constexpr unsigned char md5[]{0xe7,0x64,0xb0,0x0f,0xe7,0x03,0xee,0x7d,
                                  0x34,0x73,0x61,0x23,0xa6,0x42,0xa6,0xc0};
    for (unsigned i = 0; i < 16; ++i) bytes[240+i] = md5[i];
    auto write = [&](const char* name, const std::vector<unsigned char>& data) {
        std::ofstream out(root / name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), data.size());
        return bool(out);
    };
    if (!write("valid.bin", bytes)) return 2;
    auto bad = bytes; bad.resize(0x1001, 0xff); write("oversize.bin", bad);
    bad = bytes; bad.resize(255); write("truncated.bin", bad);
    bad = bytes; bad[2] = 0; write("type.bin", bad);
    bad = bytes; bad[3] = 3; write("subtype.bin", bad);
    bad = bytes; bad[28] = 1; write("flags.bin", bad);
    bad = bytes; bad[4] = 1; write("address.bin", bad);
    bad = bytes; bad[8] = 1; write("size.bin", bad);
    bad = bytes; bad[240] ^= 1; write("checksum.bin", bad);
    bad = bytes; bad[260] = 0; write("trailing.bin", bad);
    bad = bytes; for (unsigned i=0;i<32;++i) bad[32+i]=bad[i]; write("duplicate.bin", bad);
    bad = bytes; bad[12] = 'x'; write("extra.bin", bad);
    bad = bytes; bad[128] = 0xff; write("missing.bin", bad);
    return 0;
}
