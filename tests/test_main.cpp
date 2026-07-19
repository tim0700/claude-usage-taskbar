#include <cstdio>
#include <cstdlib>
#include <crtdbg.h>
#include <cassert>
#include <string>
#include <ctime>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../src/ApiClient.h"
#include "../src/WorkerThread.h"
#include <thread>
#include <chrono>

void test_placeholder();
void test_parse_usage_response();
void test_read_credentials();
void test_fetch_usage();
void test_worker_thread();
void test_worker_request_refresh();

int main()
{
    // Fail as a console message instead of the interactive abort dialog,
    // so unattended runs don't hang on a message box
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

    printf("=== Claude Usage Plugin Tests ===\n\n");

    test_placeholder();
    test_parse_usage_response();
    test_read_credentials();
    test_fetch_usage();
    test_worker_thread();
    test_worker_request_refresh();

    printf("\n=== All tests passed ===\n");
    return 0;
}

void test_placeholder()
{
    printf("[PASS] test_placeholder\n");
}

void test_parse_usage_response()
{
    // Full response with the newer "limits" array carrying a model-scoped weekly limit
    const char* full = R"({
        "five_hour": {"utilization": 86.0, "resets_at": "2026-07-18T17:20:00.018163+00:00"},
        "seven_day": {"utilization": 27.0, "resets_at": "2026-07-22T22:00:00.018179+00:00"},
        "seven_day_opus": null,
        "limits": [
            {"kind": "session", "group": "session", "percent": 86, "resets_at": "2026-07-18T17:20:00.018163+00:00", "scope": null},
            {"kind": "weekly_all", "group": "weekly", "percent": 27, "resets_at": "2026-07-22T22:00:00.018179+00:00", "scope": null},
            {"kind": "weekly_scoped", "group": "weekly", "percent": 51, "resets_at": "2026-07-22T22:00:00.018425+00:00",
             "scope": {"model": {"id": null, "display_name": "Fable"}, "surface": null}}
        ]
    })";
    auto r = ParseUsageResponse(full);
    assert(r.success);
    assert(r.error.empty());
    assert(r.usage.fiveHourPct == 86.0);
    assert(r.usage.sevenDayPct == 27.0);
    assert(r.usage.hasScopedWeekly);
    assert(r.usage.scopedWeeklyPct == 51.0);
    assert(r.usage.scopedWeeklyLabel == "Fable");
    assert(r.usage.scopedWeeklyResetsAt == "2026-07-22T22:00:00.018425+00:00");

    // Legacy response without limits array: no scoped data, still succeeds
    const char* legacy = R"({
        "five_hour": {"utilization": 40.0, "resets_at": "2026-07-18T17:20:00+00:00"},
        "seven_day": {"utilization": 12.5, "resets_at": "2026-07-22T22:00:00+00:00"}
    })";
    r = ParseUsageResponse(legacy);
    assert(r.success);
    assert(!r.usage.hasScopedWeekly);
    assert(r.usage.fiveHourPct == 40.0);
    assert(r.usage.sevenDayPct == 12.5);

    // Limits-only response: session/weekly fall back to the limits array
    const char* limitsOnly = R"({
        "limits": [
            {"kind": "session", "group": "session", "percent": 70, "resets_at": "2026-07-18T17:20:00+00:00", "scope": null},
            {"kind": "weekly_all", "group": "weekly", "percent": 30, "resets_at": "2026-07-22T22:00:00+00:00", "scope": null}
        ]
    })";
    r = ParseUsageResponse(limitsOnly);
    assert(r.success);
    assert(r.error.empty());
    assert(r.usage.fiveHourPct == 70.0);
    assert(r.usage.sevenDayPct == 30.0);
    assert(!r.usage.hasScopedWeekly);

    // Scoped entry without a model display name falls back to a generic label
    const char* unnamed = R"({
        "five_hour": {"utilization": 1.0, "resets_at": ""},
        "seven_day": {"utilization": 2.0, "resets_at": ""},
        "limits": [
            {"kind": "weekly_scoped", "group": "weekly", "percent": 9, "resets_at": "", "scope": null}
        ]
    })";
    r = ParseUsageResponse(unnamed);
    assert(r.success);
    assert(r.usage.hasScopedWeekly);
    assert(r.usage.scopedWeeklyLabel == "Model");

    // Malformed JSON reports a parse error
    r = ParseUsageResponse("not json");
    assert(!r.success);
    assert(!r.error.empty());

    printf("[PASS] test_parse_usage_response\n");
}

