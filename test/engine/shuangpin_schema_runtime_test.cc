// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <algorithm>
#include <memory>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <windows.h>

#include <cxxime/composition_presentation.h>
#include <cxxime/dict.h>
#include <cxxime/engine.h>
#include <cxxime/engine_runtime.h>
#include <cxxime/pinyin_resource.h>

#include "support/testutil.h"

namespace {

struct CompositionCase {
    const char* scheme;
    const char* spellings;
    const char* input;
    const char* display;
    const char* remainder_display;
};

const CompositionCase kCompositionCases[] = {
    {"microsoft_shuangpin", CXXIME_MICROSOFT_SHUANGPIN_PATH, "qiuimksiuib", "qi'ui'mk'si'ui'b",
     "mk'si'ui'b"},
    {"xiaohe_shuangpin", CXXIME_XIAOHE_SHUANGPIN_PATH, "qiuimcsiuib", "qi'ui'mc'si'ui'b",
     "mc'si'ui'b"},
    {"ziranma_shuangpin", CXXIME_ZIRANMA_SHUANGPIN_PATH, "qiuimksiuib", "qi'ui'mk'si'ui'b",
     "mk'si'ui'b"},
    {"sogou_shuangpin", CXXIME_SOGOU_SHUANGPIN_PATH, "qiuimksiuib", "qi'ui'mk'si'ui'b",
     "mk'si'ui'b"},
};

struct CompositionFixture {
    std::string dictionary_path;
    std::shared_ptr<cxxime::Dict> dictionary =
        std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    std::shared_ptr<const cxxime::PinyinResourceSet> resources;
    cxxime::Engine engine;

    explicit CompositionFixture(const CompositionCase& item) {
        char directory[MAX_PATH] = {};
        char path[MAX_PATH] = {};
        ASSERT_TRUE(GetTempPathA(MAX_PATH, directory) != 0);
        ASSERT_TRUE(GetTempFileNameA(directory, "spc", 0, path) != 0);
        dictionary_path = path;
        const std::vector<std::tuple<std::string, std::string, int>> entries = {
            {"qi:shi", "其实", 1000}, {"qi:shi", "骑士", 500}, {"qi", "气", 800},
            {"mao:si", "貌似", 1000}, {"shi", "是", 1000},     {"bu", "不", 1000},
            {"ba", "吧", 800},
        };
        ASSERT_TRUE(cxxime::Dict::create_test_dict(dictionary_path, entries));
        ASSERT_TRUE(dictionary->open_dict(dictionary_path));
        cxxime::Config config;
        config.pinyin_scheme = item.scheme;
        config.page_size = 10;
        config.candidate_learning = false;
        resources = cxxime::PinyinResourceSet::create(
            item.scheme, cxxime::PinyinSchemeKind::kShuangpin, item.spellings,
            cxxime::PinyinSpellingRequirement::kRequired);
        ASSERT_TRUE(resources != nullptr);
        ASSERT_TRUE(engine.initialize(
            cxxime::EngineRuntimeState::create(config, dictionary, nullptr, resources)));
        engine.set_query_deadline_ms(0);
        engine.set_partial_selection_enabled(true);
    }

    ~CompositionFixture() {
        engine.finalize();
        dictionary->close();
        DeleteFileA(dictionary_path.c_str());
    }

    void press(uint32_t keycode) {
        cxxime::KeyEvent key;
        key.keycode = keycode;
        ASSERT_EQ(engine.process_key(key), cxxime::ProcessResult::ACCEPTED);
    }

    void type(const std::string& input) {
        for (char character : input) {
            press(static_cast<uint32_t>(character - 'a' + 'A'));
        }
    }

    std::size_t index_of(const std::string& text) const {
        const auto& entries = engine.context().translation().entries;
        const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
            return entry.candidate.text == text;
        });
        ASSERT_TRUE(found != entries.end());
        return static_cast<std::size_t>(found - entries.begin());
    }

    const cxxime::TextSelectionAction& action_for(const std::string& text) const {
        const auto& entries = engine.context().translation().entries;
        return std::get<cxxime::TextSelectionAction>(entries[index_of(text)].selection);
    }

    cxxime::CompositionPresentation presentation(const std::string& text) const {
        const auto& action = action_for(text);
        return cxxime::derive_composition_presentation(
            engine.context().composition(), resources.get(), action.consumed_input_bytes, true,
            action.variants[action.primary_variant].syllables, true);
    }

    void select(const std::string& text) {
        ASSERT_TRUE(engine.select_candidate(static_cast<int>(index_of(text))));
    }
};

} // namespace

