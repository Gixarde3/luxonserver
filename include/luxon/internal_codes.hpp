#pragma once

#include <cstdint>

namespace luxon {

// https://gitlab.inf.uni-konstanz.de/sanani.rajabov/vr-classroom-grouping/-/blob/6ec1e00f6c17e8da59fd6f351e55d100a91cb902/Assets/Photon/PhotonLibs/Photon3Unity3D.xml#L851-862
// Timestamp keys are indeed sent over Websockets in ping iops
namespace IOpCodes {
enum Enum : uint8_t {
    // Internal operation opcodes
    IOpInitEncryption = 0,
    IOpPing = 1,

    // Internal operation dict keys
    IKeyClientKey = 1,
    IKeyServerKey = 1,
    IKeyClientTimestamp = 1,
    IKeyServerTimestamp = 2,

    // Operation return codes
    RetOk = 0
};
} // namespace IOpCodes
} // namespace luxon
