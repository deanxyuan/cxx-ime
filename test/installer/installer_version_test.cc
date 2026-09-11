// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/installer_version.h>

#include "support/testutil.h"

TEST(InstallerVersion, compares_release_and_prerelease_versions) {
    cxxime::installer::VersionOrder order = cxxime::installer::VersionOrder::kEqual;
    ASSERT_TRUE(cxxime::installer::compare_semantic_versions("0.5.0", "0.5.0", &order));
    ASSERT_EQ(order, cxxime::installer::VersionOrder::kEqual);
    ASSERT_TRUE(cxxime::installer::compare_semantic_versions("0.5.0-dev", "0.5.0", &order));
    ASSERT_EQ(order, cxxime::installer::VersionOrder::kOlder);
    ASSERT_TRUE(cxxime::installer::compare_semantic_versions("0.6.0", "0.5.9", &order));
    ASSERT_EQ(order, cxxime::installer::VersionOrder::kNewer);
    ASSERT_TRUE(cxxime::installer::compare_semantic_versions("0.5.0-rc.10", "0.5.0-rc.2", &order));
    ASSERT_EQ(order, cxxime::installer::VersionOrder::kNewer);
}

TEST(InstallerVersion, rejects_invalid_versions) {
    cxxime::installer::VersionOrder order = cxxime::installer::VersionOrder::kEqual;
    ASSERT_TRUE(!cxxime::installer::compare_semantic_versions("0.5", "0.5.0", &order));
    ASSERT_TRUE(!cxxime::installer::compare_semantic_versions("0.5.0-", "0.5.0", &order));
    ASSERT_TRUE(!cxxime::installer::compare_semantic_versions("0.5.x", "0.5.0", &order));
}

RUN_ALL_TESTS()
