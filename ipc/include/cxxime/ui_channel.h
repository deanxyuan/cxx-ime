// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_UI_CHANNEL_H_
#define CXXIME_UI_CHANNEL_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <cxxime/pipe_names.h>
#include <cxxime/ui_protocol.h>

namespace cxxime {

using UiEndpointId = std::uint64_t;

class UiChannelClient {
public:
    // Runs on the UI-channel worker. A TSF consumer must marshal commands to its TSF thread.
    using CommandHandler = std::function<void(const UiCommand&)>;

    UiChannelClient();
    ~UiChannelClient();

    UiChannelClient(const UiChannelClient&) = delete;
    UiChannelClient& operator=(const UiChannelClient&) = delete;

    bool start(CommandHandler command_handler, const std::wstring& pipe_name = UI_PIPE_BASE_NAME);
    void stop();

    // Replaces the pending snapshot for its session and never performs pipe I/O on the caller.
    // At most 32 sessions may be pending in one client instance.
    bool publish_latest(const UiPresentationSnapshot& snapshot);
    bool is_running() const;
    bool is_connected() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class UiChannelServer {
public:
    // Runs on the UI-channel IOCP thread. The handler must marshal window work to the UI thread.
    using SnapshotHandler =
        std::function<void(UiEndpointId endpoint, const UiPresentationSnapshot& snapshot)>;
    using DisconnectHandler = std::function<void(UiEndpointId endpoint)>;

    UiChannelServer();
    ~UiChannelServer();

    UiChannelServer(const UiChannelServer&) = delete;
    UiChannelServer& operator=(const UiChannelServer&) = delete;

    bool start(SnapshotHandler snapshot_handler, DisconnectHandler disconnect_handler = {},
               const std::wstring& pipe_name = UI_PIPE_BASE_NAME);
    void stop();

    // Queues a command for the endpoint without waiting for the pipe.
    bool send_command(UiEndpointId endpoint, const UiCommand& command);
    std::size_t endpoint_count() const;
    bool is_running() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cxxime

#endif // CXXIME_UI_CHANNEL_H_
