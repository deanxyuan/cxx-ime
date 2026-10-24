// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>

#include <cxxime/background_loader.h>

#include "support/background_test_image.h"

TEST(BackgroundImage, managed_copy_deduplicates_and_survives_source_removal) {
    background_test::Files files;
    const auto path = files.write(L"风景.png", 8, 4, background_test::solid(8, 4, 0, 0, 240, 128));
    auto first = cxxime::import_background_image(path);
    ASSERT_TRUE(first.error.empty());
    ASSERT_TRUE(first.image != nullptr);
    ASSERT_TRUE(first.created);
    auto same = cxxime::import_background_image(path);
    ASSERT_EQ(first.resource, same.resource);
    ASSERT_TRUE(!same.created);
    ASSERT_EQ(first.image->pixels[3], 128);
    ASSERT_LE(first.image->pixels[2], 121);
    ASSERT_TRUE(DeleteFileW(path.c_str()) != FALSE);
    const auto id = first.resource;
    first.image.reset();
    same.image.reset();
    auto loaded = cxxime::load_background_image(id);
    ASSERT_TRUE(loaded != nullptr);
    ASSERT_EQ(loaded->width, 8u);
    ASSERT_EQ(loaded->height, 4u);
    ASSERT_EQ(loaded->pixels[3], 128);
}

TEST(BackgroundImage, drafts_delete_only_unpublished_copies_after_leases_end) {
    background_test::Files files;
    const auto path = files.write(L"draft.png", 8, 4, background_test::solid(8, 4, 10, 60, 190));
    std::string id;
    {
        cxxime::BackgroundDraftResources drafts;
        auto imported = cxxime::import_background_image(path);
        ASSERT_TRUE(imported.image != nullptr);
        id = imported.resource;
        drafts.add(imported);
    }
    ASSERT_EQ(GetFileAttributesW(cxxime::background_resource_path(id).c_str()),
              INVALID_FILE_ATTRIBUTES);
    {
        cxxime::BackgroundDraftResources drafts;
        auto imported = cxxime::import_background_image(path);
        ASSERT_TRUE(imported.image != nullptr);
        drafts.add(imported);
        drafts.protect(imported.resource);
    }
    ASSERT_TRUE(cxxime::load_background_image(id) != nullptr);
}

TEST(BackgroundImage, resize_preserves_transparency_without_dark_edges) {
    background_test::Files files;
    auto pixels = background_test::solid(4096, 8, 0, 0, 255);
    for (std::size_t i = 0; i < pixels.size(); i += 8) {
        pixels[i + 3] = 0;
        pixels[i + 2] = 0;
    }
    const auto imported =
        cxxime::import_background_image(files.write(L"edge.png", 4096, 8, pixels));
    ASSERT_TRUE(imported.image != nullptr);
    ASSERT_EQ(imported.image->width, 2048u);
    ASSERT_EQ(imported.image->height, 4u);
    for (std::size_t i = 0; i < imported.image->pixels.size(); i += 4) {
        ASSERT_LE(std::abs(int(imported.image->pixels[i + 2]) - imported.image->pixels[i + 3]), 1);
        ASSERT_EQ(imported.image->pixels[i], 0);
    }
}

TEST(BackgroundImage, jpeg_orientation_handles_all_eight_directions) {
    background_test::Files files;
    // Six uniform blocks avoid JPEG boundary artifacts; labels are grayscale intensities.
    auto pixels = background_test::solid(96, 64, 0, 0, 0);
    for (unsigned y = 0; y < 64; ++y) {
        for (unsigned x = 0; x < 96; ++x) {
            const BYTE value = static_cast<BYTE>((y / 32 * 3 + x / 32 + 1) * 35);
            const auto offset = (y * 96 + x) * 4;
            pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = value;
        }
    }
    const unsigned expected[][6] = {
        {1, 2, 3, 4, 5, 6}, {3, 2, 1, 6, 5, 4}, {6, 5, 4, 3, 2, 1}, {4, 5, 6, 1, 2, 3},
        {1, 4, 2, 5, 3, 6}, {4, 1, 5, 2, 6, 3}, {6, 3, 5, 2, 4, 1}, {3, 6, 2, 5, 1, 4},
    };
    for (unsigned orientation = 1; orientation <= 8; ++orientation) {
        auto imported = cxxime::import_background_image(files.write(
            (std::to_wstring(orientation) + L".jpg").c_str(), 96, 64, pixels, true, orientation));
        ASSERT_TRUE(imported.image != nullptr);
        const unsigned columns = orientation >= 5 ? 2 : 3;
        ASSERT_EQ(imported.image->width, columns * 32);
        ASSERT_EQ(imported.image->height, 6 / columns * 32);
        for (unsigned i = 0; i < 6; ++i) {
            const auto offset =
                ((i / columns * 32 + 16) * imported.image->width + i % columns * 32 + 16) * 4;
            ASSERT_LE(std::abs(int(imported.image->pixels[offset]) -
                               int(expected[orientation - 1][i] * 35)),
                      3);
        }
    }
}

