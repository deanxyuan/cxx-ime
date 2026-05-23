project(cxxime-engine CXX)

add_library(cxxime-engine STATIC
    src/context.cc
    src/pinyin_segmentor.cc
    src/sqlite_dict.cc
    src/pinyin_translator.cc
    src/pinyin_processor.cc
    src/config.cc
    src/engine.cc
)

target_include_directories(cxxime-engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cxxime-engine PUBLIC
    cxxime-shared
    sqlite3
    nlohmann_json
)
