project(sqlite_query CXX)

add_executable(sqlite_query main.cc)

target_link_libraries(sqlite_query PRIVATE sqlite3)
