#include "ApiClient.h"
#include "Settings.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <ctime>
#include <vector>

using json = nlohmann::json;

static std::wstring GetCredentialsPath()
{
    return Settings::Instance().GetEffectiveCredentialsPath();
}

static std::string ReadFileUtf8(const std::wstring& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return {};
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

ApiResponse ReadCredentials()
{
    ApiResponse resp;
    auto path = GetCredentialsPath();
    auto content = ReadFileUtf8(path);

    if (content.empty()) {
        resp.error = "credentials not found — install Claude Code and run 'claude login'";
        return resp;
    }

    try {
        auto j = json::parse(content);
        auto& oauth = j.at("claudeAiOauth");
        resp.credentials.accessToken = oauth.at("accessToken").get<std::string>();
        resp.credentials.expiresAt = oauth.at("expiresAt").get<int64_t>();
        resp.success = true;
    } catch (const json::exception& e) {
        resp.error = std::string("JSON parse error: ") + e.what();
    }

    return resp;
}

static const DWORD kTimeoutMs = 10000;

struct HttpResponse {
    bool success = false;
    int statusCode = 0;
    std::string body;
    std::string error;
};

static HttpResponse HttpRequest(
    const wchar_t* host,
    const wchar_t* path,
    const wchar_t* method,
    const wchar_t* headers,
    const std::string& body)
{
    HttpResponse resp;

    HINTERNET hSession = WinHttpOpen(L"claude-usage-taskbar/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!hSession) { resp.error = "WinHttpOpen failed"; return resp; }

    WinHttpSetTimeouts(hSession, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);

    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        resp.error = "WinHttpConnect failed";
        WinHttpCloseHandle(hSession);
        return resp;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, method, path,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        resp.error = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    if (headers) {
        WinHttpAddRequestHeaders(hRequest, headers, static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
    }

    BOOL sent = WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.c_str()),
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()), 0);

    if (!sent || !WinHttpReceiveResponse(hRequest, nullptr)) {
        resp.error = "HTTP request failed (error " + std::to_string(GetLastError()) + ")";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &statusCode, &size, nullptr);
    resp.statusCode = static_cast<int>(statusCode);

    std::string responseBody;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buf(bytesAvailable);
        DWORD bytesRead = 0;
        WinHttpReadData(hRequest, buf.data(), bytesAvailable, &bytesRead);
        responseBody.append(buf.data(), bytesRead);
    }
    resp.body = responseBody;
    resp.success = (statusCode >= 200 && statusCode < 300);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
}

static const wchar_t* kUsageHost = L"api.anthropic.com";
static const wchar_t* kUsagePath = L"/api/oauth/usage";

ApiResponse FetchUsage(const Credentials& creds)
{
    ApiResponse resp;

    std::wstring headers = L"Authorization: Bearer ";
    std::wstring wToken(creds.accessToken.begin(), creds.accessToken.end());
    headers += wToken;
    headers += L"\r\nanthropic-beta: oauth-2025-04-20";

    auto http = HttpRequest(kUsageHost, kUsagePath, L"GET", headers.c_str(), {});

    if (!http.success) {
        resp.error = "Usage fetch failed: " + (http.error.empty()
            ? ("HTTP " + std::to_string(http.statusCode))
            : http.error);
        return resp;
    }

    try {
        auto j = json::parse(http.body);

        auto safeDouble = [](const json& v) { return v.is_number() ? v.get<double>() : 0.0; };
        auto safeString = [](const json& v) { return v.is_string() ? v.get<std::string>() : std::string{}; };

        if (j.contains("five_hour") && j["five_hour"].is_object()) {
            auto& fh = j["five_hour"];
            resp.usage.fiveHourPct = safeDouble(fh.value("utilization", json()));
            resp.usage.fiveHourResetsAt = safeString(fh.value("resets_at", json()));
        }

        if (j.contains("seven_day") && j["seven_day"].is_object()) {
            auto& sd = j["seven_day"];
            resp.usage.sevenDayPct = safeDouble(sd.value("utilization", json()));
            resp.usage.sevenDayResetsAt = safeString(sd.value("resets_at", json()));
        }

        resp.success = true;

        if (!j.contains("five_hour") || !j.contains("seven_day")) {
            resp.error = "Partial data: some usage fields missing (unsupported plan?)";
        }
    } catch (const json::exception& e) {
        resp.error = std::string("Usage response parse error: ") + e.what();
    }

    return resp;
}

ApiResponse FetchUsageWithAutoRefresh()
{
    auto credResult = ReadCredentials();
    if (!credResult.success) return credResult;

    auto usageResult = FetchUsage(credResult.credentials);

    if (!usageResult.success && usageResult.error.find("HTTP 401") != std::string::npos) {
        usageResult.error = "Token expired — open Claude Code to refresh login";
    }

    return usageResult;
}