TEST(BackgroundImage, rejects_corrupt_oversized_and_unsupported_resources) {
    background_test::Files files;
    const auto corrupt = files.directory / L"corrupt.png";
    {
        std::ofstream stream(corrupt, std::ios::binary);
        stream << "not a PNG";
    }
    ASSERT_TRUE(!cxxime::import_background_image(corrupt.wstring()).image);
    std::filesystem::resize_file(corrupt, 21 * 1024 * 1024);
    ASSERT_TRUE(!cxxime::import_background_image(corrupt.wstring()).image);
    const auto huge =
        files.write(L"huge.png", 5000, 4001, background_test::solid(5000, 4001, 0, 0, 0));
    ASSERT_TRUE(!cxxime::import_background_image(huge).image);
    ASSERT_TRUE(cxxime::background_resource_path("../test").empty());
    ASSERT_TRUE(!cxxime::load_background_image(std::string(64, 'b')));
    const auto png = files.write(L"animated.png", 2, 2, background_test::solid(2, 2, 0, 0, 0));
    std::vector<char> bytes;
    {
        std::ifstream stream(png, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(stream), {});
    }
    // A syntactically marked APNG is rejected before invoking any decoder.
    const char animation[] = {0, 0, 0, 8, 'a', 'c', 'T', 'L', 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0};
    bytes.insert(bytes.begin() + 8, std::begin(animation), std::end(animation));
    {
        std::ofstream stream(png, std::ios::binary | std::ios::trunc);
        stream.write(bytes.data(), bytes.size());
    }
    ASSERT_TRUE(!cxxime::import_background_image(png).image);
}

TEST(BackgroundImage, reselect_repairs_corrupt_copy_without_deleting_published_identity) {
    background_test::Files files;
    const auto source =
        files.write(L"repair.png", 12, 8, background_test::solid(12, 8, 20, 40, 60));
    auto imported = cxxime::import_background_image(source);
    ASSERT_TRUE(imported.image != nullptr);
    const auto id = imported.resource;
    imported.image.reset();
    const auto path = std::filesystem::path(cxxime::background_resource_path(id));
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << "corrupt";
    }
    ASSERT_TRUE(!cxxime::load_background_image(id));
    cxxime::BackgroundLoader loader;
    loader.request(id);
    const auto failed_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (loader.loading() && std::chrono::steady_clock::now() < failed_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(!loader.loading());
    ASSERT_TRUE(!loader.ready(id));
    auto repaired = cxxime::import_background_image(source);
    ASSERT_TRUE(repaired.image != nullptr);
    ASSERT_TRUE(!repaired.created);
    ASSERT_EQ(repaired.resource, id);
    loader.request(id, true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!loader.ready(id) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(loader.ready(id) != nullptr);
}

TEST(BackgroundImage, loader_latest_request_and_missing_resource_do_not_retry) {
    background_test::Files files;
    const auto a = cxxime::import_background_image(
        files.write(L"a.png", 64, 32, background_test::solid(64, 32, 50, 100, 150)));
    const auto b = cxxime::import_background_image(
        files.write(L"b.png", 32, 64, background_test::solid(32, 64, 150, 100, 50)));
    ASSERT_TRUE(a.image && b.image);
    cxxime::BackgroundLoader loader;
    for (int i = 0; i < 30; ++i) {
        loader.request(a.resource);
        loader.request(b.resource);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!loader.ready(b.resource) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(loader.ready(b.resource) != nullptr);
    ASSERT_EQ(loader.ready(b.resource)->width, 32u);
    ASSERT_TRUE(!loader.ready(a.resource));
    loader.request("");
    ASSERT_TRUE(!loader.ready(b.resource));
    loader.request(std::string(64, 'c'));
    ASSERT_TRUE(!loader.ready(b.resource));
}

RUN_ALL_TESTS();
