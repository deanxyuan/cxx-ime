// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "engine_test_support.h"

#include <cxxime/composition_presentation.h>
#include <cxxime/mixed_translator.h>
#include <cxxime/pinyin_composer.h>
#include <cxxime/pinyin_input_graph.h>

namespace {

// Fixture records: canonical syllables, visible text, source frequency.
using DictionaryEntry = std::tuple<std::string, std::string, int>;
// Fixture spellings: raw key, canonical syllable, spelling type, credibility.
using SpellingEntry = std::tuple<std::string, std::string, int, float>;

constexpr const char* kDenseInput = "woaizhrm";
constexpr const char* kDenseReading = "wo:ai:zhong:hua:ren:min";
constexpr const char* kDenseText = "我爱中华人民";

struct MixedFixture {
    std::string path;
    std::shared_ptr<cxxime::Dict> dict =
        std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    std::shared_ptr<const cxxime::PinyinResourceSet> resources;

    void load(const std::vector<DictionaryEntry>& entries,
              const std::vector<SpellingEntry>& spellings) {
        char directory[MAX_PATH] = {};
        char file[MAX_PATH] = {};
        ASSERT_TRUE(GetTempPathA(MAX_PATH, directory) != 0);
        ASSERT_TRUE(GetTempFileNameA(directory, "pma", 0, file) != 0);
        path = file;
        ASSERT_TRUE(cxxime::Dict::create_test_dict(path, entries));
        ASSERT_TRUE(dict->open_dict(path));
        ASSERT_TRUE(cxxime::SpellingsIndex::create_test_trie(path + ".spellings", spellings));
        resources = cxxime::PinyinResourceSet::create(
            "full_pinyin", cxxime::PinyinSchemeKind::kFullPinyin, path + ".spellings");
        ASSERT_TRUE(resources != nullptr);
    }

    void initialize(bool complete_word = false) {
        std::vector<DictionaryEntry> entries = {
            {"wo:ai", "我爱", 9000},    {"zhong:hua", "中华", 8000}, {"zhong:hua", "种花", 7000},
            {"ren:min", "人民", 15000}, {"ren", "人", 30000},        {"ma", "吗", 30000},
        };
        std::vector<SpellingEntry> spellings = {
            {"wo", "wo", cxxime::kNormalSpelling, 0.0f},
            {"ai", "ai", cxxime::kNormalSpelling, 0.0f},
        };
        // 8^4 complete readings. Most are real dictionary prefixes, but they
        // cannot continue with the next raw letter. Only range pruning finds
        // the useful late path without enumerating those complete readings.
        const std::vector<std::vector<std::string>> alternatives = {
            {"za", "zai", "zan", "zao", "ze", "zei", "zeng", "zhong"},
            {"ha", "hai", "han", "hang", "hao", "he", "heng", "hua"},
            {"ran", "rang", "rao", "re", "reng", "ri", "rong", "ren"},
            {"ma", "mai", "man", "mang", "mao", "mei", "meng", "min"},
        };
        const std::string initials = "zhrm";
        for (std::size_t index = 0; index < alternatives.size(); ++index) {
            for (const auto& syllable : alternatives[index]) {
                spellings.emplace_back(initials.substr(index, 1), syllable, cxxime::kAbbreviation,
                                       -0.693f);
                entries.emplace_back(syllable + ":xi", "unused", 1);
            }
        }
        if (complete_word) {
            entries.emplace_back(kDenseReading, kDenseText, 20000);
        }
        load(entries, spellings);
    }

    void bind(cxxime::PinyinTranslator& translator) {
        translator.set_dict(dict.get());
        translator.bind_pinyin(resources, {});
    }

    ~MixedFixture() {
        dict->close();
        for (const char* suffix :
             {"", ".idx", ".spellings", ".disabled", ".user", ".prefs", ".wubi"}) {
            DeleteFileA((path + suffix).c_str());
        }
    }
};

const cxxime::CandidateEntry* composed(const cxxime::TranslationResult& result) {
    const auto found =
        std::find_if(result.entries.begin(), result.entries.end(), [](const auto& entry) {
            return entry.candidate.origin == cxxime::CandidateOrigin::kComposed;
        });
    return found == result.entries.end() ? nullptr : &*found;
}

cxxime::TranslationResult finish_query(cxxime::PinyinTranslator& translator,
                                       cxxime::TranslationRequest request) {
    for (int attempt = 0; attempt < 2000; ++attempt) {
        auto result = translator.translate(request);
        if (!result.entries.empty() || result.extent.complete) {
            return result;
        }
    }
    ASSERT_TRUE(false) << "bounded fixture query did not finish";
    return {};
}

void drain_position(cxxime::PinyinGraphComposition& composer, cxxime::CompositionStats& stats) {
    for (int attempt = 0; attempt < 100 && composer.has_work(); ++attempt) {
        uint32_t steps = 0;
        if (composer.advance({}, steps, stats)) {
            return;
        }
    }
    ASSERT_TRUE(!composer.has_work());
}

} // namespace

