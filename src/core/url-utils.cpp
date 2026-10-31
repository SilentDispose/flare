#include "core/url-utils.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "core/path-utils.h"
#include "core/text-utils.h"

namespace Flare {
    namespace {
        bool IsAlpha(char character) {
            return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
        }

        bool IsSchemeCharacter(char character) {
            return IsAlpha(character) || (character >= '0' && character <= '9') || character == '+' || character == '-'
                || character == '.';
        }

        bool IsPercentSafe(unsigned char byte) {
            return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9')
                || byte == '-' || byte == '.' || byte == '_' || byte == '~' || byte == '/' || byte == ':';
        }

        int HexValue(char character) {
            if (character >= '0' && character <= '9') {
                return character - '0';
            }
            if (character >= 'a' && character <= 'f') {
                return character - 'a' + 10;
            }
            if (character >= 'A' && character <= 'F') {
                return character - 'A' + 10;
            }
            return -1;
        }

        bool WildcardMatch(std::string_view text, std::string_view pattern) {
            std::size_t textIndex = 0;
            std::size_t patternIndex = 0;
            std::size_t starIndex = std::string_view::npos;
            std::size_t starTextIndex = 0;

            while (textIndex < text.size()) {
                if (patternIndex < pattern.size()
                    && (pattern[patternIndex] == '?' || pattern[patternIndex] == text[textIndex])) {
                    ++textIndex;
                    ++patternIndex;
                } else if (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
                    starIndex = patternIndex;
                    starTextIndex = textIndex;
                    ++patternIndex;
                } else if (starIndex != std::string_view::npos) {
                    patternIndex = starIndex + 1;
                    ++starTextIndex;
                    textIndex = starTextIndex;
                } else {
                    return false;
                }
            }
            while (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
                ++patternIndex;
            }
            return patternIndex == pattern.size();
        }

        std::string NormalizePatternOrigin(std::string_view pattern) {
            const std::size_t schemeEnd = pattern.find("://");
            if (schemeEnd == std::string_view::npos) {
                return ToLowerAscii(pattern);
            }
            const std::size_t authorityStart = schemeEnd + 3;
            const std::size_t authorityEnd = pattern.find_first_of("/?#", authorityStart);
            std::string origin(
                pattern.substr(0, authorityEnd == std::string_view::npos ? pattern.size() : authorityEnd));
            if (origin.size() > authorityStart && origin.back() == '/') {
                origin.pop_back();
            }
            return ToLowerAscii(origin);
        }
    }

    std::string ExtractOrigin(std::string_view url) {
        const std::string trimmed = TrimAscii(url);
        url = trimmed;
        if (url.empty()) {
            return {};
        }

        const std::size_t schemeEnd = url.find(':');
        if (schemeEnd == std::string_view::npos || schemeEnd == 0) {
            return {};
        }
        for (std::size_t index = 0; index < schemeEnd; ++index) {
            if ((index == 0 && !IsAlpha(url[index])) || (index > 0 && !IsSchemeCharacter(url[index]))) {
                return {};
            }
        }
        const std::string scheme = ToLowerAscii(url.substr(0, schemeEnd));

        if (scheme == "file") {
            return "file://";
        }
        if (scheme == "blob") {
            const std::string_view inner = url.substr(schemeEnd + 1);
            const std::string origin = ExtractOrigin(inner);
            return origin.empty() ? "null" : origin;
        }
        if (scheme == "about" || scheme == "data" || scheme == "javascript" || scheme == "mailto" || scheme == "tel") {
            return "null";
        }
        if (url.size() < schemeEnd + 3 || url[schemeEnd + 1] != '/' || url[schemeEnd + 2] != '/') {
            return "null";
        }

        const std::size_t authorityStart = schemeEnd + 3;
        const std::size_t authorityEnd = url.find_first_of("/?#", authorityStart);
        std::string_view authority = url.substr(authorityStart,
            authorityEnd == std::string_view::npos ? std::string_view::npos : authorityEnd - authorityStart);
        const std::size_t at = authority.rfind('@');
        if (at != std::string_view::npos) {
            authority.remove_prefix(at + 1);
        }
        if (authority.empty()) {
            return scheme + "://";
        }

        std::string host;
        std::string port;
        if (authority.front() == '[') {
            const std::size_t close = authority.find(']');
            if (close == std::string_view::npos) {
                return {};
            }
            host = std::string(authority.substr(0, close + 1));
            if (close + 1 < authority.size() && authority[close + 1] == ':') {
                port = std::string(authority.substr(close + 2));
            }
        } else {
            const std::size_t colon = authority.rfind(':');
            if (colon != std::string_view::npos && authority.find(':') == colon) {
                host = std::string(authority.substr(0, colon));
                port = std::string(authority.substr(colon + 1));
            } else {
                host = std::string(authority);
            }
        }
        if (host.empty()) {
            return {};
        }
        for (char& character : host) {
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }
        if (!port.empty()) {
            if (port.find_first_not_of("0123456789") != std::string::npos) {
                return {};
            }
            const bool defaultPort = (scheme == "http" || scheme == "ws")
                ? port == "80"
                : ((scheme == "https" || scheme == "wss") ? port == "443" : false);
            if (defaultPort) {
                port.clear();
            }
        }
        std::string origin = scheme + "://" + host;
        if (!port.empty()) {
            origin += ":";
            origin += port;
        }
        return origin;
    }

