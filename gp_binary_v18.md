# GpBinaryV18 Wire Format Specification

**This document has been written by an anonymous person that does not contribute to Luxon in any way.**

Clean-room reimplementation notes for `ExitGames.Client.Photon.Protocol18` (`GpBinaryV18`, version bytes `01 08`).

This document describes the **actual wire encoding implemented by client code**, including important quirks.


## 1. General rules

### 1.1 Version identifier
Protocol version bytes are:

- `0x01 0x08`

### 1.2 Typed vs. untyped encodings

Most values can be encoded in two forms:

1. **Typed value**  
   A leading **type byte** is present.
2. **Payload-only value**  
   No leading type byte; the surrounding container already knows the type.

This matters because some scalar types use **special compact typed forms** that are **not used** in payload-only form.

Example:
- A top-level `int 5` is encoded as type `Int1 (0x0B)` + one byte magnitude.
- An `int` inside a `Dictionary<byte,int>` value slot is encoded payload-only as **zigzag varint32**, with **no** `Int1` optimization.

### 1.3 Endianness

#### Integer endianness
- `short` / `ushort` payloads are **little-endian**.

#### Float / double endianness
The implementation copies raw bytes with `Buffer.BlockCopy` and reconstructs with `BitConverter`. Therefore the wire format uses the runtime's native float/double byte order. On all normal Photon/Unity targets this is effectively:

- `float`: IEEE-754 binary32, **little-endian**
- `double`: IEEE-754 binary64, **little-endian**

A compatible reimplementation should use little-endian IEEE-754.

### 1.4 Lengths and counts

Unless stated otherwise, lengths and counts are encoded as **compressed unsigned 32-bit integers** (`varuint32`; see §2).

Exceptions:
- Parameter table count in operation/event messages is a **single raw byte**.


## 2. Variable-length integer encodings

## 2.1 `varuint32`
Unsigned 32-bit integer, 7 bits per byte, little-endian groups, continuation bit in bit 7.

For each byte:
- low 7 bits contribute payload
- high bit `0x80` means “more bytes follow”

Writer emits the **minimal-length** representation.

### Encoding
Repeatedly output:
- `byte = value & 0x7F`
- if more bits remain, set `byte |= 0x80`
- shift `value >>= 7`
- stop when `value == 0`

### Decoding
Accumulate:
- `result |= (byte & 0x7F) << shift`
- `shift += 7`
- stop when `(byte & 0x80) == 0`

Maximum read length is 5 bytes.

## 2.2 `varuint64`
Same scheme, up to 10 bytes.

## 2.3 ZigZag for signed integers

### 32-bit
- Encode: `zigzag32(n) = (uint)((n << 1) ^ (n >> 31))`
- Decode: `n = (int)((value >> 1) ^ (0 - (value & 1)))`

### 64-bit
- Encode: `zigzag64(n) = (ulong)((n << 1) ^ (n >> 63))`
- Decode: `n = (long)((value >> 1) ^ (0 - (value & 1)))`


## 3. Type code registry

Decimal and hex values:

