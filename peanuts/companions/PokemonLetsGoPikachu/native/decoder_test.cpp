// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic records only: no console/game data or assets.
#include "pb7.hpp"
#include <cassert>
#include <cstdio>

static pikachu::Core Fixture(uint32_t ec) {
    pikachu::Core c{};
    for (unsigned i = 0; i < 4; ++i) c[i] = uint8_t(ec >> (8 * i));
    pikachu::Put16(c.data() + 8, 25);
    c[0x1c] = 3;
    const char* name = "Test";
    for (unsigned i = 0; name[i]; ++i) c[0x40 + 2 * i] = uint8_t(name[i]);
    pikachu::Put16(c.data() + 0x5a, 85);
    c[0x62] = 15;
    uint16_t sum = 0;
    for (unsigned i = 8; i < c.size(); i += 2) sum += pikachu::U16(c.data() + i);
    pikachu::Put16(c.data() + 6, sum);
    return c;
}
static pikachu::Calc Stats() {
    pikachu::Calc c{}; c[4] = 8;
    pikachu::Put16(c.data() + 8, 21); pikachu::Put16(c.data() + 10, 30);
    const uint16_t stats[] = {17, 14, 23, 16, 15};
    for (unsigned i = 0; i < 5; ++i) pikachu::Put16(c.data() + 12 + i * 2, stats[i]);
    return c;
}
int main() {
    for (unsigned selector = 0; selector < 32; ++selector) {
        uint32_t ec = (UINT32_C(0x12340001) & ~(31u << 13)) | (selector << 13);
        const auto plain = Fixture(ec);
        for (bool encrypted : {false, true}) {
            auto core = plain; auto calc = Stats();
            // Independently construct wire order by placing canonical block i
            // at its specified stored position, then encrypt as the native ABI.
            for (unsigned i = 0; i < 4; ++i)
                std::memcpy(core.data() + 8 + pikachu::kBlocks[selector % 24][i] * 0x38,
                            plain.data() + 8 + i * 0x38, 0x38);
            if (encrypted) {
                pikachu::Crypt(core.data() + 8, core.size() - 8, ec);
                pikachu::Crypt(calc.data(), calc.size(), ec);
            }
            pikachu::Pokemon p;
            assert(pikachu::DecodeNative(core, calc, encrypted, p));
            assert(p.name == "Test" && p.species == 25 && p.level == 8);
            assert(p.hp == 21 && p.hpmax == 30 && p.atk == 17 && p.def == 14);
            assert(p.spe == 23 && p.spa == 16 && p.spd == 15 && p.moves[0] == 85);
            core[0x10] ^= 1;
            assert(!pikachu::DecodeNative(core, calc, encrypted, p));
        }
    }
    pikachu::Pokemon p;
    const auto zero_ec = Fixture(0);
    assert(pikachu::DecodeNative(zero_ec, Stats(), false, p));
    auto invalid = Stats(); pikachu::Put16(invalid.data() + 8, 31);
    assert(!pikachu::DecodeNative(zero_ec, invalid, false, p));
    invalid = Stats(); invalid[4] = 101;
    assert(!pikachu::DecodeNative(zero_ec, invalid, false, p));
    assert(!pikachu::DecodeNative({}, Stats(), false, p));
    const uint8_t unicode[] = {0x3d,0xd8,0x00,0xde,0,0};
    assert(pikachu::Utf16(unicode,3) == "\xf0\x9f\x98\x80");
    std::puts("PB7 decoder: 32 shuffle selectors, encrypted/plain native cores, corruption and bounds passed");
}
