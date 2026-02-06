// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string>
#include <string_view>
#include <map>
#include <expected>

namespace luxon {
struct HttpRequest {
    std::string method;
    std::string path;
    std::string protocol_version;
    std::map<std::string, std::string> query_params;
    std::map<std::string, std::string> headers;
    std::string body;
};

std::expected<HttpRequest, std::string> parse_raw_http(std::string_view raw);
} // namespace luxon