    bool MatchesPattern(std::string_view text, std::string_view pattern) {
        if (pattern.empty()) {
            return text.empty();
        }
        if (text.size() > 32768 || pattern.size() > 4096) {
            return false;
        }
        return WildcardMatch(ToLowerAscii(text), ToLowerAscii(pattern));
    }

    bool MatchesUrlPattern(std::string_view url, std::string_view pattern) {
        return MatchesPattern(url, pattern);
    }

    bool OriginMatchesPattern(std::string_view origin, std::string_view pattern) {
        if (pattern == "*") {
            return true;
        }
        if (origin.empty() || pattern.empty()) {
            return false;
        }
        return MatchesPattern(origin, NormalizePatternOrigin(pattern));
    }

    bool IsValidOriginPattern(std::string_view pattern) {
        const std::string trimmed = TrimAscii(pattern);
        if (trimmed.empty() || trimmed.size() > 1024) {
            return false;
        }
        if (trimmed == "*" || trimmed == "null") {
            return true;
        }
        for (unsigned char byte : trimmed) {
            if (byte < 0x20 || byte == 0x7F || byte == ' ' || byte == '"' || byte == '\'' || byte == '<' || byte == '>'
                || byte == '\\') {
                return false;
            }
        }
        const std::size_t schemeEnd = trimmed.find("://");
        if (schemeEnd == std::string::npos || schemeEnd == 0) {
            return false;
        }
        for (std::size_t index = 0; index < schemeEnd; ++index) {
            char character = trimmed[index];
            if (character == '*') {
                continue;
            }
            if ((index == 0 && !IsAlpha(character)) || (index > 0 && !IsSchemeCharacter(character))) {
                return false;
            }
        }
        const std::size_t authorityStart = schemeEnd + 3;
        const std::size_t authorityEnd = trimmed.find_first_of("/?#", authorityStart);
        const std::string_view authority = std::string_view(trimmed).substr(
            authorityStart, authorityEnd == std::string::npos ? std::string::npos : authorityEnd - authorityStart);
        return !authority.empty();
    }

    bool IsHttpLikeUrl(std::string_view url) {
        const std::string trimmed = TrimAscii(url);
        const std::size_t colon = trimmed.find(':');
        if (colon == std::string::npos) {
            return false;
        }
        const std::string scheme = ToLowerAscii(std::string_view(trimmed).substr(0, colon));
        return scheme == "http" || scheme == "https" || scheme == "file" || scheme == "about" || scheme == "data";
    }

    std::string FileUrlFromPath(const std::filesystem::path& path) {
        const std::filesystem::path absolute = std::filesystem::absolute(path).lexically_normal();
        const std::u8string utf8 = absolute.generic_u8string();
        const std::string text(reinterpret_cast<const char*>(utf8.data()), utf8.size());

        std::string encoded = "file://";
        if (text.empty() || text.front() != '/') {
            encoded += "/";
        }
        constexpr char digits[] = "0123456789ABCDEF";
        for (unsigned char byte : text) {
            if (IsPercentSafe(byte)) {
                encoded.push_back(static_cast<char>(byte));
            } else {
                encoded.push_back('%');
                encoded.push_back(digits[(byte >> 4) & 0x0Fu]);
                encoded.push_back(digits[byte & 0x0Fu]);
            }
        }
        return encoded;
    }

    std::filesystem::path PathFromFileUrl(std::string_view url) {
        std::string path = std::string(url);
        if (path.rfind("file://", 0) == 0) {
            path.erase(0, 7);
        }
        std::string decoded;
        decoded.reserve(path.size());
        for (std::size_t index = 0; index < path.size(); ++index) {
            if (path[index] == '%' && index + 2 < path.size()) {
                const int high = HexValue(path[index + 1]);
                const int low = HexValue(path[index + 2]);
                if (high >= 0 && low >= 0) {
                    decoded.push_back(static_cast<char>((high << 4) | low));
                    index += 2;
                    continue;
                }
            }
            decoded.push_back(path[index]);
        }
#if defined(_WIN32)
        if (decoded.size() >= 3 && decoded.front() == '/' && decoded[2] == ':') {
            decoded.erase(decoded.begin());
        }
        for (char& character : decoded) {
            if (character == '/') {
                character = '\\';
            }
        }
#endif
        return PathFromUtf8(decoded);
    }

    bool ResolveWithinDirectory(
        const std::filesystem::path& directory, std::string_view relativePath, std::filesystem::path& resolved) {
        std::string relative(relativePath);
        if (!relative.empty() && relative.front() == '/') {
            relative.erase(relative.begin());
        }
        while (relative.rfind("./", 0) == 0) {
            relative.erase(0, 2);
        }
        if (relative.empty()) {
            relative = "index.html";
        }

        std::error_code error;
        const std::filesystem::path base = std::filesystem::weakly_canonical(directory, error);
        if (error) {
            return false;
        }
        const std::filesystem::path candidate = std::filesystem::weakly_canonical(base / PathFromUtf8(relative), error);
        if (error) {
            return false;
        }

        std::string baseText = base.generic_string();
        std::string candidateText = candidate.generic_string();
#if defined(_WIN32)
        baseText = ToLowerAscii(baseText);
        candidateText = ToLowerAscii(candidateText);
#endif
        if (candidateText.size() < baseText.size() || candidateText.compare(0, baseText.size(), baseText) != 0) {
            return false;
        }
        if (candidateText.size() > baseText.size()) {
            const char separator = candidateText[baseText.size()];
            if (separator != '/' && separator != '\\') {
                return false;
            }
        }
        resolved = candidate;
        return true;
    }
}