| Dec | Hex | Name |
|---:|---:|---|
| 0 | 00 | Unknown |
| 2 | 02 | Boolean |
| 3 | 03 | Byte |
| 4 | 04 | Short |
| 5 | 05 | Float |
| 6 | 06 | Double |
| 7 | 07 | String |
| 8 | 08 | Null |
| 9 | 09 | CompressedInt |
| 10 | 0A | CompressedLong |
| 11 | 0B | Int1 |
| 12 | 0C | Int1_ |
| 13 | 0D | Int2 |
| 14 | 0E | Int2_ |
| 15 | 0F | L1 |
| 16 | 10 | L1_ |
| 17 | 11 | L2 |
| 18 | 12 | L2_ |
| 19 | 13 | Custom |
| 20 | 14 | Dictionary |
| 21 | 15 | Hashtable |
| 23 | 17 | ObjectArray |
| 24 | 18 | OperationRequest |
| 25 | 19 | OperationResponse |
| 26 | 1A | EventData |
| 27 | 1B | BooleanFalse |
| 28 | 1C | BooleanTrue |
| 29 | 1D | ShortZero |
| 30 | 1E | IntZero |
| 31 | 1F | LongZero |
| 32 | 20 | FloatZero |
| 33 | 21 | DoubleZero |
| 34 | 22 | ByteZero |
| 64 | 40 | Array |
| 66 | 42 | BooleanArray |
| 67 | 43 | ByteArray |
| 68 | 44 | ShortArray |
| 69 | 45 | FloatArray |
| 70 | 46 | DoubleArray |
| 71 | 47 | StringArray |
| 73 | 49 | CompressedIntArray |
| 74 | 4A | CompressedLongArray |
| 83 | 53 | CustomTypeArray |
| 84 | 54 | DictionaryArray |
| 85 | 55 | HashtableArray |
| 128..228 | 80..E4 | CustomTypeSlim (implicit custom code = type byte - 128) |

Notes:
- `CustomTypeSlim` is not a single code; it is a range.
- Writer emits slim custom type only for custom code `< 100`, i.e. `0x80..0xE3`.
- Reader accepts `0x80..0xE4` (custom code `0..100`).


## 4. Full typed value encodings

A **typed value** is normally:

```text
[type byte][payload]
```

except:
- `Null`, `BooleanFalse`, `BooleanTrue`, zero-specials have no payload
- `CustomTypeSlim` folds the custom type code into the type byte itself


## 5. Scalar types

## 5.1 Null

### Typed form
```text
08
```

No payload.


## 5.2 Boolean

### Canonical typed forms emitted by writer
- `true`  → `1C`
- `false` → `1B`

No payload.

### Additional typed form accepted by reader
```text
02 [1 byte]
```

Payload interpretation:
- `0` → false
- any nonzero byte → true

### Payload-only form
```text
[1 byte]
```

Writer emits `00` or `01`.  
Reader treats any nonzero as true.


## 5.3 Byte

### Typed forms
- zero:
  ```text
  22
  ```
- nonzero:
  ```text
  03 [u8]
  ```

### Payload-only form
```text
[u8]
```


## 5.4 Int16 / short

### Typed forms
- zero:
  ```text
  1D
  ```
- nonzero:
  ```text
  04 [u16le]
  ```

Payload is the raw 16-bit pattern in little-endian.

### Payload-only form
```text
[u16le]
```


## 5.5 Int32 / int

There are several typed forms.

### Typed forms accepted by reader

#### Zero
```text
1E
```

#### Small positive magnitude, 1 byte
```text
0B [u8]
```
Decoded as `+payload`.

#### Small negative magnitude, 1 byte
```text
0C [u8]
```
Decoded as `-payload`.

#### Small positive magnitude, 2 bytes
```text
0D [u16le]
```
Decoded as `+payload`.

#### Small negative magnitude, 2 bytes
```text
0E [u16le]
```
Decoded as `-payload`.

#### General form
```text
09 [varuint32 zigzag-encoded]
```

### Canonical typed form emitted by writer
Writer chooses:
- `1E` for `0`
- `0B` for `1..255`
- `0D` for `256..65535`
- `0C` for `-1..-255`
- `0E` for `-256..-65535`
- otherwise `09 + zigzag(varuint32)`

### Payload-only form
```text
[varuint32 zigzag-encoded]
```

Important:
- payload-only `int` **never** uses `0B/0C/0D/0E/1E`
- it is always the general zigzag-varint form


## 5.6 Int64 / long

Exactly analogous to `int`, but using `varuint64` in the general form.

### Typed forms accepted by reader

#### Zero
```text
1F
```

#### Small positive magnitude, 1 byte
```text
0F [u8]
```

#### Small negative magnitude, 1 byte
```text
10 [u8]
```

#### Small positive magnitude, 2 bytes
```text
11 [u16le]
```

#### Small negative magnitude, 2 bytes
```text
12 [u16le]
```

#### General form
```text
0A [varuint64 zigzag-encoded]
```

