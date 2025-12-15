// rtf_test_macros.hpp
#pragma once
#include <robottestingframework/TestCase.h>
#include <robottestingframework/dll/Plugin.h>
#include <robottestingframework/TestAssert.h>
/**
 *  RTF_TEST_CASE
 *  example:
 * 
 *  RTF_TEST_CASE("TestDummy") {
 *    ROBOTTESTINGFRAMEWORK_TEST_REPORT("testing integers");
 *    ROBOTTESTINGFRAMEWORK_TEST_FAIL_IF_FALSE(2 < 3, "is not smaller");
 *  }
 * 
 */
#define RTF_TEST_CASE(NameStr)                                            \
  class RTF_Test : public robottestingframework::TestCase                 \
  {                                                                       \
  public:                                                                 \
      RTF_Test() : robottestingframework::TestCase(NameStr) {}            \
      void run() override;                                                \
  };                                                                      \
  ROBOTTESTINGFRAMEWORK_PREPARE_PLUGIN(RTF_Test)                          \
  void RTF_Test::run()




/**
 *  RTF_TEST_CASE_FULL
 *  example: 
 * 
 * RTF_TEST_CASE_FULL(
 *  "QuickTest",
 *  [](int, char**) -> bool { return true; },
 *  [] { ROBOTTESTINGFRAMEWORK_TEST_FAIL_IF_FALSE(2 < 3, "nope"); },
 *  [] { }
 * );
 * 
 */
#define RTF_TEST_CASE_FULL(NameStr, SetupLambda, RunLambda, TearDownLambda) \
  class RTF_Test : public robottestingframework::TestCase                  \
  {                                                                        \
  public:                                                                  \
      RTF_Test() : robottestingframework::TestCase(NameStr) {}             \
                                                                           \
      bool setup(int argc, char** argv) override                            \
      {                                                                    \
          static constexpr auto fn = (SetupLambda);                        \
          return fn(argc, argv);                                           \
      }                                                                    \
                                                                           \
      void run() override                                                  \
      {                                                                    \
          static constexpr auto fn = (RunLambda);                          \
          fn();                                                            \
      }                                                                    \
                                                                           \
      void tearDown() override                                             \
      {                                                                    \
          static constexpr auto fn = (TearDownLambda);                     \
          fn();                                                            \
      }                                                                    \
  };                                                                       \
  ROBOTTESTINGFRAMEWORK_PREPARE_PLUGIN(RTF_Test)
