// config.cc – Teil der libiso8583-Bibliothek
// Ursprünglicher Bug: `#ifdef _WIN32 || _WIN64` ist kein gültiger Präprozessor-Ausdruck.
// Der `||`-Operator gilt im Präprozessor nicht in #ifdef; korrekt ist `#if defined(...)`.

#include <iso8583/config.h>

// <Windows.h> nur auf echten Windows-Targets einbinden
#if defined(_WIN32) || defined(_WIN64)
#  include <Windows.h>
#  include <vector>
#  include <string>
#endif

namespace tng::utils {

#if defined(_WIN32) || defined(_WIN64)

std::string ConvertToUTF8(const std::string& input_string) {
    if (input_string.empty())
        return {};

    // ACP → Wide
    int wide_len = MultiByteToWideChar(CP_ACP, 0,
        input_string.c_str(), static_cast<int>(input_string.size()),
        nullptr, 0);
    if (wide_len <= 0)
        return input_string;

    std::vector<wchar_t> wide(wide_len);
    if (MultiByteToWideChar(CP_ACP, 0,
            input_string.c_str(), static_cast<int>(input_string.size()),
            wide.data(), wide_len) == 0)
        return input_string;

    // Wide → UTF-8
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0,
        wide.data(), wide_len,
        nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 0)
        return input_string;

    std::vector<char> utf8(utf8_len);
    if (WideCharToMultiByte(CP_UTF8, 0,
            wide.data(), wide_len,
            utf8.data(), utf8_len,
            nullptr, nullptr) == 0)
        return input_string;

    return {utf8.data(), static_cast<std::size_t>(utf8_len)};
}

#else // POSIX – Eingabe wird bereits als UTF-8 angenommen

std::string ConvertToUTF8(const std::string& input_string) {
    return input_string;
}

#endif

} // namespace tng::utils