TEST(PinyinMixedAbbreviation, dense_graph_uses_words_and_consumes_the_whole_input) {
    MixedFixture fixture;
    fixture.initialize();
    const auto old_paths = fixture.resources->segment(kDenseInput);
    ASSERT_EQ(old_paths.paths.size(), 256u);
    cxxime::Config config;
    config.pinyin_scheme = "full_pinyin";
    config.candidate_learning = false;
    cxxime::Engine engine;
    ASSERT_TRUE(test::initialize_engine(engine, fixture.dict, config, fixture.path + ".spellings"));
    engine.set_partial_selection_enabled(true);
    engine.set_query_deadline_ms(0);
    type_code(engine, kDenseInput);
    const auto& result = engine.context().translation();
    ASSERT_TRUE(composed(result) != nullptr);
    ASSERT_EQ(result.entries.front().candidate.text, kDenseText);
    const auto& action = std::get<cxxime::TextSelectionAction>(result.entries.front().selection);
    ASSERT_EQ(action.consumed_input_bytes, std::strlen(kDenseInput));
    ASSERT_EQ(action.variants[action.primary_variant].syllables, kDenseReading);
    const auto graph = cxxime::PinyinInputGraph::build(*fixture.resources, kDenseInput, false);
    const auto aligned = graph.align({}, kDenseReading, std::strlen(kDenseInput));
    ASSERT_EQ(aligned.size(), 1u);
    ASSERT_TRUE(aligned.front().input_lengths == std::vector<uint16_t>({2, 2, 1, 1, 1, 1}));
    ASSERT_TRUE(engine.select_candidate(0));
    ASSERT_EQ(engine.get_commit_text(), kDenseText);
    ASSERT_TRUE(engine.context().active_input().empty());
    type_code(engine, kDenseInput);
    const auto& choices = engine.context().translation().entries;
    const auto prefix = std::find_if(choices.begin(), choices.end(), [](const auto& entry) {
        return entry.candidate.text == "我爱";
    });
    ASSERT_TRUE(prefix != choices.end());
    ASSERT_TRUE(engine.select_candidate(static_cast<int>(prefix - choices.begin())));
    ASSERT_EQ(engine.context().active_input(), "zhrm");
    cxxime::KeyEvent key;
    key.keycode = VK_BACK;
    engine.process_key(key);
    ASSERT_EQ(engine.context().active_input(), "zhr");
    type_code(engine, "m");
    key.keycode = VK_SPACE;
    engine.process_key(key);
    ASSERT_EQ(engine.get_commit_text(), kDenseText);
    ASSERT_TRUE(engine.context().active_input().empty());
    engine.finalize();
}

TEST(PinyinMixedAbbreviation, complete_sources_and_disabled_words_are_authoritative) {
    MixedFixture fixture;
    fixture.initialize(true);
    ASSERT_TRUE(fixture.dict->load_disabled_system_entries(fixture.path + ".disabled"));
    ASSERT_TRUE(fixture.dict->load_candidate_preferences(fixture.path + ".prefs"));
    cxxime::Candidate other_code;
    other_code.text = "其他编码的偏好";
    other_code.code = "woaizhonghuarenmin";
    other_code.syllables = kDenseReading;
    other_code.source = cxxime::CandidateSource::kPinyin;
    other_code.origin = cxxime::CandidateOrigin::kLearned;
    ASSERT_TRUE(fixture.dict->record_candidate_preference(other_code, "wazhrm"));
    cxxime::PinyinTranslator translator;
    fixture.bind(translator);
    translator.set_candidate_learning_enabled(true);
    cxxime::TranslationRequest request;
    request.input = kDenseInput;
    auto result = finish_query(translator, request);
    ASSERT_TRUE(composed(result) == nullptr);
    ASSERT_EQ(result.entries.front().candidate.text, kDenseText);
    ASSERT_EQ(result.entries.front().candidate.origin, cxxime::CandidateOrigin::kSystem);
    ASSERT_TRUE(std::none_of(result.entries.begin(), result.entries.end(), [&](const auto& entry) {
        return entry.candidate.text == other_code.text;
    }));

    ASSERT_TRUE(fixture.dict->disable_system_entry("中华"));
    result = finish_query(translator, request);
    ASSERT_EQ(result.entries.front().candidate.text, kDenseText);
    ASSERT_TRUE(fixture.dict->disable_system_entry(kDenseText));
    result = finish_query(translator, request);
    ASSERT_TRUE(std::none_of(result.entries.begin(), result.entries.end(),
                             [](const auto& entry) {
                                 return entry.candidate.text == kDenseText ||
                                        entry.candidate.text == "中华";
                             }));

    request.suppress_graph_composition = true;
    result = finish_query(translator, request);
    ASSERT_TRUE(composed(result) == nullptr);
}

