// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_USER_BACKUP_CONTROL_H_
#define CXXIME_USER_BACKUP_CONTROL_H_

#include <cstdint>
#include <string>

#include <cxxime/user_backup.h>

namespace cxxime {

enum class UserBackupOperation {
    kUnknown,
    kInspect,
    kExport,
    kImport,
};

struct UserBackupControlRequest {
    UserBackupOperation operation = UserBackupOperation::kUnknown;
    std::string path;
    std::uint32_t components = 0;
};

struct UserBackupControlResult {
    UserBackupOperation operation = UserBackupOperation::kUnknown;
    bool succeeded = false;
    std::uint32_t error_code = 0;
    UserBackupSummary summary;
    std::uint64_t imported_count = 0;
    std::uint64_t skipped_count = 0;
};

bool encode_user_backup_request(const UserBackupControlRequest& request, std::string* payload);
bool decode_user_backup_request(const std::string& payload, UserBackupControlRequest* request);
bool encode_user_backup_result(const UserBackupControlResult& result, std::string* payload);
bool decode_user_backup_result(const std::string& payload, UserBackupControlResult* result);

class UserBackupControlClient {
public:
    explicit UserBackupControlClient(int timeout_ms = 1500, const std::wstring& pipe_name = L"");

    bool inspect(const std::string& path, UserBackupControlResult* result) const;
    bool export_backup(const std::string& path, std::uint32_t components,
                       UserBackupControlResult* result) const;
    bool import_backup(const std::string& path, std::uint32_t components,
                       UserBackupControlResult* result) const;

private:
    bool execute(const UserBackupControlRequest& request, UserBackupControlResult* result) const;

    int timeout_ms_;
    std::wstring pipe_name_;
};

} // namespace cxxime

#endif // CXXIME_USER_BACKUP_CONTROL_H_
