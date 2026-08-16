// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CONTROL_PROTOCOL_H_
#define CXXIME_CONTROL_PROTOCOL_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cxxime {

constexpr std::uint32_t CONTROL_MAGIC = 0x43434647; // CCFG
constexpr std::uint16_t CONTROL_PROTOCOL_VERSION = 1;
constexpr std::size_t CONTROL_MAX_PAYLOAD = 64 * 1024;

enum class ControlMessageType : std::uint16_t {
    kSubscribe = 1,
    kConfigSnapshot = 2,
    kReplaceUserConfig = 3,
    kPatchUserConfig = 4,
    kMutationResult = 5,
    kPing = 6,
    kPong = 7,
    kLexiconRequest = 8,
    kLexiconResult = 9,
};

enum class UserConfigMutationKind : std::uint8_t {
    kReplace = 1,
    kMergePatch = 2,
};

struct ConfigGeneration {
    std::uint64_t server_epoch = 0;
    std::uint64_t revision = 0;
};

inline bool operator==(const ConfigGeneration& left, const ConfigGeneration& right) {
    return left.server_epoch == right.server_epoch && left.revision == right.revision;
}

inline bool operator!=(const ConfigGeneration& left, const ConfigGeneration& right) {
    return !(left == right);
}

#pragma pack(push, 1)
struct ControlHeader {
    std::uint32_t magic = CONTROL_MAGIC;
    std::uint16_t protocol_version = CONTROL_PROTOCOL_VERSION;
    ControlMessageType message_type = ControlMessageType::kPing;
    std::uint32_t payload_size = 0;
    std::uint64_t server_epoch = 0;
    std::uint64_t revision = 0;
};

struct ControlSubscribe {
    std::uint32_t process_id = 0;
    std::uint16_t pointer_size = 0;
    std::uint16_t reserved = 0;
};

struct ControlMutationResult {
    std::uint32_t succeeded = 0;
    std::uint32_t error_code = 0;
};
#pragma pack(pop)

static_assert(sizeof(ControlHeader) == 28, "Unexpected control header size");
static_assert(sizeof(ControlSubscribe) == 8, "Unexpected subscribe payload size");
static_assert(sizeof(ControlMutationResult) == 8, "Unexpected mutation result size");

struct ControlMessage {
    ControlMessageType type = ControlMessageType::kPing;
    ConfigGeneration generation;
    std::string payload;
};

bool build_control_packet(ControlMessageType type, ConfigGeneration generation, const void* payload,
                          std::size_t payload_size, std::vector<std::uint8_t>* packet);

bool parse_control_packet(const void* data, std::size_t size, ControlMessage* message);

} // namespace cxxime

#endif // CXXIME_CONTROL_PROTOCOL_H_
