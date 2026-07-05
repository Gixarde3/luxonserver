// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "visualizer.hpp"

#include "enet_protocol.hpp"
#include "http_parser.hpp"

#include <luxon/ser_interface.hpp>
#include <luxon/ser_types.hpp>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace luxon {
namespace visualizer {
namespace helpers {
ser::ByteArray hex_to_bytes(std::string hex) {
    ser::ByteArray bytes;
    hex.erase(std::remove(hex.begin(), hex.end(), ' '), hex.end());
    hex.erase(std::remove(hex.begin(), hex.end(), ':'), hex.end());

    if (hex.size() >= 2 && hex.substr(0, 2) == "0x")
        hex = hex.substr(2);

    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(strtol(byteString.c_str(), nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

void print_indent(int indent) {
    for (int i = 0; i < indent; ++i)
        std::cout << "  ";
}

void print_hex_dump(std::span<const uint8_t> data, int indent) {
    print_indent(indent);
    std::cout << "[ ";
    for (auto b : data)
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
    std::cout << std::dec << "]\n";
}

template <typename T> struct is_vector : std::false_type {};
template <typename... Args> struct is_vector<std::vector<Args...>> : std::true_type {};
template <typename T> inline constexpr bool is_vector_v = is_vector<T>::value;

static std::string error_to_string(const ser::Error& e) { return "Error(code=" + std::to_string((int)e.code) + ", msg=\"" + e.message + "\")"; }
} // namespace helpers

void print_value(const ser::Value& value, int indent);

static void print_parameters(const ser::ParameterList& params, int indent) {
    std::string prefix(indent * 2, ' ');
    std::cout << prefix << "Parameters (" << params.size() << "):\n";
    for (const auto& [key, value] : params) {
        std::cout << prefix << "  [" << (int)key << "]:\n";
        print_value(value, indent + 2);
    }
}

void print_value(const ser::Value& value, int indent) {
    std::string prefix(indent * 2, ' ');

    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;

            if constexpr (std::is_same_v<T, std::monostate>) {
                std::cout << prefix << "null\n";

            } else if constexpr (std::is_same_v<T, bool>) {
                std::cout << prefix << (v ? "true" : "false") << "\n";

            } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
                std::cout << prefix << v << "\n";

            } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                // print integers without char formatting surprises
                if constexpr (std::is_signed_v<T>)
                    std::cout << prefix << (long long)v << " (" << sizeof(T) * 8 << " bits)\n";
                else
                    std::cout << prefix << (unsigned long long)v << " (" << sizeof(T) * 8 << " bits)\n";

            } else if constexpr (std::is_same_v<T, std::string>) {
                std::cout << prefix << "\"" << v << "\"\n";

            } else if constexpr (std::is_same_v<T, ser::ByteArray>) {
                std::cout << prefix << "byte[" << v.size() << "]: ";
                for (auto byte : v) {
                    std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)byte << " ";
                }
                std::cout << std::dec << "\n";

            } else if constexpr (std::is_same_v<T, ser::ObjectArray>) {
                std::cout << prefix << "array[" << v.size() << "]:\n";
                for (size_t i = 0; i < v.size(); ++i) {
                    std::cout << prefix << "  [" << i << "]:\n";
                    print_value(v[i], indent + 2);
                }

            } else if constexpr (std::is_same_v<T, ser::JaggedArray>) {
                std::cout << prefix << "jagged_array[" << v.elements.size() << "]:\n";
                for (size_t i = 0; i < v.elements.size(); ++i) {
                    std::cout << prefix << "  [" << i << "]:\n";
                    print_value(v.elements[i], indent + 2);
                }

            } else if constexpr (helpers::is_vector_v<T>) {
                // vectors of primitives/strings/etc
                std::cout << prefix << "array[" << v.size() << "]:\n";
                for (size_t i = 0; i < v.size(); ++i) {
                    std::cout << prefix << "  [" << i << "]:\n";
                    if constexpr (std::is_same_v<T, std::vector<bool>>)
                        print_value(ser::Value(static_cast<bool>(v[i])), indent + 2);
                    else
                        print_value(ser::Value(v[i]), indent + 2);
                }

            } else if constexpr (std::is_same_v<T, ser::Dictionary>) {
                std::cout << prefix << "dictionary[" << v.size() << "]:\n";
                for (const auto& [key, val] : v) {
                    std::cout << prefix << "  " << (int)key << ":\n";
                    print_value(val, indent + 2);
                }

            } else if constexpr (std::is_same_v<T, ser::GenericDictionary>) {
                std::cout << prefix << "generic_dictionary[" << v.entries.size() << "]:\n";
                if (!v.header.empty()) {
                    std::cout << prefix << "  header: ";
                    for (auto byte : v.header) {
                        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)byte << " ";
                    }
                    std::cout << std::dec << "\n";
                }
                for (size_t i = 0; i < v.entries.size(); ++i) {
                    std::cout << prefix << "  [" << i << "] key:\n";
                    print_value(v.entries[i].first, indent + 2);
                    std::cout << prefix << "  [" << i << "] value:\n";
                    print_value(v.entries[i].second, indent + 2);
                }

            } else if constexpr (std::is_same_v<T, ser::HashtablePtr>) {
                if (!v) {
                    std::cout << prefix << "hashtable: null\n";
                    return;
                }
                std::cout << prefix << "hashtable[" << v->size() << "]:\n";
                for (const auto& [k, val] : *v) {
                    std::cout << prefix << "  key:\n";
                    print_value(k, indent + 2);
                    std::cout << prefix << "  value:\n";
                    print_value(val, indent + 2);
                }

            } else if constexpr (std::is_same_v<T, ser::RawCustomValue>) {
                std::cout << prefix << "custom[" << v.data.size() << "], code=" << (int)v.custom_code << "\n";
                helpers::print_hex_dump(v.data, indent + 1);

            } else if constexpr (std::is_same_v<T, ser::EventMessage>) {
                std::cout << prefix << "EventData (Code: " << (int)v.event_code << ")\n";
                print_parameters(v.parameters, indent + 1);

            } else if constexpr (std::is_same_v<T, ser::OperationRequestMessage>) {
                std::cout << prefix << "OperationRequest (Code: " << (int)v.operation_code << ")\n";
                print_parameters(v.parameters, indent + 1);

            } else if constexpr (std::is_same_v<T, ser::OperationResponseMessage>) {
                std::cout << prefix << "OperationResponse (Code: " << (int)v.operation_code << ", ReturnCode: " << v.return_code << ")\n";
                if (v.debug_message)
                    std::cout << prefix << "  DebugMsg: " << *v.debug_message << "\n";
                print_parameters(v.parameters, indent + 1);

            } else {
                std::cout << prefix << "[unhandled value type]\n";
            }
        },
        value.value);
}