### Canonical typed form emitted by writer
Writer chooses:
- `1F` for `0`
- `0F` for `1..255`
- `11` for `256..65535`
- `10` for `-1..-255`
- `12` for `-256..-65535`
- otherwise `0A + zigzag(varuint64)`

### Payload-only form
```text
[varuint64 zigzag-encoded]
```


## 5.7 Float / single

### Typed form emitted by writer
```text
05 [4 bytes IEEE754 little-endian]
```

### Additional typed zero form accepted by reader
```text
20
```

No payload.

### Payload-only form
```text
[4 bytes IEEE754 little-endian]
```

Note:
- writer does **not** emit `FloatZero (0x20)`


## 5.8 Double

### Typed form emitted by writer
```text
06 [8 bytes IEEE754 little-endian]
```

### Additional typed zero form accepted by reader
```text
21
```

No payload.

### Payload-only form
```text
[8 bytes IEEE754 little-endian]
```

Note:
- writer does **not** emit `DoubleZero (0x21)`


## 5.9 String

UTF-8 encoded.

### Typed form
```text
07 [varuint32 byteLength] [UTF-8 bytes]
```

### Payload-only form
```text
[varuint32 byteLength] [UTF-8 bytes]
```

Rules:
- empty string = length `0`, no UTF-8 bytes
- null string is **not** encoded with type 07; null is encoded as `Null (08)`
- writer rejects strings whose UTF-8 byte length exceeds `32767`


## 6. Arrays

## 6.1 BooleanArray (`0x42`)

### Typed form
```text
42 [varuint32 count] [packed bits]
```

### Payload-only form
```text
[varuint32 count] [packed bits]
```

Packing:
- 8 booleans per byte
- first array element = bit 0 (`0x01`)
- second = bit 1 (`0x02`)
- ...
- eighth = bit 7 (`0x80`)
- final partial byte uses the same low-bit-first order

Example for 3 elements `[b0,b1,b2]`:
- payload byte = `(b0?1:0) | (b1?2:0) | (b2?4:0)`


## 6.2 ByteArray (`0x43`)

### Typed form
```text
43 [varuint32 length] [raw bytes]
```

### Payload-only form
```text
[varuint32 length] [raw bytes]
```

`ArraySegment<byte>` and `ByteArraySlice` are encoded identically to `byte[]`.


## 6.3 ShortArray (`0x44`)

### Typed form
```text
44 [varuint32 count] [count * int16le]
```

### Payload-only form
```text
[varuint32 count] [count * int16le]
```


## 6.4 FloatArray (`0x45`)

### Typed form
```text
45 [varuint32 count] [count * 4 raw float bytes]
```

### Payload-only form
```text
[varuint32 count] [count * 4 raw float bytes]
```

Bytes are copied directly from the runtime float array representation.


## 6.5 DoubleArray (`0x46`)

### Typed form
```text
46 [varuint32 count] [count * 8 raw double bytes]
```

### Payload-only form
```text
[varuint32 count] [count * 8 raw double bytes]
```


## 6.6 StringArray (`0x47`)

### Typed form
```text
47 [varuint32 count] [string payload 0] ... [string payload count-1]
```

Each element is encoded as a **payload-only string**:
```text
[varuint32 utf8ByteLength][utf8Bytes]
```

### Payload-only form
Same as above without the leading `47`.

Rules:
- writer forbids `null` elements
- empty strings are allowed


## 6.7 CompressedIntArray (`0x49`)

### Typed form
```text
49 [varuint32 count] [count * payload-only int]
```

Each element is payload-only `int`:
```text
[varuint32 zigzag-encoded]
```

### Payload-only form
Same without the leading `49`.


## 6.8 CompressedLongArray (`0x4A`)

### Typed form
```text
4A [varuint32 count] [count * payload-only long]
```

Each element is payload-only `long`:
```text
[varuint64 zigzag-encoded]
```

### Payload-only form
Same without the leading `4A`.


## 6.9 ObjectArray (`0x17`)