TEST(PinyinMixedAbbreviation, small_windows_resume_without_read_or_search_pollution) {
    MixedFixture fixture;
    fixture.initialize();
    cxxime::PinyinTranslator translator;
    fixture.bind(translator);
    cxxime::QueryBudget small;
    small.max_prefix_scan = 1;
    small.max_user_scan = 1;
    cxxime::QueryTrace trace;
    cxxime::TranslationRequest request;
    request.input = kDenseInput;
    request.input_revision = 7;
    request.budget = &small;
    request.trace = &trace;
    auto pending = translator.translate(request);
    ASSERT_TRUE(!pending.extent.complete);
    ASSERT_TRUE(composed(pending) == nullptr);
    auto read = request;
    read.work = cxxime::QueryWork::kRead;
    read.input = "woai";
    read.input_revision = 8;
    read.trace = nullptr;
    translator.translate(read);
    auto search = read;
    search.purpose = cxxime::TranslationPurpose::kSearch;
    search.work = cxxime::QueryWork::kAdvance;
    search.budget = nullptr;
    translator.translate(search);
    auto result = finish_query(translator, request);
    ASSERT_TRUE(composed(result) != nullptr);
    ASSERT_EQ(composed(result)->candidate.text, kDenseText);
    const uint32_t resumed_queries = trace.span_query_count;

    cxxime::PinyinTranslator control;
    fixture.bind(control);
    cxxime::QueryTrace control_trace;
    request.trace = &control_trace;
    const auto expected = finish_query(control, request);
    ASSERT_EQ(composed(expected)->candidate.text, kDenseText);
    ASSERT_EQ(resumed_queries, control_trace.span_query_count);

    read = request;
    read.work = cxxime::QueryWork::kRead;
    cxxime::QueryTrace read_trace;
    read.trace = &read_trace;
    const auto snapshot = translator.translate(read);
    ASSERT_TRUE(composed(snapshot) != nullptr);
    ASSERT_EQ(composed(snapshot)->candidate.text, kDenseText);
    ASSERT_EQ(read_trace.span_query_count, 0u);
    std::vector<cxxime::CandidateEntry> published = result.entries;
    request.published = &published;
    const auto next = translator.translate(request);
    ASSERT_TRUE(composed(next) == nullptr);
}

TEST(PinyinMixedAbbreviation, position_budgets_preserve_late_quality_and_long_suffixes) {
    MixedFixture fixture;
    fixture.initialize();
    constexpr uint16_t length = 10;
    cxxime::PinyinGraphComposition composer(*fixture.dict, length);
    cxxime::CompositionStats stats;
    std::string expected;
    for (uint16_t position = 0; position < length; ++position) {
        ASSERT_TRUE(composer.reaches(position));
        // More than 256 words in total, and a better spelling arrives after
        // each position already has 32 alternatives. Neither is an early stop.
        for (int alternative = 0; alternative < 34; ++alternative) {
            cxxime::Candidate candidate;
            candidate.text = alternative == 33 ? "best" : std::to_string(alternative);
            candidate.code = "a";
            candidate.syllables = "a";
            candidate.source_frequency = alternative == 33 ? 1 : 10000;
            cxxime::CompositionWordEdge word;
            word.start = position;
            word.end = position + 1;
            word.candidate = std::move(candidate);
            word.quality.abbreviation = alternative == 33 ? 0 : 1;
            composer.add_word(std::move(word));
        }
        if (position == 0) {
            cxxime::QueryDeadline expired;
            expired.enabled = true;
            expired.expires_at = cxxime::QueryDeadline::Clock::time_point::min();
            uint32_t steps = 1;
            ASSERT_TRUE(!composer.advance(expired, steps, stats));
            ASSERT_EQ(steps, 1u);
        }
        drain_position(composer, stats);
        expected += "best";
    }
    composer.seal();
    drain_position(composer, stats);
    const auto candidates = composer.take_candidates();
    ASSERT_EQ(candidates.size(), 1u);
    ASSERT_EQ(candidates.front().text, expected);
    ASSERT_TRUE(stats.span_candidate_count > 256u);
    ASSERT_TRUE(stats.state_count <= 1u + length * 32u);
}

