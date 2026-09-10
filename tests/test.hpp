// Minimal test framework for host-executed unit tests.
//
// Copyright (c) 2026 Hunter Shurniak. All rights reserved.
//
// Kernel modules without hardware dependencies are ordinary C++ and can be
// compiled and executed by the host toolchain. See the test target in the
// Makefile for which modules qualify.
//
//   TEST(name) { ... }       declares and registers a test case
//   CHECK(expr)              records a failure if expr is false
//   CHECK_STREQ(got, want)   records a failure and reports both strings
//
// Cases register themselves through a static constructor, so adding one
// requires no change to the runner.

#pragma once

#include <cstdio>
#include <cstring>

namespace testing {

struct Test {
    const char* name;
    void (*fn)();
    Test* next;
};

// Function-local statics rather than namespace-scope objects: this header is
// included by every test translation unit, and this guarantees a single
// instance with no initialisation-order dependency between them.
inline Test*& registry()  { static Test* head = nullptr; return head; }
inline int&   failures()  { static int f = 0; return f; }
inline int&   checks()    { static int c = 0; return c; }

struct Registrar {
    explicit Registrar(Test* t) { t->next = registry(); registry() = t; }
};

inline void fail(const char* file, int line, const char* expr) {
    ++failures();
    std::printf("    FAIL  %s:%d\n      %s\n", file, line, expr);
}

inline void fail_str(const char* file, int line, const char* expr,
                     const char* got, const char* want) {
    ++failures();
    std::printf("    FAIL  %s:%d\n      %s\n      got:  \"%s\"\n      want: \"%s\"\n",
                file, line, expr, got, want);
}

// Runs every registered case. Returns non-zero if any check failed.
int run_all();

}  // namespace testing

#define TEST(name)                                             \
    static void name();                                        \
    static ::testing::Test name##_t{#name, name, nullptr};     \
    static ::testing::Registrar name##_r{&name##_t};           \
    static void name()

#define CHECK(expr)                                            \
    do {                                                       \
        ++::testing::checks();                                 \
        if (!(expr)) ::testing::fail(__FILE__, __LINE__, #expr); \
    } while (0)

#define CHECK_STREQ(got, want)                                 \
    do {                                                       \
        ++::testing::checks();                                 \
        if (std::strcmp((got), (want)) != 0)                   \
            ::testing::fail_str(__FILE__, __LINE__, #got, (got), (want)); \
    } while (0)