Used for:
- `object[]`
- `List<object>`

### Typed form
```text
17 [varuint32 count] [typed value 0] ... [typed value count-1]
```

### Payload-only form
Same without the leading `17`.

Each element is a full typed value and may be heterogeneous.


## 6.10 HashtableArray (`0x55`)

### Typed form
```text
55 [varuint32 count] [payload-only hashtable 0] ... [payload-only hashtable count-1]
```

### Payload-only form
Same without the leading `55`.


## 6.11 DictionaryArray (`0x54`)

### Typed form
```text
54 [dictionary header] [varuint32 count] [payload-only dictionary 0] ... [payload-only dictionary count-1]
```

Each element dictionary uses the **same header-defined key/value types**.

Important quirk:
- the writer always emits the leading `54`, even if called with `writeType = false`


## 6.12 CustomTypeArray (`0x53`)

For arrays `T[]` where `T` is a registered custom type.

### Typed form
```text
53 [varuint32 count] [customTypeCode:u8] [element0] ... [elementN-1]
```

Each element is:
```text
[varuint32 bodyLength] [bodyBytes]
```

There is **no per-element type byte**.

### Payload-only form
Same without the leading `53`.


## 6.13 Array / jagged array (`0x40`)

This is used for arrays whose element type is itself an array, i.e. jagged arrays such as:
- `int[][]`
- `string[][]`
- `byte[][][]`

### Typed form
```text
40 [varuint32 count] [typed subarray 0] ... [typed subarray count-1]
```

Each element is serialized as a full typed value; typically one of the array types above, or another `40`.

Important:
- this is **jagged array** encoding, not CLR multidimensional array encoding
- the serializer is effectively for one-dimensional arrays and jagged arrays only
- writer always emits the leading `40`, regardless of `writeType`


## 7. Hashtable (`0x15`)

Photon hashtable type: `ExitGames.Client.Photon.Hashtable`.

### Typed form
```text
15 [varuint32 entryCount] [typed key0][typed value0] ... [typed keyN-1][typed valueN-1]
```

### Payload-only form
```text
[varuint32 entryCount] [typed key0][typed value0] ...
```

Rules:
- keys and values are both full typed values
- null keys can be written; reader ignores entries whose decoded key is null


## 8. Dictionary (`0x14`)

CLR generic dictionary type:
```csharp
Dictionary<TKey, TValue>
```

## 8.1 Typed form
```text
14 [dictionary header] [varuint32 entryCount] [entries]
```

## 8.2 Payload-only form
```text
[dictionary header] [varuint32 entryCount] [entries]
```

Entries are encoded according to the header:
- if a side is declared as `object`, each element on that side is a full typed value
- otherwise that side uses a payload-only value of the declared type


## 8.3 Dictionary header

The header begins with:

```text
[key type descriptor:u8] [value type descriptor...]
```

### Key type descriptor
One byte:
- `00` if `TKey == object`
- otherwise the scalar type code of `TKey`

Writer allows primitive/string keys; reader only reliably accepts:
- `Byte (03)`
- `Short (04)`
- `Float (05)`
- `Double (06)`
- `String (07)`
- `CompressedInt (09)`
- `CompressedLong (0A)`

Quirk:
- writer would emit `Boolean (02)` for `Dictionary<bool,...>`, but reader rejects it as invalid for dictionary keys

### Value type descriptor
One of:

- `00` for `TValue == object`
- a single non-array type code
- `14` followed by a nested dictionary header for `TValue = Dictionary<,>`
- an array type descriptor (see below)


## 8.4 Dictionary array type descriptors

For `TValue` that is an array, writer encodes the type in the header, not per element.

### One-dimensional arrays
The descriptor is a single byte:

| TValue | Header byte |
|---|---:|
| `bool[]` | 66 |
| `byte[]` | 67 |
| `short[]` | 68 |
| `float[]` | 69 |
| `double[]` | 70 |
| `string[]` | 71 |
| `int[]` | 73 |
| `long[]` | 74 |
| `object[]` | 23 |
| `Hashtable[]` | 85 |