TEST(PinyinMixedAbbreviation, missing_normal_words_recover_the_reading_and_visible_boundaries) {
    MixedFixture fixture;
    fixture.load(
        {
            {"ni", "你", 9000},
            {"yi", "一", 8000},
            {"a", "啊", 7000},
            {"o", "哦", 6000},
            {"ni:yao:ma", "你要吗", 5000},
        },
        {
            {"ni", "ni", cxxime::kNormalSpelling, 0.0f},
            {"yao", "yao", cxxime::kNormalSpelling, 0.0f},
            {"y", "yi", cxxime::kAbbreviation, -1.0f},
            {"a", "a", cxxime::kNormalSpelling, 0.0f},
            {"o", "o", cxxime::kNormalSpelling, 0.0f},
        });

    cxxime::PinyinTranslator translator;
    fixture.bind(translator);
    cxxime::TranslationRequest request;
    request.input = "niyao";
    const auto result = translator.translate(request);
    ASSERT_TRUE(composed(result) != nullptr);
    ASSERT_EQ(composed(result)->candidate.text, "你一啊哦");
    ASSERT_EQ(composed(result)->candidate.syllables, "ni:yi:a:o");
    ASSERT_TRUE(std::any_of(result.entries.begin(), result.entries.end(), [](const auto& entry) {
        return entry.candidate.text == "你要吗" &&
               entry.match_kind == cxxime::CandidateMatchKind::kPrefix;
    }));
    ASSERT_EQ(
        std::get<cxxime::TextSelectionAction>(composed(result)->selection).consumed_input_bytes,
        request.input.size());
    cxxime::CompositionState state;
    ASSERT_TRUE(state.set_active_input(request.input, request.input.size()));
    const auto presentation = cxxime::derive_composition_presentation(
        state, fixture.resources.get(), request.input.size(), true, "ni:yi:a:o");
    ASSERT_EQ(presentation.display_preedit, "ni'y'a'o");
    std::vector<cxxime::CandidateEntry> published{*composed(result)};
    request.published = &published;
    request.page_offset = 1;
    request.page_index = 1;
    request.page_size = 1;
    const auto next = translator.translate(request);
    ASSERT_EQ(next.entries.size(), 1u);
    ASSERT_EQ(next.entries.front().candidate.text, "你要吗");
    ASSERT_TRUE(!next.sequence_invalidated);
}

TEST(PinyinMixedAbbreviation, mixed_predictions_finish_without_resetting_published_words) {
    MixedFixture fixture;
    fixture.load(
        {
            {"ni", "你", 9000},
            {"ren:min", "人民", 8000},
        },
        {
            {"n", "ni", cxxime::kAbbreviation, -1.0f},
            {"r", "ren", cxxime::kAbbreviation, -1.0f},
            {"m", "min", cxxime::kAbbreviation, -1.0f},
        });

    std::vector<DictionaryEntry> predictions;
    for (int index = 0; index < 40; ++index) {
        predictions.emplace_back("nrma", "prediction" + std::to_string(index), 1000 - index);
    }
    ASSERT_TRUE(cxxime::Dict::create_test_dict(fixture.path + ".wubi", predictions));
    cxxime::Dict wubi(cxxime::UserDictKind::WUBI);
    ASSERT_TRUE(wubi.open_dict(fixture.path + ".wubi"));
    cxxime::MixedTranslator translator;
    translator.set_pinyin_dict(fixture.dict.get());
    translator.set_wubi_dict(&wubi);
    translator.bind_pinyin(fixture.resources, {});
    cxxime::TranslationRequest request;
    request.input = "nrm";
    request.scheme = cxxime::CompositionScheme::kMixed;
    request.input_revision = 1;
    std::vector<cxxime::CandidateEntry> published;
    request.published = &published;
    bool finished = false;
    for (int attempt = 0; attempt < 10 && !finished; ++attempt) {
        const auto batch = translator.translate(request);
        ASSERT_TRUE(!batch.sequence_invalidated);
        for (const auto& entry : batch.entries) {
            ASSERT_TRUE(std::none_of(published.begin(), published.end(), [&](const auto& previous) {
                return previous.candidate.text == entry.candidate.text;
            }));
            published.push_back(entry);
        }
        finished = batch.extent.complete;
    }
    ASSERT_TRUE(finished);
    ASSERT_EQ(published.size(), 41u);
    ASSERT_EQ(std::count_if(published.begin(), published.end(),
                            [](const auto& entry) {
                                return entry.candidate.origin ==
                                           cxxime::CandidateOrigin::kComposed &&
                                       entry.candidate.text == "你人民";
                            }),
              1);
    wubi.close();
}

