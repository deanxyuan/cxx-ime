// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <string>

#include <cxxime/ipc_protocol.h>

#include "ipc_response_builder.h"
#include "support/testutil.h"

TEST(IpcResponseBuilder, serializes_complete_segmented_presentation) {
    ProcessKeyResult result;
    result.status = cxxime::IPCStatus::OK;
    result.composing = true;
    result.preedit = "华锐jishu";
    result.preedit_cursor = result.preedit.size();
    result.converted_prefix_bytes = std::string("华锐").size();
    result.candidate_revision = 9;
    result.presentation.page_index = 1;
    result.presentation.page_offset = 5;
    result.presentation.page_size = 5;
    result.presentation.total_count = 11;
    result.presentation.highlighted = 1;
    result.presentation.items.push_back({"技术", "a"});

    cxxime::IPCResponse response = {};
    fill_process_response(result, &response);

    ASSERT_EQ(response.status, cxxime::IPCStatus::OK);
    ASSERT_EQ(std::string(response.preedit), result.preedit);
    ASSERT_EQ(response.preedit_cursor, static_cast<uint32_t>(result.preedit.size()));
    ASSERT_EQ(response.converted_prefix_bytes, static_cast<uint32_t>(std::string("华锐").size()));
    ASSERT_EQ(response.candidate_revision, 9u);
    ASSERT_EQ(response.candidate_count, 1u);
    ASSERT_EQ(std::string(response.candidates[0]), "技术");
    ASSERT_EQ(std::string(response.candidate_hints[0]), "a");
    ASSERT_EQ(response.candidate_offset, 5u);
    ASSERT_EQ(response.candidate_total, 11u);
    ASSERT_EQ(response.page_current, 2u);
    ASSERT_EQ(response.page_total, 3u);
    ASSERT_EQ(response.highlighted, 1u);
}

TEST(IpcResponseBuilder, rejects_non_utf8_preedit_boundaries) {
    ProcessKeyResult result;
    result.status = cxxime::IPCStatus::OK;
    result.composing = true;
    result.preedit = "华a";
    result.preedit_cursor = 1;
    result.converted_prefix_bytes = 3;

    cxxime::IPCResponse response = {};
    fill_process_response(result, &response);

    ASSERT_EQ(response.status, cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED);
    ASSERT_EQ(response.candidate_count, 0u);
}

TEST(IpcResponseBuilder, rejects_converted_prefix_after_the_cursor) {
    ProcessKeyResult result;
    result.status = cxxime::IPCStatus::OK;
    result.composing = true;
    result.preedit = u8"华锐jishu";
    result.preedit_cursor = 0;
    result.converted_prefix_bytes = std::string(u8"华锐").size();
    cxxime::IPCResponse response;

    fill_process_response(result, &response);

    ASSERT_EQ(response.status, cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED);
}
