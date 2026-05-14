# Luxon IPC Binary Protocol Specification

This is a custom, Luxon specific protocol. Its only purpose is fast serialization/deserialization for IPC. It's serialized form might be very big! Do not use it for data storage or network transmissions.

## 1. General Conventions

* Byte Order: All multi-byte numeric types (16-bit, 32-bit, 64-bit integers and floating-point numbers) are encoded in Little-Endian byte order.
* Protocol Limitations: The maximum allowed nesting depth for complex, recursive value structures (e.g., Arrays, Dictionaries) is 64 levels. Exceeding this limit will result in an immediate parsing error.
* Encryption: This specific protocol does not support transport-level encryption natively (the cryptographic handshake hooks are deliberately bypassed). However, an `encrypted` flag is carried in the message header for metadata purposes.

## 2. Top-Level Message Structure

Every IPC transmission constitutes a single `Message` prefixed by a static 3-byte header, followed immediately by a variant-specific payload.

| Offset | Type | Name | Description |
| :--- | :--- | :--- | :--- |
| 0x00 | `u8` | `magic` | Magic byte constant (`0xF5`). Parsing fails if this does not match the expected constant. |
| 0x01 | `u8` | `variant_index` | Defines the type of message being transmitted. Determines the payload structure. |
| 0x02 | `u8` | `encrypted_flag` | Evaluated as boolean (`1` = true, `0` = false). |
| 0x03 | `...` | `payload` | The structure of the payload depends on `variant_index`. |

## 3. Message Variant Payloads

The `variant_index` dictates how the remainder of the packet is processed. 

### Variant 0: InitMessage
Sent during initialization.
* `u8` protocol_major
* `u8` protocol_minor
* `u8` client_sdk_id
* `u8` ipv6 (Boolean flag: 0 or 1)
* `u8` version_major
* `u8` version_minor
* `u8` version_patch
* `u8` version_revision
* `String` app_id (See Primitive Types for `String` encoding)

### Variant 1: InitResponseMessage
Empty payload. Represents a successful initialization acknowledgement.

### Variant 2: OperationRequestMessage / Variant 6: InternalOperationRequestMessage
Both variants share the exact same payload structure, varying only by their top-level index.
* `u8` operation_code
* `Dictionary` parameters (See Composite Types for `Dictionary` encoding)

### Variant 3: OperationResponseMessage / Variant 7: InternalOperationResponseMessage
Both variants share the exact same payload structure.
* `u8` operation_code
* `i16` return_code
* `u8` has_debug_message (Boolean flag: 0 or 1)
* `String` debug_message (Conditionally present ONLY if `has_debug_message` == 1)
* `Dictionary` parameters

### Variant 4: EventMessage
* `u8` event_code
* `Dictionary` parameters

### Variant 5: DisconnectMessage
* `i16` code
* `u8` has_message (Boolean flag: 0 or 1)
* `String` message (Conditionally present ONLY if `has_message` == 1)
* `Dictionary` parameters

### Variant 8: GenericValueMessage
Wraps a dynamically typed `Value`.
* `Value` value (See Value System section)

### Variant 9: RawMessage
Carries unformatted byte data.
* `u32` byte_length
* `bytes[]` data (Raw array of bytes of size `byte_length`)

## 4. Value System and Type Tags

The protocol heavily utilizes a dynamically typed `Value` structure. Every serialized `Value` begins with a 1-byte `Type Tag`, followed by the corresponding data payload.

| Tag | Type | Payload Description |
| :--- | :--- | :--- |
| `0` | `Null` | No payload. Represents `std::monostate`. |
| `1` | `Boolean` | `u8` (0 for false, >0 for true). |
| `2` | `UInt8` | `u8` raw byte. |
| `3` | `Int16` | `i16` little-endian. |
| `4` | `Int32` | `i32` little-endian. |
| `5` | `Int64` | `i64` little-endian. |
| `6` | `Float32` | `f32` little-endian IEEE 754. |
| `7` | `Float64` | `f64` little-endian IEEE 754. |
| `8` | `String` | See `String` in Primitive Types. |
| `9` | `ByteArray` | See `ByteArray` in Primitive Types. |
| `10` | `BoolArray` | `u32` count, followed by `count` consecutive `u8` boolean values. |
| `11` | `Int16Array` | `u32` count, followed by `count * 2` bytes. |
| `12` | `Int32Array` | `u32` count, followed by `count * 4` bytes. |
| `13` | `Int64Array` | `u32` count, followed by `count * 8` bytes. |
| `14` | `Float32Array` | `u32` count, followed by `count * 4` bytes. |
| `15` | `Float64Array` | `u32` count, followed by `count * 8` bytes. |
| `16` | `StringArray` | `u32` count, followed by `count` encoded `String` objects. |
| `17` | `ObjectArray` | `u32` count, followed by `count` encoded `Value` objects. |
| `18` | `JaggedArray` | `u32` count, followed by `count` encoded `Value` objects. |
| `19` | `Dictionary` | See `Dictionary` in Composite Types. |
| `20` | `GenericDict` | `ByteArray` header, `u32` count, then `count` pairs of (`Value` key, `Value` val). |
| `21` | `Hashtable` | `u32` count. If `0xFFFFFFFF`, represents a null pointer. Else, `count` pairs of (`Value` key, `Value` val). |
| `22` | `RawCustomVal` | `u8` custom_code, followed by a `ByteArray` representing the data. |
| `23` | `EventMsg` | Encoded `EventMessage` payload (without the variant index header). |
| `24` | `OpReqMsg` | Encoded `OperationRequestMessage` payload. |
| `25` | `OpRespMsg` | Encoded `OperationResponseMessage` payload. |
| `26` | `DictArray` | `u32` count, followed by `count` encoded `Dictionary` objects. |
| `27` | `GenDictArray` | `u32` count, followed by `count` encoded `GenericDictionary` structures. |
| `28` | `HashTblArray` | `u32` count, followed by `count` encoded `Hashtable` structures. |
| `29` | `RawCustArray` | `u32` count, followed by `count` encoded `RawCustomValue` structures. |
| `30` | `PreSerialized`| `ByteArray` payload containing previously serialized data. |

## 5. Primitive and Composite Type Encodings

### Primitive Types
* `String`: Encoded as a `u32` indicating the length of the string in bytes, followed immediately by that exact number of un-terminated string bytes.
* `ByteArray`: Identical to `String`. A `u32` length, followed by the raw byte data.

### Composite Types
* `Dictionary`: 
  * `u32` representing the number of entries.
  * Followed by N iterations of key-value pairs where the key is a `u8` and the value is a fully encoded `Value` (including its Type Tag).
* `GenericDictionary`:
  * `ByteArray` acting as a header or descriptor.
  * `u32` representing the number of entries.
  * Followed by N iterations of key-value pairs where BOTH the key and the value are fully encoded `Value` structures.
* `Hashtable`:
  * `u32` entry count. A special value of `0xFFFFFFFF` denotes a null pointer and ends the parsing for this object immediately.
  * If valid, followed by N iterations of (`Value` key, `Value` value) pairs.
* `RawCustomValue`:
  * `u8` defining the custom type identifier.
  * `ByteArray` payload representing the internal custom state.
