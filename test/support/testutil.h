// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
// Based on connx testutil by connx contributors (MIT).

#ifndef CXXIME_TEST_UTIL_H_
#define CXXIME_TEST_UTIL_H_

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <string_view>
#include <sstream>
#include <type_traits>
#include <vector>

namespace test {

// ostream helpers for string types
static inline std::ostream& operator<<(std::ostream& os, std::string_view sv) {
    os.write(sv.data(), static_cast<std::streamsize>(sv.size()));
    return os;
}

static inline std::ostream& operator<<(std::ostream& os, const std::wstring& ws) {
    for (wchar_t c : ws)
        os << static_cast<char>(c > 127 ? '?' : c);
    return os;
}

static inline std::ostream& operator<<(std::ostream& os, const wchar_t* s) {
    if (s) {
        while (*s) {
            os << static_cast<char>(*s > 127 ? '?' : *s);
            ++s;
        }
    }
    return os;
}

template <typename T>
auto operator<<(std::ostream& os, const T& v) ->
    typename std::enable_if<std::is_enum_v<T>, std::ostream&>::type {
    return os << static_cast<std::underlying_type_t<T>>(v);
}

// Narrow string type trait
template <typename T>
struct IsStringType : std::false_type {};
template <>
struct IsStringType<char*> : std::true_type {};
template <>
struct IsStringType<const char*> : std::true_type {};
template <size_t N>
struct IsStringType<char[N]> : std::true_type {};
template <size_t N>
struct IsStringType<const char[N]> : std::true_type {};
template <>
struct IsStringType<std::string> : std::true_type {};
template <>
struct IsStringType<const std::string> : std::true_type {};
template <>
struct IsStringType<std::string&> : std::true_type {};
template <>
struct IsStringType<const std::string&> : std::true_type {};
template <>
struct IsStringType<std::string_view> : std::true_type {};

template <typename T>
auto ToComparable(const T& value) ->
    typename std::enable_if<IsStringType<T>::value, std::string_view>::type {
    return std::string_view(value);
}
template <typename T>
auto ToComparable(const T& value) ->
    typename std::enable_if<!IsStringType<T>::value, const T&>::type {
    return value;
}

int RunAllTests();

class Tester {
private:
    bool ok_;
    const char* fname_;
    int line_;
    std::stringstream ss_;

public:
    Tester(const char* f, int l)
        : ok_(true)
        , fname_(f)
        , line_(l) {}

    ~Tester() {
        if (!ok_) {
            fprintf(stderr, "%s:%d:%s\n", fname_, line_, ss_.str().c_str());
            exit(1);
        }
    }

    Tester& Is(bool b, const char* msg) {
        if (!b) {
            ss_ << " Assertion failure " << msg;
            ok_ = false;
        }
        return *this;
    }

#define BINARY_OP(name, op)                                                                        \
    template <class X, class Y>                                                                    \
    Tester& name(const X& x, const Y& y) {                                                         \
        auto&& lhs = ToComparable(x);                                                              \
        auto&& rhs = ToComparable(y);                                                              \
        if (!(lhs op rhs)) {                                                                       \
            ss_ << " failed: " << lhs << (" " #op " ") << rhs;                                     \
            ok_ = false;                                                                           \
        }                                                                                          \
        return *this;                                                                              \
    }

    BINARY_OP(IsEq, ==)
    BINARY_OP(IsNe, !=)
    BINARY_OP(IsGe, >=)
    BINARY_OP(IsGt, >)
    BINARY_OP(IsLe, <=)
    BINARY_OP(IsLt, <)
#undef BINARY_OP

    template <class V>
    Tester& operator<<(const V& value) {
        if (!ok_) {
            ss_ << " " << value;
        }
        return *this;
    }
};

#define ASSERT_TRUE(c)  ::test::Tester(__FILE__, __LINE__).Is((c), #c)
#define ASSERT_EQ(a, b) ::test::Tester(__FILE__, __LINE__).IsEq((a), (b))
#define ASSERT_NE(a, b) ::test::Tester(__FILE__, __LINE__).IsNe((a), (b))
#define ASSERT_GE(a, b) ::test::Tester(__FILE__, __LINE__).IsGe((a), (b))
#define ASSERT_GT(a, b) ::test::Tester(__FILE__, __LINE__).IsGt((a), (b))
#define ASSERT_LE(a, b) ::test::Tester(__FILE__, __LINE__).IsLe((a), (b))
#define ASSERT_LT(a, b) ::test::Tester(__FILE__, __LINE__).IsLt((a), (b))

bool RegisterTest(const char* base, const char* name, void (*func)());

// TEST(Suite, Name): self-registering test function with static linkage.
// Uses __COUNTER__ (captured once via helper macro) for unique names per TU.
#define _TC(a, b)  a##b
#define _TCC(a, b) _TC(a, b)

#define TEST(TestSuit, TestName) _TEST_IMPL(TestSuit, TestName, __COUNTER__)
#define _TEST_IMPL(TestSuit, TestName, C)                                                          \
    static void _TCC(_tf_, C)();                                                                   \
    namespace {                                                                                    \
    bool _TCC(_tr_, C) = ::test::RegisterTest(#TestSuit, #TestName, &_TCC(_tf_, C));               \
    }                                                                                              \
    static void _TCC(_tf_, C)()

#define RUN_ALL_TESTS()                                                                            \
    int main() { return ::test::RunAllTests(); }

} // namespace test

#endif // CXXIME_TEST_UTIL_H_
