#include "http_parser.hpp"
#include <vector>
#include <optional>
#include <ranges>
#include <algorithm>
#include <cctype>

namespace luxon {
namespace {
constexpr std::string_view trim(std::string_view sv) {
    auto is_space = [](unsigned char c) { return std::isspace(c); };
    auto start = std::ranges::find_if_not(sv, is_space);
    auto end = std::ranges::find_if_not(sv | std::views::reverse, is_space);
    return (start == sv.end()) ? std::string_view{} : std::string_view{start, end.base()};
}
} // namespace

std::expected<HttpRequest, std::string> parse_raw_http(std::string_view raw) {
    HttpRequest req;
    size_t cursor = 0;

    auto read_until = [&](std::string_view delimiter) -> std::optional<std::string_view> {
        size_t pos = raw.find(delimiter, cursor);
        if (pos == std::string_view::npos)
            return std::nullopt;
        std::string_view token = raw.substr(cursor, pos - cursor);
        cursor = pos + delimiter.size();
        return token;
    };

    // Parse request line
    auto request_line = read_until("\r\n");
    if (!request_line)
        return std::unexpected("Malformed HTTP: No request line found");

    auto parts_view = *request_line | std::views::split(' ') | std::views::transform([](auto&& rng) { return std::string_view(rng); });
    std::vector<std::string_view> parts(parts_view.begin(), parts_view.end());

    if (parts.size() < 3)
        return std::unexpected("Malformed Request Line");

    req.method = parts[0];
    std::string_view full_path = parts[1];
    req.protocol_version = parts[2];

    // Parse path and query params
    size_t q_pos = full_path.find('?');
    if (q_pos != std::string_view::npos) {
        req.path = full_path.substr(0, q_pos);
        std::string_view query_str = full_path.substr(q_pos + 1);

        for (const auto& pair : query_str | std::views::split('&')) {
            std::string_view pair_sv(pair);
            size_t eq_pos = pair_sv.find('=');
            if (eq_pos != std::string_view::npos)
                req.query_params.emplace(pair_sv.substr(0, eq_pos), pair_sv.substr(eq_pos + 1));
            else if (!pair_sv.empty())
                req.query_params.emplace(pair_sv, "");
        }
    } else {
        req.path = full_path;
    }

    // Parse headers
    while (true) {
        size_t next_crlf = raw.find("\r\n", cursor);

        if (next_crlf == cursor) {
            cursor += 2;
            break;
        }

        if (next_crlf == std::string_view::npos)
            break;

        std::string_view header_line = raw.substr(cursor, next_crlf - cursor);
        cursor = next_crlf + 2;

        size_t colon_pos = header_line.find(':');
        if (colon_pos != std::string_view::npos) {
            std::string key(trim(header_line.substr(0, colon_pos)));
            std::string val(trim(header_line.substr(colon_pos + 1)));
            req.headers[key] = val;
        }
    }

    // Parse body
    if (cursor < raw.size())
        req.body = raw.substr(cursor);

    return req;
}
} // namespace luxon
