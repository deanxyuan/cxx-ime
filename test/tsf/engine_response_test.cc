// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cstring>
#include <string>

#include <cxxime/ipc_protocol.h>

#include "engine_response.h"
#include "support/testutil.h"

namespace {

template <std::size_t Capacity>
void copy_field(char (&field)[Capacity], const std::string& value) {
    ASSERT_TRUE(value.size() < Capacity);
    std::memcpy(field, value.data(), value.size());
    field[value.size()] = '\0';
}

cxxime::IPCResponse make_response() {
    cxxime::IPCResponse response;
    response.status = cxxime::IPCStatus::OK;
    response.composing = 1;
    const std::string converted = u8"华锐";
    const std::string preedit = converted + "jishu";
    copy_field(response.preedit, preedit);
    response.preedit_cursor = static_cast<std::uint32_t>(preedit.size());
    response.converted_prefix_bytes = static_cast<std::uint32_t>(converted.size());
    response.candidate_count = 1;
    response.candidate_total = 1;
    response.page_current = 1;
    response.page_total = 1;
    copy_field(response.candidates[0], u8"技术");
    copy_field(response.candidate_hints[0], "/rs");
    response.candidate_revision = 7;
    return response;
}

} // namespace

TEST(EngineResponse, decodes_converted_prefix_and_candidate_hint) {
    const cxxime::IPCResponse response = make_response();
    cxxime_tsf::DecodedEnginePresentation presentation;

    ASSERT_TRUE(cxxime_tsf::decode_engine_presentation(response, &presentation));
    ASSERT_EQ(presentation.preedit, std::wstring(L"华锐jishu"));
    ASSERT_EQ(presentation.preedit_cursor_utf16, static_cast<std::size_t>(7));
    ASSERT_EQ(presentation.converted_prefix_utf16, static_cast<std::size_t>(2));
    ASSERT_EQ(presentation.focused_preedit_start_utf16, static_cast<std::size_t>(2));
    ASSERT_EQ(presentation.focused_preedit_end_utf16, static_cast<std::size_t>(7));
    ASSERT_EQ(presentation.candidates.items.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(presentation.candidates.items[0].text, std::string(u8"技术"));
    ASSERT_EQ(presentation.candidates.items[0].hint, std::string("/rs"));
}

TEST(EngineResponse, decodes_explicit_focus_before_a_syllable_separator) {
    cxxime::IPCResponse response = make_response();
    const std::string converted = u8"华锐";
    const std::string preedit = converted + "ji'shu";
    copy_field(response.preedit, preedit);
    response.preedit_cursor = static_cast<std::uint32_t>(preedit.size());
    response.focused_preedit_start_bytes = static_cast<std::uint32_t>(converted.size());
    response.focused_preedit_end_bytes =
        static_cast<std::uint32_t>(converted.size() + std::string("ji").size());
    response.preedit_presentation_flags = cxxime::preedit_presentation_flag(
        cxxime::PreeditPresentationFlag::SyllableBoundaries);
    cxxime_tsf::DecodedEnginePresentation presentation;

    ASSERT_TRUE(cxxime_tsf::decode_engine_presentation(response, &presentation));
    ASSERT_EQ(presentation.display_preedit, std::wstring(L"华锐ji'shu"));
    ASSERT_EQ(presentation.preedit, std::wstring(L"华锐jishu"));
    ASSERT_EQ(presentation.focused_preedit_start_utf16, static_cast<std::size_t>(2));
    ASSERT_EQ(presentation.focused_preedit_end_utf16, static_cast<std::size_t>(4));
    ASSERT_EQ(presentation.display_preedit[4], L'\'');
}

TEST(EngineResponse, unflagged_apostrophe_remains_in_logical_preedit) {
    cxxime::IPCResponse response = {};
    response.status = cxxime::IPCStatus::OK;
    response.composing = 1;
    copy_field(response.preedit, "don't");
    response.preedit_cursor = 5;
    response.focused_preedit_end_bytes = 5;
    cxxime_tsf::DecodedEnginePresentation presentation;

    ASSERT_TRUE(cxxime_tsf::decode_engine_presentation(response, &presentation));
    ASSERT_EQ(presentation.preedit, std::wstring(L"don't"));
    ASSERT_TRUE(!presentation.has_syllable_boundaries);
}

TEST(EngineResponse, old_wubi_and_mixed_responses_default_to_no_focus) {
    for (cxxime::InputMode mode : {cxxime::InputMode::WUBI, cxxime::InputMode::MIXED}) {
        cxxime::IPCResponse response = {};
        response.status = cxxime::IPCStatus::OK;
        response.composing = 1;
        response.ime_status.input_mode = mode;
        copy_field(response.preedit, "abcd");
        response.preedit_cursor = 4;
        cxxime_tsf::DecodedEnginePresentation presentation;

        ASSERT_TRUE(cxxime_tsf::decode_engine_presentation(response, &presentation));
        ASSERT_EQ(presentation.focused_preedit_start_utf16, static_cast<std::size_t>(4));
        ASSERT_EQ(presentation.focused_preedit_end_utf16, static_cast<std::size_t>(4));
        ASSERT_EQ(presentation.display_focused_preedit_start_bytes, static_cast<std::size_t>(4));
        ASSERT_EQ(presentation.display_focused_preedit_end_bytes, static_cast<std::size_t>(4));
    }
}

TEST(EngineResponse, rejects_offsets_inside_a_utf8_code_point) {
    cxxime::IPCResponse response = make_response();
    response.converted_prefix_bytes = 1;
    cxxime_tsf::DecodedEnginePresentation presentation;

    ASSERT_TRUE(!cxxime_tsf::decode_engine_presentation(response, &presentation));
}

TEST(EngineResponse, rejects_converted_prefix_after_the_cursor) {
    cxxime::IPCResponse response = make_response();
    response.preedit_cursor = 0;
    cxxime_tsf::DecodedEnginePresentation presentation;

    ASSERT_TRUE(!cxxime_tsf::decode_engine_presentation(response, &presentation));
}

TEST(EngineResponse, rejects_invalid_commit_text_utf8) {
    cxxime::IPCResponse response;
    response.commit_text[0] = static_cast<char>(0xff);
    response.commit_text[1] = '\0';
    std::wstring commit_text;

    ASSERT_TRUE(!cxxime_tsf::decode_engine_commit_text(response, &commit_text));
}

RUN_ALL_TESTS()
