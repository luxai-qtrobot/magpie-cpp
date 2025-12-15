// rtf_test_macros.hpp
#pragma once
#include <exception>
#include <string>

#include <robottestingframework/TestCase.h>
#include <robottestingframework/dll/Plugin.h>
#include <robottestingframework/TestAssert.h>
#include <robottestingframework/Asserter.h>


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


  // -----------------------------------------------------------------------------
// RTF helper macros
// -----------------------------------------------------------------------------
//
// These macros provide Catch2-like exception checking for
// RobotTestingFramework tests.
//
// They NEVER throw. All failures are reported via
// ROBOTTESTINGFRAMEWORK_TEST_FAIL_IF_FALSE / TEST_REPORT,
// so the test continues executing.
//
// Usage examples:
//
//   RTF_CHECK_THROWS(v.asInt());
//   RTF_CHECK_THROWS_AS(v.asInt(), std::runtime_error);
//   RTF_CHECK_THROWS_MSG_CONTAINS(v.asInt(), std::runtime_error, "not an int");
//   RTF_CHECK_NOTHROW(v.asDouble());
//
// IMPORTANT:
//   Must be used inside a TestCase (run/setup/teardown), since RTF macros
//   rely on `this`.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// RTF_CHECK_THROWS
//   Passes if *any* exception is thrown
// -----------------------------------------------------------------------------
#define RTF_CHECK_THROWS(expr)                                                      \
    do {                                                                            \
        bool _rtf_thrown = false;                                                   \
        try {                                                                       \
            (void)(expr);                                                           \
        } catch (...) {                                                             \
            _rtf_thrown = true;                                                     \
        }                                                                           \
        ROBOTTESTINGFRAMEWORK_TEST_FAIL_IF_FALSE(                                    \
            _rtf_thrown, "expected an exception, but none was thrown"               \
        );                                                                          \
    } while (0)


// -----------------------------------------------------------------------------
// RTF_CHECK_THROWS_AS
//   Passes only if the given exception type is thrown
// -----------------------------------------------------------------------------
#define RTF_CHECK_THROWS_AS(expr, ex_type)                                           \
    do {                                                                            \
        bool _rtf_ok = false;                                                       \
        try {                                                                       \
            (void)(expr);                                                           \
        } catch (const ex_type&) {                                                  \
            _rtf_ok = true;                                                         \
        } catch (...) {                                                             \
            _rtf_ok = false;                                                        \
        }                                                                           \
        ROBOTTESTINGFRAMEWORK_TEST_FAIL_IF_FALSE(                                    \
            _rtf_ok, "expected exception of type " #ex_type                          \
        );                                                                          \
    } while (0)


// -----------------------------------------------------------------------------
// RTF_CHECK_THROWS_MSG_CONTAINS
//   Passes only if the given exception type is thrown AND
//   e.what() contains the given substring
// -----------------------------------------------------------------------------
#define RTF_CHECK_THROWS_MSG_CONTAINS(expr, ex_type, needle)                         \
    do {                                                                            \
        bool _rtf_ok = false;                                                       \
        try {                                                                       \
            (void)(expr);                                                           \
        } catch (const ex_type& _rtf_e) {                                           \
            const std::string _rtf_msg = _rtf_e.what();                             \
            _rtf_ok = (_rtf_msg.find(needle) != std::string::npos);                 \
            if (!_rtf_ok) {                                                         \
                ROBOTTESTINGFRAMEWORK_TEST_REPORT(                                   \
                    ::robottestingframework::Asserter::format(                       \
                        "exception message mismatch: expected to contain '%s', got '%s'", \
                        needle, _rtf_msg.c_str()                                     \
                    )                                                               \
                );                                                                  \
            }                                                                       \
        } catch (...) {                                                             \
            _rtf_ok = false;                                                        \
        }                                                                           \
        ROBOTTESTINGFRAMEWORK_TEST_FAIL_IF_FALSE(                                    \
            _rtf_ok,                                                                \
            "expected " #ex_type " with message containing '" needle "'"            \
        );                                                                          \
    } while (0)


// -----------------------------------------------------------------------------
// RTF_CHECK_NOTHROW
//   Passes only if no exception is thrown
// -----------------------------------------------------------------------------
#define RTF_CHECK_NOTHROW(expr)                                                      \
    do {                                                                            \
        bool _rtf_ok = true;                                                        \
        try {                                                                       \
            (void)(expr);                                                           \
        } catch (const std::exception& _rtf_e) {                                    \
            _rtf_ok = false;                                                        \
            ROBOTTESTINGFRAMEWORK_TEST_REPORT(                                       \
                ::robottestingframework::Asserter::format(                           \
                    "unexpected std::exception: %s", _rtf_e.what()                  \
                )                                                                   \
            );                                                                      \
        } catch (...) {                                                             \
            _rtf_ok = false;                                                        \
            ROBOTTESTINGFRAMEWORK_TEST_REPORT(                                       \
                "unexpected non-std exception"                                      \
            );                                                                      \
        }                                                                           \
        ROBOTTESTINGFRAMEWORK_TEST_FAIL_IF_FALSE(                                    \
            _rtf_ok, "expected no exception"                                        \
        );                                                                          \
    } while (0)

