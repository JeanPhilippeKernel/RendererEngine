#include <ZEngine/CrashHandlers/CrashHandler.h>
#include <gtest/gtest.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#if defined(_WIN32)
#include <Windows.h>
#endif

namespace fs = std::filesystem;
using namespace ZEngine::CrashHandlers;

static const bool kDeathTestStyleSet = []() {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
#if defined(_WIN32)
    _putenv_s("ZENGINE_CRASH_NO_DIALOG", "1");
#else
    setenv("ZENGINE_CRASH_NO_DIALOG", "1", 1);
#endif
    return true;
}();

static std::string GetTestLogDir()
{
#if defined(_WIN32)
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    return std::string(tmp) + "zengine_crash_test_logs";
#else
    return "/tmp/zengine_crash_test_logs";
#endif
}
static const std::string kTestLogDirStr = GetTestLogDir();
static const char*       kTestLogDir    = kTestLogDirStr.c_str();

static void              CleanLogDir()
{
    std::error_code ec;
    fs::remove_all(kTestLogDir, ec);
}

static std::string FindLatestLog()
{
    std::string     latest;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(kTestLogDir, ec))
    {
        if (entry.path().extension() == ".log")
        {
            if (latest.empty() || entry.path().string() > latest)
                latest = entry.path().string();
        }
    }
    return latest;
}

static std::string ReadFile(const std::string& path)
{
    std::ifstream f(path);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

class CrashHandlerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        CleanLogDir();
        CrashHandler::Install("ZEngineTest", "0.0.1-test", kTestLogDir);
    }

    void TearDown() override
    {
        CrashHandler::Uninstall();
        CleanLogDir();
    }
};

class CrashHandlerDeathTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        CleanLogDir();
    }
    void TearDown() override
    {
        CleanLogDir();
    }
};

TEST_F(CrashHandlerTest, InstallCreatesLogDirectory)
{
    EXPECT_TRUE(fs::exists(kTestLogDir));
}

TEST_F(CrashHandlerTest, DoubleInstallIsNoop)
{
    CrashHandler::Install("ZEngineTest", "0.0.1-test", kTestLogDir);
    SUCCEED();
}

TEST_F(CrashHandlerTest, UninstallLeavesProcessAlive)
{
    CrashHandler::Uninstall();
    SUCCEED();
    CrashHandler::Install("ZEngineTest", "0.0.1-test", kTestLogDir);
}

TEST_F(CrashHandlerTest, DoubleUninstallIsNoop)
{
    CrashHandler::Uninstall();
    CrashHandler::Uninstall();
    SUCCEED();
    CrashHandler::Install("ZEngineTest", "0.0.1-test", kTestLogDir);
}

TEST_F(CrashHandlerTest, SetPreCrashCallbackNullIsValid)
{
    CrashHandler::SetPreCrashCallback(nullptr, nullptr);
    SUCCEED();
}

TEST_F(CrashHandlerDeathTest, PreCrashCallbackFiredBeforeDeath)
{
    ASSERT_DEATH(
        {
            CrashHandler::Install("ZEngineTest", "0.0.1-test", kTestLogDir);
            CrashHandler::SetPreCrashCallback([](void*) { fputs("[test] PreCrashCallback fired\n", stderr); }, nullptr);
            CrashHandler::OnCrash("pre-crash callback check");
        },
        "\\[test\\] PreCrashCallback fired");
}

TEST_F(CrashHandlerDeathTest, UpdatedCallbackIsUsedNotStaleOne)
{
    ASSERT_DEATH(
        {
            CrashHandler::Install("ZEngineTest", "0.0.1-test", kTestLogDir);
            CrashHandler::SetPreCrashCallback([](void*) { fputs("[test] stale callback\n", stderr); }, nullptr);
            CrashHandler::SetPreCrashCallback([](void*) { fputs("[test] updated callback OK\n", stderr); }, nullptr);
            CrashHandler::OnCrash("callback update check");
        },
        "\\[test\\] updated callback OK");
}

TEST_F(CrashHandlerDeathTest, OnCrashWritesLogFile)
{
    ASSERT_DEATH(
        {
            CrashHandler::Install("ZEngineTest", "0.0.1-test", kTestLogDir);
            CrashHandler::OnCrash("GPU device lost");
        },
        "");

    EXPECT_FALSE(FindLatestLog().empty()) << "No log file found in " << kTestLogDir;
}