void print_http_message(const HttpRequest& req, int indent) {
    std::string prefix(indent * 2, ' ');

    std::cout << prefix << "--- Parsed HTTP Request ---\n";
    std::cout << prefix << "Method:    " << req.method << "\n";
    std::cout << prefix << "Path:      " << req.path << "\n";
    std::cout << prefix << "Version:   " << req.protocol_version << "\n";

    std::cout << "\n" << prefix << "[Query Parameters]\n";
    for (const auto& [k, v] : req.query_params)
        std::cout << prefix << "  " << k << ": " << v << "\n";

    std::cout << "\n" << prefix << "[Headers]\n";
    for (const auto& [k, v] : req.headers)
        std::cout << prefix << "  " << k << ": " << v << "\n";

    std::cout << "\n" << prefix << "[Body]\n";
    std::cout << prefix << "  Size: " << req.body.size() << " bytes\n";
    std::cout << prefix << "  Content: " << req.body << std::endl;
}

bool print_http_message(std::span<const uint8_t> data, int indent) {
    auto result = parse_raw_http(std::string_view{reinterpret_cast<const char *>(data.data()), data.size()});
    if (!result)
        return false;

    print_http_message(result.value(), indent);
    return true;
}

void print_ser_message(const ser::Message& msg, int indent) {
    std::string prefix(indent * 2, ' ');

    msg.visit([&](const auto& m) {
        using T = std::decay_t<decltype(m)>;

        if constexpr (std::is_same_v<T, ser::InitMessage>) {
            std::cout << prefix << ">>> INIT\n";
            std::cout << prefix << "Protocol: " << (int)m.protocol_major << "." << (int)m.protocol_minor << "\n";
            std::cout << prefix << "Client SDK ID: " << (int)m.client_sdk_id << "\n";
            std::cout << prefix << "IPv6: " << (m.ipv6 ? "true" : "false") << "\n";
            std::cout << prefix << "Version: " << (int)m.version_major << "." << (int)m.version_minor << "." << (int)m.version_patch << " (rev "
                      << (int)m.version_revision << ")\n";
            std::cout << prefix << "App ID: " << m.app_id << "\n";

        } else if constexpr (std::is_same_v<T, ser::InitResponseMessage>) {
            std::cout << prefix << "<<< INIT RESPONSE\n";

        } else if constexpr (std::is_same_v<T, ser::OperationRequestMessage>) {
            std::cout << prefix << ">>> OPERATION REQUEST\n";
            std::cout << prefix << "OpCode: " << (int)m.operation_code << "\n";
            print_parameters(m.parameters, indent);

        } else if constexpr (std::is_same_v<T, ser::OperationResponseMessage>) {
            std::cout << prefix << "<<< OPERATION RESPONSE\n";
            std::cout << prefix << "OpCode: " << (int)m.operation_code << "\n";
            std::cout << prefix << "ReturnCode: " << m.return_code << "\n";
            if (m.debug_message)
                std::cout << prefix << "DebugMsg: " << *m.debug_message << "\n";
            print_parameters(m.parameters, indent);

        } else if constexpr (std::is_same_v<T, ser::EventMessage>) {
            std::cout << prefix << "<<< EVENT\n";
            std::cout << prefix << "EventCode: " << (int)m.event_code << "\n";
            print_parameters(m.parameters, indent);

        } else if constexpr (std::is_same_v<T, ser::DisconnectMessage>) {
            std::cout << prefix << "<<< DISCONNECT\n";
            std::cout << prefix << "Code: " << m.code << "\n";
            if (m.message)
                std::cout << prefix << "Message: " << *m.message << "\n";
            print_parameters(m.parameters, indent);

        } else if constexpr (std::is_same_v<T, ser::InternalOperationRequestMessage>) {
            std::cout << prefix << ">>> INTERNAL OP REQUEST\n";
            std::cout << prefix << "OpCode: " << (int)m.operation_code << "\n";
            print_parameters(m.parameters, indent);

        } else if constexpr (std::is_same_v<T, ser::InternalOperationResponseMessage>) {
            std::cout << prefix << "<<< INTERNAL OP RESPONSE\n";
            std::cout << prefix << "OpCode: " << (int)m.operation_code << "\n";
            std::cout << prefix << "ReturnCode: " << m.return_code << "\n";
            if (m.debug_message)
                std::cout << prefix << "DebugMsg: " << *m.debug_message << "\n";
            print_parameters(m.parameters, indent);

        } else if constexpr (std::is_same_v<T, ser::GenericValueMessage>) {
            std::cout << prefix << "<<< GENERIC VALUE MESSAGE\n";
            print_value(m.value, indent + 1);

        } else if constexpr (std::is_same_v<T, ser::RawMessage>) {
            std::cout << prefix << "<<< RAW MESSAGE (" << m.bytes.size() << " bytes)\n";
            helpers::print_hex_dump(m.bytes, indent + 1);

        } else {
            std::cout << prefix << "[unhandled message type]\n";
        }
    });
    std::cout << std::endl;
}

