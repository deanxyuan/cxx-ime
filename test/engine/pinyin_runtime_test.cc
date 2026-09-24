// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <atomic>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include <windows.h>

#include <cxxime/composition_learning.h>
#include <cxxime/composition_presentation.h>
#include <cxxime/composition_state.h>
#include <cxxime/config.h>
#include <cxxime/dict.h>
#include <cxxime/engine.h>
#include <cxxime/engine_runtime.h>
#include <cxxime/pinyin_resource.h>
#include <cxxime/spellings_index.h>
#include <cxxime/symbol_table.h>

#include "support/testutil.h"

namespace {

std::string runtime_temp_path(const char* prefix) {
    char directory[MAX_PATH] = {};
    char path[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, directory) == 0 ||
        GetTempFileNameA(directory, prefix, 0, path) == 0) {
        return {};
    }
    return path;
}

void type_code(cxxime::Engine& engine, const std::string& code) {
    for (char character : code) {
        cxxime::KeyEvent event;
        event.keycode = static_cast<uint32_t>(character - 'a' + 'A');
        ASSERT_EQ(engine.process_key(event), cxxime::ProcessResult::ACCEPTED);
    }
}

bool has_candidate(const cxxime::Engine& engine, const std::string& text) {
    const auto& candidates = engine.context().candidate_page().candidates;
    return std::any_of(candidates.begin(), candidates.end(),
                       [&](const auto& candidate) { return candidate.text == text; });
}

} // namespace

TEST(PinyinRuntime, resource_factory_enforces_spelling_requirements) {
    const std::string missing = runtime_temp_path("prm");
    ASSERT_TRUE(!missing.empty());
    DeleteFileA(missing.c_str());

    ASSERT_TRUE(cxxime::PinyinResourceSet::create(
                    "full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin, missing,
                    cxxime::PinyinSpellingRequirement::kRequired) == nullptr);
    const auto fallback = cxxime::PinyinResourceSet::create(
        "full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin, missing,
        cxxime::PinyinSpellingRequirement::kOptionalForFullPinyin);
    ASSERT_TRUE(fallback != nullptr);
    ASSERT_TRUE(!fallback->has_spellings());
    ASSERT_TRUE(cxxime::PinyinResourceSet::create(
                    "microsoft_shuangpin", cxxime::PinyinSchemeKind::kShuangpin, missing,
                    cxxime::PinyinSpellingRequirement::kOptionalForFullPinyin) == nullptr);
}

