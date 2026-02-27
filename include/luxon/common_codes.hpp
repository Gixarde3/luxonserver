// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <cstdint>

// For clarity reasons, some of the listed names here differ from the documented names

namespace luxon {
// https://doc-api.photonengine.com/en/pun/v1/class_operation_code.html (2026-01-29)
// https://web.archive.org/web/20260129180500/https://doc-api.photonengine.com/en/pun/v1/class_operation_code.html
namespace OpCodes {
// Core Lite operations
namespace Lite {
enum Enum : uint8_t {
    Join = 255,
    Leave = 254,
    RaiseEvent = 253,
    SetProperties = 252,
    GetProperties = 251,
    Ping = 249,
    ChangeInterestGroups = 248,
};
} // namespace Lite

// Authentication operations
namespace Auth {
enum Enum : uint8_t {
    AuthenticateOnce = 231,
    Authenticate = 230,
};
} // namespace Auth

// Lobby and Game List operations
namespace Lobby {
enum Enum : uint8_t {
    JoinLobby = 229,
    LeaveLobby = 228,
    LobbyStats = 221,
    GetGameList = 217,
};
} // namespace Lobby

// Matchmaking operations
namespace Matchmaking {
enum Enum : uint8_t {
    CreateGame = 227,
    JoinGame = 226,
    JoinRandomGame = 225,
    DebugGame = 223,
};
} // namespace Matchmaking

// Social operations
namespace Social {
enum Enum : uint8_t {
    FindFriends = 222,
};
} // namespace Social

// Remote Procedure Calls and Configuration
namespace RpcAndMisc {
enum Enum : uint8_t {
    Rpc = 219,
    GetRegions = 220,
    Settings = 218,
};
} // namespace RpcAndMisc
} // namespace OpCodes

// https://doc-api.photonengine.com/en/pun/v1/class_event_code.html (2026-01-29)
// https://web.archive.org/web/20260129180424/https://doc-api.photonengine.com/en/pun/v1/class_event_code.html
namespace EventCodes {
enum Enum : uint8_t {
    LobbyStats = 224,
    AppStats = 226,
    GameList = 230,
    GameListUpdate = 229,
    PropertiesUpdate = 253,
    TokenUpdate = 223,
    Join = 255,
    Leave = 254,
    Disconnect = 252,
    Error = 251,
};
} // namespace EventCodes

// https://doc-api.photonengine.com/en/pun/v1/class_parameter_code.html (2026-01-29)
// https://web.archive.org/web/20260129180742/https://doc-api.photonengine.com/en/pun/v1/class_parameter_code.html
namespace DictKeyCodes {
namespace ProtocolDetails {
enum Enum : uint8_t {
    ExpectedProtocol = 195,
    CustomInitData = 194,
    EncryptionMode = 193,
    EncryptionData = 192,
};
} // namespace ProtocolDetails

namespace GameAndActor {
enum Enum : uint8_t {
    GameId = 255,
    ActorNo = 254,
    TargetActorNo = 253,
    ActorList = 252,
};
} // namespace GameAndActor

namespace Properties {
enum Enum : uint8_t {
    Properties = 251,
    ActorProperties = 249,
    GameProperties = 248,
    ExpectedValues = 231,
};
} // namespace Properties

namespace RoutingAndEvents {
enum Enum : uint8_t {
    Broadcast = 250,
    Cache = 247,
    ReceiverGroup = 246,
    Data = 245,
    Code = 244,
    Flush = 243,
    CleanupCacheOnLeave = 241,
    InterestGroup = 240,
    Remove = 239,
    PublishUserId = 239,
    Add = 238,
    AddUsers = 238,
    SuppressRoomEvents = 237,
};
} // namespace RoutingAndEvents

namespace GameSettings {
enum Enum : uint8_t {
    EmptyRoomTTL = 236,
    PlayerTTL = 235,
    IsInactive = 233,
    CheckUserOnJoin = 232,
    GameFlags = 191,
};
} // namespace GameSettings

namespace WebAndForwarding {
enum Enum : uint8_t {
    EventForward = 234,
    WebFlags = 234,
};
} // namespace WebAndForwarding

namespace LoadBalancing {
enum Enum : uint8_t {
    Address = 230,
    PeerCount = 229,
    ForceRejoin = 229,
    GameCount = 228,
    MasterPeerCount = 227,
    UserId = 225,
    ApplicationId = 224,
    Position = 223,
    MatchmakingType = 223,
    GameList = 222,
    Token = 221,
    AppVersion = 220,
    NodeId = 219,
    Info = 218,
};
} // namespace LoadBalancing

namespace AuthAndLobby {
enum Enum : uint8_t {
    ClientAuthenticationType = 217,
    ClientAuthenticationParameters = 216,
    ClientAuthenticationData = 214,
    CreateIfNotExists = 215,
    JoinMode = 215,
    LobbyName = 213,
    LobbyType = 212,
    LobbyStats = 211,
    Region = 210,
    Cluster = 196,
    UriPath = 209,
    FindFriendsResponseOnlineList = 1,
    FindFriendsResponseRoomIdList = 2,
    FindFriendsRequestList = 1,
    FindFriendsOptions = 2
};
} // namespace AuthAndLobby

namespace RpcAndPlugins {
enum Enum : uint8_t {
    RpcCallParams = 208,
    RpcCallRetCode = 207,
    RpcCallRetMessage = 206,
    CacheSliceIndex = 205,
    Plugins = 204,
    PluginName = 201,
    PluginVersion = 200,
};
} // namespace RpcAndPlugins

namespace MetadataAndMisc {
enum Enum : uint8_t {
    MasterClientId = 203,
    NickName = 202,
    Flags = 199,
    CloudType = 198,
    GameRemoveReason = 197,
};
} // namespace MetadataAndMisc
} // namespace DictKeyCodes

// https://doc-api.photonengine.com/en/pun/v1/class_error_code.html (2026-01-29)
// https://web.archive.org/web/20260129180245/https://doc-api.photonengine.com/en/pun/v1/class_error_code.html
namespace ErrorCodes {
// Core: Success states, ranges, and generic system errors
namespace Core {
enum Enum : int16_t {
    Ok = 0,
    InternalServerError = -1,
    OperationInvalid = -2,
    OperationNotAllowedInCurrentState = -3,
    ArgumentOutOfRange = -4,
    SerializationLimitError = -13,
};
} // namespace Core

// Authentication & Security: Tokens, Crypto, and Permissions
namespace Auth {
enum Enum : int16_t {
    InvalidAuthentication = 32767,
    AuthenticationTokenExpired = 32753,
    AuthRequestWaitTimeout = 32736,
    CustomAuthenticationFailed = 32755,
    AuthenticationServiceTemporarilyUnavailable = 32754,
    SecureConnectionRequired = 32740,