TEST(PinyinMixedAbbreviation, actual_dictionary_covers_the_reported_input) {
    auto dict = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    ASSERT_TRUE(dict->open_dict(CXXIME_DATA_DIR "pinyin.dict.bin"));
    const std::string input = "buyuanyizuonulidrenm";
    for (bool fuzzy : {false, true}) {
        cxxime::Config config;
        config.pinyin_scheme = "full_pinyin";
        config.fuzzy_pinyin = fuzzy;
        config.candidate_learning = false;
        cxxime::Engine engine;
        ASSERT_TRUE(
            test::initialize_engine(engine, dict, config, CXXIME_DATA_DIR "pinyin.spellings.bin"));
        // CI checks deterministic coverage. The separate Release replay also
        // exercises the production 30ms deadline and records per-key timings.
        engine.set_query_deadline_ms(0);
        engine.set_partial_selection_enabled(true);
        for (const char key : input) {
            type_code(engine, std::string(1, key));
            if (engine.context().active_input().size() >= std::strlen("buyuanyizuonulid")) {
                ASSERT_TRUE(engine.context().candidate(0) != nullptr);
                ASSERT_EQ(engine.context().candidate(0)->text.compare(
                              0, std::strlen("不愿意做奴隶的"), "不愿意做奴隶的"),
                          0);
            }
        }
        const auto& result = engine.context().translation();
        const auto* sentence = composed(result);
        ASSERT_TRUE(sentence != nullptr);
        const auto& action = std::get<cxxime::TextSelectionAction>(sentence->selection);
        ASSERT_EQ(action.consumed_input_bytes, input.size());
        ASSERT_EQ(sentence->candidate.syllables, "bu:yuan:yi:zuo:nu:li:de:ren:men");
        const std::string text = sentence->candidate.text;
        ASSERT_TRUE(engine.select_candidate(static_cast<int>(sentence - result.entries.data())));
        ASSERT_EQ(engine.get_commit_text(), text);
        ASSERT_TRUE(engine.context().active_input().empty());
        engine.finalize();
    }
}

TEST(PinyinMixedAbbreviation, appending_initials_keeps_text_but_allows_a_new_word_boundary) {
    MixedFixture fixture;
    fixture.load(
        {
            {"wo:ai", "我爱", 90000},
            {"de", "的", 80000},
            {"ren", "人", 70000},
            {"ren:men", "人们", 50000},
            {"dui:ren:min", "对人民", 900000},
        },
        {
            {"wo", "wo", cxxime::kNormalSpelling, 0.0f},
            {"ai", "ai", cxxime::kNormalSpelling, 0.0f},
            {"d", "de", cxxime::kAbbreviation, -1.0f},
            {"d", "dui", cxxime::kAbbreviation, -1.0f},
            {"r", "ren", cxxime::kAbbreviation, -1.0f},
            {"m", "men", cxxime::kAbbreviation, -1.0f},
            {"m", "min", cxxime::kAbbreviation, -1.0f},
        });
    cxxime::Config config;
    config.pinyin_scheme = "full_pinyin";
    config.candidate_learning = false;
    cxxime::Engine engine;
    ASSERT_TRUE(test::initialize_engine(engine, fixture.dict, config, fixture.path + ".spellings"));
    engine.set_query_deadline_ms(0);
    engine.set_partial_selection_enabled(true);
    type_code(engine, "woaid");
    ASSERT_EQ(engine.context().candidate(0)->text, "我爱的");
    type_code(engine, "r");
    ASSERT_EQ(engine.context().candidate(0)->text, "我爱的人");
    type_code(engine, "m");
    ASSERT_EQ(engine.context().candidate(0)->text, "我爱的人们");
    ASSERT_TRUE(engine.select_candidate(0));
    ASSERT_EQ(engine.get_commit_text(), "我爱的人们");
    ASSERT_TRUE(engine.context().active_input().empty());
    engine.finalize();
}

