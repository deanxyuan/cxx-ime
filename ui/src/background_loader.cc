// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/background_loader.h>

namespace cxxime {

BackgroundLoader::~BackgroundLoader() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        changed_.notify_one();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void BackgroundLoader::request(const std::string& resource, bool retry_failed) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (requested_ == resource &&
        (!retry_failed || image_ || pending_ || loading_ || resource.empty())) {
        return;
    }
    requested_ = resource;
    image_.reset();
    pending_ = !resource.empty();
    if (pending_ && !worker_.joinable()) {
        try {
            worker_ = std::thread(&BackgroundLoader::run, this);
        } catch (...) {
            pending_ = false;
        }
    }
    changed_.notify_one();
}

std::shared_ptr<const BackgroundImage> BackgroundLoader::ready(const std::string& resource) {
    std::lock_guard<std::mutex> lock(mutex_);
    return requested_ == resource ? image_ : nullptr;
}

bool BackgroundLoader::loading() {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_ || loading_;
}

void BackgroundLoader::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
        changed_.wait(lock, [&]() { return stopping_ || pending_; });
        if (stopping_) {
            break;
        }
        const auto resource = requested_;
        pending_ = false;
        loading_ = true;
        lock.unlock();
        std::shared_ptr<const BackgroundImage> image;
        try {
            image = load_background_image(resource);
        } catch (...) {
            // Resource failure never escapes into the host process.
        }
        lock.lock();
        loading_ = false;
        if (requested_ == resource) {
            image_ = std::move(image);
        }
    }
}
} // namespace cxxime
