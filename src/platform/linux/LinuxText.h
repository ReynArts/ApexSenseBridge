#pragma once

#include <string>
#include <string_view>

namespace asb::platform::linux_text {

// HidDeviceInfo stores wide strings because the Windows PnP identity matchers
// are written against them. Linux sources are UTF-8, so every value crosses
// this boundary exactly once per enumeration, never in the hot path.
[[nodiscard]] inline std::wstring widen(std::string_view utf8) {
    std::wstring wide;
    wide.reserve(utf8.size());
    std::size_t index = 0;
    while (index < utf8.size()) {
        const auto lead = static_cast<unsigned char>(utf8[index]);
        char32_t code = 0;
        std::size_t extra = 0;
        if (lead < 0x80) {
            code = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            code = lead & 0x1F;
            extra = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            code = lead & 0x0F;
            extra = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            code = lead & 0x07;
            extra = 3;
        } else {
            // Invalid lead byte: keep going rather than truncating an identity.
            ++index;
            continue;
        }
        if (index + extra >= utf8.size()) {
            break;
        }
        for (std::size_t offset = 1; offset <= extra; ++offset) {
            const auto continuation = static_cast<unsigned char>(utf8[index + offset]);
            if ((continuation & 0xC0) != 0x80) {
                code = U'�';
                extra = offset - 1;
                break;
            }
            code = (code << 6) | (continuation & 0x3F);
        }
        wide.push_back(static_cast<wchar_t>(code));
        index += extra + 1;
    }
    return wide;
}

[[nodiscard]] inline std::string narrow(std::wstring_view wide) {
    std::string utf8;
    utf8.reserve(wide.size());
    for (const auto character : wide) {
        const auto code = static_cast<char32_t>(character);
        if (code < 0x80) {
            utf8.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            utf8.push_back(static_cast<char>(0xC0 | (code >> 6)));
            utf8.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            utf8.push_back(static_cast<char>(0xE0 | (code >> 12)));
            utf8.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            utf8.push_back(static_cast<char>(0xF0 | (code >> 18)));
            utf8.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }
    return utf8;
}

} // namespace asb::platform::linux_text
