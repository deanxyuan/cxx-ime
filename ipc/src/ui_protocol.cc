// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/ui_protocol.h>

#include <cstring>

namespace cxxime {
namespace {

template <typename Payload>
bool build_packet(UiPacketType type, const Payload& payload, std::uint64_t sequence,
                  std::vector<std::uint8_t>* packet) {
    if (!packet || sizeof(UiPacketHeader) + sizeof(Payload) > kUiMaxPacketSize) {
        return false;
    }

    UiPacketHeader header;
    header.packet_type = type;
    header.payload_size = static_cast<std::uint32_t>(sizeof(Payload));
    header.sequence = sequence;
    packet->resize(sizeof(header) + sizeof(payload));
    std::memcpy(packet->data(), &header, sizeof(header));
    std::memcpy(packet->data() + sizeof(header), &payload, sizeof(payload));
    return true;
}

bool valid_ownership(UiOwnership ownership) {
    return ownership == UiOwnership::kNone || ownership == UiOwnership::kExternal ||
           ownership == UiOwnership::kHost;
}

bool valid_command_type(UiCommandType type) {
    return type >= UiCommandType::kSelectCandidate && type <= UiCommandType::kMenuCommand;
}

template <typename Payload>
bool parse_packet(const void* data, std::size_t size, UiPacketType type, Payload* payload) {
    if (!data || !payload || size != sizeof(UiPacketHeader) + sizeof(Payload)) {
        return false;
    }

    UiPacketHeader header = {};
    std::memcpy(&header, data, sizeof(header));
    if (header.magic != UI_PROTOCOL_MAGIC || header.protocol_version != UI_PROTOCOL_VERSION ||
        header.packet_type != type || header.payload_size != sizeof(Payload)) {
        return false;
    }
    std::memcpy(payload, static_cast<const std::uint8_t*>(data) + sizeof(header), sizeof(*payload));
    return true;
}

} // namespace

bool build_ui_snapshot_packet(const UiPresentationSnapshot& snapshot, std::uint64_t sequence,
                              std::vector<std::uint8_t>* packet) {
    return is_valid_ui_snapshot(snapshot) &&
           build_packet(UiPacketType::kSnapshot, snapshot, sequence, packet);
}

bool parse_ui_snapshot_packet(const void* data, std::size_t size,
                              UiPresentationSnapshot* snapshot) {
    if (!snapshot) {
        return false;
    }
    UiPresentationSnapshot parsed;
    if (!parse_packet(data, size, UiPacketType::kSnapshot, &parsed) ||
        !is_valid_ui_snapshot(parsed)) {
        return false;
    }
    *snapshot = parsed;
    return true;
}

bool build_ui_command_packet(const UiCommand& command, std::uint64_t sequence,
                             std::vector<std::uint8_t>* packet) {
    if (!is_valid_ui_command(command)) {
        return false;
    }
    UiCommand normalized = {};
    normalized.session_id = command.session_id;
    normalized.session_generation = command.session_generation;
    normalized.target_generation = command.target_generation;
    normalized.composition_generation = command.composition_generation;
    normalized.type = command.type;
    normalized.candidate_index = command.candidate_index;
    normalized.value = command.value;
    return build_packet(UiPacketType::kCommand, normalized, sequence, packet);
}

bool parse_ui_command_packet(const void* data, std::size_t size, UiCommand* command) {
    if (!command) {
        return false;
    }
    UiCommand parsed;
    if (!parse_packet(data, size, UiPacketType::kCommand, &parsed) ||
        !is_valid_ui_command(parsed)) {
        return false;
    }
    *command = parsed;
    return true;
}

bool is_valid_ui_snapshot(const UiPresentationSnapshot& snapshot) {
    constexpr std::uint32_t kKnownFlags = ui_snapshot_flag(UiSnapshotFlag::kComposing) |
                                          ui_snapshot_flag(UiSnapshotFlag::kCandidateVisible) |
                                          ui_snapshot_flag(UiSnapshotFlag::kStatusVisible) |
                                          ui_snapshot_flag(UiSnapshotFlag::kHasCaret) |
                                          ui_snapshot_flag(UiSnapshotFlag::kHasPreedit) |
                                          ui_snapshot_flag(UiSnapshotFlag::kHasCandidates) |
                                          ui_snapshot_flag(UiSnapshotFlag::kSessionEnded);
    if (snapshot.session_id == 0 || snapshot.session_generation == 0 ||
        (snapshot.flags & ~kKnownFlags) != 0 || !valid_ownership(snapshot.ownership) ||
        snapshot.preedit_length > static_cast<std::uint32_t>(kUiPreeditCapacity) ||
        snapshot.preedit_cursor > snapshot.preedit_length ||
        snapshot.candidate_page.count > static_cast<std::uint32_t>(kCandidateCapacity)) {
        return false;
    }
    if (snapshot.candidate_page.count != 0 &&
        snapshot.candidate_page.highlighted >= snapshot.candidate_page.count) {
        return false;
    }
    for (std::uint32_t index = 0; index < snapshot.candidate_page.count; ++index) {
        const UiCandidate& candidate = snapshot.candidate_page.candidates[index];
        if (candidate.text_length == 0 ||
            candidate.text_length > static_cast<std::uint32_t>(kCandidateTextCapacity) ||
            candidate.hint_length > static_cast<std::uint32_t>(kUiCandidateHintCapacity)) {
            return false;
        }
    }
    return true;
}

bool is_valid_ui_command(const UiCommand& command) {
    if (command.session_id == 0 || command.session_generation == 0 ||
        !valid_command_type(command.type)) {
        return false;
    }
    if (command.type == UiCommandType::kSelectCandidate &&
        command.candidate_index >= static_cast<std::uint32_t>(kCandidateCapacity)) {
        return false;
    }
    if (command.type == UiCommandType::kSwitchInputMode &&
        command.value > static_cast<std::uint32_t>(InputMode::MIXED)) {
        return false;
    }
    if (command.type == UiCommandType::kVisibleCandidateCount &&
        (command.value == 0 || command.value > static_cast<std::uint32_t>(kCandidateCapacity))) {
        return false;
    }
    return true;
}

} // namespace cxxime
