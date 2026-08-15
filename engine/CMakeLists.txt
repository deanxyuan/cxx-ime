project(cxxime-engine CXX)

add_library(cxxime-engine STATIC
    src/context.cc
    src/pinyin_segmentor.cc
    src/dict.cc
    src/dict_user_data.cc
    src/dict_span_query.cc
    src/dict_wubi_query.cc
    src/user_data_file.cc
    src/user_lexicon.cc
    src/user_lexicon_query.cc
    src/candidate_preference.cc
    src/wubi_prefix_index.cc
    src/spellings_index.cc
    src/syllabifier.cc
    src/pinyin_composer.cc
    src/pinyin_path_filter.cc
    src/pinyin_translator.cc
    src/pinyin_processor.cc
    src/wubi_translator.cc
    src/mixed_translator.cc
    src/wubi_processor.cc
    src/config.cc
    src/engine.cc
    src/engine_symbol.cc
    src/ascii_composer.cc
    src/short_code_cache.cc
    src/output_composer.cc
    src/symbol_processor.cc
    src/symbol_table.cc
)

target_include_directories(cxxime-engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cxxime-engine PUBLIC
    cxxime-shared
    nlohmann_json
)
