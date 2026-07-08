// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/dictionary_manifest.h>
#include <cxxime/data_path.h>

#include <bcrypt.h>
#include <json.hpp>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <memory>
#include <new>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace cxxime {
namespace {

std::string dirname_of(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos)
        return ".";
    if (pos == 0)
        return path.substr(0, 1);
    if (pos == 2 && path.size() > 2 && path[1] == ':')
        return path.substr(0, 3);
    return path.substr(0, pos);
}

std::string join_path(const std::string& dir, const std::string& name) {
    if (name.empty())
        return dir;
    if (name.size() >= 2 && name[1] == ':')
        return name;
    if (!name.empty() && (name[0] == '\\' || name[0] == '/'))
        return name;
    if (dir.empty() || dir == ".")
        return name;
    char tail = dir.back();
    if (tail == '\\' || tail == '/')
        return dir + name;
    return dir + "\\" + name;
}

std::string to_hex(const unsigned char* data, size_t size) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out[i * 2] = kHex[(data[i] >> 4) & 0x0f];
        out[i * 2 + 1] = kHex[data[i] & 0x0f];
    }
    return out;
}

bool read_file_size(const std::string& path, uint64_t& size) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
        return false;
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return false;
    size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return true;
}

void set_error(std::string* error, const std::string& message) {
    if (error)
        *error = message;
}

bool equals_ignore_case(const std::string& a, const std::string& b) {
    return _stricmp(a.c_str(), b.c_str()) == 0;
}

std::string lowercase_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool is_safe_manifest_path(const std::string& path) {
    if (path.empty())
        return false;
    if (path.size() >= 2 && path[1] == ':')
        return false;
    if (path[0] == '\\' || path[0] == '/')
        return false;

    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find_first_of("\\/", start);
        std::string component = end == std::string::npos
            ? path.substr(start)
            : path.substr(start, end - start);
        if (component.empty() || component == "." || component == "..")
            return false;
        if (component.find(':') != std::string::npos)
            return false;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return true;
}

bool is_sha256_hex(const std::string& text) {
    if (text.size() != 64)
        return false;
    for (unsigned char c : text) {
        if (!std::isxdigit(c))
            return false;
    }
    return true;
}

const char* const kRequiredPinyinRoles[] = {
    "pinyin_dict",
    "pinyin_idx",
    "pinyin_spellings",
    "pinyin_topn",
};

} // namespace

const DictionaryManifestFile* DictionaryManifest::find_role(const std::string& role) const {
    for (const auto& file : files) {
        if (file.role == role)
            return &file;
    }
    return nullptr;
}

std::string dictionary_manifest_path_for_dict(const std::string& dict_path) {
    return join_path(dirname_of(dict_path), "dictionary_manifest.json");
}

std::string dictionary_manifest_default_path() {
    return data_path("dictionary_manifest.json");
}