void test_read_credentials()
{
    auto result = ReadCredentials();
    if (result.success) {
        assert(!result.credentials.accessToken.empty());
        assert(result.credentials.expiresAt > 0);
        printf("[PASS] test_read_credentials (live)\n");
    } else {
        printf("[SKIP] test_read_credentials: %s\n", result.error.c_str());
    }
}

void test_fetch_usage()
{
    auto result = FetchUsageWithAutoRefresh();
    if (result.success) {
        assert(result.usage.fiveHourPct >= 0.0 && result.usage.fiveHourPct <= 100.0);
        assert(result.usage.sevenDayPct >= 0.0 && result.usage.sevenDayPct <= 100.0);
        printf("[PASS] test_fetch_usage (live) - 5h: %.1f%%, 7d: %.1f%%",
            result.usage.fiveHourPct, result.usage.sevenDayPct);
        if (result.usage.hasScopedWeekly)
            printf(", %s: %.1f%%", result.usage.scopedWeeklyLabel.c_str(), result.usage.scopedWeeklyPct);
        printf("\n");
    } else if (result.rateLimited) {
        printf("[SKIP] test_fetch_usage: rate limited (429) - not a code failure\n");
    } else {
        printf("[FAIL] test_fetch_usage: %s\n", result.error.c_str());
        assert(false);
    }
}

static bool IsRateLimited(const UsageData& snap)
{
    return snap.has_error && snap.error_msg.find(L"Rate limited") != std::wstring::npos;
}

void test_worker_thread()
{
    WorkerThread worker;
    worker.Start();

    for (int i = 0; i < 150; ++i) {
        auto snap = worker.GetSnapshot();
        if (snap.last_success_tick > 0) {
            assert(snap.five_hour_pct >= 0.0 && snap.five_hour_pct <= 100.0);
            assert(snap.seven_day_pct >= 0.0 && snap.seven_day_pct <= 100.0);
            printf("[PASS] test_worker_thread - 5h: %.1f%%, 7d: %.1f%%\n",
                snap.five_hour_pct, snap.seven_day_pct);
            worker.Stop();
            return;
        }
        if (IsRateLimited(snap)) {
            printf("[SKIP] test_worker_thread: rate limited (429) - not a code failure\n");
            worker.Stop();
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    worker.Stop();
    printf("[FAIL] test_worker_thread: no data after 15s\n");
    assert(false);
}

void test_worker_request_refresh()
{
    WorkerThread worker;
    worker.Start();

    for (int i = 0; i < 150; ++i) {
        auto snap = worker.GetSnapshot();
        if (snap.last_success_tick > 0) break;
        if (IsRateLimited(snap)) {
            printf("[SKIP] test_worker_request_refresh: rate limited (429) - not a code failure\n");
            worker.Stop();
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto before = worker.GetSnapshot().last_success_tick;
    worker.RequestRefresh();

    for (int i = 0; i < 150; ++i) {
        auto snap = worker.GetSnapshot();
        if (snap.last_success_tick > before) {
            printf("[PASS] test_worker_request_refresh\n");
            worker.Stop();
            return;
        }
        if (IsRateLimited(snap)) {
            printf("[SKIP] test_worker_request_refresh: rate limited (429) on refresh\n");
            worker.Stop();
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    worker.Stop();
    printf("[FAIL] test_worker_request_refresh: no update\n");
    assert(false);
}
