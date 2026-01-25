#pragma once

#include "ser_types.hpp"

#include <string>

namespace luxon {
struct HttpRequest;

namespace ser {
class IProtocol;
} // namespace ser

namespace visualizer {
namespace helpers {
ser::ByteArray hex_to_bytes(std::string hex);
void print_indent(int indent);
void print_hex_dump(const ser::ByteArray& data, int indent);
} // namespace helpers

void print_value(const ser::Value& value, int indent = 0);
void print_http_message(const HttpRequest& req, int indent = 0);
bool print_http_message(const ser::ByteArray& data, int indent = 0);
bool print_ser_message(const ser::ByteArray& data, int indent, ser::IProtocol& proto);
void print_ser_message(const ser::Message& msg, int indent = 0);
void print_enet_packet(const ser::ByteArray& data, ser::IProtocol& proto);
} // namespace visualizer
} // namespace luxon
