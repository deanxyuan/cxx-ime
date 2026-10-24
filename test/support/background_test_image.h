// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEST_BACKGROUND_IMAGE_H_
#define CXXIME_TEST_BACKGROUND_IMAGE_H_

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cxxime/background_image.h>
#include <cxxime/data_path.h>

#include "testutil.h"

namespace background_test {

struct Files {
    std::filesystem::path directory;
    std::string previous = cxxime::user_data_dir();
    HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    Files() {
        wchar_t temporary[MAX_PATH]{};
        wchar_t name[MAX_PATH]{};
        ASSERT_TRUE(GetTempPathW(MAX_PATH, temporary) != 0);
        ASSERT_TRUE(GetTempFileNameW(temporary, L"cbi", 0, name) != 0);
        ASSERT_TRUE(DeleteFileW(name) != FALSE);
        directory = name;
        std::filesystem::create_directory(directory);
        cxxime::set_user_data_dir(directory.u8string());
    }
    ~Files() {
        cxxime::set_user_data_dir(previous);
        std::filesystem::remove_all(directory);
        if (SUCCEEDED(apartment)) {
            CoUninitialize();
        }
    }

    std::wstring write(const wchar_t* name, unsigned width, unsigned height,
                       const std::vector<BYTE>& pixels, bool jpeg = false,
                       unsigned orientation = 1) {
        using Microsoft::WRL::ComPtr;
        const auto path = (directory / name).wstring();
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IWICBitmap> bitmap;
        ComPtr<IWICBitmapSource> converted;
        ComPtr<IPropertyBag2> options;
        ASSERT_TRUE(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                               CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))));
        ASSERT_TRUE(SUCCEEDED(factory->CreateStream(&stream)));
        ASSERT_TRUE(SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)));
        ASSERT_TRUE(SUCCEEDED(factory->CreateEncoder(
            jpeg ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng, nullptr, &encoder)));
        ASSERT_TRUE(SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)));
        ASSERT_TRUE(SUCCEEDED(encoder->CreateNewFrame(&frame, &options)));
        if (jpeg) {
            PROPBAG2 property{};
            property.pstrName = const_cast<wchar_t*>(L"ImageQuality");
            VARIANT value{};
            value.vt = VT_R4;
            value.fltVal = 1.0f;
            ASSERT_TRUE(SUCCEEDED(options->Write(1, &property, &value)));
        }
        ASSERT_TRUE(SUCCEEDED(frame->Initialize(options.Get())));
        ASSERT_TRUE(SUCCEEDED(frame->SetSize(width, height)));
        GUID format = jpeg ? GUID_WICPixelFormat24bppBGR : GUID_WICPixelFormat32bppBGRA;
        ASSERT_TRUE(SUCCEEDED(frame->SetPixelFormat(&format)));
        if (jpeg) {
            ComPtr<IWICMetadataQueryWriter> writer;
            ASSERT_TRUE(SUCCEEDED(frame->GetMetadataQueryWriter(&writer)));
            PROPVARIANT value{};
            value.vt = VT_UI2;
            value.uiVal = static_cast<USHORT>(orientation);
            ASSERT_TRUE(SUCCEEDED(writer->SetMetadataByName(L"/app1/ifd/{ushort=274}", &value)));
        }
        ASSERT_TRUE(SUCCEEDED(factory->CreateBitmapFromMemory(
            width, height, GUID_WICPixelFormat32bppBGRA, width * 4,
            static_cast<UINT>(pixels.size()), const_cast<BYTE*>(pixels.data()), &bitmap)));
        ASSERT_TRUE(SUCCEEDED(WICConvertBitmapSource(format, bitmap.Get(), &converted)));
        ASSERT_TRUE(SUCCEEDED(frame->WriteSource(converted.Get(), nullptr)));
        ASSERT_TRUE(SUCCEEDED(frame->Commit()));
        ASSERT_TRUE(SUCCEEDED(encoder->Commit()));
        return path;
    }

    void snapshot(HWND window, const std::wstring& destination) {
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        RECT bounds{};
        ASSERT_TRUE(GetWindowRect(window, &bounds));
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        HDC source = GetWindowDC(window);
        HDC buffer = CreateCompatibleDC(source);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* data = nullptr;
        HBITMAP bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        ASSERT_TRUE(bitmap != nullptr && source != nullptr && buffer != nullptr);
        auto old = SelectObject(buffer, bitmap);
        ASSERT_TRUE(BitBlt(buffer, 0, 0, width, height, source, 0, 0, SRCCOPY));
        GdiFlush();
        std::vector<BYTE> pixels(static_cast<std::size_t>(width) * height * 4);
        std::memcpy(pixels.data(), data, pixels.size());
        for (std::size_t i = 3; i < pixels.size(); i += 4) {
            pixels[i] = 255;
        }
        SelectObject(buffer, old);
        DeleteObject(bitmap);
        DeleteDC(buffer);
        ReleaseDC(window, source);
        write(destination.c_str(), width, height, pixels);
    }
};

inline std::vector<BYTE> solid(unsigned width, unsigned height, BYTE blue, BYTE green, BYTE red,
                               BYTE alpha = 255) {
    std::vector<BYTE> pixels(std::size_t(width) * height * 4);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = blue;
        pixels[i + 1] = green;
        pixels[i + 2] = red;
        pixels[i + 3] = alpha;
    }
    return pixels;
}

} // namespace background_test

#endif // CXXIME_TEST_BACKGROUND_IMAGE_H_