TEST(PinyinMixedAbbreviation, continuation_filters_homophones_before_pruning_and_resumes) {
    MixedFixture fixture;
    fixture.load(
        {
            {"wo", "窝", 90000},
            {"wo", "握", 80000},
            {"wo", "卧", 70000},
            {"wo", "沃", 60000},
            {"wo", "我", 100},
            {"ren:men", "人们", 10000},
        },
        {
            {"w", "wo", cxxime::kAbbreviation, -1.0f},
            {"r", "ren", cxxime::kAbbreviation, -1.0f},
            {"m", "men", cxxime::kAbbreviation, -1.0f},
        });
    cxxime::CandidateContinuation previous;
    previous.input = "wr";
    previous.candidate.text = "我人";
    previous.candidate.syllables = "wo:ren";
    previous.candidate.source = cxxime::CandidateSource::kPinyin;
    cxxime::PinyinTranslator translator;
    fixture.bind(translator);
    cxxime::QueryBudget budget;
    budget.max_prefix_scan = 1;
    cxxime::TranslationRequest request;
    request.input = "wrm";
    request.continuation = &previous;
    request.budget = &budget;
    budget.deadline.enabled = true;
    budget.deadline.expires_at = cxxime::QueryDeadline::Clock::time_point::min();
    const auto pending = translator.translate(request);
    ASSERT_TRUE(!pending.extent.complete);
    ASSERT_TRUE(composed(pending) == nullptr);
    budget.deadline.enabled = false;
    const auto result = finish_query(translator, request);
    ASSERT_TRUE(composed(result) != nullptr);
    ASSERT_EQ(composed(result)->candidate.text, "我人们");

    auto search = request;
    search.purpose = cxxime::TranslationPurpose::kSearch;
    search.budget = nullptr;
    const auto unanchored = translator.translate(search);
    ASSERT_TRUE(composed(unanchored) != nullptr);
    ASSERT_EQ(composed(unanchored)->candidate.text, "窝人们");
    request.work = cxxime::QueryWork::kRead;
    ASSERT_EQ(composed(translator.translate(request))->candidate.text, "我人们");
}

TEST(PinyinMixedAbbreviation, completing_a_tail_releases_only_that_tail_and_absence_can_recover) {
    MixedFixture fixture;
    fixture.load(
        {
            {"wo", "我", 9000},
            {"guan", "关", 8000},
            {"guo", "国", 7000},
        },
        {
            {"w", "wo", cxxime::kAbbreviation, -1.0f},
            {"g", "ge", cxxime::kAbbreviation, -1.0f},
            {"gu", "guan", cxxime::kCompletionSpelling, -0.5f},
            {"gu", "guo", cxxime::kCompletionSpelling, -0.5f},
        });
    cxxime::CandidateContinuation previous;
    previous.input = "wg";
    previous.candidate.text = "我个";
    previous.candidate.syllables = "wo:ge";
    previous.candidate.source = cxxime::CandidateSource::kPinyin;
    for (const std::string& text : {std::string("我个"), std::string("卧个")}) {
        previous.candidate.text = text;
        cxxime::PinyinTranslator translator;
        fixture.bind(translator);
        cxxime::TranslationRequest request;
        request.input = "wgu";
        request.continuation = &previous;
        const auto result = finish_query(translator, request);
        ASSERT_TRUE(composed(result) != nullptr);
        ASSERT_EQ(composed(result)->candidate.text, "我关");
        ASSERT_EQ(composed(result)->candidate.syllables, "wo:guan");
    }
}

TEST(PinyinMixedAbbreviation, only_an_immediate_tail_append_reuses_the_visible_choice) {
    cxxime::Context context;
    const auto prepare = [&]() {
        context.reset();
        context.set_preedit("wd");
        cxxime::Candidate candidate;
        candidate.text = "我的";
        candidate.syllables = "wo:de";
        candidate.source = cxxime::CandidateSource::kPinyin;
        cxxime::TranslationResult result;
        result.entries.push_back(cxxime::make_text_candidate_entry(candidate, 2));
        context.update_translation(std::move(result));
    };
    prepare();
    context.insert_preedit('r');
    ASSERT_TRUE(context.candidate_continuation(0).has_value());
    ASSERT_EQ(context.candidate_continuation(0)->candidate.text, "我的");
    context.translation().validation_version = 1;
    ASSERT_TRUE(!context.candidate_continuation(1));
    prepare();
    context.move_preedit_cursor_home();
    context.insert_preedit('r');
    ASSERT_TRUE(!context.candidate_continuation(0));
    prepare();
    context.erase_preedit_before_cursor();
    context.insert_preedit('d');
    context.insert_preedit('r');
    ASSERT_TRUE(!context.candidate_continuation(0));
    prepare();
    ASSERT_TRUE(context.commit_candidate(0));
    context.insert_preedit('r');
    ASSERT_TRUE(!context.candidate_continuation(0));
}

