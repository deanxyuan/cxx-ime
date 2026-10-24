// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_BACKGROUND_LOADER_H_
#define CXXIME_BACKGROUND_LOADER_H_

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <cxxime/background_image.h>

namespace cxxime {

// One latest request, one decoded result. No HWND callbacks or input-thread I/O.
class BackgroundLoader {
public:
    ~BackgroundLoader();

    void request(const std::string& resource, bool retry_failed = false);
    std::shared_ptr<const BackgroundImage> ready(const std::string& resource);
    bool loading();

private:
    void run();

    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    bool stopping_ = false;
    bool pending_ = false;
    bool loading_ = false;
    std::string requested_;
    std::shared_ptr<const BackgroundImage> image_;
};

} // namespace cxxime

#endif // CXXIME_BACKGROUND_LOADER_H_