    // Client specific
    TokenRequired = 2,
    ProtocolUnavailable = 3,

    // Crypto specific
    CryptoProviderNotSet = -16,
    DecryptionFailure = -17,
    InvalidEncryptionParameters = -18,
};
} // namespace Auth

// Server & Connection: Connectivity, Availability, and Regions
namespace Server {
enum Enum : int16_t {
    NotReady = -7,
    Overload = -8,
    Maintenance = -10,
    Backoff = -9,
    RedirectRepeat = 32759,
    InvalidRegion = 32756,
    ConnectionSwitched = 32735,
    ServerFull = 32762,
    ApplicationUnavailable = 1,
};
} // namespace Server

// Matchmaking: Games, Slots, Joining, and Game Logic
namespace Matchmaking {
enum Enum : int16_t {
    GameIdAlreadyExists = 32766,
    GameIdNotExists = 32758,
    NoRandomMatchFound = 32760,
    GameClosed = 32764,
    GameFull = 32765,
    AlreadyMatched = 32763,
    ServerForbidden = 32761,
    SlotError = 32742,
    ActorListFull = 32734,

    // Consistency checks
    PluginReportedError = 32752,
    ServerCheckFailed = 32738,
};

namespace JoinFail {
enum Enum : int16_t {
    JoinFailedFoundActiveJoiner = 32746,
    JoinFailedFoundInactiveJoiner = 32749,
    JoinFailedWithRejoinerNotFound = 32748,
    JoinFailedPeerAlreadyJoined = 32750,
    JoinFailedFoundExcludedUserId = 32747,
};
} // namespace JoinFail
} // namespace Matchmaking

// Data & Operations: Parsing, Buffers, and Op Logic
namespace Data {
enum Enum : int16_t {
    InvalidRequestParameters = -6,
    SendBufferFull = -11,
    UnexpectedData = -12,
    WrongInitRequestData = -14,
    ResponseParseError = -15,
    EventCacheExceeded = 32739,

    // Operation specific
    OperationLimitReached = 32743,
    OperationSizeLimitExceeded = -38,
    OperationParametersLimitExceeded = -39,
};
} // namespace Data

// Throttling: Rate limits and caps
namespace Throttling {
enum Enum : int16_t {
    IncomingDataRateExceeded = -30,
    IncomingMsgRateExceeded = -31,
    IncomingMaxMsgSizeExceeded = -32,