TEST(PinyinMixedAbbreviation, actual_dictionary_preserves_each_displayed_prefix_until_commit) {
    auto dict = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    ASSERT_TRUE(dict->open_dict(CXXIME_DATA_DIR "pinyin.dict.bin"));
    for (bool fuzzy : {false, true}) {
        for (const std::string& stem :
             {std::string("buyuanzuonulid"), std::string("buyuanyizuonulid")}) {
            cxxime::Config config;
            config.pinyin_scheme = "full_pinyin";
            config.fuzzy_pinyin = fuzzy;
            config.candidate_learning = false;
            cxxime::Engine engine;
            ASSERT_TRUE(test::initialize_engine(engine, dict, config,
                                                CXXIME_DATA_DIR "pinyin.spellings.bin"));
            engine.set_partial_selection_enabled(true);
            engine.set_query_deadline_ms(0);
            type_code(engine, stem);
            ASSERT_TRUE(engine.context().candidate(0) != nullptr);
            const auto prefix = engine.context().candidate(0)->text;
            ASSERT_EQ(prefix.substr(prefix.size() - std::strlen("的")), "的");
            type_code(engine, "r");
            ASSERT_EQ(engine.context().candidate(0)->text, prefix + "人");
            type_code(engine, "m");
            ASSERT_EQ(engine.context().candidate(0)->text, prefix + "人们");
            ASSERT_TRUE(engine.select_candidate(0));
            ASSERT_EQ(engine.get_commit_text(), prefix + "人们");
            ASSERT_TRUE(engine.context().active_input().empty());
            engine.finalize();
        }
    }
}

TEST(PinyinMixedAbbreviation, ordinary_sentence_resumes_with_its_original_visible_prefix) {
    MixedFixture fixture;
    fixture.load(
        {
            {"wo", "窝", 90000},
            {"wo", "我", 100},
            {"ren:men", "人们", 10000},
        },
        {
            {"wo", "wo", cxxime::kNormalSpelling, 0.0f},
            {"ren", "ren", cxxime::kNormalSpelling, 0.0f},
            {"men", "men", cxxime::kNormalSpelling, 0.0f},
        });
    cxxime::PinyinTranslator translator;
    fixture.bind(translator);
    cxxime::CandidateContinuation previous;
    previous.input = "woren";
    previous.candidate.text = "我人";
    previous.candidate.syllables = "wo:ren";
    previous.candidate.source = cxxime::CandidateSource::kPinyin;
    cxxime::QueryBudget budget;
    budget.deadline.enabled = true;
    budget.deadline.expires_at = cxxime::QueryDeadline::Clock::time_point::min();
    cxxime::TranslationRequest request;
    request.input = "worenm";
    request.input_revision = 1;
    request.budget = &budget;
    request.continuation = &previous;
    ASSERT_TRUE(!translator.requires_graph_query(request.input));
    ASSERT_TRUE(!translator.translate(request).extent.complete);
    request.continuation = nullptr;
    request.work = cxxime::QueryWork::kRead;
    ASSERT_TRUE(composed(translator.translate(request)) == nullptr);
    request.work = cxxime::QueryWork::kAdvance;
    budget.deadline.enabled = false;
    budget.max_prefix_scan = 1;
    const auto first = finish_query(translator, request);
    ASSERT_TRUE(composed(first) != nullptr);
    ASSERT_EQ(composed(first)->candidate.text, "我人们");

    // An independent search shares source caches, never the constrained sentence.
    const auto search = translator.translate_page(request.input);
    ASSERT_EQ(search.candidates.front().text, "窝人们");
    request.work = cxxime::QueryWork::kRead;
    ASSERT_EQ(composed(translator.translate(request))->candidate.text, "我人们");
    request.work = cxxime::QueryWork::kAdvance;
    ++request.input_revision;
    const auto fresh = finish_query(translator, request);
    ASSERT_EQ(composed(fresh)->candidate.text, "窝人们");
}

