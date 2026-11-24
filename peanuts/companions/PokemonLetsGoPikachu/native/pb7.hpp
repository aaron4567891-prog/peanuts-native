// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only LGPE native Pokemon decoder. Format references:
// PKHeX.Core/PKM/PB7.cs, PKM/Util/PokeCrypto.cs (kwsch/PKHeX), and
// runtime/hle/wildbattle.c (raulzrrs/letsgo-recomp). No game data is bundled.
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace pikachu {
using Core = std::array<uint8_t, 0xe8>;
using Calc = std::array<uint8_t, 0x1c>;

inline uint16_t U16(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
inline uint32_t U32(const uint8_t* p) {
    return uint32_t(U16(p)) | (uint32_t(U16(p + 2)) << 16);
}
inline void Put16(uint8_t* p, uint16_t n) { p[0] = uint8_t(n); p[1] = uint8_t(n >> 8); }

// This permutation numbering is part of the Gen6/7 data format, not lexical
// permutation order. Selectors 24..31 repeat 0..7.
inline constexpr uint8_t kBlocks[24][4] = {
    {0,1,2,3}, {0,1,3,2}, {0,2,1,3}, {0,3,1,2},
    {0,2,3,1}, {0,3,2,1}, {1,0,2,3}, {1,0,3,2},
    {2,0,1,3}, {3,0,1,2}, {2,0,3,1}, {3,0,2,1},
    {1,2,0,3}, {1,3,0,2}, {2,1,0,3}, {3,1,0,2},
    {2,3,0,1}, {3,2,0,1}, {1,2,3,0}, {1,3,2,0},
    {2,1,3,0}, {3,1,2,0}, {2,3,1,0}, {3,2,1,0}
};

inline void Crypt(uint8_t* p, size_t size, uint32_t seed) {
    for (size_t i = 0; i + 1 < size; i += 2) {
        seed = seed * UINT32_C(0x41c64e6d) + UINT32_C(0x6073);
        Put16(p + i, U16(p + i) ^ uint16_t(seed >> 16));
    }
}

inline void Utf8Append(std::string& s, uint32_t cp) {
    if (cp <= 0x7f) s.push_back(char(cp));
    else if (cp <= 0x7ff) {
        s.push_back(char(0xc0 | (cp >> 6))); s.push_back(char(0x80 | (cp & 63)));
    } else if (cp <= 0xffff) {
        s.push_back(char(0xe0 | (cp >> 12))); s.push_back(char(0x80 | ((cp >> 6) & 63)));
        s.push_back(char(0x80 | (cp & 63)));
    } else {
        s.push_back(char(0xf0 | (cp >> 18))); s.push_back(char(0x80 | ((cp >> 12) & 63)));
        s.push_back(char(0x80 | ((cp >> 6) & 63))); s.push_back(char(0x80 | (cp & 63)));
    }
}

inline std::string Utf16(const uint8_t* p, size_t units) {
    std::string result;
    for (size_t i = 0; i < units; ++i) {
        uint32_t cp = U16(p + i * 2);
        if (!cp || cp == 0xffff) break;
        if (cp >= 0xd800 && cp <= 0xdbff) {
            uint32_t lo = i + 1 < units ? U16(p + (i + 1) * 2) : 0;
            if (lo >= 0xdc00 && lo <= 0xdfff) {
                cp = 0x10000 + ((cp - 0xd800) << 10) + lo - 0xdc00; ++i;
            } else cp = 0xfffd;
        } else if (cp >= 0xdc00 && cp <= 0xdfff) cp = 0xfffd;
        if (cp < 32 || cp == 0x7f) return {};
        Utf8Append(result, cp);
    }
    return result;
}

struct Pokemon {
    std::string name;
    uint32_t ec{}, experience{}, status{};
    uint16_t species{}, hp{}, hpmax{}, atk{}, def{}, spa{}, spd{}, spe{};
    uint16_t moves[4]{};
    uint8_t pp[4]{}, level{}, nature{};
};

inline bool DecodeNative(const Core& input, const Calc& party, bool encrypted, Pokemon& out) {
    Core core = input;
    Calc calc = party;
    const uint32_t ec = U32(core.data());
    if (U16(core.data() + 4) != 0) return false;
    if (encrypted) {
        Crypt(core.data() + 8, core.size() - 8, ec);
        // The party-calculation block starts a separate encryption stream.
        Crypt(calc.data(), calc.size(), ec);
    }
    const unsigned selection = ((ec >> 13) & 31) % 24;
    Core canonical = core;
    for (unsigned block = 0; block < 4; ++block)
        std::memcpy(canonical.data() + 8 + block * 0x38,
                    core.data() + 8 + kBlocks[selection][block] * 0x38, 0x38);
    uint16_t checksum = 0;
    for (size_t i = 8; i < canonical.size(); i += 2)
        checksum = uint16_t(checksum + U16(canonical.data() + i));
    if (checksum != U16(canonical.data() + 6)) return false;

    Pokemon p;
    p.ec = ec; p.species = U16(canonical.data() + 8);
    if (!((p.species >= 1 && p.species <= 151) || p.species == 808 || p.species == 809))
        return false;
    p.experience = U32(canonical.data() + 0x10);
    p.nature = canonical[0x1c];
    if (p.nature > 24) return false;
    p.name = Utf16(canonical.data() + 0x40, 13);
    for (unsigned i = 0; i < 4; ++i) {
        p.moves[i] = U16(canonical.data() + 0x5a + 2 * i);
        p.pp[i] = canonical[0x62 + i];
        if (p.moves[i] > 742) return false;
    }
    p.status = U32(calc.data()); p.level = calc[4];
    p.hp = U16(calc.data() + 8); p.hpmax = U16(calc.data() + 0x0a);
    p.atk = U16(calc.data() + 0x0c); p.def = U16(calc.data() + 0x0e);
    p.spe = U16(calc.data() + 0x10); p.spa = U16(calc.data() + 0x12);
    p.spd = U16(calc.data() + 0x14);
    if (!p.level || p.level > 100 || !p.hpmax || p.hpmax > 5000 || p.hp > p.hpmax)
        return false;
    if (!p.atk || !p.def || !p.spe || !p.spa || !p.spd ||
        p.atk > 5000 || p.def > 5000 || p.spe > 5000 || p.spa > 5000 || p.spd > 5000)
        return false;
    out = std::move(p);
    return true;
}
} // namespace pikachu
