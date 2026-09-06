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
    copy_field(response.candidate_annotations[0], u8"后续");
    response.candidate_revision = 7;
    return response;
}

} // namespace

TEST(EngineResponse, decodes_converted_prefix_and_independent_candidate_fields) {
    const cxxime::IPCResponse response = make_response();
    cxxime_tsf::DecodedEnginePresentation presentation;

    ASSERT_TRUE(cxxime_tsf::decode_engine_presentation(response, &presentation));
    ASSERT_EQ(presentation.preedit, std::wstring(L"华锐jishu"));
    ASSERT_EQ(presentation.preedit_cursor_utf16, static_cast<std::size_t>(7));
    ASSERT_EQ(presentation.converted_prefix_utf16, static_cast<std::size_t>(2));
    ASSERT_EQ(presentation.candidates.items.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(presentation.candidates.items[0].text, std::string(u8"技术"));
    ASSERT_EQ(presentation.candidates.items[0].hint, std::string("/rs"));
    ASSERT_EQ(presentation.candidates.items[0].annotation, std::string(u8"后续"));
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

TEST(EngineResponse, rejects_invalid_candidate_annotation_utf8) {
    cxxime::IPCResponse response = make_response();
    response.candidate_annotations[0][0] = static_cast<char>(0xff);
    response.candidate_annotations[0][1] = '\0';
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