TEST(PinyinRuntime, factory_rejects_mismatched_resource_and_dictionary_identity) {
    const std::string dict_path = runtime_temp_path("prd");
    const std::string spellings_path = runtime_temp_path("prs");
    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, {{"ni", "candidate", 100}}));
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(
        spellings_path, {{"ni", "ni", cxxime::kNormalSpelling, 0.0f}}));

    auto pinyin_dict = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    auto wrong_pinyin_dict = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::WUBI);
    auto wrong_wubi_dict = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    ASSERT_TRUE(pinyin_dict->open_dict(dict_path));
    ASSERT_TRUE(wrong_pinyin_dict->open_dict(dict_path));
    ASSERT_TRUE(wrong_wubi_dict->open_dict(dict_path));
    auto full_resources = cxxime::PinyinResourceSet::create(
        "full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin, spellings_path);
    auto shuangpin_resources = cxxime::PinyinResourceSet::create(
        "microsoft_shuangpin", cxxime::PinyinSchemeKind::kShuangpin, spellings_path);
    ASSERT_TRUE(full_resources != nullptr);
    ASSERT_TRUE(shuangpin_resources != nullptr);

    cxxime::Config config;
    ASSERT_TRUE(cxxime::EngineRuntimeState::create(config, pinyin_dict, nullptr,
                                                   shuangpin_resources) == nullptr);
    ASSERT_TRUE(cxxime::EngineRuntimeState::create(config, wrong_pinyin_dict, nullptr,
                                                   full_resources) == nullptr);
    ASSERT_TRUE(cxxime::EngineRuntimeState::create(config, pinyin_dict, wrong_wubi_dict,
                                                   full_resources) == nullptr);
    ASSERT_TRUE(cxxime::EngineRuntimeState::create(config, pinyin_dict, nullptr, full_resources) !=
                nullptr);

    pinyin_dict->close();
    wrong_pinyin_dict->close();
    wrong_wubi_dict->close();
    full_resources.reset();
    shuangpin_resources.reset();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(PinyinRuntime, shared_resources_keep_fuzzy_policy_per_engine) {
    const std::string dict_path = runtime_temp_path("pdf");
    const std::string spellings_path = runtime_temp_path("psf");
    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path,
                                               {{"zong", "exact", 100}, {"zhong", "fuzzy", 1000}}));
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(
        spellings_path, {{"zong", "zong", cxxime::kNormalSpelling, 0.0f},
                         {"zong", "zhong", cxxime::kFuzzySpelling, -0.5f}}));

    auto pinyin_dict = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    ASSERT_TRUE(pinyin_dict->open_dict(dict_path));
    auto pinyin_resources = cxxime::PinyinResourceSet::create(
        "full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin, spellings_path);
    ASSERT_TRUE(pinyin_resources != nullptr);

    cxxime::Config fuzzy_config;
    fuzzy_config.page_size = 10;
    cxxime::Config exact_config = fuzzy_config;
    exact_config.fuzzy_pinyin = false;
    cxxime::Engine fuzzy_engine;
    cxxime::Engine exact_engine;
    ASSERT_TRUE(fuzzy_engine.initialize(
        cxxime::EngineRuntimeState::create(fuzzy_config, pinyin_dict, nullptr, pinyin_resources)));
    ASSERT_TRUE(exact_engine.initialize(
        cxxime::EngineRuntimeState::create(exact_config, pinyin_dict, nullptr, pinyin_resources)));

    fuzzy_engine.set_query_deadline_ms(0);
    exact_engine.set_query_deadline_ms(0);
    std::atomic<int> ready{0};
    bool fuzzy_ok = true;
    bool exact_ok = true;
    auto query = [&](cxxime::Engine& engine, bool expect_fuzzy, bool& ok) {
        for (int round = 0; round < 128; ++round) {
            engine.clear();
            engine.clear_query_cache();
            ready.fetch_add(1);
            while (ready.load() < (round + 1) * 2) {
                std::this_thread::yield();
            }
            for (char character : std::string("zong")) {
                cxxime::KeyEvent event;
                event.keycode = static_cast<uint32_t>(character - 'a' + 'A');
                ok = (engine.process_key(event) == cxxime::ProcessResult::ACCEPTED) && ok;
            }
            ok = has_candidate(engine, "exact") &&
                 (has_candidate(engine, "fuzzy") == expect_fuzzy) && ok;
        }
    };
    std::thread fuzzy_worker([&]() { query(fuzzy_engine, true, fuzzy_ok); });
    std::thread exact_worker([&]() { query(exact_engine, false, exact_ok); });
    fuzzy_worker.join();
    exact_worker.join();
    ASSERT_TRUE(fuzzy_ok);
    ASSERT_TRUE(exact_ok);

    fuzzy_engine.finalize();
    exact_engine.finalize();
    pinyin_dict->close();
    pinyin_resources.reset();
    DeleteFileA(dict_path.c_str());
    DeleteFileA(spellings_path.c_str());
}

TEST(PinyinRuntime, composition_presentation_uses_the_query_fuzzy_policy) {
    const std::string spellings_path = runtime_temp_path("ppf");
    ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(
        spellings_path, {{"zong", "zhong", cxxime::kFuzzySpelling, -0.5f},
                         {"guo", "guo", cxxime::kNormalSpelling, 0.0f}}));
    auto pinyin_resources = cxxime::PinyinResourceSet::create(
        "full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin, spellings_path);
    ASSERT_TRUE(pinyin_resources != nullptr);
    cxxime::CompositionState state;
    ASSERT_TRUE(state.set_active_input("zongguo", 7));

    const auto fuzzy = cxxime::derive_composition_presentation(state, pinyin_resources.get(), 7,
                                                               true, {}, false, true);
    const auto exact = cxxime::derive_composition_presentation(state, pinyin_resources.get(), 7,
                                                               true, {}, false, false);
    ASSERT_EQ(fuzzy.display_preedit, "zong'guo");
    ASSERT_EQ(exact.display_preedit, "zongguo");

    pinyin_resources.reset();
    DeleteFileA(spellings_path.c_str());
}

TEST(PinyinRuntime, standalone_full_pinyin_without_spellings_uses_static_segmentation) {
    const std::string dict_path = runtime_temp_path("pff");
    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, {{"ni:hao", "hello", 100}}));
    ASSERT_EQ(GetFileAttributesA(cxxime::Engine::derive_spellings_path(dict_path).c_str()),
              INVALID_FILE_ATTRIBUTES);
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(dict_path));
    engine.set_query_deadline_ms(0);
    type_code(engine, "nihao");
    ASSERT_TRUE(has_candidate(engine, "hello"));
    ASSERT_TRUE(engine.select_candidate(0));
    ASSERT_EQ(engine.get_commit_text(), "hello");
    engine.finalize();
    ASSERT_TRUE(DeleteFileA(dict_path.c_str()));
}