bool compute_file_sha256(const std::string& path, std::string& out, std::string* error) {
    out.clear();

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0;
    DWORD result_size = 0;
    DWORD hash_size = 0;

    NTSTATUS status = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) {
        set_error(error, "BCryptOpenAlgorithmProvider failed");
        return false;
    }

    status = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
                               reinterpret_cast<PUCHAR>(&object_size),
                               sizeof(object_size), &result_size, 0);
    if (status >= 0) {
        status = BCryptGetProperty(alg, BCRYPT_HASH_LENGTH,
                                   reinterpret_cast<PUCHAR>(&hash_size),
                                   sizeof(hash_size), &result_size, 0);
    }
    if (status < 0 || object_size == 0 || hash_size == 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        set_error(error, "BCryptGetProperty failed");
        return false;
    }

    std::unique_ptr<unsigned char[]> object(new (std::nothrow) unsigned char[object_size]);
    std::unique_ptr<unsigned char[]> digest(new (std::nothrow) unsigned char[hash_size]);
    if (!object || !digest) {
        BCryptCloseAlgorithmProvider(alg, 0);
        set_error(error, "hash buffer allocation failed");
        return false;
    }

    status = BCryptCreateHash(alg, &hash, object.get(), object_size, nullptr, 0, 0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        set_error(error, "BCryptCreateHash failed");
        return false;
    }

    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        set_error(error, "open file failed: " + path);
        return false;
    }

    constexpr DWORD kBufferSize = 1024 * 1024;
    std::unique_ptr<unsigned char[]> buffer(new (std::nothrow) unsigned char[kBufferSize]);
    if (!buffer) {
        CloseHandle(file);
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        set_error(error, "read buffer allocation failed");
        return false;
    }

    bool ok = true;
    while (true) {
        DWORD bytes_read = 0;
        if (!ReadFile(file, buffer.get(), kBufferSize, &bytes_read, nullptr)) {
            set_error(error, "read file failed: " + path);
            ok = false;
            break;
        }
        if (bytes_read == 0)
            break;
        status = BCryptHashData(hash, buffer.get(), bytes_read, 0);
        if (status < 0) {
            set_error(error, "BCryptHashData failed");
            ok = false;
            break;
        }
    }

    CloseHandle(file);

    if (ok) {
        status = BCryptFinishHash(hash, digest.get(), hash_size, 0);
        if (status < 0) {
            set_error(error, "BCryptFinishHash failed");
            ok = false;
        } else {
            out = to_hex(digest.get(), hash_size);
        }
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

bool load_dictionary_manifest(const std::string& manifest_path,
                              DictionaryManifest& out,
                              std::string* error) {
    out = {};
    std::ifstream file(manifest_path);
    if (!file.is_open()) {
        set_error(error, "manifest not found: " + manifest_path);
        return false;
    }

    nlohmann::json json;
    try {
        json = nlohmann::json::parse(file);
    } catch (const nlohmann::json::exception& e) {
        set_error(error, std::string("manifest parse failed: ") + e.what());
        return false;
    }

    int schema = json.value("schema", 0);
    if (schema != 1) {
        set_error(error, "unsupported dictionary manifest schema");
        return false;
    }
    if (!json.contains("files") || !json["files"].is_array()) {
        set_error(error, "manifest files must be an array");
        return false;
    }

    out.manifest_path = manifest_path;
    out.directory = dirname_of(manifest_path);
    out.generation = json.value("generation", "");

    for (const auto& item : json["files"]) {
        if (!item.is_object()) {
            set_error(error, "manifest file entry must be an object");
            return false;
        }
        DictionaryManifestFile manifest_file;
        manifest_file.role = item.value("role", "");
        manifest_file.path = item.value("path", "");
        manifest_file.sha256 = item.value("sha256", "");
        manifest_file.size = item.value("size", static_cast<uint64_t>(0));
        manifest_file.required = item.value("required", true);
        if (manifest_file.role.empty() || manifest_file.path.empty()) {
            set_error(error, "manifest file entry missing role or path");
            return false;
        }
        if (manifest_file.size == 0 || manifest_file.sha256.empty()) {
            set_error(error, "manifest file entry missing size or sha256");
            return false;
        }
        if (!is_safe_manifest_path(manifest_file.path)) {
            set_error(error, "manifest file path is not relative: " + manifest_file.path);
            return false;
        }
        manifest_file.absolute_path = join_path(out.directory, manifest_file.path);
        out.files.push_back(std::move(manifest_file));
    }

    for (const char* role : kRequiredPinyinRoles) {
        if (!out.find_role(role)) {
            set_error(error, std::string("manifest missing ") + role);
            return false;
        }
    }

    return true;
}

bool validate_dictionary_manifest(const DictionaryManifest& manifest, std::string* error) {
    std::unordered_set<std::string> roles;
    std::unordered_set<std::string> paths;
    for (const auto& file : manifest.files) {
        if (!roles.insert(file.role).second) {
            set_error(error, "manifest duplicate role: " + file.role);
            return false;
        }
        std::string path_key = lowercase_ascii(file.path);
        if (!paths.insert(path_key).second) {
            set_error(error, "manifest duplicate path: " + file.path);
            return false;
        }
        if (!is_safe_manifest_path(file.path)) {
            set_error(error, "manifest file path is not relative: " + file.path);
            return false;
        }
        if (!is_sha256_hex(file.sha256)) {
            set_error(error, "manifest sha256 is invalid: " + file.path);
            return false;
        }

        uint64_t actual_size = 0;
        if (!read_file_size(file.absolute_path, actual_size)) {
            if (file.required) {
                set_error(error, "manifest file not found: " + file.path);
                return false;
            }
            continue;
        }
        if (actual_size != file.size) {
            std::ostringstream oss;
            oss << "manifest size mismatch: " << file.path
                << " expected=" << file.size << " actual=" << actual_size;
            set_error(error, oss.str());
            return false;
        }

        std::string actual_hash;
        std::string hash_error;
        if (!compute_file_sha256(file.absolute_path, actual_hash, &hash_error)) {
            set_error(error, hash_error);
            return false;
        }
        if (!equals_ignore_case(actual_hash, file.sha256)) {
            set_error(error, "manifest hash mismatch: " + file.path);
            return false;
        }
    }
    for (const char* role : kRequiredPinyinRoles) {
        const auto* file = manifest.find_role(role);
        if (!file) {
            set_error(error, std::string("manifest missing ") + role);
            return false;
        }
        if (!file->required) {
            set_error(error, std::string("manifest required role marked optional: ") + role);
            return false;
        }
    }
    return true;
}

} // namespace cxxime
