// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/background_image.h>

#include <algorithm>
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
#include <cxxime/data_path.h>

namespace cxxime {
namespace {
using Microsoft::WRL::ComPtr;
constexpr unsigned kMaxBytes = 20 * 1024 * 1024;
constexpr unsigned kMaxPixels = 20000000;
constexpr unsigned kMaxSide = 2048;

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

std::shared_ptr<BackgroundImage> decode(const std::wstring& path, bool managed,
                                        std::wstring& error) {
    error = L"图片不可用，请选择有效的静态 PNG 或 JPEG。";
    auto result = std::make_shared<BackgroundImage>();
    result->lease = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (result->lease == INVALID_HANDLE_VALUE) {
        return {};
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(result->lease, &size) || size.QuadPart <= 0 || size.QuadPart > kMaxBytes) {
        error = L"图片文件不能超过 20 MiB。";
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
        std::uint64_t(width) * height > kMaxPixels ||
        (managed && (width > kMaxSide || height > kMaxSide))) {
        error = L"图片不能超过 2000 万像素。";
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
        source->GetSize(&width, &height);
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
    if (width > kMaxSide || height > kMaxSide) {
        const double scale = double(kMaxSide) / (std::max)(width, height);
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

std::string digest(const BackgroundImage& image) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::uint8_t bytes[32]{};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) {
        ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    }
    unsigned dimensions[] = {image.width, image.height};
    if (ok) {
        ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(dimensions), sizeof(dimensions), 0) >=
                 0 &&
             BCryptHashData(hash, const_cast<PUCHAR>(image.pixels.data()),
                            static_cast<ULONG>(image.pixels.size()), 0) >= 0 &&
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

std::wstring background_resource_path(const std::string& resource) {
    if (!valid_background_resource(resource)) {
        return {};
    }
    return (std::filesystem::u8path(user_data_path("backgrounds")) /
            std::filesystem::u8path(resource + ".png"))
        .wstring();
}

BackgroundImport import_background_image(const std::wstring& source) {
    BackgroundImport result;
    auto image = decode(source, false, result.error);
    if (!image) {
        return result;
    }
    result.resource = digest(*image);
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
        const auto temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
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
        DeleteFileW(background_resource_path(imported.resource).c_str());
    }
}

void prune_background_images(const std::string& current_resource) {
    const auto directory = std::filesystem::u8path(user_data_path("backgrounds"));
    std::error_code error;
    const auto now = std::filesystem::file_time_type::clock::now();
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error)) {
        const auto& path = it->path();
        const auto id = path.stem().u8string();
        if (path.extension() != L".png" || !valid_background_resource(id) ||
            id == current_resource) {
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
