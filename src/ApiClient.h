#pragma once

#include <string>
#include <cstdint>

struct Credentials {
    std::string accessToken;
    int64_t expiresAt = 0;
};

struct UsageResult {
    double fiveHourPct = 0.0;
    double sevenDayPct = 0.0;
    std::string fiveHourResetsAt;
    std::string sevenDayResetsAt;
    // Model-scoped weekly limit (e.g. Fable) from the "limits" array; absent on plans without one
    bool hasScopedWeekly = false;
    double scopedWeeklyPct = 0.0;
    std::string scopedWeeklyResetsAt;
    std::string scopedWeeklyLabel;
};

struct ApiResponse {
    bool success = false;
    bool rateLimited = false;
    int retryAfterSec = 0;
    std::string error;
    Credentials credentials;
    UsageResult usage;
};

ApiResponse ReadCredentials();
ApiResponse ParseUsageResponse(const std::string& body);
ApiResponse FetchUsage(const Credentials& creds);
ApiResponse FetchUsageWithAutoRefresh();