TEST_F(CrashHandlerDeathTest, OnCrashLogContainsAppMetadata)
{
    ASSERT_DEATH(
        {
            CrashHandler::Install("ZEngineTest", "0.0.1-test", kTestLogDir);
            CrashHandler::OnCrash("metadata check");
        },
        "");

    std::string log = FindLatestLog();
    ASSERT_FALSE(log.empty()) << "No log file found in " << kTestLogDir;
    std::string body = ReadFile(log);
    ASSERT_FALSE(body.empty()) << "Log file is empty: " << log;

    EXPECT_NE(body.find("ZEngineTest"), std::string::npos) << "App name missing\n--- log ---\n" << body.substr(0, 1024) << "\n---";
    EXPECT_NE(body.find("0.0.1-test"), std::string::npos) << "Version missing\n--- log ---\n" << body.substr(0, 1024) << "\n---";
    EXPECT_NE(body.find("metadata check"), std::string::npos) << "Reason missing\n--- log ---\n" << body.substr(0, 1024) << "\n---";
}

TEST_F(CrashHandlerDeathTest, OnCrashLogContainsStackTrace)
{
    ASSERT_DEATH(
        {
            CrashHandler::Install("ZEngineTest", "0.0.1-test", kTestLogDir);
            CrashHandler::OnCrash("stack trace check");
        },
        "");

    std::string body = ReadFile(FindLatestLog());
    EXPECT_NE(body.find("Stack Trace"), std::string::npos) << "Stack trace header missing";
    EXPECT_NE(body.find("#0"), std::string::npos) << "No frames in stack trace";
}

TEST_F(CrashHandlerDeathTest, OnCrashLogIsProperlyTerminated)
{
    ASSERT_DEATH(
        {
            CrashHandler::Install("ZEngineTest", "0.0.1-test", kTestLogDir);
            CrashHandler::OnCrash("termination check");
        },
        "");

    std::string body = ReadFile(FindLatestLog());
    EXPECT_NE(body.find("End of Report"), std::string::npos) << "Log not properly terminated";
}

TEST_F(CrashHandlerDeathTest, OnAssertionFailureWritesFileAndLine)
{
    ASSERT_DEATH(
        {
            CrashHandler::Install("ZEngineTest", "0.0.1-test", kTestLogDir);
            CrashHandler::OnAssertionFailure("assertion_test.cpp", 42, "x > 0");
        },
        "");

    std::string log  = FindLatestLog();
    std::string body = ReadFile(log);
    ASSERT_FALSE(body.empty()) << "Log file is empty: " << log;

    EXPECT_NE(body.find("assertion_test.cpp"), std::string::npos) << "File name missing\n--- log ---\n" << body.substr(0, 1024) << "\n---";
    EXPECT_NE(body.find("42"), std::string::npos) << "Line number missing\n--- log ---\n" << body.substr(0, 1024) << "\n---";
    EXPECT_NE(body.find("x > 0"), std::string::npos) << "Condition missing\n--- log ---\n" << body.substr(0, 1024) << "\n---";
}

TEST_F(CrashHandlerDeathTest, OnAssertionFailureHandlesNullArgs)
{
    ASSERT_DEATH(
        {
            CrashHandler::Install("ZEngineTest", "0.0.1-test", kTestLogDir);
            CrashHandler::OnAssertionFailure(nullptr, 0, nullptr);
        },
        "");
}

static void TriggerSegfault()
{
#if defined(_WIN32)
    CrashHandler::OnCrash("SIGSEGV");
#else
    volatile int* p = nullptr;
    (void) *p;
#endif
}

TEST_F(CrashHandlerDeathTest, SIGSEGVWritesLog)
{
    ASSERT_DEATH(
        {
            CrashHandler::Install("ZEngineTest", "0.0.1-test", kTestLogDir);
            TriggerSegfault();
        },
        "");

    std::string log = FindLatestLog();
    ASSERT_FALSE(log.empty()) << "No log file found after SIGSEGV";
    EXPECT_NE(ReadFile(log).find("SIGSEGV"), std::string::npos);
}

TEST_F(CrashHandlerDeathTest, DialogSuppressedInHeadlessMode)
{
    ASSERT_DEATH(
        {
            CrashHandler::Install("ZEngineTest", "0.0.1-test", kTestLogDir);
            CrashHandler::OnCrash("headless mode check");
        },
        "");

    EXPECT_FALSE(FindLatestLog().empty()) << "Log must still be written in headless mode";
}

static void TriggerAbort()
{
#if defined(_WIN32)
    CrashHandler::OnCrash("SIGABRT");
#else
    std::abort();
#endif
}

TEST_F(CrashHandlerDeathTest, SIGABRTWritesLog)
{
    ASSERT_DEATH(
        {
            CrashHandler::Install("ZEngineTest", "0.0.1-test", kTestLogDir);
            TriggerAbort();
        },
        "");

    std::string log = FindLatestLog();
    ASSERT_FALSE(log.empty()) << "No log file found after SIGABRT";
    EXPECT_NE(ReadFile(log).find("SIGABRT"), std::string::npos);
}
