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
constexpr uint8_t Join = 255;
constexpr uint8_t Leave = 254;
constexpr uint8_t RaiseEvent = 253;
constexpr uint8_t SetProperties = 252;
constexpr uint8_t GetProperties = 251;
constexpr uint8_t Ping = 249;
constexpr uint8_t ChangeInterestGroups = 248;
} // namespace Lite

// Authentication operations
namespace Auth {
constexpr uint8_t AuthenticateOnce = 231;
constexpr uint8_t Authenticate = 230;
} // namespace Auth

// Lobby and Game List operations
namespace Lobby {
constexpr uint8_t JoinLobby = 229;
constexpr uint8_t LeaveLobby = 228;
constexpr uint8_t LobbyStats = 221;
constexpr uint8_t GetGameList = 217;
} // namespace Lobby

// Matchmaking operations
namespace Matchmaking {
constexpr uint8_t CreateGame = 227;
constexpr uint8_t JoinGame = 226;
constexpr uint8_t JoinRandomGame = 225;
constexpr uint8_t DebugGame = 223;
} // namespace Matchmaking

// Social operations
namespace Social {
constexpr uint8_t FindFriends = 222;
} // namespace Social

// Remote Procedure Calls and Configuration
namespace RpcAndMisc {
constexpr uint8_t Rpc = 219;
constexpr uint8_t GetRegions = 220;
constexpr uint8_t Settings = 218;
} // namespace RpcAndMisc
} // namespace OpCodes

// https://doc-api.photonengine.com/en/pun/v1/class_event_code.html (2026-01-29)
// https://web.archive.org/web/20260129180424/https://doc-api.photonengine.com/en/pun/v1/class_event_code.html
namespace EventCodes {
static constexpr uint8_t LobbyStats = 224;
static constexpr uint8_t AppStats = 226;
static constexpr uint8_t GameList = 230;
static constexpr uint8_t GameListUpdate = 229;
static constexpr uint8_t PropertiesUpdate = 253;
static constexpr uint8_t TokenUpdate = 223;
static constexpr uint8_t Join = 255;
static constexpr uint8_t Leave = 254;
static constexpr uint8_t Disconnect = 252;
static constexpr uint8_t Error = 251;
} // namespace EventCodes