TEST(ShuangpinSchemaRuntime, generated_schema_loads_and_segments_through_runtime_index) {
    auto pinyin_resources = cxxime::PinyinResourceSet::create(
        "synthetic", cxxime::PinyinSchemeKind::kShuangpin, CXXIME_SYNTHETIC_SHUANGPIN_PATH,
        cxxime::PinyinSpellingRequirement::kRequired);
    ASSERT_TRUE(pinyin_resources != nullptr);

    const auto initial = pinyin_resources->prefix_search("xa");
    ASSERT_TRUE(std::any_of(initial.begin(), initial.end(), [](const auto& match) {
        return match.syllable == "ni" && match.type == cxxime::kNormalSpelling;
    }));
    const auto final = pinyin_resources->prefix_search("zb");
    ASSERT_TRUE(std::any_of(final.begin(), final.end(), [](const auto& match) {
        return match.syllable == "hao" && match.type == cxxime::kNormalSpelling;
    }));

    cxxime::SyllabifierOptions options;
    options.collect_path_metadata = true;
    const cxxime::SegmentResult result = pinyin_resources->segment("xazb", nullptr, options);
    const auto path = std::find_if(result.paths.begin(), result.paths.end(), [](const auto& item) {
        return item.syllables == std::vector<std::string>{"ni", "hao"};
    });
    ASSERT_TRUE(path != result.paths.end());
    ASSERT_EQ(std::accumulate(path->input_lengths.begin(), path->input_lengths.end(), 0u), 4u);
}

TEST(ShuangpinSchemaRuntime, built_in_schemes_segment_full_and_zero_initial_syllables) {
    struct SchemeCase {
        const char* id;
        const char* path;
        const char* nihao;
        const char* zero_initial_hao;
    };
    const SchemeCase cases[] = {
        {"microsoft", CXXIME_MICROSOFT_SHUANGPIN_PATH, "nihk", "ajhk"},
        {"xiaohe", CXXIME_XIAOHE_SHUANGPIN_PATH, "nihc", "anhc"},
        {"ziranma", CXXIME_ZIRANMA_SHUANGPIN_PATH, "nihk", "anhk"},
        {"sogou", CXXIME_SOGOU_SHUANGPIN_PATH, "nihk", "ojhk"},
    };

    for (const auto& item : cases) {
        auto pinyin_resources = cxxime::PinyinResourceSet::create(
            item.id, cxxime::PinyinSchemeKind::kShuangpin, item.path,
            cxxime::PinyinSpellingRequirement::kRequired);
        ASSERT_TRUE(pinyin_resources != nullptr);

        for (const auto& path_case : {
                 std::make_pair(std::string(item.nihao), std::vector<std::string>{"ni", "hao"}),
                 std::make_pair(std::string(item.zero_initial_hao),
                                std::vector<std::string>{"an", "hao"}),
             }) {
            cxxime::SyllabifierOptions options;
            options.collect_path_metadata = true;
            const cxxime::SegmentResult result =
                pinyin_resources->segment(path_case.first, nullptr, options);
            const auto path =
                std::find_if(result.paths.begin(), result.paths.end(), [&](const auto& candidate) {
                    return candidate.syllables == path_case.second;
                });
            ASSERT_TRUE(path != result.paths.end());
            ASSERT_EQ(std::accumulate(path->input_lengths.begin(), path->input_lengths.end(), 0u),
                      path_case.first.size());
        }
    }
}

TEST(ShuangpinSchemaRuntime, terminal_completion_keeps_normal_pairs_and_actual_key_spans) {
    for (const auto& item : kCompositionCases) {
        CompositionFixture fixture(item);
        fixture.type(item.input);
        const auto sentence = fixture.presentation("其实貌似是不");
        ASSERT_EQ(sentence.display_preedit, item.display);
        ASSERT_EQ(sentence.logical_preedit, item.input);
        ASSERT_EQ(fixture.action_for("其实貌似是不").consumed_input_bytes, 11u);
        ASSERT_EQ(fixture.action_for("其实貌似是不").variants[0].syllables, "qi:shi:mao:si:shi:bu");
        ASSERT_EQ(fixture.presentation("其实").focused_preedit_end_bytes, 5u);
        ASSERT_EQ(fixture.presentation("气").focused_preedit_end_bytes, 2u);
        ASSERT_EQ(fixture.presentation("骑士").display_preedit, item.display);

        fixture.press('U');
        ASSERT_EQ(fixture.presentation("其实貌似是不").display_preedit,
                  std::string(item.display) + "u");
        ASSERT_EQ(fixture.action_for("其实貌似是不").consumed_input_bytes, 12u);
        fixture.press(VK_BACK);
        ASSERT_EQ(fixture.presentation("其实貌似是不").display_preedit, item.display);
        fixture.select("其实貌似是不");
        ASSERT_EQ(fixture.engine.get_commit_text(), "其实貌似是不");
        ASSERT_TRUE(!fixture.engine.context().is_composing());
    }
}