### Jagged arrays
For `TValue = U[][]...[]`, the descriptor is:

```text
40 repeated (rank-1) times, then one final byte for the one-dimensional array type of U
```

Examples:
- `int[][]` → `40 49`
- `int[][][]` → `40 40 49`
- `string[][]` → `40 47`
- `byte[][]` → `40 43`

Notes:
- `49` is `CompressedIntArray`, used as the one-dimensional `int[]` marker
- `43` is `ByteArray`, used as the one-dimensional `byte[]` marker

### Unsupported dictionary value array types in writer
Writer cannot emit array type descriptors for:
- custom type arrays as dictionary values
- dictionary arrays as dictionary values

Even though standalone `CustomTypeArray (83)` and `DictionaryArray (84)` exist.


## 8.5 Dictionary entry encoding

After header and entry count:

For each entry:
- key:
  - full typed value if key descriptor was `00`
  - otherwise payload-only value of declared key type
- value:
  - full typed value if value descriptor was `00`
  - otherwise payload-only value of declared value type

Examples:

### `Dictionary<byte,int>`
```text
14
03            ; key type = Byte
09            ; value type = CompressedInt
[varuint32 count]
[key:u8][value:zigzag varint32]...
```

### `Dictionary<object,string>`
```text
14
00            ; key type = object
07            ; value type = String
[varuint32 count]
[typed key][string payload]...
```

### `Dictionary<byte,Dictionary<string,int>>`
```text
14
03            ; key type = Byte
14            ; value type = Dictionary
07 09         ; nested header: string -> int
[varuint32 count]
[key:u8][nested dictionary payload]...
```


## 9. Custom types

Custom types are registered externally in:
- `Protocol.TypeDict` (CLR type → custom registration)
- `Protocol.CodeDict` (custom code → registration)

A custom registration supplies either:
- `SerializeFunction(object) -> byte[]`
- or `SerializeStreamFunction(StreamBuffer, object) -> short`

The wire format is the same regardless:

```text
[custom type selector] [varuint32 bodyLength] [bodyBytes]
```

## 9.1 Typed custom value forms

### Slim form
If custom code `< 100`, writer emits:
```text
(128 + code) [varuint32 bodyLength] [bodyBytes]
```

So type byte range `80..E3` corresponds to custom codes `0..99`.

### Explicit form
If custom code `>= 100`, writer emits:
```text
13 [customCode:u8] [varuint32 bodyLength] [bodyBytes]
```

### Reader behavior
Reader treats any first byte in `80..E4` as slim custom:
- custom code = `typeByte - 128`

So `E4` (code 100) is accepted by reader but never emitted by writer.

## 9.2 Payload-only custom value
When type is known externally:
```text
[customCode:u8] [varuint32 bodyLength] [bodyBytes]
```

## 9.3 CustomTypeArray recap
For `T[]` where `T` is custom:
```text
53 [varuint32 count] [customCode:u8] [count * ([varuint32 bodyLength][bodyBytes])]
```


## 10. Protocol message types

## 10.1 EventData (`0x1A`)

### Typed form
```text
1A [eventCode:u8] [parameterTable]
```

### Parameter table
```text
[paramCount:u8] repeated paramCount times:
    [paramKey:u8] [typed value]
```

Notes:
- count is one raw byte, not varuint32
- values are full typed values


## 10.2 OperationRequest (`0x18`)

### Typed form
```text
18 [operationCode:u8] [parameterTable]
```

Parameter table format is exactly the same as for `EventData`.


## 10.3 OperationResponse (`0x19`)

### Typed form
```text
19 [operationCode:u8] [returnCode:int16le] [debugMessageField] [parameterTable]
```

### `debugMessageField`
Special-case encoding:

- if `DebugMessage` is null **or empty string**:
  ```text
  08
  ```
- otherwise:
  ```text
  07 [string payload]
  ```

So empty string is encoded exactly like null.

