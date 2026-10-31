#include "core/text-utils.h"

#include <cctype>

namespace Flare {
    std::string TrimAscii(std::string_view text) {
        std::size_t start = 0;
        while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
            ++start;
        }
        std::size_t end = text.size();
        while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
            --end;
        }
        return std::string(text.substr(start, end - start));
    }

    std::string ToLowerAscii(std::string_view text) {
        std::string lowered(text);
        for (char& character : lowered) {
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }
        return lowered;
    }

    bool EqualsIgnoreCase(std::string_view left, std::string_view right) {
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index) {
            char a = left[index];
            char b = right[index];
            if (a >= 'A' && a <= 'Z') {
                a = static_cast<char>(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = static_cast<char>(b - 'A' + 'a');
            }
            if (a != b) {
                return false;
            }
        }
        return true;
    }

    bool IsValidName(std::string_view name) {
        if (name.empty() || name.size() > 128) {
            return false;
        }
        const auto validFirst = [](char character) {
            return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
                || (character >= '0' && character <= '9') || character == '_';
        };
        const auto validRest = [](char character) {
            return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
                || (character >= '0' && character <= '9') || character == '_' || character == '.' || character == ':'
                || character == '-';
        };
        if (!validFirst(name.front())) {
            return false;
        }
        for (char character : name.substr(1)) {
            if (!validRest(character)) {
                return false;
            }
        }
        return true;
    }

    std::string SanitizeFileName(std::string_view name) {
        std::string result;
        result.reserve(name.size());
        for (char character : name) {
            const bool allowed = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
                || (character >= '0' && character <= '9') || character == '.' || character == '_' || character == '-';
            result.push_back(allowed ? character : '_');
            if (result.size() >= 96) {
                break;
            }
        }
        while (!result.empty() && (result.front() == '.' || result.front() == ' ')) {
            result.erase(result.begin());
        }
        if (result.empty() || result == "." || result == "..") {
            return "window";
        }
        return result;
    }
}
