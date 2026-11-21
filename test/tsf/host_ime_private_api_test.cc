// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cstdint>
#include <cstring>
#include <initializer_list>

#include <windows.h>

#include "host_compatibility/host_ime_private_api.h"
#include "support/testutil.h"

namespace {

#if defined(_M_X64)

class HostImage {
public:
    static constexpr uint32_t kSize = 0x3000;
    static constexpr uint32_t kNotify = 0x1000;
    static constexpr uint32_t kNotifySize = 0x400;
    static constexpr uint32_t kChange = 0x1900;
    static constexpr uint32_t kOpen = 0x1920;

    HostImage() {
        memory_ = static_cast<uint8_t*>(
            VirtualAlloc(nullptr, kSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        ASSERT_TRUE(memory_ != nullptr);
        std::memset(memory_ + kNotify, 0x90, 0x1000);
        function_.BeginAddress = kNotify;
        function_.EndAddress = kNotify + kNotifySize;
        function_.UnwindData = 0x2000;
        memory_[0x2000] = 1; // UNWIND_INFO version 1; code is inspected, never called.
        ASSERT_TRUE(RtlAddFunctionTable(&function_, 1, base()));
        executable(true);
    }

    ~HostImage() {
        RtlDeleteFunctionTable(&function_);
        VirtualFree(memory_, 0, MEM_RELEASE);
    }

    uintptr_t base() const { return reinterpret_cast<uintptr_t>(memory_); }

    void executable(bool enabled) {
        DWORD previous = 0;
        ASSERT_TRUE(VirtualProtect(memory_ + 0x1000, 0x1000,
                                   enabled ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &previous));
    }

    void bytes(uint32_t offset, std::initializer_list<uint8_t> values) {
        ASSERT_TRUE(offset <= kSize && values.size() <= kSize - offset);
        std::memcpy(memory_ + offset, values.begin(), values.size());
    }

    template <typename Value>
    void value(uint32_t offset, Value data) {
        ASSERT_TRUE(offset <= kSize && sizeof(data) <= kSize - offset);
        std::memcpy(memory_ + offset, &data, sizeof(data));
    }

    void branch(uint32_t block, uint32_t target, uint32_t slot, bool near_jump) {
        // A different supported classification before ours must not affect recognition.
        bytes(block, {0x8b, 0x41, 0x68, 0x3d, 0x00, 0x30, 0x00, 0x00, 0x74, 0x00, 0x3d, 0x00, 0x00,
                      0x81, 0x00});
        if (near_jump) {
            bytes(block + 15, {0x0f, 0x84});
            value<int32_t>(block + 17, static_cast<int32_t>(target) - (block + 21));
        } else {
            bytes(block + 15, {0x74});
            value<int8_t>(block + 16, static_cast<int8_t>(target - (block + 17)));
        }
        bytes(target, {0x48, 0x8b, 0x01, 0x33, 0xd2, 0xff, 0x90});
        value(target + 7, slot);
    }

    void candidate_protocol(bool near_jump = false, uint32_t shift = 0) {
        branch(kNotify + 0x40 + shift, kNotify + 0x80 + shift, 0xe0, near_jump);
        branch(kNotify + 0x100 + shift, kNotify + 0x140 + shift, 0xd8, near_jump);
    }

    bool candidates_match() const {
        return cxxime_tsf::host_ime_candidate_methods_match(base(), kSize, base() + kNotify,
                                                            base() + kChange, base() + kOpen);
    }

    cxxime_tsf::HostImePrivateApiRequest getter_request() {
        // Three tiny leaf getters are our own code. Only these are executed by this test.
        bytes(0x1800, {0x0f, 0xb6, 0x41, 0x60, 0xc3});
        bytes(0x1810, {0x0f, 0xb6, 0x41, 0x61, 0xc3});
        bytes(0x1820, {0x81, 0xb9, 0xd0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xb8, 0x00,
                       0x00, 0x00, 0x01, 0x74, 0x06, 0x8b, 0x81, 0xd4, 0x00, 0x00, 0x00, 0xc3});
        value<uintptr_t>(0x100, base() + 0x300);
        value<uintptr_t>(0x360, base() + 0x1800);
        value<uintptr_t>(0x370, base() + 0x1810);
        bytes(0x160, {1, 1});
        value<uintptr_t>(0x500, base() + 0x700);
        value<uintptr_t>(0x710, base() + 0x1820);
        value<uint32_t>(0x5d0, 0x00080000);
        value<uint32_t>(0x5d4, 0x01000000);
        FlushInstructionCache(GetCurrentProcess(), memory_ + 0x1800, 0x40);
        cxxime_tsf::HostImePrivateApiRequest request;
        request.imemanager_base = base();
        request.imemanager_image_size = kSize;
        request.manager = base() + 0x100;
        request.names_manager = base() + 0x500;
        request.module_readable = true;
        return request;
    }

private:
    uint8_t* memory_ = nullptr;
    RUNTIME_FUNCTION function_{};
};

#endif

} // namespace

#if defined(_M_X64)

TEST(HostImePrivateApi, candidate_contract_survives_prologue_register_and_branch_layout_changes) {
    for (bool near_jump : {false, true}) {
        for (uint32_t shift : {0u, 0x20u}) {
            HostImage image;
            image.candidate_protocol(near_jump, shift);
            // Old and updated DOTA2 entry forms: the host owns these methods, not CxxIME.
            image.bytes(HostImage::kChange, {0x40, 0x53, 0x56, 0x57, 0x41, 0x54});
            image.bytes(HostImage::kOpen, {0x40, 0x53, 0x56, 0x57, 0x41, 0x56});
            ASSERT_TRUE(image.candidates_match());
            image.bytes(HostImage::kChange, {0x40, 0x53, 0x55, 0x56, 0x57});
            image.bytes(HostImage::kOpen, {0x40, 0x53, 0x55, 0x56, 0x57});
            image.bytes(HostImage::kNotify, {0x55, 0x56, 0x57, 0x48, 0x83, 0xec, 0x20});
            ASSERT_TRUE(image.candidates_match());
        }
    }
}

TEST(HostImePrivateApi, rejects_changes_to_classification_dataflow_or_candidate_dispatch) {
    struct Mutation {
        uint32_t offset;
        uint8_t byte;
    };
    const Mutation mutations[] = {
        {0x42, 0x6c},  // Changed candidate classification field.
        {0x4d, 0x82},  // Changed target protocol.
        {0x4a, 0xb8},  // mov instead of cmp: constant alone does not prove a branch.
        {0x4f, 0x75},  // jne instead of je.
        {0x50, 0x00},  // Equality branch no longer reaches the dispatch.
        {0x85, 0x90},  // Removed virtual call.
        {0x87, 0xd8},  // Open branch incorrectly dispatches change.
        {0x147, 0xe0}, // Change branch incorrectly dispatches open.
    };
    for (const auto& mutation : mutations) {
        HostImage image;
        image.candidate_protocol();
        image.bytes(HostImage::kNotify + mutation.offset, {mutation.byte});
        ASSERT_TRUE(!image.candidates_match()) << mutation.offset;
    }
    for (uint8_t prefix :
         {0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65, 0x66, 0x67, 0xf0, 0xf2, 0xf3, 0x40, 0x41, 0x42,
          0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f}) {
        HostImage image;
        image.candidate_protocol();
        image.bytes(HostImage::kNotify + 0x3f, {prefix});
        ASSERT_TRUE(!image.candidates_match()) << static_cast<unsigned>(prefix);
    }
}

TEST(HostImePrivateApi, rejects_missing_truncated_foreign_and_nonexecutable_methods) {
    HostImage image;
    image.candidate_protocol(true);
    ASSERT_TRUE(image.candidates_match());
    image.value<int32_t>(HostImage::kNotify + 0x51, -0x2000);
    ASSERT_TRUE(!image.candidates_match());
    image.candidate_protocol(true);
    // A valid dispatch elsewhere in the module is not evidence for this function.
    image.branch(HostImage::kNotify + 0x40, HostImage::kNotify + HostImage::kNotifySize, 0xe0,
                 true);
    ASSERT_TRUE(!image.candidates_match());
    image.candidate_protocol();
    ASSERT_TRUE(!cxxime_tsf::host_ime_candidate_notify_method_matches(
        image.base(), HostImage::kNotify + 0x100, image.base() + HostImage::kNotify));
    ASSERT_TRUE(!cxxime_tsf::host_ime_candidate_notify_method_matches(
        image.base(), HostImage::kSize, image.base() + HostImage::kNotify + 1));
    ASSERT_TRUE(!cxxime_tsf::host_ime_candidate_methods_match(
        image.base(), HostImage::kSize, image.base() + HostImage::kNotify,
        image.base() + HostImage::kSize, image.base() + HostImage::kOpen));
    ASSERT_TRUE(!cxxime_tsf::host_ime_candidate_methods_match(
        image.base(), HostImage::kSize, image.base() + HostImage::kNotify,
        image.base() + HostImage::kChange, image.base() + 0x100));
    image.executable(false);
    ASSERT_TRUE(!image.candidates_match());
}

TEST(HostImePrivateApi, private_getters_still_require_verified_fields_and_callable_code) {
    HostImage image;
    auto request = image.getter_request();
    auto snapshot = cxxime_tsf::inspect_host_ime_private_api(request);
    ASSERT_TRUE(snapshot.verified);
    ASSERT_TRUE(snapshot.manager_called && snapshot.classification_called);
    ASSERT_TRUE(snapshot.manager_initialized && snapshot.manager_enabled);
    ASSERT_EQ(snapshot.classification, 0x01000000u);

    image.value<uint32_t>(0x5d4, 0x00810000);
    snapshot = cxxime_tsf::inspect_host_ime_private_api(request);
    ASSERT_EQ(snapshot.classification, 0x00810000u);
    image.value<uint32_t>(0x5d0, 0x02000000);
    snapshot = cxxime_tsf::inspect_host_ime_private_api(request);
    ASSERT_EQ(snapshot.classification, 0x01000000u);

    image.bytes(0x1803, {0x62});
    image.bytes(0x1833, {0xd8});
    snapshot = cxxime_tsf::inspect_host_ime_private_api(request);
    ASSERT_TRUE(!snapshot.verified);
    ASSERT_TRUE(!snapshot.manager_called && !snapshot.classification_called);
    request = image.getter_request();
    image.executable(false);
    snapshot = cxxime_tsf::inspect_host_ime_private_api(request);
    ASSERT_TRUE(!snapshot.verified);
    ASSERT_TRUE(!snapshot.manager_called && !snapshot.classification_called);
}

#else

TEST(HostImePrivateApi, unsupported_architecture_never_calls_private_getters) {
    const auto snapshot = cxxime_tsf::inspect_host_ime_private_api({});
    ASSERT_TRUE(!snapshot.verified);
    ASSERT_TRUE(!snapshot.manager_called && !snapshot.classification_called);
}

#endif

RUN_ALL_TESTS()