### Parameter table
Same format as above:
```text
[paramCount:u8] repeated:
    [paramKey:u8] [typed value]
```


## 10.4 DisconnectMessage (decode-only in this class)

This class only implements deserialization, but the expected wire structure is:

```text
[code:int16le] [typed string-or-null] [parameterTable]
```

The parameter table here is the same byte-count + typed values format.


## 11. Runtime type → wire type selection

This is how `Serialize(object, setType)` chooses the encoded type.

## 11.1 Scalars
- `null` → `Null`
- `bool` → `Boolean`
- `byte` → `Byte`
- `short` → `Short`
- `int` → `CompressedInt`
- `long` → `CompressedLong`
- `float` → `Float`
- `double` → `Double`
- `string` → `String`

Enums are treated like primitives by `Type.GetTypeCode(enumType)`. Only enums whose type code maps to a supported primitive will serialize successfully.

## 11.2 Arrays
Given CLR type `T[]`:
- `byte[]` → `ByteArray`
- `short[]` → `ShortArray`
- `int[]` → `CompressedIntArray`
- `long[]` → `CompressedLongArray`
- `bool[]` → `BooleanArray`
- `float[]` → `FloatArray`
- `double[]` → `DoubleArray`
- `string[]` → `StringArray`
- `object[]` → `ObjectArray`
- `Hashtable[]` → `HashtableArray`
- `Dictionary<,>[]` → `DictionaryArray`
- any other `U[]` → `CustomTypeArray`
- if element type is itself an array (`T[][]`, etc.) → `Array`

## 11.3 Containers and messages
- `ExitGames.Client.Photon.Hashtable` → `Hashtable`
- `List<object>` → `ObjectArray`
- `Dictionary<,>` → `Dictionary`
- `EventData` → `EventData`
- `OperationRequest` → `OperationRequest`
- `OperationResponse` → `OperationResponse`

## 11.4 Custom / unknown
If no built-in mapping exists, serializer attempts registered custom-type handling.


## 12. Exact payload-only forms by declared type

When a container already knows the type, these are the payload bytes used:

| Declared type | Payload-only encoding |
|---|---|
| `bool` | `u8` (`00` or `01`; nonzero accepted as true) |
| `byte` | `u8` |
| `short` | `u16le` |
| `int` | `zigzag32 -> varuint32` |
| `long` | `zigzag64 -> varuint64` |
| `float` | 4 raw bytes |
| `double` | 8 raw bytes |
| `string` | `varuint32 utf8Len + utf8` |
| `Hashtable` | hashtable body, no leading `15` |
| `Dictionary<,>` | dictionary body, no leading `14` |
| `object[]` | object-array body, no leading `17` |
| `bool[]` | bool-array body, no leading `42` |
| `byte[]` | byte-array body, no leading `43` |
| `short[]` | short-array body, no leading `44` |
| `int[]` | compressed-int-array body, no leading `49` |
| `long[]` | compressed-long-array body, no leading `4A` |
| `float[]` | float-array body, no leading `45` |
| `double[]` | double-array body, no leading `46` |
| `string[]` | string-array body, no leading `47` |
| custom type | `customCode:u8 + bodyLength + body` |
| custom type array | count + customCode + elements, no leading `53` |


## 13. Important quirks for exact compatibility

1. **Typed booleans are emitted as `1B/1C`, not `02 + payload`.**
2. **Typed float/double zero-specials (`20`, `21`) are accepted on read but never emitted.**
3. **Payload-only `int` / `long` always use zigzag varints.**
   They never use `Int1`, `Int2`, `L1`, `L2`, or zero-specials.
4. **OperationResponse empty debug message is encoded as null (`08`).**
5. **Parameter table counts are one raw byte.**
6. **Dictionary key type `bool` can be emitted by writer but is not accepted by reader.**
7. **`DictionaryArray` and jagged `Array` writers always emit their type byte even if `writeType=false`.**
8. **String writer enforces max UTF-8 length 32767.**
9. **This format supports jagged arrays, not CLR multidimensional array semantics.**
