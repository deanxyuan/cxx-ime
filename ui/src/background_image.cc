// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/background_image.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <unordered_map>

#include <bcrypt.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cxxime/background_config.h>
#include <cxxime/background_layout.h>
#include <cxxime/data_path.h>

namespace cxxime {
namespace {
using Microsoft::WRL::ComPtr;
constexpr unsigned kMaxBytes = 64 * 1024 * 1024;
constexpr unsigned kMaxSide = 2048;
constexpr unsigned kPreviewSide = 1024;

struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Apartment() {
        if (SUCCEEDED(result)) {
            CoUninitialize();
        }
    }
};

bool animated_png(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 8 || std::memcmp(bytes.data(), "\x89PNG\r\n\x1a\n", 8) != 0) {
        return false;
    }
    for (std::size_t offset = 8; offset + 12 <= bytes.size();) {
        const auto* p = bytes.data() + offset;
        const std::uint32_t length = (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
                                     (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
        if (std::memcmp(p + 4, "acTL", 4) == 0) {
            return true;
        }
        if (length > bytes.size() - offset - 12) {
            break;
        }
        offset += length + 12;
    }
    return false;
}

std::shared_ptr<BackgroundImage> decode(const std::wstring& path, bool managed, std::wstring& error,
                                        const BackgroundView* view = nullptr,
                                        BackgroundRect* retained = nullptr) {
    error = L"图片不可用，请选择有效的静态 PNG 或 JPEG。";
    auto result = std::make_shared<BackgroundImage>();
    result->lease = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (result->lease == INVALID_HANDLE_VALUE) {
        return {};
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(result->lease, &size) || size.QuadPart <= 0 || size.QuadPart > kMaxBytes) {
        error = L"图片文件不能超过 64 MiB。";
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    if (!ReadFile(result->lease, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) ||
        read != bytes.size() || animated_png(bytes)) {
        return {};
    }
    Apartment apartment;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(bytes.data(), static_cast<DWORD>(bytes.size()))) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                                &decoder))) {
        return {};
    }
    GUID format{};
    UINT frames = 0;
    if (FAILED(decoder->GetContainerFormat(&format)) ||
        (format != GUID_ContainerFormatPng && format != GUID_ContainerFormatJpeg) ||
        FAILED(decoder->GetFrameCount(&frames)) || frames != 1 ||
        FAILED(decoder->GetFrame(0, &frame))) {
        return {};
    }
    UINT width = 0;
    UINT height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || !width || !height ||
        std::uint64_t(width) * height > kMaxBackgroundSourcePixels ||
        (managed && (width > kMaxSide || height > kMaxSide))) {
        error = L"图片不能超过 3200 万像素。";
        return {};
    }
    ComPtr<IWICBitmapSource> source = frame;
    // Normalize embedded profiles to sRGB before generating the reusable resource.
    ComPtr<IWICColorContext> profile;
    ComPtr<IWICColorContext> srgb;
    ComPtr<IWICColorTransform> transform;
    UINT profiles = 0;
    if (SUCCEEDED(frame->GetColorContexts(0, nullptr, &profiles)) && profiles > 0) {
        if (FAILED(factory->CreateColorContext(&profile)) ||
            FAILED(factory->CreateColorContext(&srgb))) {
            return {};
        }
        IWICColorContext* contexts[] = {profile.Get()};
        if (FAILED(frame->GetColorContexts(1, contexts, &profiles)) ||
            FAILED(srgb->InitializeFromExifColorSpace(1)) ||
            FAILED(factory->CreateColorTransformer(&transform)) ||
            FAILED(transform->Initialize(source.Get(), profile.Get(), srgb.Get(),
                                         GUID_WICPixelFormat32bppBGRA))) {
            return {};
        }
        source = transform;
    }
    ComPtr<IWICMetadataQueryReader> metadata;
    unsigned orientation = 1;
    if (SUCCEEDED(frame->GetMetadataQueryReader(&metadata))) {
        PROPVARIANT value{};
        if (SUCCEEDED(metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}", &value)) &&
            value.vt == VT_UI2) {
            orientation = value.uiVal;
        }
        PropVariantClear(&value);
    }
    const WICBitmapTransformOptions rotations[] = {
        WICBitmapTransformRotate0,
        WICBitmapTransformFlipHorizontal,
        WICBitmapTransformRotate180,
        WICBitmapTransformFlipVertical,
        static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 |
                                               WICBitmapTransformFlipHorizontal),
        WICBitmapTransformRotate90,
        static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 |
                                               WICBitmapTransformFlipHorizontal),
        WICBitmapTransformRotate270};
    ComPtr<IWICBitmapFlipRotator> rotate;
    if (orientation >= 2 && orientation <= 8) {
        if (FAILED(factory->CreateBitmapFlipRotator(&rotate)) ||
            FAILED(rotate->Initialize(source.Get(), rotations[orientation - 1]))) {
            return {};
        }
        source = rotate;
        if (FAILED(source->GetSize(&width, &height))) {
            return {};
        }
    }
    result->source_width = width;
    result->source_height = height;
    ComPtr<IWICBitmapClipper> clipper;
    if (view) {
        const auto region = background_retained_region(width, height, *view);
        WICRect crop{static_cast<INT>(region.x), static_cast<INT>(region.y),
                     static_cast<INT>(region.width), static_cast<INT>(region.height)};
        if (FAILED(factory->CreateBitmapClipper(&clipper)) ||
            FAILED(clipper->Initialize(source.Get(), &crop))) {
            return {};
        }
        source = clipper;
        width = crop.Width;
        height = crop.Height;
        *retained = region;
    }
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> premultiplied;
    if (FAILED(factory->CreateFormatConverter(&premultiplied)) ||
        FAILED(premultiplied->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA,
                                         WICBitmapDitherTypeNone, nullptr, 0,
                                         WICBitmapPaletteTypeCustom))) {
        return {};
    }
    source = premultiplied;
    const unsigned max_side = managed || view ? kMaxSide : kPreviewSide;
    if (width > max_side || height > max_side) {
        const double scale = static_cast<double>(max_side) / (std::max)(width, height);
        width = (std::max)(1u, static_cast<unsigned>(width * scale));
        height = (std::max)(1u, static_cast<unsigned>(height * scale));
        if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
            FAILED(
                scaler->Initialize(source.Get(), width, height, WICBitmapInterpolationModeFant))) {
            return {};
        }
        source = scaler;
    }
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom))) {
        return {};
    }
    result->width = width;
    result->height = height;
    result->pixels.resize(std::size_t(width) * height * 4);
    if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(result->pixels.size()),
                                     result->pixels.data()))) {
        return {};
    }
    error.clear();
    return result;
}

