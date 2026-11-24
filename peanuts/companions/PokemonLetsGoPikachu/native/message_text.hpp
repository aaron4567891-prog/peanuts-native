// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only LGPE plain-name message tables. No game text or assets are embedded.
// Format reference: kwsch/pkNX, pkNX.Structures/Text/TextFile.cs.
// Checked against the local base dump's English monsname.dat and wazaname.dat.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pikachu {
namespace message_detail {

inline std::uint16_t Read16(const std::vector<std::uint8_t>& data, std::size_t at) {
    return static_cast<std::uint16_t>(data[at] | (std::uint16_t(data[at + 1]) << 8));
}

inline std::uint32_t Read32(const std::vector<std::uint8_t>& data, std::size_t at) {
    return std::uint32_t(Read16(data, at)) | (std::uint32_t(Read16(data, at + 2)) << 16);
}

inline void AppendUtf8(std::string& out, std::uint32_t value) {
    if (value < 0x80) {
        out.push_back(static_cast<char>(value));
    } else if (value < 0x800) {
        out.push_back(static_cast<char>(0xc0 | (value >> 6)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else if (value < 0x10000) {
        out.push_back(static_cast<char>(0xe0 | (value >> 12)));
        out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (value >> 18)));
        out.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
}

inline std::uint16_t UnmapSymbol(std::uint16_t value) {
    switch (value) {
    case 0xe07f: return 0x202f;
    case 0xe08d: return 0x2026;
    case 0xe08e: return 0x2642;
    case 0xe08f: return 0x2640;
    default: return value;
    }
}

} // namespace message_detail

// Output indices are the game's numeric species/move IDs, including empty rows.
// An empty result means unavailable/unsupported data; callers must keep their
// numeric-ID fallback. This deliberately rejects template/control messages: it
// decodes plain names, rather than guessing dynamic game formatting.
inline std::vector<std::string> DecodeMessages(const std::vector<std::uint8_t>& data) {
    using message_detail::Read16;
    using message_detail::Read32;
    if (data.size() < 20 || data.size() > 16u * 1024u * 1024u ||
        Read16(data, 0) != 1 || Read32(data, 8) != 0) return {};
    const std::size_t count = Read16(data, 2);
    const std::size_t length = Read32(data, 4);
    const std::size_t section = Read32(data, 12);
    if (count > 16384 || section < 16 || section > data.size() - 4 ||
        length != data.size() - section || Read32(data, section) != length ||
        count > (length - 4) / 8) return {};
    const std::size_t table_end = 4 + count * 8;
    std::vector<std::string> result;
    result.reserve(count);
    std::size_t total_units = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const auto entry = section + 4 + index * 8;
        const std::size_t offset = Read32(data, entry);
        const std::size_t units = Read16(data, entry + 4);
        if (offset < table_end || offset > length || units == 0 || units > 4096 ||
            units > (length - offset) / 2 || units > 1024u * 1024u - total_units) return {};
        total_units += units;
        std::uint16_t key = static_cast<std::uint16_t>(0x7c89u + index * 0x2983u);
        std::string text;
        std::uint16_t high = 0;
        bool terminated = false;
        for (std::size_t unit = 0; unit < units; ++unit) {
            auto value = static_cast<std::uint16_t>(Read16(data, section + offset + unit * 2) ^ key);
            key = static_cast<std::uint16_t>((key << 3) | (key >> 13));
            if (value == 0) { terminated = true; break; }
            if (value < 0x20 || value == 0xffff || value == 0xfffe) return {};
            if (value >= 0xd800 && value <= 0xdbff) {
                if (high != 0) return {};
                high = value;
                continue;
            }
            if (value >= 0xdc00 && value <= 0xdfff) {
                if (high == 0) return {};
                message_detail::AppendUtf8(text, 0x10000u + ((std::uint32_t(high) - 0xd800u) << 10) + value - 0xdc00u);
                high = 0;
                continue;
            }
            if (high != 0) return {};
            message_detail::AppendUtf8(text, message_detail::UnmapSymbol(value));
        }
        if (!terminated || high != 0) return {};
        result.push_back(std::move(text));
    }
    return result;
}

} // namespace pikachu
