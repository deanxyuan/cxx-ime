// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_BACKGROUND_IMAGE_H_
#define CXXIME_BACKGROUND_IMAGE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

namespace cxxime {

struct BackgroundImage {
    BackgroundImage() = default;
    BackgroundImage(const BackgroundImage&) = delete;
    BackgroundImage& operator=(const BackgroundImage&) = delete;

    unsigned width = 0;
    unsigned height = 0;
    std::vector<std::uint8_t> pixels; // sRGB, premultiplied BGRA, top-down.
    HANDLE lease = INVALID_HANDLE_VALUE;

    ~BackgroundImage();
};

struct BackgroundImport {
    std::string resource;
    std::shared_ptr<const BackgroundImage> image;
    std::wstring error;
    bool created = false;
};

std::wstring background_resource_path(const std::string& resource);
BackgroundImport import_background_image(const std::wstring& source);
std::shared_ptr<const BackgroundImage> load_background_image(const std::string& resource);
void discard_background_import(const BackgroundImport& imported);

// Old files are retained through propagation and while any reader holds a lease.
void prune_background_images(const std::string& current_resource);

class BackgroundDraftResources {
public:
    ~BackgroundDraftResources() {
        for (const auto& resource : resources_) {
            discard_background_import(resource);
        }
    }

    void add(const BackgroundImport& imported) {
        if (imported.created) {
            BackgroundImport entry;
            entry.resource = imported.resource;
            entry.created = true;
            resources_.push_back(std::move(entry));
        }
    }

    // Returns whether ownership of a newly created draft was transferred.
    bool protect(const std::string& resource) {
        bool created = false;
        for (auto& entry : resources_) {
            if (entry.resource == resource) {
                created = created || entry.created;
                entry.created = false;
            }
        }
        return created;
    }

private:
    std::vector<BackgroundImport> resources_;
};

} // namespace cxxime

#endif // CXXIME_BACKGROUND_IMAGE_H_
