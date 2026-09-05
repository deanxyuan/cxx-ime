// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "session_manager_integration_test_support.h"

namespace {

std::string setup_segmented_session_dict() {
    const std::string dict_path = make_temp_path("session_segmented_dict.bin");
    create_test_dictionary_bundle(dict_path, {
                                                 {"hua:rui:ji:shu", "华锐技术", 12000},
                                                 {"hua:rui", "华锐", 9000},
                                                 {"ji:shu", "技术", 10000},
                                                 {"hua", "华", 7000},
                                                 {"rui", "锐", 7000},
                                                 {"ji", "技", 7000},
                                                 {"shu", "术", 7000},
                                             });
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(
        dict_path + ".spellings.bin", {{"hua", "hua", cxxime::kNormalSpelling, 0.0f},
                                       {"rui", "rui", cxxime::kNormalSpelling, 0.0f},
                                       {"ji", "ji", cxxime::kNormalSpelling, 0.0f},
                                       {"shu", "shu", cxxime::kNormalSpelling, 0.0f}}));
    write_manifest_for_files(dict_path, {
                                            {"pinyin_dict", dict_path},
                                            {"pinyin_idx", dict_path + ".idx"},
                                            {"pinyin_spellings", dict_path + ".spellings.bin"},
                                            {"pinyin_topn", dict_path + ".topn.bin"},
                                            {"wubi_dict", dict_path + ".wubi.bin"},
                                            {"wubi_prefix_index", dict_path + ".wubi.bin.idx"},
                                        });
    return dict_path;
}

ProcessKeyResult type_code(SessionManager& manager, uint32_t id, const std::string& code,
                           uint32_t visible_candidate_count = 0) {
    ProcessKeyResult result;
    for (char value : code) {
        result = manager.process_key(id, make_key(static_cast<uint32_t>(value - 'a' + 'A')),
                                     visible_candidate_count);
    }
    return result;
}