    OperationRateExceeded = -35,
    OperationDataRateExceeded = -36,
    OperationBlocked = -37,

    MessagesRateExceeded = -40,
    MessagesDataRateExceeded = -41,
    MessagesBlocked = -42,
    MessageSizeLimitExceeded = -43,

    HttpLimitReached = 32745,
    MaxCcuReached = 32757,
};
} // namespace Throttling
} // namespace ErrorCodes

// https://doc-api.photonengine.com/en/pun/current/class_photon_1_1_realtime_1_1_room_options.html (2026-02-04)
// https://web.archive.org/web/20260204185936/https://doc-api.photonengine.com/en/pun/current/class_photon_1_1_realtime_1_1_room_options.html
namespace GameProps {
enum Enum : uint8_t {
    MaxPlayers = 255,
    IsVisible = 254,
    IsOpen = 253,
    PlayerCount = 252,
    Removed = 251,
    CleanupCacheOnLeave = 249,
    LobbyProperties = 250,
    MasterClientId = 248,
    ExpectedUsers = 247,
    PlayerTTL = 246,
    EmptyGameTTL = 245,
};
} // namespace GameProps

// https://doc-api.photonengine.com/en/pun/current/class_photon_1_1_realtime_1_1_actor_properties.html (2026-02-04)
// https://web.archive.org/web/20260204190349/https://doc-api.photonengine.com/en/pun/current/class_photon_1_1_realtime_1_1_actor_properties.html
namespace ActorProps {
enum Enum : uint8_t {
    NickName = 255,
    IsInactive = 254,
    UserId = 253,
};
} // namespace ActorProps

// https://doc-api.photonengine.com/en/pun/current/class_photon_1_1_realtime_1_1_room_options.html (2026-02-04)
// https://web.archive.org/web/20260204185936/https://doc-api.photonengine.com/en/pun/current/class_photon_1_1_realtime_1_1_room_options.html
namespace GameFlags {
enum Enum : uint32_t {
    CheckUserOnJoin = 1,
    DeleteCacheOnLeave = 2,
    SuppressRoomEvents = 4,
    PublishUserId = 8,
    DeleteNullProps = 16,
    BroadcastPropsChangeToAll = 32,
    SuppressPlayerInfo = 64,
};
} // namespace GameFlags

namespace ReceiverGroup {
enum Enum : uint8_t {
    Others = 0,
    All = 1,
    MasterClient = 2,
};
} // namespace ReceiverGroup

// https://doc-api.photonengine.com/en/plugins/current/class_photon_1_1_hive_1_1_plugin_1_1_cache_operations.html (2026-02-04)
// https://web.archive.org/web/20260204190053/https://doc-api.photonengine.com/en/plugins/current/class_photon_1_1_hive_1_1_plugin_1_1_cache_operations.html
namespace CacheOperation {
enum Enum : uint8_t {
    DoNotCache = 0,
    MergeCache = 1,
    ReplaceCache = 2,
    RemoveCache = 3,
    AddToRoomCache = 4,
    AddToRoomCacheGlobal = 5,
    RemoveFromRoomCache = 6,
    RemoveFromCacheForActorsLeft = 7,
    SliceIncreaseIndex = 10,
    SliceSetIndex = 11,
    SlicePurgeIndex = 12,
    SlicePurgeUpToIndex = 13,
};
} // namespace CacheOperation

// https://doc-api.photonengine.com/en/pun/current/namespace_photon_1_1_realtime.html#a0b5a0270c468f91b73474bae9bbca85e (2026-02-04)
// https://web.archive.org/web/20260204190216/https://doc-api.photonengine.com/en/pun/current/namespace_photon_1_1_realtime.html#a0b5a0270c468f91b73474bae9bbca85e
namespace MatchmakingType {
enum Enum : uint8_t {
    FillRoom = 0,
    SerialMatching = 1,
    RandomMatching = 2,
};
} // namespace MatchmakingType

// https://doc-api.photonengine.com/en/pun/v1/_loadbalancing_peer_8cs.html#ab34738ecd04700648af88bf53d1d74ad (2026-02-12)
// http://web.archive.org/web/20250630140825/https://doc-api.photonengine.com/en/pun/v1/_loadbalancing_peer_8cs.html#ab34738ecd04700648af88bf53d1d74ad
namespace LobbyType {
enum Enum : uint8_t {
    Default = 0,
    SqlLobby = 2,
    AsyncLobby = 3,
};
} // namespace LobbyType

// Guessed and checked via black box test
namespace FindFriendsOptions {
enum Enum : int32_t { Default = 0, CreatedOnGS = 1, Visible = 2, Open = 4 };
}
} // namespace luxon
