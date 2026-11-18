// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEST_SUPPORT_SHUANGPIN_FALLBACK_FIXTURE_H_
#define CXXIME_TEST_SUPPORT_SHUANGPIN_FALLBACK_FIXTURE_H_

#include <algorithm>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <windows.h>

#include <cxxime/composition_presentation.h>
#include <cxxime/engine.h>
#include <cxxime/pinyin_resource.h>
#include <cxxime/query_budget.h>
#include <cxxime/translator.h>

#include "testutil.h"

namespace test {

// Canonical readings, visible word, source frequency.
using FallbackDictionaryRow = std::tuple<std::string, std::string, int>;

struct FallbackScheme {
    const char* id;
    const char* path;
};

inline const std::vector<FallbackScheme>& fallback_schemes() {
    static const std::vector<FallbackScheme> schemes = {
        {"microsoft_shuangpin", CXXIME_MICROSOFT_SHUANGPIN_PATH},
        {"xiaohe_shuangpin", CXXIME_XIAOHE_SHUANGPIN_PATH},
        {"ziranma_shuangpin", CXXIME_ZIRANMA_SHUANGPIN_PATH},
        {"sogou_shuangpin", CXXIME_SOGOU_SHUANGPIN_PATH},
    };
    return schemes;
}

struct FallbackFixture {
    std::string path;
    std::shared_ptr<cxxime::Dict> dict =
        std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    std::shared_ptr<const cxxime::PinyinResourceSet> resources;
    cxxime::Engine engine;
    cxxime::PinyinTranslator translator;
    cxxime::Config config;

    explicit FallbackFixture(const std::vector<FallbackDictionaryRow>& rows,
                             const FallbackScheme& scheme = fallback_schemes().back(),
                             int page_size = 7) {
        char directory[MAX_PATH] = {};
        char temporary[MAX_PATH] = {};
        ASSERT_TRUE(GetTempPathA(MAX_PATH, directory) != 0);
        ASSERT_TRUE(GetTempFileNameA(directory, "sfg", 0, temporary) != 0);
        path = temporary;
        ASSERT_TRUE(cxxime::Dict::create_test_dict(path, rows));
        ASSERT_TRUE(dict->open_dict(path));
        resources = cxxime::PinyinResourceSet::create(
            scheme.id, cxxime::PinyinSchemeKind::kShuangpin, scheme.path);
        ASSERT_TRUE(resources != nullptr);
        config.pinyin_scheme = scheme.id;
        config.page_size = page_size;
        config.candidate_learning = false;
        ASSERT_TRUE(engine.initialize(
            cxxime::EngineRuntimeState::create(config, dict, nullptr, resources)));
        engine.set_partial_selection_enabled(true);
        engine.set_query_deadline_ms(0);
        translator.set_dict(dict.get());
        translator.bind_pinyin(resources, {false});
    }

    ~FallbackFixture() {
        engine.finalize();
        dict->close();
        DeleteFileA(path.c_str());
        DeleteFileA((path + ".learning").c_str());
        DeleteFileA((path + ".manual").c_str());
    }

    void press(uint32_t key) {
        cxxime::KeyEvent event;
        event.keycode = key;
        engine.process_key(event);
    }

    void type(const std::string& input) {
        for (char key : input) {
            press(key == ';' ? VK_OEM_1 : static_cast<uint32_t>(key - 'a' + 'A'));
        }
    }

    const cxxime::CandidateEntry* find(const std::string& text, std::size_t consumed) const {
        const auto& entries = engine.context().translation().entries;
        const auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
            const auto* action = std::get_if<cxxime::TextSelectionAction>(&entry.selection);
            return entry.candidate.text == text && action &&
                   action->consumed_input_bytes == consumed;
        });
        return it == entries.end() ? nullptr : &*it;
    }

    cxxime::TranslationResult
    query(const std::string& input, bool partial = true,
          const cxxime::QueryBudget* budget = nullptr,
          const std::vector<cxxime::CandidateEntry>* published = nullptr) {
        cxxime::TranslationRequest request;
        request.input = input;
        request.policy.allow_partial_selection = partial;
        request.budget = budget;
        request.published = published;
        return translator.translate(request);
    }
};

} // namespace test

#endif // CXXIME_TEST_SUPPORT_SHUANGPIN_FALLBACK_FIXTURE_H_
