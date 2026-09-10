// Copyright (c) 2026 Hunter Shurniak. All rights reserved.

#include "test.hpp"

namespace testing {

int run_all() {
    int count = 0;
    for (Test* t = registry(); t != nullptr; t = t->next) ++count;

    std::printf("running %d test%s\n\n", count, count == 1 ? "" : "s");

    for (Test* t = registry(); t != nullptr; t = t->next) {
        const int before = failures();
        t->fn();
        std::printf("  %-28s %s\n", t->name,
                    failures() == before ? "ok" : "FAILED");
    }

    std::printf("\n%d checks, %d failure%s\n",
                checks(), failures(), failures() == 1 ? "" : "s");

    return failures() != 0;
}

}  // namespace testing

// Non-zero exit on failure, so `make test` fails the build and CI notices.
int main() { return testing::run_all(); }
