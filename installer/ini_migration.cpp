#include "ini_migration.hpp"

#include <cctype>
#include <string_view>

namespace {

bool IsSpace(char character)
{
    return character == ' ' || character == '\t';
}

std::string_view Trim(std::string_view value)
{
    while (!value.empty() && IsSpace(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && IsSpace(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

bool EqualsAsciiInsensitive(std::string_view left, std::string_view right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

bool IsFalseValue(std::string_view value)
{
    return EqualsAsciiInsensitive(value, "false") ||
           EqualsAsciiInsensitive(value, "off") ||
           EqualsAsciiInsensitive(value, "no") ||
           value == "0";
}

std::size_t CommentPosition(std::string_view line)
{
    const std::size_t semicolon = line.find(';');
    const std::size_t hash = line.find('#');
    if (semicolon == std::string_view::npos) {
        return hash;
    }
    if (hash == std::string_view::npos) {
        return semicolon;
    }
    return semicolon < hash ? semicolon : hash;
}

} // namespace

namespace InstallerConfig {

MigrationResult EnableResetRestartFallback(std::string text)
{
    MigrationResult result;
    result.text.reserve(text.size());

    bool in_recovery_section = false;
    std::size_t line_start = 0;
    while (line_start < text.size()) {
        const std::size_t newline = text.find('\n', line_start);
        const std::size_t line_end = newline == std::string::npos ? text.size() : newline;
        std::size_t content_end = line_end;
        if (content_end > line_start && text[content_end - 1] == '\r') {
            --content_end;
        }

        std::string_view line(text.data() + line_start, content_end - line_start);
        std::size_t prefix_bytes = 0;
        if (line_start == 0 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            prefix_bytes = 3;
        }

        const std::string_view parse_line = line.substr(prefix_bytes);
        const std::size_t comment = CommentPosition(parse_line);
        const std::string_view active = Trim(parse_line.substr(0, comment));

        if (active.size() >= 2 && active.front() == '[' && active.back() == ']') {
            in_recovery_section = EqualsAsciiInsensitive(Trim(active.substr(1, active.size() - 2)), "Recovery");
        }

        bool replaced = false;
        if (in_recovery_section && !active.empty() && active.front() != '[') {
            const std::size_t equals = parse_line.find('=');
            if (equals != std::string_view::npos && (comment == std::string_view::npos || equals < comment) &&
                EqualsAsciiInsensitive(Trim(parse_line.substr(0, equals)), "ResetRestartFallback")) {
                const std::size_t value_limit = comment == std::string_view::npos ? parse_line.size() : comment;
                std::size_t value_begin = equals + 1;
                while (value_begin < value_limit && IsSpace(parse_line[value_begin])) {
                    ++value_begin;
                }
                std::size_t value_end = value_limit;
                while (value_end > value_begin && IsSpace(parse_line[value_end - 1])) {
                    --value_end;
                }
                if (IsFalseValue(parse_line.substr(value_begin, value_end - value_begin))) {
                    const std::size_t absolute_value_begin = line_start + prefix_bytes + value_begin;
                    const std::size_t absolute_value_end = line_start + prefix_bytes + value_end;
                    result.text.append(text, line_start, absolute_value_begin - line_start);
                    result.text.append("true");
                    result.text.append(text, absolute_value_end, line_end - absolute_value_end);
                    ++result.replacements;
                    replaced = true;
                }
            }
        }

        if (!replaced) {
            result.text.append(text, line_start, line_end - line_start);
        }
        if (newline != std::string::npos) {
            result.text.push_back('\n');
            line_start = newline + 1;
        } else {
            line_start = text.size();
        }
    }

    return result;
}

} // namespace InstallerConfig
