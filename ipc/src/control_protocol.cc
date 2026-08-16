// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/control_protocol.h>

#include <cstring>

namespace cxxime {

bool build_control_packet(ControlMessageType type, ConfigGeneration generation, const void* payload,
                          std::size_t payload_size, std::vector<std::uint8_t>* packet) {
    if (!packet || payload_size > CONTROL_MAX_PAYLOAD || (payload_size != 0 && !payload)) {
        return false;
    }

    ControlHeader header;
    header.message_type = type;
    header.payload_size = static_cast<std::uint32_t>(payload_size);
    header.server_epoch = generation.server_epoch;
    header.revision = generation.revision;

    packet->resize(sizeof(header) + payload_size);
    std::memcpy(packet->data(), &header, sizeof(header));
    if (payload_size != 0) {
        std::memcpy(packet->data() + sizeof(header), payload, payload_size);
    }
    return true;
}

bool parse_control_packet(const void* data, std::size_t size, ControlMessage* message) {
    if (!data || !message || size < sizeof(ControlHeader)) {
        return false;
    }

    ControlHeader header;
    std::memcpy(&header, data, sizeof(header));
    if (header.magic != CONTROL_MAGIC || header.protocol_version != CONTROL_PROTOCOL_VERSION ||
        header.payload_size > CONTROL_MAX_PAYLOAD || size != sizeof(header) + header.payload_size) {
        return false;
    }

    switch (header.message_type) {
        case ControlMessageType::kSubscribe:
        case ControlMessageType::kConfigSnapshot:
        case ControlMessageType::kReplaceUserConfig:
        case ControlMessageType::kPatchUserConfig:
        case ControlMessageType::kMutationResult:
        case ControlMessageType::kPing:
        case ControlMessageType::kPong:
        case ControlMessageType::kLexiconRequest:
        case ControlMessageType::kLexiconResult:
            break;
        default:
            return false;
    }

    message->type = header.message_type;
    message->generation = {header.server_epoch, header.revision};
    const char* payload = static_cast<const char*>(data) + sizeof(header);
    message->payload.assign(payload, payload + header.payload_size);
    return true;
}

} // namespace cxxime