TEST(ShuangpinSchemaRuntime, partial_selection_preserves_prefix_tail_and_reopened_raw_keys) {
    for (const auto& item : kCompositionCases) {
        for (const auto& selected : {std::string("其实"), std::string("气")}) {
            CompositionFixture fixture(item);
            fixture.type(item.input);
            const auto original = fixture.presentation(selected);
            ASSERT_EQ(original.display_preedit, item.display);
            const std::size_t consumed = fixture.action_for(selected).consumed_input_bytes;
            ASSERT_EQ(consumed, selected == "其实" ? 4u : 2u);
            fixture.select(selected);
            const std::string tail = std::string(item.input).substr(consumed);
            const auto& state = fixture.engine.context().composition();
            ASSERT_EQ(state.active().input, tail);
            const auto presentation = cxxime::derive_composition_presentation(
                state, fixture.resources.get(), tail.size(), true, {}, true);
            ASSERT_EQ(presentation.logical_preedit, selected + tail);
            ASSERT_EQ(presentation.display_preedit,
                      selected + (selected == "其实" ? "" : "ui'") + item.remainder_display);
            ASSERT_EQ(presentation.display_converted_prefix_bytes, selected.size());

            // Moving to the start and deleting reopens the selected segment for correction.
            for (std::size_t key = 0; key < tail.size(); ++key) {
                fixture.press(VK_LEFT);
            }
            fixture.press(VK_BACK);
            ASSERT_EQ(fixture.engine.context().active_input(), item.input);
            ASSERT_TRUE(fixture.engine.context().composition().converted_segments().empty());
            ASSERT_EQ(fixture.presentation("其实").display_preedit, item.display);
        }
    }
}

TEST(ShuangpinSchemaRuntime, production_dictionary_keeps_candidates_through_a_long_sentence) {
    auto dictionary = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    ASSERT_TRUE(dictionary->open_dict(CXXIME_DATA_DIR "pinyin.dict.bin"));
    auto resources =
        cxxime::PinyinResourceSet::create("sogou_shuangpin", cxxime::PinyinSchemeKind::kShuangpin,
                                          CXXIME_DATA_DIR "pinyin.sogou-shuangpin.spellings.bin");
    ASSERT_TRUE(resources != nullptr);
    cxxime::Config config;
    config.pinyin_scheme = "sogou_shuangpin";
    config.candidate_learning = false;
    config.page_size = 7;
    cxxime::Engine engine;
    ASSERT_TRUE(engine.initialize(
        cxxime::EngineRuntimeState::create(config, dictionary, nullptr, resources)));
    engine.set_partial_selection_enabled(true);
    engine.set_query_deadline_ms(0);
    const std::string input = "hryigeuurufaqiuivgihuibuhvveyhde";
    for (char character : input) {
        cxxime::KeyEvent key;
        key.keycode = static_cast<uint32_t>(character - 'a' + 'A');
        ASSERT_EQ(engine.process_key(key), cxxime::ProcessResult::ACCEPTED);
        ASSERT_TRUE(!engine.context().translation().entries.empty());
        ASSERT_TRUE(engine.context().translation().usable());
    }
    const std::string expected = "换一个输入法其实正常是不会这样的";
    const auto& entries = engine.context().translation().entries;
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
        return entry.candidate.text == expected;
    });
    ASSERT_TRUE(found != entries.end());
    ASSERT_EQ(std::get<cxxime::TextSelectionAction>(found->selection).consumed_input_bytes,
              input.size());
    ASSERT_TRUE(engine.select_candidate(static_cast<int>(found - entries.begin())));
    ASSERT_EQ(engine.get_commit_text(), expected);
    ASSERT_TRUE(!engine.context().is_composing());

    for (char character : input) {
        cxxime::KeyEvent key;
        key.keycode = static_cast<uint32_t>(character - 'a' + 'A');
        ASSERT_EQ(engine.process_key(key), cxxime::ProcessResult::ACCEPTED);
    }
    for (std::size_t remaining = input.size(); remaining > 1; --remaining) {
        cxxime::KeyEvent key;
        key.keycode = VK_BACK;
        ASSERT_EQ(engine.process_key(key), cxxime::ProcessResult::ACCEPTED);
        ASSERT_EQ(engine.context().active_input(), input.substr(0, remaining - 1));
        ASSERT_TRUE(!engine.context().translation().entries.empty());
    }
}

int main() { return test::RunAllTests(); }
