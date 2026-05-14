#pragma once

namespace luxon::ser {
enum class ProtocolImplID : unsigned short { GpBinaryV18, GpBinaryV16, IPCBinary, Custom, __length = Custom };
}