bool print_ser_message(std::span<const uint8_t> data, int indent, ser::IProtocol& proto) {
    if (data.empty())
        return false;

    std::string prefix(indent * 2, ' ');

    if (data[0] != ser::GP_MAGIC)
        return false;

    auto decoded = proto.Deserialize(std::span<const uint8_t>(data.data(), data.size()));
    if (!decoded) {
        std::cout << prefix << "ser decode failed: " << helpers::error_to_string(decoded.error()) << "\n";
        return false;
    }

    print_ser_message(*decoded, indent);
    return true;
}

void print_enet_packet(std::span<const uint8_t> data, ser::IProtocol& proto) {
    enet::EnetPacketHeader header;
    std::vector<enet::EnetCommand> commands;

    try {
        commands = enet::parse_packet(data, header);
    } catch (const std::exception& e) {
        std::cout << "Failed to parse as ENet packet: " << e.what() << "\n";
        std::cout << "Attempting raw ser parse...\n";
        if (!print_ser_message(data, 0, proto)) {
            std::cout << "Raw ser parse failed. Attempting HTTP request parse...\n";
            if (!print_http_message(data, 0)) {
                std::cout << "HTTP request parse failed.\n";
                helpers::print_hex_dump(data, 0);
            }
        }
        return;
    }

    std::string typeStr;
    switch (header.type) {
    case enet::EnetUdpHeaderType::PlainNoCrc:
        typeStr = "Plain (No CRC)";
        break;
    case enet::EnetUdpHeaderType::PlainWithCrc:
        typeStr = "Plain (CRC)";
        break;
    case enet::EnetUdpHeaderType::Encrypted:
        typeStr = "Encrypted (Gcm/Cbc)";
        break;
    default:
        typeStr = "Unknown";
        break;
    }

    std::cout << "=== ENet UDP Packet ===\n";
    std::cout << "Type:      " << typeStr << "\n";
    std::cout << "PeerID:    " << header.peer_id << "\n";
    std::cout << "CRC:       " << header.crc32.value_or(0) << (header.crc32.has_value() ? "" : " (None)") << "\n";
    std::cout << "Count:     " << (int)header.command_count << "\n";
    std::cout << "Time:      " << header.sent_time << "\n";
    std::cout << "Challenge: 0x" << std::hex << header.challenge << std::dec << "\n";
    std::cout << "=======================" << std::endl;

    for (size_t i = 0; i < commands.size(); ++i) {
        const auto& cmd = commands[i];
        std::cout << "\nCommand [" << i << "]: ";

        std::string cmdTypeStr = "Unknown";
        switch (cmd.header.command_type) {
        case enet::EnetCommandType::Acknowledge:
            cmdTypeStr = "Acknowledge";
            break;
        case enet::EnetCommandType::Connect:
            cmdTypeStr = "Connect";
            break;
        case enet::EnetCommandType::VerifyConnect:
            cmdTypeStr = "VerifyConnect";
            break;
        case enet::EnetCommandType::Disconnect:
            cmdTypeStr = "Disconnect";
            break;
        case enet::EnetCommandType::Ping:
            cmdTypeStr = "Ping";
            break;
        case enet::EnetCommandType::SendReliable:
            cmdTypeStr = "SendReliable";
            break;
        case enet::EnetCommandType::SendUnreliable:
            cmdTypeStr = "SendUnreliable";
            break;
        case enet::EnetCommandType::SendFragment:
            cmdTypeStr = "SendFragment";
            break;
        case enet::EnetCommandType::SendUnreliableUnsequenced:
            cmdTypeStr = "SendUnreliableUnsequenced";
            break;
        case enet::EnetCommandType::EgServerTime:
            cmdTypeStr = "EgServerTime";
            break;
        case enet::EnetCommandType::EgSendUnreliableProcessed:
            cmdTypeStr = "EgSendUnreliableProcessed";
            break;
        case enet::EnetCommandType::EgSendReliableUnsequenced:
            cmdTypeStr = "EgSendReliableUnsequenced";
            break;
        case enet::EnetCommandType::EgSendFragmentUnsequenced:
            cmdTypeStr = "EgSendFragmentUnsequenced";
            break;
        case enet::EnetCommandType::EgAcknowledgeUnsequenced:
            cmdTypeStr = "EgAcknowledgeUnsequenced";
            break;
        default:
            cmdTypeStr = "Type_" + std::to_string((int)cmd.header.command_type);
            break;
        }

        std::cout << cmdTypeStr << " (Ch: " << (int)cmd.header.channel_id << ", Seq: " << cmd.header.reliable_seq << ", Len: " << cmd.header.command_length
                  << ", Flags: " << (int)cmd.header.flags << ")\n";

        if (cmd.header.command_type == enet::EnetCommandType::Acknowledge || cmd.header.command_type == enet::EnetCommandType::EgAcknowledgeUnsequenced) {
            std::cout << "  Ack Seq: " << cmd.ack_received_reliable_sequence_number << "\n";
            std::cout << "  Ack Time: " << cmd.ack_received_sent_time << "\n";
        } else if (cmd.header.command_type == enet::EnetCommandType::SendFragment ||
                   cmd.header.command_type == enet::EnetCommandType::EgSendFragmentUnsequenced) {
            std::cout << "  Fragment: " << cmd.fragment_number << " / " << cmd.fragment_count << " (Total Len: " << cmd.fragment_total_length
                      << ", Offset: " << cmd.fragment_offset << ", StartSeq: " << cmd.fragment_start_seq << ")\n";
            std::cout << "  Fragment Payload:\n";
            helpers::print_hex_dump(cmd.get_payload(), 1);
        } else if (cmd.header.command_type == enet::EnetCommandType::SendUnreliable) {
            std::cout << "  Unreliable Seq: " << cmd.unreliable_seq << "\n";
        } else if (cmd.header.command_type == enet::EnetCommandType::SendUnreliableUnsequenced) {
            std::cout << "  Unseq Group: " << cmd.unsequenced_group_number << "\n";
        }

        bool isDataCommand =
            (cmd.header.command_type == enet::EnetCommandType::SendReliable || cmd.header.command_type == enet::EnetCommandType::SendUnreliable ||
             cmd.header.command_type == enet::EnetCommandType::SendUnreliableUnsequenced ||
             cmd.header.command_type == enet::EnetCommandType::EgSendReliableUnsequenced ||
             cmd.header.command_type == enet::EnetCommandType::EgSendUnreliableProcessed);

        if (isDataCommand) {
            if (!cmd.is_payload_empty()) {
                if (!print_ser_message(cmd.get_payload(), 1, proto)) {
                    if (!print_http_message(cmd.get_payload(), 1)) {
                        std::cout << "  [Raw Payload Data]\n";
                        helpers::print_hex_dump(cmd.get_payload(), 1);
                    }
                }
            }
        } else if (cmd.header.command_type == enet::EnetCommandType::Connect || cmd.header.command_type == enet::EnetCommandType::VerifyConnect) {
            if (!cmd.is_payload_empty()) {
                std::cout << "  [Connect Payload]\n";
                helpers::print_hex_dump(cmd.get_payload(), 1);
            }
        }
    }
}
} // namespace visualizer
} // namespace luxon
