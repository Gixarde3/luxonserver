# Luxon

**Luxon** is a clean-room, modern C++ reimplementation of the Photon ENet reliable UDP protocol and the GP Binary serialization format (Versions 1.6 and 1.8). It provides both client and server-side network logic, encryption support, and protocol visualization/debugging tools.\
The project is written in **C++23** and aims to be a lightweight, dependency-flexible way to interact with Photon-based applications.

## Legal Disclaimer

> Luxon is an independent, open‑source project developed by its contributors. It is **not** affiliated with, endorsed by, or sponsored by Exit Games GmbH or any of its subsidiaries.
> 
> **Photon** and **Photon Realtime** are registered trademarks of Exit Games GmbH. All other trademarks, service marks, and trade names referenced in this project are the property of their respective owners.
> 
> Luxon is designed to be protocol‑compatible with the Photon Realtime client SDKs. This compatibility is achieved solely through the "Chinese Wall" doctrine.
> 
> Any use of the term "Photon" within this repository is for descriptive purposes only, to indicate compatibility, and does not imply any endorsement or official relationship.
> 
> If you are a representative of Exit Games and have concerns regarding this project, please contact me at tuxifan@posteo.de so I may address them promptly.

## Features

### Networking (Luxon ENet)

This is where Photon ENet protocol is implemented:

* **Full ENet Implementation**: Handles the custom reliable UDP protocol used by Photon.
* **Reliability Modes**: Supports Reliable, Unreliable, Unsequenced, and Fragmented delivery.
* **Command Aggregation**: Packs multiple commands into single MTU-sized datagrams.
* **Flow Control**: Sliding window acknowledgments, round-trip time (RTT) calculation, and automatic resending.
* **CRC Integrity**: Optional CRC32 checksum verification for packets.

### Serialization (Luxon Ser)

This is where Photon's binary serialization is implemented:

* **Protocol Support**: Implementation of **GP Binary V16** and **GP Binary V18**.
* **Type System**: Supports primitives (u8, i16, i64, f32, etc.), arrays, hashmaps, dictionaries, and custom types.
* **Encryption**:
  * **Diffie-Hellman Key Exchange** (Oakley Group 1, 768-bit).
  * **AES-256-CBC** payload encryption with PKCS#7 padding.
  * Backends: Choice between **mbedTLS** (default) or **LibTomCrypt**.

### Utilities

* **Visualizer**: A robust packet inspector that can hex-dump and pretty-print parsed ENet commands and Serialization messages.
* **HTTP Parser**: Lightweight internal HTTP parser for HTTP protocol init.

## Requirements

* **C++ Compiler**: Must support **C++23** (MSVC 2022+, GCC 13+, Clang 16+).
* **CMake**: Version 3.21 or higher.

## Build Instructions

Luxon uses standard CMake workflows.

### 1. Basic Build (mbedTLS)

This is the default configuration.

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### 2. Build with LibTomCrypt

If you prefer LibTomCrypt/LibTomMath over mbedTLS:

```bash
cmake .. -DLUXON_USE_TOMCRYPT=ON
cmake --build .
```

## Project Structure

The project is modularized into several libraries:

* **luxon_enet**: Core networking logic (Peers, Channels, Sockets, Packet Fragmentation, ...).
* **luxon_ser**: Serialization engine and encryption.
* **luxon_visualizer**: Debugging tool to pretty-print packet contents.
* **luxon_http_parser**: Helper for parsing HTTP-like handshake responses.

## Usage Examples

### Initialization

Luxon relies on EnetPeerConfig to set up timeouts, MTU, and channel counts.

```c++
#include <luxon/enet_peer.hpp>

luxon::enet::EnetPeerConfig config;
config.channel_count = 2;
config.mtu = 1200;
config.crc_enabled = true; // Optionally use CRC
```

### **Serialization (GP Binary V18)**

```c++
#include <luxon/ser_gp_binary_v18.hpp>

using namespace luxon::ser;

// Create Protocol Handler
auto protocol = std::make_unique<GpBinaryV18>();

// Create a message (e.g., Operation Request)
OperationRequestMessage req;
req.operation_code = 101; // Example OpCode
req.parameters[1] = "Hello World";
req.parameters[2] = 12345;

// Serialize
auto bytes = protocol->Serialize(Message(req));

// Deserialize
auto decoded = protocol->Deserialize(*bytes);
```