std::string digest(const std::uint8_t* data, std::size_t size, unsigned width = 0,
                   unsigned height = 0) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::uint8_t bytes[32]{};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) {
        ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    }
    unsigned dimensions[] = {width, height};
    if (ok) {
        ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(dimensions), sizeof(dimensions), 0) >=
                 0 &&
             BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0) >= 0 &&
             BCryptFinishHash(hash, bytes, sizeof(bytes), 0) >= 0;
    }
    if (hash) {
        BCryptDestroyHash(hash);
    }
    if (algorithm) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    std::string result;
    if (ok) {
        for (auto byte : bytes) {
            result += "0123456789abcdef"[byte >> 4];
            result += "0123456789abcdef"[byte & 15];
        }
    }
    return result;
}

bool encode(const std::wstring& path, const BackgroundImage& image) {
    Apartment apartment;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICBitmapSource> converted;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) {
        return false;
    }
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(image.width, image.height)) ||
        FAILED(frame->SetPixelFormat(&format))) {
        return false;
    }
    if (FAILED(factory->CreateBitmapFromMemory(image.width, image.height,
                                               GUID_WICPixelFormat32bppPBGRA, image.width * 4,
                                               static_cast<UINT>(image.pixels.size()),
                                               const_cast<BYTE*>(image.pixels.data()), &bitmap)) ||
        FAILED(WICConvertBitmapSource(format, bitmap.Get(), &converted)) ||
        FAILED(frame->WriteSource(converted.Get(), nullptr)) || FAILED(frame->Commit()) ||
        FAILED(encoder->Commit())) {
        return false;
    }
    return true;
}
} // namespace