// https://doc-api.photonengine.com/en/pun/v1/class_parameter_code.html (2026-01-29)
// https://web.archive.org/web/20260129180742/https://doc-api.photonengine.com/en/pun/v1/class_parameter_code.html
namespace DictKeyCodes {
namespace ProtocolDetails {
constexpr uint8_t ExpectedProtocol = 195;
constexpr uint8_t CustomInitData = 194;
constexpr uint8_t EncryptionMode = 193;
constexpr uint8_t EncryptionData = 192;
} // namespace ProtocolDetails

namespace GameAndActor {
constexpr uint8_t GameId = 255;
constexpr uint8_t ActorNo = 254;
constexpr uint8_t TargetActorNo = 253;
constexpr uint8_t ActorList = 252;
} // namespace GameAndActor

namespace Properties {
constexpr uint8_t Properties = 251;
constexpr uint8_t ActorProperties = 249;
constexpr uint8_t GameProperties = 248;
constexpr uint8_t ExpectedValues = 231;
} // namespace Properties

namespace RoutingAndEvents {
constexpr uint8_t Broadcast = 250;
constexpr uint8_t Cache = 247;
constexpr uint8_t ReceiverGroup = 246;
constexpr uint8_t Data = 245;
constexpr uint8_t Code = 244;
constexpr uint8_t Flush = 243;
constexpr uint8_t CleanupCacheOnLeave = 241;
constexpr uint8_t InterestGroup = 240;
constexpr uint8_t Remove = 239;
constexpr uint8_t PublishUserId = 239;
constexpr uint8_t Add = 238;
constexpr uint8_t AddUsers = 238;
constexpr uint8_t SuppressRoomEvents = 237;
} // namespace RoutingAndEvents

namespace GameSettings {
constexpr uint8_t EmptyRoomTTL = 236;
constexpr uint8_t PlayerTTL = 235;
constexpr uint8_t IsInactive = 233;
constexpr uint8_t CheckUserOnJoin = 232;
constexpr uint8_t GameFlags = 191;
} // namespace GameSettings

namespace WebAndForwarding {
constexpr uint8_t EventForward = 234;
constexpr uint8_t WebFlags = 234;
} // namespace WebAndForwarding

namespace LoadBalancing {
constexpr uint8_t Address = 230;
constexpr uint8_t PeerCount = 229;
constexpr uint8_t ForceRejoin = 229;
constexpr uint8_t GameCount = 228;
constexpr uint8_t MasterPeerCount = 227;
constexpr uint8_t UserId = 225;
constexpr uint8_t ApplicationId = 224;
constexpr uint8_t Position = 223;
constexpr uint8_t MatchmakingType = 223;
constexpr uint8_t GameList = 222;
constexpr uint8_t Token = 221;
constexpr uint8_t AppVersion = 220;
constexpr uint8_t NodeId = 219;
constexpr uint8_t Info = 218;
} // namespace LoadBalancing

namespace AuthAndLobby {
constexpr uint8_t ClientAuthenticationType = 217;
constexpr uint8_t ClientAuthenticationParameters = 216;
constexpr uint8_t ClientAuthenticationData = 214;
constexpr uint8_t CreateIfNotExists = 215;
constexpr uint8_t JoinMode = 215;
constexpr uint8_t LobbyName = 213;
constexpr uint8_t LobbyType = 212;
constexpr uint8_t LobbyStats = 211;
constexpr uint8_t Region = 210;
constexpr uint8_t Cluster = 196;
constexpr uint8_t UriPath = 209;
constexpr uint8_t FindFriendsResponseRoomIdList = 2;
constexpr uint8_t FindFriendsResponseOnlineList = 1;
} // namespace AuthAndLobby

namespace RpcAndPlugins {
constexpr uint8_t RpcCallParams = 208;
constexpr uint8_t RpcCallRetCode = 207;
constexpr uint8_t RpcCallRetMessage = 206;
constexpr uint8_t CacheSliceIndex = 205;
constexpr uint8_t Plugins = 204;
constexpr uint8_t PluginName = 201;
constexpr uint8_t PluginVersion = 200;
} // namespace RpcAndPlugins

namespace MetadataAndMisc {
constexpr uint8_t MasterClientId = 203;
constexpr uint8_t NickName = 202;
constexpr uint8_t Flags = 199;
constexpr uint8_t CloudType = 198;
constexpr uint8_t GameRemoveReason = 197;
} // namespace MetadataAndMisc
} // namespace DictKeyCodes

