#include "WorkerThread.h"
#include "ApiClient.h"
#include "Settings.h"

#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>

static std::wstring FormatResetsIn(const std::string& isoTimestamp)
{
    if (isoTimestamp.empty()) return L"";

    std::tm tm = {};
    std::istringstream ss(isoTimestamp);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) return L"Unknown";

    time_t resetTime = _mkgmtime(&tm);
    time_t now = time(nullptr);
    auto diff = static_cast<int64_t>(difftime(resetTime, now));

    if (diff <= 0) return L"Now";

    int days = static_cast<int>(diff / 86400);
    int hours = static_cast<int>((diff % 86400) / 3600);
    int minutes = static_cast<int>((diff % 3600) / 60);

    wchar_t buf[64];
    if (days > 0)
        swprintf_s(buf, L"Resets in %dd %dh", days, hours);
    else if (hours > 0)
        swprintf_s(buf, L"Resets in %dh %dm", hours, minutes);
    else
        swprintf_s(buf, L"Resets in %dm", minutes);

    return buf;
}

static std::wstring Utf8ToWide(const std::string& str)
{
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, result.data(), len);
    return result;
}

void WorkerThread::Start()
{
    m_shutdown = false;
    m_thread = std::thread(&WorkerThread::Run, this);
}

void WorkerThread::Stop()
{
    m_shutdown = true;
    m_cv.notify_one();
    if (m_thread.joinable()) {
        if (m_thread.get_id() != std::this_thread::get_id())
            m_thread.join();
        else
            m_thread.detach();
    }
}

void WorkerThread::RequestRefresh()
{
    m_refreshRequested = true;
    m_cv.notify_one();
}

UsageData WorkerThread::GetSnapshot()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data;
}

static const int kBackoffSeconds = 600;

FILETIME WorkerThread::GetCredentialsFileTime()
{
    FILETIME ft{};
    auto path = Settings::Instance().GetEffectiveCredentialsPath();
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        GetFileTime(hFile, nullptr, nullptr, &ft);
        CloseHandle(hFile);
    }
    return ft;
}

bool CredentialsFileChanged(const FILETIME& a, const FILETIME& b)
{
    return CompareFileTime(&a, &b) != 0;
}

void WorkerThread::Run()
{
    m_lastCredentialsTime = GetCredentialsFileTime();

    while (!m_shutdown) {
        bool skipFetch = false;

        if (m_inBackoff && m_refreshRequested) {
            auto currentTime = GetCredentialsFileTime();
            if (CredentialsFileChanged(m_lastCredentialsTime, currentTime)) {
                m_lastCredentialsTime = currentTime;
                m_inBackoff = false;
            } else {
                skipFetch = true;
            }
        }

        m_refreshRequested = false;

        if (!skipFetch) {
            auto result = FetchUsageWithAutoRefresh();

            int waitSeconds = Settings::Instance().Get().pollInterval;

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (result.success) {
                    m_data.five_hour_pct = result.usage.fiveHourPct;
                    m_data.seven_day_pct = result.usage.sevenDayPct;
                    m_data.five_hour_resets = FormatResetsIn(result.usage.fiveHourResetsAt);
                    m_data.seven_day_resets = FormatResetsIn(result.usage.sevenDayResetsAt);
                    m_data.has_scoped = result.usage.hasScopedWeekly;
                    m_data.scoped_pct = result.usage.scopedWeeklyPct;
                    m_data.scoped_resets = FormatResetsIn(result.usage.scopedWeeklyResetsAt);
                    m_data.scoped_label = Utf8ToWide(result.usage.scopedWeeklyLabel);
                    m_data.last_success_tick = GetTickCount64();
                    m_data.backoff_until_tick = 0;
                    m_inBackoff = false;

                    if (!result.error.empty()) {
                        m_data.has_error = true;
                        m_data.error_msg = Utf8ToWide(result.error);
                    } else {
                        m_data.has_error = false;
                        m_data.error_msg.clear();
                    }
                } else {
                    m_data.has_error = true;
                    if (result.rateLimited) {
                        m_inBackoff = true;
                        int backoff = kBackoffSeconds;
                        // Honor the server's Retry-After when present, within sane bounds
                        if (result.retryAfterSec > 0) {
                            backoff = result.retryAfterSec;
                            if (backoff < 60) backoff = 60;
                            if (backoff > 3600) backoff = 3600;
                        }
                        waitSeconds = backoff;
                        m_lastCredentialsTime = GetCredentialsFileTime();
                        m_data.error_msg = L"Rate limited";
                        m_data.backoff_until_tick = GetTickCount64()
                            + static_cast<ULONGLONG>(backoff) * 1000;
                    } else {
                        m_data.error_msg = Utf8ToWide(result.error);
                    }
                }
            }

            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait_for(lock, std::chrono::seconds(waitSeconds), [this] {
                return m_shutdown.load() || m_refreshRequested.load();
            });
        } else {
            // Refresh request arrived mid-backoff: keep waiting only until the
            // original deadline instead of re-arming a fresh backoff window
            std::unique_lock<std::mutex> lock(m_mutex);
            ULONGLONG now = GetTickCount64();
            int remainSec = 1;
            if (m_data.backoff_until_tick > now)
                remainSec = static_cast<int>((m_data.backoff_until_tick - now) / 1000) + 1;
            m_cv.wait_for(lock, std::chrono::seconds(remainSec), [this] {
                return m_shutdown.load() || m_refreshRequested.load();
            });
        }
    }
}