BackgroundImage::~BackgroundImage() {
    if (lease != INVALID_HANDLE_VALUE) {
        CloseHandle(lease);
    }
}

std::wstring background_resource_path(const std::string& resource, bool source) {
    if (!valid_background_resource(resource)) {
        return {};
    }
    return (std::filesystem::u8path(user_data_path("backgrounds")) /
            std::filesystem::u8path(resource + (source ? ".source" : ".png")))
        .wstring();
}

static std::wstring temporary_path(const std::wstring& path) {
    static std::atomic<unsigned> serial{0};
    return path + L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++serial) +
           L".tmp";
}

static BackgroundImport store_image(const std::shared_ptr<BackgroundImage>& image) {
    BackgroundImport result;
    result.resource =
        digest(image->pixels.data(), image->pixels.size(), image->width, image->height);
    const auto path = background_resource_path(result.resource);
    if (path.empty()) {
        result.error = L"无法计算图片标识。";
        return result;
    }
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    if (error) {
        result.error = L"无法创建图片保存目录。";
        return result;
    }
    result.image = load_background_image(result.resource);
    if (!result.image) {
        // A corrupt copy can be restored by reselecting its original. Reader leases
        // prevent replacing any resource still used by a candidate window.
        const bool existed = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
        const auto temporary = temporary_path(path);
        const bool written = encode(temporary, *image);
        const bool published =
            written && MoveFileExW(temporary.c_str(), path.c_str(),
                                   MOVEFILE_WRITE_THROUGH | MOVEFILE_REPLACE_EXISTING);
        result.created = published && !existed;
        DeleteFileW(temporary.c_str());
        if (!published) {
            result.error = L"无法保存图片副本。";
            return result;
        }
        result.image = load_background_image(result.resource);
    }
    if (!result.image) {
        result.error = L"图片副本不可用，请重新选择图片。";
    }
    return result;
}

static bool source_matches(const std::wstring& path, const std::vector<std::uint8_t>& bytes) {
    const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    bool matches = GetFileSizeEx(file, &size) && size.QuadPart == bytes.size();
    std::uint8_t chunk[65536];
    for (std::size_t offset = 0; matches && offset < bytes.size();) {
        const auto count = static_cast<DWORD>((std::min)(sizeof(chunk), bytes.size() - offset));
        DWORD read = 0;
        matches = ReadFile(file, chunk, count, &read, nullptr) && read == count &&
                  std::memcmp(chunk, bytes.data() + offset, count) == 0;
        offset += count;
    }
    CloseHandle(file);
    return matches;
}

BackgroundImport import_background_source(const std::wstring& source) {
    BackgroundImport result;
    result.source = true;
    auto image = decode(source, false, result.error);
    if (!image) {
        return result;
    }
    LARGE_INTEGER size{};
    LARGE_INTEGER start{};
    DWORD read = 0;
    if (!GetFileSizeEx(image->lease, &size) || size.QuadPart <= 0 || size.QuadPart > kMaxBytes ||
        !SetFilePointerEx(image->lease, start, nullptr, FILE_BEGIN)) {
        result.error = L"无法读取原始图片。";
        return result;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.QuadPart));
    if (!ReadFile(image->lease, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) ||
        read != bytes.size()) {
        result.error = L"无法读取原始图片。";
        return result;
    }
    result.resource = digest(bytes.data(), bytes.size());
    const auto path = background_resource_path(result.resource, true);
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    if (path.empty() || error) {
        result.error = L"无法创建图片保存目录。";
        return result;
    }
    const bool existed = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    // The source is immutable and leased throughout validation and copying.
    if (!existed || !source_matches(path, bytes)) {
        const auto temporary = temporary_path(path);
        const bool published = CopyFileW(source.c_str(), temporary.c_str(), TRUE) &&
                               MoveFileExW(temporary.c_str(), path.c_str(),
                                           MOVEFILE_WRITE_THROUGH | MOVEFILE_REPLACE_EXISTING);
        DeleteFileW(temporary.c_str());
        if (!published && !source_matches(path, bytes)) {
            result.error = L"无法保存原始图片。";
            return result;
        }
        result.created = published && !existed;
    }
    const auto lease = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lease == INVALID_HANDLE_VALUE) {
        result.error = L"原始图片不可用。";
        return result;
    }
    CloseHandle(image->lease);
    image->lease = lease;
    result.image = std::move(image);
    return result;
}

