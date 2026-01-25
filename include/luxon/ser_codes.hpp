#pragma once

#include <cstdint>

namespace luxon {
namespace ser {

// https://gitlab.inf.uni-konstanz.de/sanani.rajabov/vr-classroom-grouping/-/blob/6ec1e00f6c17e8da59fd6f351e55d100a91cb902/Assets/Photon/PhotonLibs/Photon3Unity3D.xml#L851-862
// Timestamp keys are indeed sent over Websockets in ping iops
namespace Codes {
// Internal operation opcodes
static constexpr uint8_t IOpInitEncryption = 0;
static constexpr uint8_t IOpPing = 1;

// Internal operation dict keys
static constexpr uint8_t IKeyClientKey = 1;
static constexpr uint8_t IKeyServerKey = 1;
static constexpr uint8_t IKeyClientTimestamp = 1;
static constexpr uint8_t IKeyServerTimestamp = 2;

// Operation return codes
static constexpr uint8_t RetOk = 0;
} // namespace Codes
} // namespace ser
} // namespace luxon
