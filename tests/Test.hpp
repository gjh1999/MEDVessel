#pragma once
#ifdef MEDVESSEL_HAVE_GTEST
#include <gtest/gtest.h>
#define MEDVESSEL_TEST(name) TEST(MEDVessel, name)
#else
// Keep offline core builds usable without fetching a test framework.
#define MEDVESSEL_TEST(name) int main()
#endif