// https://doc-api.photonengine.com/en/pun/v1/class_error_code.html (2026-01-29)
// https://web.archive.org/web/20260129180245/https://doc-api.photonengine.com/en/pun/v1/class_error_code.html
namespace ErrorCodes {
// Core: Success states, ranges, and generic system errors
namespace Core {
static constexpr int16_t Ok = 0;
static constexpr int16_t InternalServerError = -1;
static constexpr int16_t OperationInvalid = -2;
static constexpr int16_t OperationNotAllowedInCurrentState = -3;
static constexpr int16_t ArgumentOutOfRange = -4;
static constexpr int16_t SerializationLimitError = -13;
} // namespace Core

// Authentication & Security: Tokens, Crypto, and Permissions
namespace Auth {
static constexpr int16_t InvalidAuthentication = 32767;
static constexpr int16_t AuthenticationTokenExpired = 32753;
static constexpr int16_t AuthRequestWaitTimeout = 32736;
static constexpr int16_t CustomAuthenticationFailed = 32755;
static constexpr int16_t AuthenticationServiceTemporarilyUnavailable = 32754;
static constexpr int16_t SecureConnectionRequired = 32740;

// Client specific
static constexpr int16_t TokenRequired = 2;
static constexpr int16_t ProtocolUnavailable = 3;

// Crypto specific
static constexpr int16_t CryptoProviderNotSet = -16;
static constexpr int16_t DecryptionFailure = -17;
static constexpr int16_t InvalidEncryptionParameters = -18;
} // namespace Auth

// Server & Connection: Connectivity, Availability, and Regions
namespace Server {
static constexpr int16_t NotReady = -7;
static constexpr int16_t Overload = -8;
static constexpr int16_t Maintenance = -10;
static constexpr int16_t Backoff = -9;
static constexpr int16_t RedirectRepeat = 32759;
static constexpr int16_t InvalidRegion = 32756;
static constexpr int16_t ConnectionSwitched = 32735;
static constexpr int16_t ServerFull = 32762;
static constexpr int16_t ApplicationUnavailable = 1;
} // namespace Server

// Matchmaking: Games, Slots, Joining, and Game Logic
namespace Matchmaking {
static constexpr int16_t GameIdAlreadyExists = 32766;
static constexpr int16_t GameIdNotExists = 32758;
static constexpr int16_t NoRandomMatchFound = 32760;
static constexpr int16_t GameClosed = 32764;
static constexpr int16_t GameFull = 32765;
static constexpr int16_t AlreadyMatched = 32763;
static constexpr int16_t ServerForbidden = 32761;
static constexpr int16_t SlotError = 32742;
static constexpr int16_t ActorListFull = 32734;

// Consistency checks
static constexpr int16_t PluginReportedError = 32752;
static constexpr int16_t ServerCheckFailed = 32738;

namespace JoinFail {
static constexpr int16_t JoinFailedFoundActiveJoiner = 32746;
static constexpr int16_t JoinFailedFoundInactiveJoiner = 32749;
static constexpr int16_t JoinFailedWithRejoinerNotFound = 32748;
static constexpr int16_t JoinFailedPeerAlreadyJoined = 32750;
static constexpr int16_t JoinFailedFoundExcludedUserId = 32747;
} // namespace JoinFail
} // namespace Matchmaking

// Data & Operations: Parsing, Buffers, and Op Logic
namespace Data {
static constexpr int16_t InvalidRequestParameters = -6;
static constexpr int16_t SendBufferFull = -11;
static constexpr int16_t UnexpectedData = -12;
static constexpr int16_t WrongInitRequestData = -14;
static constexpr int16_t ResponseParseError = -15;
static constexpr int16_t EventCacheExceeded = 32739;

// Operation specific
static constexpr int16_t OperationLimitReached = 32743;
static constexpr int16_t OperationSizeLimitExceeded = -38;
static constexpr int16_t OperationParametersLimitExceeded = -39;
} // namespace Data

// Throttling: Rate limits and caps
namespace Throttling {
static constexpr int16_t IncomingDataRateExceeded = -30;
static constexpr int16_t IncomingMsgRateExceeded = -31;
static constexpr int16_t IncomingMaxMsgSizeExceeded = -32;

static constexpr int16_t OperationRateExceeded = -35;
static constexpr int16_t OperationDataRateExceeded = -36;
static constexpr int16_t OperationBlocked = -37;

static constexpr int16_t MessagesRateExceeded = -40;
static constexpr int16_t MessagesDataRateExceeded = -41;
static constexpr int16_t MessagesBlocked = -42;
static constexpr int16_t MessageSizeLimitExceeded = -43;

static constexpr int16_t HttpLimitReached = 32745;
static constexpr int16_t MaxCcuReached = 32757;
} // namespace Throttling
} // namespace ErrorCodes

