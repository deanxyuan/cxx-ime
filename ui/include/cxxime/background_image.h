// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_BACKGROUND_IMAGE_H_
#define CXXIME_BACKGROUND_IMAGE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

#include <cxxime/background_config.h>

namespace cxxime {

struct BackgroundImage {
    BackgroundImage() = default;
    BackgroundImage(const BackgroundImage&) = delete;
    BackgroundImage& operator=(const BackgroundImage&) = delete;

    unsigned width = 0;
    unsigned height = 0;
    unsigned source_width = 0;
    unsigned source_height = 0;
    std::vector<std::uint8_t> pixels; // sRGB, premultiplied BGRA, top-down.
    HANDLE lease = INVALID_HANDLE_VALUE;

    ~BackgroundImage();
};

struct BackgroundImport {
    std::string resource;
    std::shared_ptr<const BackgroundImage> image;
    std::wstring error;
    bool created = false;
    bool source = false;
};

struct BackgroundPreparation {
    BackgroundConfig config;
    std::vector<BackgroundImport> resources;
    std::shared_ptr<const BackgroundImage> horizontal;
    std::shared_ptr<const BackgroundImage> vertical;
    std::wstring error;
};

std::wstring background_resource_path(const std::string& resource, bool source = false);
BackgroundImport import_background_source(const std::wstring& source);
BackgroundImport load_background_source(const std::string& resource);
BackgroundPreparation prepare_background(const BackgroundConfig& config);
std::shared_ptr<const BackgroundImage> load_background_image(const std::string& resource);
void discard_background_import(const BackgroundImport& imported);

// Old files are retained through propagation and while any reader holds a lease.
void prune_background_images(const BackgroundConfig& config);

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
            entry.source = imported.source;
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

    void protect(const BackgroundConfig& config) {
        protect(config.source);
        protect(config.horizontal.resource);
        protect(config.vertical.resource);
    }

    void discard_unused(const BackgroundConfig& config) {
        for (auto it = resources_.begin(); it != resources_.end();) {
            if (it->created && it->resource != config.source &&
                it->resource != config.horizontal.resource &&
                it->resource != config.vertical.resource) {
                const auto path = background_resource_path(it->resource, it->source);
                if (DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) {
                    it = resources_.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

private:
    std::vector<BackgroundImport> resources_;
};

} // namespace cxxime

#endif // CXXIME_BACKGROUND_IMAGE_H_