BackgroundImport load_background_source(const std::string& resource) {
    BackgroundImport result;
    result.source = true;
    result.resource = resource;
    result.image = decode(background_resource_path(resource, true), false, result.error);
    return result;
}

BackgroundPreparation prepare_background(const BackgroundConfig& config) {
    BackgroundPreparation result;
    result.config = config;
    normalize_background(result.config);
    if (result.config.source.empty()) {
        result.error = L"请先选择图片。";
        return result;
    }
    result.horizontal = load_background_image(result.config.horizontal.resource);
    result.vertical = load_background_image(result.config.vertical.resource);
    for (bool vertical : {false, true}) {
        auto& view = vertical ? result.config.vertical : result.config.horizontal;
        auto& pixels = vertical ? result.vertical : result.horizontal;
        if (pixels) {
            continue;
        }
        BackgroundRect region;
        const auto image = decode(background_resource_path(config.source, true), false,
                                  result.error, &view, &region);
        if (!image) {
            return result;
        }
        if (image->source_width != config.source_width ||
            image->source_height != config.source_height) {
            result.error = L"素材尺寸不匹配，请重新选择图片。";
            return result;
        }
        auto imported = store_image(image);
        result.resources.push_back(imported);
        if (!imported.image) {
            result.error = imported.error;
            return result;
        }
        view.resource = imported.resource;
        view.generation = kBackgroundGeneration;
        view.left = static_cast<int>(region.x);
        view.top = static_cast<int>(region.y);
        view.width = static_cast<int>(region.width);
        view.height = static_cast<int>(region.height);
        pixels = imported.image;
        // Equal framing can reuse the same result without a second source decode.
        auto& other = result.config.vertical;
        if (!vertical && other.resource.empty() && same_background_framing(other, view)) {
            other = view;
            result.vertical = pixels;
        }
    }
    return result;
}

std::shared_ptr<const BackgroundImage> load_background_image(const std::string& resource) {
    const auto path = background_resource_path(resource);
    if (path.empty()) {
        return {};
    }
    static std::mutex mutex;
    static std::unordered_map<std::wstring, std::weak_ptr<const BackgroundImage>> cache;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto it = cache.begin(); it != cache.end();) {
            it = it->second.expired() ? cache.erase(it) : std::next(it);
        }
        const auto found = cache.find(path);
        if (found != cache.end()) {
            if (auto image = found->second.lock()) {
                return image;
            }
        }
    }
    std::wstring error;
    auto image = decode(path, true, error);
    if (image) {
        std::lock_guard<std::mutex> lock(mutex);
        cache[path] = image;
    }
    return image;
}

void discard_background_import(const BackgroundImport& imported) {
    if (imported.created) {
        DeleteFileW(background_resource_path(imported.resource, imported.source).c_str());
    }
}

void prune_background_images(const BackgroundConfig& config) {
    const auto directory = std::filesystem::u8path(user_data_path("backgrounds"));
    std::error_code error;
    const auto now = std::filesystem::file_time_type::clock::now();
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error)) {
        const auto& path = it->path();
        const auto id = path.stem().u8string();
        if ((path.extension() != L".png" && path.extension() != L".source") ||
            !valid_background_resource(id) || id == config.source ||
            id == config.horizontal.resource || id == config.vertical.resource) {
            continue;
        }
        const auto modified = it->last_write_time(error);
        if (!error && now - modified > std::chrono::hours(24 * 30)) {
            // Open reader leases deliberately deny FILE_SHARE_DELETE.
            DeleteFileW(path.c_str());
        }
    }
}
} // namespace cxxime