// https://doc-api.photonengine.com/en/pun/current/class_photon_1_1_realtime_1_1_room_options.html (2024-02-04)
// https://web.archive.org/web/20260204185936/https://doc-api.photonengine.com/en/pun/current/class_photon_1_1_realtime_1_1_room_options.html
namespace GameProps {
static constexpr uint8_t MaxPlayers = 255;
static constexpr uint8_t IsVisible = 254;
static constexpr uint8_t IsOpen = 253;
static constexpr uint8_t PlayerCount = 252;
static constexpr uint8_t Removed = 251;
static constexpr uint8_t CleanupCacheOnLeave = 249;
static constexpr uint8_t LobbyProperties = 250;
static constexpr uint8_t MasterClientId = 248;
static constexpr uint8_t ExpectedUsers = 247;
static constexpr uint8_t PlayerTTL = 246;
static constexpr uint8_t EmptyGameTTL = 245;
} // namespace GameProps

// https://doc-api.photonengine.com/en/pun/current/class_photon_1_1_realtime_1_1_actor_properties.html (2024-02-04)
// https://web.archive.org/web/20260204190349/https://doc-api.photonengine.com/en/pun/current/class_photon_1_1_realtime_1_1_actor_properties.html
namespace ActorProps {
static constexpr uint8_t NickName = 255;
static constexpr uint8_t IsInactive = 254;
static constexpr uint8_t UserId = 253;
} // namespace ActorProps

// https://doc-api.photonengine.com/en/pun/current/class_photon_1_1_realtime_1_1_room_options.html (2024-02-04)
// https://web.archive.org/web/20260204185936/https://doc-api.photonengine.com/en/pun/current/class_photon_1_1_realtime_1_1_room_options.html
namespace GameFlags {
static constexpr uint32_t CheckUserOnJoin = 0x01;
static constexpr uint32_t DeleteCacheOnLeave = 0x02;
static constexpr uint32_t SuppressRoomEvents = 0x04;
static constexpr uint32_t PublishUserId = 0x08;
static constexpr uint32_t DeleteNullProps = 0x10;
static constexpr uint32_t BroadcastPropsChangeToAll = 0x20;
static constexpr uint32_t SuppressPlayerInfo = 0x40;
} // namespace GameFlags

namespace ReceiverGroup {
static constexpr uint8_t Others = 0;
static constexpr uint8_t All = 1;
static constexpr uint8_t MasterClient = 2;
} // namespace ReceiverGroup

// https://doc-api.photonengine.com/en/plugins/current/class_photon_1_1_hive_1_1_plugin_1_1_cache_operations.html (2024-02-04)
// https://web.archive.org/web/20260204190053/https://doc-api.photonengine.com/en/plugins/current/class_photon_1_1_hive_1_1_plugin_1_1_cache_operations.html
namespace CacheOperation {
static constexpr uint8_t DoNotCache = 0;
static constexpr uint8_t MergeCache = 1;
static constexpr uint8_t ReplaceCache = 2;
static constexpr uint8_t RemoveCache = 3;
static constexpr uint8_t AddToRoomCache = 4;
static constexpr uint8_t AddToRoomCacheGlobal = 5;
static constexpr uint8_t RemoveFromRoomCache = 6;
static constexpr uint8_t RemoveFromCacheForActorsLeft = 7;
static constexpr uint8_t SliceIncreaseIndex = 10;
static constexpr uint8_t SliceSetIndex = 11;
static constexpr uint8_t SlicePurgeIndex = 12;
static constexpr uint8_t SlicePurgeUpToIndex = 13;
} // namespace CacheOperation

// https://doc-api.photonengine.com/en/pun/current/namespace_photon_1_1_realtime.html#a0b5a0270c468f91b73474bae9bbca85e (2024-02-04)
// https://web.archive.org/web/20260204190216/https://doc-api.photonengine.com/en/pun/current/namespace_photon_1_1_realtime.html#a0b5a0270c468f91b73474bae9bbca85e
namespace MatchmakingType {
static constexpr uint8_t FillRoom = 0;
static constexpr uint8_t SerialMatching = 1;
static constexpr uint8_t RandomMatching = 2;
} // namespace MatchmakingType
} // namespace luxon