TEST(PinyinMixedAbbreviation, same_sentence_keeps_people_before_tail_completion_in_every_scheme) {
    struct Scheme {
        const char* id;
        const char* path;
        const char* stem;
        const char* suffix;
    };
    const Scheme schemes[] = {
        {"full_pinyin", CXXIME_DATA_DIR "pinyin.spellings.bin", "buyuanzuonulideren", "en"},
        {"microsoft_shuangpin", CXXIME_DATA_DIR "pinyin.microsoft-shuangpin.spellings.bin",
         "buyrzonuliderf", "f"},
        {"sogou_shuangpin", CXXIME_DATA_DIR "pinyin.sogou-shuangpin.spellings.bin",
         "buyrzonuliderf", "f"},
        {"xiaohe_shuangpin", CXXIME_DATA_DIR "pinyin.xiaohe-shuangpin.spellings.bin",
         "buyrzonuliderf", "f"},
        {"ziranma_shuangpin", CXXIME_DATA_DIR "pinyin.ziranma-shuangpin.spellings.bin",
         "buyrzonuliderf", "f"},
    };
    auto dict = std::make_shared<cxxime::Dict>(cxxime::UserDictKind::PINYIN);
    ASSERT_TRUE(dict->open_dict(CXXIME_DATA_DIR "pinyin.dict.bin"));
    for (const auto& scheme : schemes) {
        for (bool fuzzy : {false, true}) {
            cxxime::Config config;
            config.pinyin_scheme = scheme.id;
            config.fuzzy_pinyin = fuzzy;
            config.candidate_learning = false;
            cxxime::Engine engine;
            ASSERT_TRUE(test::initialize_engine(engine, dict, config, scheme.path));
            engine.set_query_deadline_ms(0);
            engine.set_partial_selection_enabled(true);
            type_code(engine, scheme.stem);
            ASSERT_TRUE(engine.context().candidate(0) != nullptr);
            ASSERT_EQ(engine.context().candidate(0)->text, "不愿做奴隶的人") << scheme.id;
            type_code(engine, "m");
            ASSERT_EQ(engine.context().candidate(0)->text, "不愿做奴隶的人们") << scheme.id;
            for (const char key : std::string(scheme.suffix)) {
                type_code(engine, std::string(1, key));
                ASSERT_TRUE(engine.context().candidate(0) != nullptr);
                ASSERT_EQ(engine.context().candidate(0)->text.compare(
                              0, std::strlen("不愿做奴隶的人"), "不愿做奴隶的人"),
                          0)
                    << scheme.id;
            }
            ASSERT_EQ(engine.context().candidate(0)->text, "不愿做奴隶的人们") << scheme.id;
            ASSERT_TRUE(engine.select_candidate(0));
            ASSERT_EQ(engine.get_commit_text(), "不愿做奴隶的人们");
            ASSERT_TRUE(engine.context().active_input().empty());
            engine.finalize();
        }
    }
}

TEST(PinyinMixedAbbreviation, completed_tail_word_does_not_unlock_earlier_abbreviations) {
    MixedFixture fixture;
    fixture.load(
        {
            {"wo", "窝", 90000},
            {"wo", "我", 100},
            {"de", "的", 100},
            {"dui", "对", 90000},
            {"nu", "怒", 10000},
            {"li", "里", 10000},
            {"nu:li", "奴隶", 80000},
            {"dui:nu:li", "对奴隶", 900000},
            {"ai", "爱", 10000},
            {"hang", "行", 1000},
            {"a", "啊", 500},
            {"ao", "奥", 500},
            {"han:gao", "汉高", 900000},
        },
        {
            {"w", "wo", cxxime::kAbbreviation, -1.0f},
            {"d", "de", cxxime::kAbbreviation, -1.0f},
            {"d", "dui", cxxime::kAbbreviation, -1.0f},
            {"nu", "nu", cxxime::kNormalSpelling, 0.0f},
            {"li", "li", cxxime::kNormalSpelling, 0.0f},
            {"wo", "wo", cxxime::kNormalSpelling, 0.0f},
            {"ai", "ai", cxxime::kNormalSpelling, 0.0f},
            {"hang", "hang", cxxime::kNormalSpelling, 0.0f},
            {"han", "han", cxxime::kNormalSpelling, 0.0f},
            {"gao", "gao", cxxime::kNormalSpelling, 0.0f},
            {"a", "a", cxxime::kNormalSpelling, 0.0f},
            {"ao", "ao", cxxime::kNormalSpelling, 0.0f},
        });
    cxxime::PinyinTranslator translator;
    fixture.bind(translator);
    cxxime::CandidateContinuation previous;
    previous.input = "wdnul";
    previous.candidate.text = "我的怒里";
    previous.candidate.syllables = "wo:de:nu:li";
    previous.candidate.source = cxxime::CandidateSource::kPinyin;
    cxxime::TranslationRequest request;
    request.input = "wdnuli";
    request.continuation = &previous;
    const auto result = finish_query(translator, request);
    ASSERT_TRUE(composed(result) != nullptr);
    ASSERT_EQ(composed(result)->candidate.text, "我的奴隶");
    ASSERT_EQ(composed(result)->candidate.syllables, "wo:de:nu:li");

    // Normal full spellings may resegment within the completed word. Locking all
    // earlier boundaries would preserve hang + ao and wrongly prevent han + gao.
    previous.input = "woaihanga";
    previous.candidate.text = "我爱行啊";
    previous.candidate.syllables = "wo:ai:hang:a";
    request.input = "woaihangao";
    ++request.input_revision;
    const auto resegmented = finish_query(translator, request);
    ASSERT_TRUE(composed(resegmented) != nullptr);
    ASSERT_EQ(composed(resegmented)->candidate.text, "我爱汉高");
    ASSERT_EQ(composed(resegmented)->candidate.syllables, "wo:ai:han:gao");
}

int main() {
    GetTempPathA(MAX_PATH, engine_test_temp_path);
    return test::RunAllTests();
}
