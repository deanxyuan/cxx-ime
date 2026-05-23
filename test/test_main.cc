// Copyright (c) 2026 CxxIME Contributors. MIT License.

#include <cstdio>

// Test declarations (return number of failures)
int run_engine_tests();
int run_segmentor_tests();
int run_dict_tests();
int run_config_tests();

int main() {
    printf("=== CxxIME Test Suite ===\n\n");

    int failures = 0;
    failures += run_engine_tests();
    printf("\n");
    failures += run_segmentor_tests();
    printf("\n");
    failures += run_dict_tests();
    printf("\n");
    failures += run_config_tests();

    printf("\n=== All Tests Complete ===\n");
    if (failures > 0) {
        printf("FAILED: %d test(s) failed\n", failures);
        return 1;
    }
    printf("PASSED\n");
    return 0;
}