TEST(PinyinRuntime, standalone_shuangpin_without_spellings_fails_for_every_scheme) {
    const std::string directory = runtime_temp_path("psm");
    ASSERT_TRUE(DeleteFileA(directory.c_str()));
    ASSERT_TRUE(CreateDirectoryA(directory.c_str(), nullptr));
    const std::string dict_path = directory + "\\pinyin.dict.bin";
    const std::string config_path = directory + "\\config.json";
    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, {{"ni:hao", "hello", 100}}));
    for (const char* scheme :
         {"microsoft_shuangpin", "xiaohe_shuangpin", "ziranma_shuangpin", "sogou_shuangpin"}) {
        cxxime::Config config;
        config.pinyin_scheme = scheme;
        {
            std::ofstream output(config_path);
            output << config.to_runtime_json();
            ASSERT_TRUE(output.good());
        }
        cxxime::Engine engine;
        ASSERT_TRUE(!engine.initialize(dict_path, config_path)) << scheme;
    }
    ASSERT_TRUE(DeleteFileA(config_path.c_str()));
    ASSERT_TRUE(DeleteFileA(dict_path.c_str()));
    ASSERT_TRUE(RemoveDirectoryA(directory.c_str()));
}

TEST(PinyinRuntime, engine_keeps_owned_dependencies_until_runtime_replacement_or_finalization) {
    const std::string dict_path = runtime_temp_path("pol");
    ASSERT_TRUE(cxxime::Dict::create_test_dict(dict_path, {{"ni", "candidate", 100}}));
    auto dict = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    auto wubi = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::WUBI);
    ASSERT_TRUE(dict->open_dict(dict_path));
    ASSERT_TRUE(wubi->open_dict(dict_path));
    auto symbols = std::make_shared<cxxime::SymbolTable>();
    auto learning = std::make_shared<cxxime::CompositionLearningService>();
    auto resources = cxxime::PinyinResourceSet::create(
        "full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin, {},
        cxxime::PinyinSpellingRequirement::kOptionalForFullPinyin);
    auto runtime = cxxime::EngineRuntimeState::create({}, dict, wubi, resources, symbols, learning);
    ASSERT_TRUE(runtime != nullptr);
    std::weak_ptr<cxxime::Dict> weak_dict = dict;
    std::weak_ptr<cxxime::Dict> weak_wubi = wubi;
    std::weak_ptr<cxxime::SymbolTable> weak_symbols = symbols;
    std::weak_ptr<cxxime::CompositionLearningService> weak_learning = learning;
    std::weak_ptr<const cxxime::PinyinResourceSet> weak_resources = resources;
    std::weak_ptr<const cxxime::EngineRuntimeState> weak_runtime = runtime;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(runtime));
    dict.reset();
    wubi.reset();
    symbols.reset();
    learning.reset();
    resources.reset();
    runtime.reset();
    ASSERT_TRUE(!weak_dict.expired() && !weak_wubi.expired() && !weak_symbols.expired() &&
                !weak_learning.expired() && !weak_resources.expired() && !weak_runtime.expired());
    type_code(engine, "ni");
    ASSERT_TRUE(has_candidate(engine, "candidate"));

    auto next_dict = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    ASSERT_TRUE(next_dict->open_dict(dict_path));
    auto next_resources = cxxime::PinyinResourceSet::create(
        "full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin, {},
        cxxime::PinyinSpellingRequirement::kOptionalForFullPinyin);
    auto next = cxxime::EngineRuntimeState::create({}, next_dict, nullptr, next_resources);
    ASSERT_TRUE(engine.apply_runtime_state(next));
    ASSERT_TRUE(weak_dict.expired() && weak_wubi.expired() && weak_symbols.expired() &&
                weak_learning.expired() && weak_resources.expired() && weak_runtime.expired());
    weak_dict = next_dict;
    weak_resources = next_resources;
    weak_runtime = next;
    next_dict.reset();
    next_resources.reset();
    next.reset();
    engine.clear();
    type_code(engine, "ni");
    ASSERT_TRUE(has_candidate(engine, "candidate"));
    engine.finalize();
    ASSERT_TRUE(weak_dict.expired() && weak_resources.expired() && weak_runtime.expired());
    ASSERT_TRUE(DeleteFileA(dict_path.c_str()));
}
