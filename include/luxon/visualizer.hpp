// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "ser_types.hpp"

#include <string>
#include <span>

namespace luxon {
struct HttpRequest;

namespace ser {
class IProtocol;
} // namespace ser

namespace visualizer {
namespace helpers {
ser::ByteArray hex_to_bytes(std::string hex);
void print_indent(int indent);
void print_hex_dump(std::span<const uint8_t> data, int indent);
} // namespace helpers

void print_value(const ser::Value& value, int indent = 0);
void print_http_message(const HttpRequest& req, int indent = 0);
bool print_http_message(std::span<const uint8_t> data, int indent = 0);
bool print_ser_message(std::span<const uint8_t> data, int indent, ser::IProtocol& proto);
void print_ser_message(const ser::Message& msg, int indent = 0);
void print_enet_packet(std::span<const uint8_t> data, ser::IProtocol& proto);
} // namespace visualizer
} // namespace luxon
