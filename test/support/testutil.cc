// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Based on connx testutil by connx contributors (MIT).

#include "testutil.h"

#include <map>
#include <string>
#include <vector>

namespace test {

namespace {
struct Test {
    const char* base;
    const char* name;
    void (*func)();
};
std::map<std::string, std::vector<Test>>* tests;
} // namespace

bool RegisterTest(const char* base, const char* name, void (*func)()) {
    if (tests == nullptr) {
        tests = new std::map<std::string, std::vector<Test>>;
    }
    Test t;
    t.base = base;
    t.name = name;
    t.func = func;
    (*tests)[base].push_back(t);
    return true;
}

int RunAllTests() {
    int num = 0;
    int failed = 0;
    if (tests != nullptr) {
        for (auto& [base, vec] : *tests) {
            fprintf(stderr, "==== Test %s\n", base.c_str());
            for (auto& t : vec) {
                fprintf(stderr, "  %s ...", t.name);
                (*t.func)();
                fprintf(stderr, " OK\n");
                ++num;
            }
        }
    }
    fprintf(stderr, "==== PASSED %d tests\n", num);
    return failed;
}

} // namespace test