int find_presentation_item(const ProcessKeyResult& result, const std::string& text) {
    for (size_t index = 0; index < result.presentation.items.size(); ++index) {
        if (result.presentation.items[index].text == text) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

} // namespace

TEST(SessionSegmented, legacy_session_keeps_full_span_selection_and_zero_revision) {
    const std::string dict_path = setup_segmented_session_dict();
    {
        SessionManager manager;
        ASSERT_TRUE(manager.initialize(dict_path));
        const uint32_t id = manager.create_session();

        const ProcessKeyResult typed = type_code(manager, id, "huaruijishu");
        ASSERT_EQ(typed.candidate_revision, 0u);
        ASSERT_EQ(find_presentation_item(typed, "华锐"), -1);

        const ProcessKeyResult selected = manager.select_candidate(id, 0, 999);
        ASSERT_EQ(selected.status, cxxime::IPCStatus::OK);
        ASSERT_EQ(selected.result, cxxime::ProcessResult::COMMITTED);
        ASSERT_EQ(selected.commit_text, "华锐技术");
        ASSERT_EQ(selected.candidate_revision, 0u);
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionSegmented, stale_selection_refreshes_without_changing_composition) {
    const std::string dict_path = setup_segmented_session_dict();
    {
        SessionManager manager;
        ASSERT_TRUE(manager.initialize(dict_path));
        const uint32_t id = manager.create_session(cxxime::kClientCapabilitySegmentedSelection);

        const ProcessKeyResult typed = type_code(manager, id, "huaruijishu");
        const int prefix_index = find_presentation_item(typed, "华锐");
        ASSERT_GE(prefix_index, 0);
        ASSERT_GT(typed.candidate_revision, 1u);

        const ProcessKeyResult stale =
            manager.select_candidate(id, prefix_index, typed.candidate_revision - 1);
        ASSERT_EQ(stale.status, cxxime::IPCStatus::ERR_STALE_CANDIDATE);
        ASSERT_EQ(stale.result, cxxime::ProcessResult::REJECTED);
        ASSERT_TRUE(stale.commit_text.empty());
        ASSERT_TRUE(stale.composing);
        ASSERT_EQ(stale.preedit, "huaruijishu");
        ASSERT_EQ(stale.candidate_revision, typed.candidate_revision);
        ASSERT_EQ(stale.presentation.items.size(), typed.presentation.items.size());
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionSegmented, current_revision_confirms_prefix_then_finalizes_suffix) {
    const std::string dict_path = setup_segmented_session_dict();
    {
        SessionManager manager;
        ASSERT_TRUE(manager.initialize(dict_path));
        const uint32_t id = manager.create_session(cxxime::kClientCapabilitySegmentedSelection);

        const ProcessKeyResult typed = type_code(manager, id, "huaruijishu");
        const int prefix_index = find_presentation_item(typed, "华锐");
        ASSERT_GE(prefix_index, 0);

        const ProcessKeyResult prefix =
            manager.select_candidate(id, prefix_index, typed.candidate_revision);
        ASSERT_EQ(prefix.status, cxxime::IPCStatus::OK);
        ASSERT_EQ(prefix.result, cxxime::ProcessResult::ACCEPTED);
        ASSERT_TRUE(prefix.commit_text.empty());
        ASSERT_TRUE(prefix.composing);
        ASSERT_EQ(prefix.preedit, "华锐jishu");
        ASSERT_EQ(prefix.converted_prefix_bytes, std::string("华锐").size());
        ASSERT_GT(prefix.candidate_revision, typed.candidate_revision);

        const ProcessKeyResult stale = manager.select_candidate(id, 0, typed.candidate_revision);
        ASSERT_EQ(stale.status, cxxime::IPCStatus::ERR_STALE_CANDIDATE);
        ASSERT_EQ(stale.candidate_revision, prefix.candidate_revision);

        const int suffix_index = find_presentation_item(prefix, "技术");
        ASSERT_GE(suffix_index, 0);
        const ProcessKeyResult suffix =
            manager.select_candidate(id, suffix_index, prefix.candidate_revision);
        ASSERT_EQ(suffix.status, cxxime::IPCStatus::OK);
        ASSERT_EQ(suffix.result, cxxime::ProcessResult::COMMITTED);
        ASSERT_EQ(suffix.commit_text, "华锐技术");
        ASSERT_TRUE(!suffix.composing);
        ASSERT_GT(suffix.candidate_revision, prefix.candidate_revision);
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionSegmented, invalid_selection_and_cursor_motion_preserve_revision) {
    const std::string dict_path = setup_segmented_session_dict();
    {
        SessionManager manager;
        ASSERT_TRUE(manager.initialize(dict_path));
        const uint32_t id = manager.create_session(cxxime::kClientCapabilitySegmentedSelection);

        const ProcessKeyResult typed = type_code(manager, id, "huaruijishu");
        const ProcessKeyResult moved = manager.process_key(id, make_key(VK_LEFT));
        ASSERT_EQ(moved.candidate_revision, typed.candidate_revision);

        const ProcessKeyResult invalid = manager.select_candidate(id, 99, moved.candidate_revision);
        ASSERT_EQ(invalid.status, cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED);
        ASSERT_EQ(invalid.candidate_revision, moved.candidate_revision);
        ASSERT_TRUE(invalid.composing);
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionSegmented, paging_and_partial_undo_advance_revision) {
    const std::string dict_path = setup_segmented_session_dict();
    {
        SessionManager manager;
        ASSERT_TRUE(manager.initialize(dict_path));
        const uint32_t id = manager.create_session(cxxime::kClientCapabilitySegmentedSelection);

        const ProcessKeyResult typed = type_code(manager, id, "huaruijishu", 2);
        const ProcessKeyResult next_page = manager.process_key(id, make_key(VK_NEXT), 2);
        ASSERT_GT(next_page.presentation.page_offset, typed.presentation.page_offset);
        ASSERT_GT(next_page.candidate_revision, typed.candidate_revision);

        ASSERT_EQ(manager.clear_composition(id).status, cxxime::IPCStatus::OK);
        const ProcessKeyResult retyped = type_code(manager, id, "huaruijishu");
        const int prefix_index = find_presentation_item(retyped, "华锐");
        ASSERT_GE(prefix_index, 0);
        const ProcessKeyResult prefix =
            manager.select_candidate(id, prefix_index, retyped.candidate_revision);
        const ProcessKeyResult home = manager.process_key(id, make_key(VK_HOME));
        ASSERT_EQ(home.candidate_revision, prefix.candidate_revision);
        const ProcessKeyResult undone = manager.process_key(id, make_key(VK_BACK));
        ASSERT_EQ(undone.preedit, "huaruijishu");
        ASSERT_GT(undone.candidate_revision, prefix.candidate_revision);

        const ProcessKeyResult stale = manager.select_candidate(id, 0, prefix.candidate_revision);
        ASSERT_EQ(stale.status, cxxime::IPCStatus::ERR_STALE_CANDIDATE);
        ASSERT_EQ(stale.candidate_revision, undone.candidate_revision);
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionSegmented, clear_and_focus_out_invalidate_the_visible_page) {
    const std::string dict_path = setup_segmented_session_dict();
    {
        SessionManager manager;
        ASSERT_TRUE(manager.initialize(dict_path));
        const uint32_t id = manager.create_session(cxxime::kClientCapabilitySegmentedSelection);

        const ProcessKeyResult typed = type_code(manager, id, "huaruijishu");
        const ProcessKeyResult cleared = manager.clear_composition(id);
        ASSERT_EQ(cleared.status, cxxime::IPCStatus::OK);
        ASSERT_TRUE(!cleared.composing);
        ASSERT_GT(cleared.candidate_revision, typed.candidate_revision);

        const ProcessKeyResult focused = manager.focus_in(id);
        ASSERT_EQ(focused.status, cxxime::IPCStatus::OK);
        ASSERT_EQ(focused.candidate_revision, cleared.candidate_revision);

        const ProcessKeyResult retyped = type_code(manager, id, "huaruijishu");
        const ProcessKeyResult focused_out = manager.focus_out(id);
        ASSERT_EQ(focused_out.status, cxxime::IPCStatus::OK);
        ASSERT_TRUE(!focused_out.composing);
        ASSERT_GT(focused_out.candidate_revision, retyped.candidate_revision);
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionSegmented, config_reload_invalidates_the_visible_page) {
    const std::string dict_path = setup_segmented_session_dict();
    {
        SessionManager manager;
        ASSERT_TRUE(manager.initialize(dict_path));
        const uint32_t id = manager.create_session(cxxime::kClientCapabilitySegmentedSelection);

        const ProcessKeyResult typed = type_code(manager, id, "huaruijishu");
        manager.apply_config(std::make_shared<const cxxime::Config>());
        const ProcessKeyResult config_stale =
            manager.select_candidate(id, 0, typed.candidate_revision);
        ASSERT_EQ(config_stale.status, cxxime::IPCStatus::ERR_STALE_CANDIDATE);
        ASSERT_GT(config_stale.candidate_revision, typed.candidate_revision);
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionSegmented, dictionary_reload_invalidates_the_visible_page) {
    const std::string dict_path = setup_segmented_session_dict();
    {
        SessionManager manager;
        ASSERT_TRUE(manager.initialize(dict_path));
        const uint32_t id = manager.create_session(cxxime::kClientCapabilitySegmentedSelection);

        const ProcessKeyResult typed = type_code(manager, id, "huaruijishu");
        ASSERT_EQ(manager.reload_dictionaries(), cxxime::IPCStatus::OK);
        const ProcessKeyResult dictionary_stale =
            manager.select_candidate(id, 0, typed.candidate_revision);
        ASSERT_EQ(dictionary_stale.status, cxxime::IPCStatus::ERR_STALE_CANDIDATE);
        ASSERT_GT(dictionary_stale.candidate_revision, typed.candidate_revision);
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionSegmented, selection_aligns_before_validating_an_old_revision) {
    const std::string dict_path = setup_segmented_session_dict();
    {
        SessionManager manager;
        ASSERT_TRUE(manager.initialize(dict_path));
        const uint32_t composing =
            manager.create_session(cxxime::kClientCapabilitySegmentedSelection);
        const uint32_t controller =
            manager.create_session(cxxime::kClientCapabilitySegmentedSelection);

        const ProcessKeyResult typed = type_code(manager, composing, "huaruijishu");
        ASSERT_TRUE(typed.composing);
        // Normally FOCUS_OUT cancels this composition first. This ordering models a candidate
        // click that was already in flight on another pipe when the input mode changed.
        ASSERT_EQ(manager.switch_input_mode(controller, cxxime::InputMode::WUBI).first,
                  cxxime::IPCStatus::OK);

        const ProcessKeyResult stale =
            manager.select_candidate(composing, 0, typed.candidate_revision);
        ASSERT_EQ(stale.status, cxxime::IPCStatus::ERR_STALE_CANDIDATE);
        ASSERT_TRUE(stale.commit_text.empty());
        ASSERT_TRUE(!stale.composing);
        ASSERT_GT(stale.candidate_revision, typed.candidate_revision);
    }
    delete_test_dictionary_bundle(dict_path);
}

TEST(SessionSegmented, prior_status_alignment_still_rejects_an_old_revision) {
    const std::string dict_path = setup_segmented_session_dict();
    {
        SessionManager manager;
        ASSERT_TRUE(manager.initialize(dict_path));
        const uint32_t composing =
            manager.create_session(cxxime::kClientCapabilitySegmentedSelection);
        const uint32_t controller =
            manager.create_session(cxxime::kClientCapabilitySegmentedSelection);

        const ProcessKeyResult typed = type_code(manager, composing, "huaruijishu");
        ASSERT_TRUE(typed.composing);
        // Normally FOCUS_OUT cancels this composition first. This ordering models a delayed
        // focus notification followed by status synchronization on another pipe.
        ASSERT_EQ(manager.switch_input_mode(controller, cxxime::InputMode::WUBI).first,
                  cxxime::IPCStatus::OK);
        const auto [status, ime_status] = manager.get_ime_status(composing);
        ASSERT_EQ(status, cxxime::IPCStatus::OK);
        ASSERT_EQ(ime_status.input_mode, cxxime::InputMode::WUBI);

        const ProcessKeyResult stale =
            manager.select_candidate(composing, 0, typed.candidate_revision);
        ASSERT_EQ(stale.status, cxxime::IPCStatus::ERR_STALE_CANDIDATE);
        ASSERT_TRUE(stale.commit_text.empty());
        ASSERT_TRUE(!stale.composing);
        ASSERT_GT(stale.candidate_revision, typed.candidate_revision);
    }
    delete_test_dictionary_bundle(dict_path);
}
