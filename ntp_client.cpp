// ntp_client.cpp
// NTP客户端实现，用于内部时间校准（不修改系统时间）

#define NOMINMAX

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <chrono>
#include <random>
#include <algorithm>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")

#include "ntp_client.h"
#include "debug_log.h"

// 前向声明
class NTPClient;

class NTPClient {
private:
    WSADATA wsaData;
    std::vector<NTP_SERVER_INFO> servers;
    std::mutex server_mutex;
    HANDLE timer_handle;
    std::atomic<bool> is_auto_syncing{ false };
    std::atomic<bool> is_initialized{ false };
    std::mt19937 rng;

    // ===== 新增：网络时间存储 =====
    SYSTEMTIME m_lastNetworkTime;
    bool m_hasNetworkTime;
    CRITICAL_SECTION m_timeLock;

    // ===== 取消机制 & socket 归属 =====
    std::atomic<bool> m_cancelFlag{ false };
    // 当前正在进行收发的 socket。仅在 m_sockMutex 保护下访问，
    // 使 CancelPendingOperations 能关闭它来中断阻塞中的 recvfrom。
    SOCKET m_activeSock = INVALID_SOCKET;
    std::mutex m_sockMutex;
    // 串行化整个同步会话：定时器线程与手动同步可能并发调用 SyncTimeOnce，
    // 用它确保同一时刻只有一个会话操作共享状态（socket、rng、Winsock 等）。
    std::mutex m_syncMutex;

    // 初始化Winsock
    int InitializeWinsock() {
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return NTP_SOCKET_ERROR;
        }
        is_initialized.store(true);
        return NTP_ERROR_NONE;
    }

    // 清理Winsock
    void CleanupWinsock() {
        if (is_initialized.load()) {
            {
                std::lock_guard<std::mutex> lock(m_sockMutex);
                if (m_activeSock != INVALID_SOCKET) {
                    closesocket(m_activeSock);
                    m_activeSock = INVALID_SOCKET;
                }
            }
            WSACleanup();
            is_initialized.store(false);
        }
    }

    // 创建UDP socket，返回新 socket（失败返回 INVALID_SOCKET）
    SOCKET CreateSocket() {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) {
            return INVALID_SOCKET;
        }
        DWORD timeout = TIMEOUT_SECONDS * 1000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        return s;
    }

    // 登记活动 socket；若期间已请求取消则返回 false（调用方负责关闭）
    bool RegisterSocket(SOCKET s) {
        std::lock_guard<std::mutex> lock(m_sockMutex);
        if (m_cancelFlag.load()) {
            return false;
        }
        m_activeSock = s;
        return true;
    }

    // 注销并关闭 socket；若已被取消操作接管则不再重复关闭，避免 double-close
    void ReleaseSocket(SOCKET s) {
        std::lock_guard<std::mutex> lock(m_sockMutex);
        if (m_activeSock == s) {
            closesocket(s);
            m_activeSock = INVALID_SOCKET;
        }
    }

    // 解析主机名
    int ResolveHost(const std::wstring& host, sockaddr_in& addr) {
        ZeroMemory(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(NTP_SERVER_PORT);

        std::string host_a;
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, host.c_str(), (int)host.length(), NULL, 0, NULL, NULL);
        if (size_needed > 0) {
            host_a.resize(size_needed);
            WideCharToMultiByte(CP_UTF8, 0, host.c_str(), (int)host.length(), &host_a[0], size_needed, NULL, NULL);
        }

        if (inet_pton(AF_INET, host_a.c_str(), &addr.sin_addr) == 1) {
            return NTP_ERROR_NONE;
        }

        ADDRINFO hints = { 0 }, * result_info = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;

        int res = getaddrinfo(host_a.c_str(), "123", &hints, &result_info);
        if (res != 0 || result_info == nullptr) {
            return NTP_RESOLVE_ERROR;
        }

        addr = *(sockaddr_in*)result_info->ai_addr;
        freeaddrinfo(result_info);
        return NTP_ERROR_NONE;
    }

    // 发送请求
    int SendRequest(SOCKET s, const sockaddr_in& server_addr) {
        NTP_PACKET packet = { 0 };
        packet.LI_VN_Mode = 0x1B;
        time_t current_time = time(NULL);
        packet.TransmitTimestampSec = htonl((u_long)(current_time + NTP_TIME_OFFSET));
        packet.TransmitTimestampFrac = 0;

        int sent = sendto(s, (const char*)&packet, sizeof(packet), 0,
            (const sockaddr*)&server_addr, sizeof(server_addr));
        if (sent == SOCKET_ERROR) {
            return NTP_SEND_ERROR;
        }
        return NTP_ERROR_NONE;
    }

    // 接收响应
    int ReceiveResponse(SOCKET s, NTP_PACKET& response) {
        sockaddr_in from;
        int from_len = sizeof(from);
        int received = recvfrom(s, (char*)&response, sizeof(response), 0,
            (sockaddr*)&from, &from_len);

        if (received == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAETIMEDOUT) {
                return NTP_TIMEOUT;
            }
            return NTP_RECV_ERROR;
        }

        if (received < (int)sizeof(NTP_PACKET)) {
            return NTP_INVALID_RESPONSE;
        }

        return NTP_ERROR_NONE;
    }

    // 单次同步（内部）：每次会话使用独立 socket
    int SyncOnceWithServer(const std::wstring& host, time_t& network_time) {
        if (m_cancelFlag.load()) {
            return NTP_RECV_ERROR;
        }

        sockaddr_in server_addr;
        int resolve_result = ResolveHost(host, server_addr);
        if (resolve_result != NTP_ERROR_NONE) {
            UpdateServerHealth(host, INT_MAX, false);
            return resolve_result;
        }

        if (m_cancelFlag.load()) {
            return NTP_RECV_ERROR;
        }

        // 每次同步创建自己的 socket，避免定时器线程与手动同步共享同一 SOCKET
        // 造成泄漏 / double-close / 使用已关闭句柄（issue IK6H7H）。
        SOCKET s = CreateSocket();
        if (s == INVALID_SOCKET) {
            return NTP_SOCKET_ERROR;
        }
        if (!RegisterSocket(s)) {
            // 会话建立前已被取消
            closesocket(s);
            return NTP_RECV_ERROR;
        }

        LARGE_INTEGER freq, start_time;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start_time);

        int send_result = SendRequest(s, server_addr);
        if (send_result != NTP_ERROR_NONE) {
            ReleaseSocket(s);
            return send_result;
        }

        NTP_PACKET response;
        int recv_result = ReceiveResponse(s, response);
        if (recv_result != NTP_ERROR_NONE) {
            ReleaseSocket(s);
            UpdateServerHealth(host, INT_MAX, false);
            return recv_result;
        }

        LARGE_INTEGER end_time;
        QueryPerformanceCounter(&end_time);
        LONGLONG rtt_ticks = end_time.QuadPart - start_time.QuadPart;
        int rtt_ms = (int)((rtt_ticks * 1000) / freq.QuadPart);

        ULONG trans_sec = ntohl(response.TransmitTimestampSec);
        ULONG trans_frac = ntohl(response.TransmitTimestampFrac);
        network_time = ConvertToUnixTime(trans_sec, trans_frac);

        ReleaseSocket(s);

        UpdateServerHealth(host, rtt_ms, true);
        return NTP_ERROR_NONE;
    }

    // 更新服务器健康状态
    void UpdateServerHealth(const std::wstring& host, int rtt_ms, bool healthy) {
        std::lock_guard<std::mutex> lock(server_mutex);
        for (auto& server : servers) {
            if (wcscmp(server.serverName, host.c_str()) == 0) {
                server.responseTime = rtt_ms;
                server.status = healthy ? SERVER_ACTIVE : SERVER_DISABLED;
                break;
            }
        }
    }

    // 获取最佳服务器
    std::wstring GetBestServer() {
        std::lock_guard<std::mutex> lock(server_mutex);
        std::wstring best_server;
        int min_rtt = INT_MAX;

        for (const auto& server : servers) {
            if (server.status == SERVER_ACTIVE && (int)server.responseTime < min_rtt) {
                min_rtt = server.responseTime;
                best_server = server.serverName;
            }
        }

        if (best_server.empty() && !servers.empty()) {
            best_server = servers[0].serverName;
        }

        return best_server;
    }

    // 退避延迟
    void ApplyBackoffDelay(int retry_count) {
        int base_delay = 1000;
        int delay = base_delay * (1 << retry_count);
        std::uniform_int_distribution<int> dist(-delay / 2, delay / 2);
        int jitter = dist(rng);
        int total_delay = delay + jitter;
        if (total_delay > 0) {
            Sleep(total_delay);
        }
    }

    // 定时器回调
    static VOID CALLBACK TimerCallback(PVOID lpParam, BOOLEAN TimerOrWaitFired) {
        if (!TimerOrWaitFired) return;
        NTPClient* client = (NTPClient*)lpParam;
        client->SyncTimeOnce();
    }

public:
    NTPClient() : timer_handle(NULL), rng(std::random_device{}()),
        m_hasNetworkTime(false) {
        InitializeCriticalSection(&m_timeLock);
        ZeroMemory(&m_lastNetworkTime, sizeof(m_lastNetworkTime));
        AddServer(L"pool.ntp.org");
        AddServer(L"time.windows.com");
        AddServer(L"ntp.aliyun.com");
    }

    ~NTPClient() {
        StopAutoSync();
        {
            // 等待可能正在执行的同步会话结束，再清理 Winsock，
            // 避免与定时器回调中的 socket 操作竞争。
            std::lock_guard<std::mutex> syncLock(m_syncMutex);
        }
        CleanupWinsock();
        DeleteCriticalSection(&m_timeLock);
    }

    void AddServer(const std::wstring& host) {
        std::lock_guard<std::mutex> lock(server_mutex);
        if (servers.size() >= NTP_SERVER_MAX_COUNT) return;

        NTP_SERVER_INFO info = {};
        wcsncpy_s(info.serverName, _countof(info.serverName), host.c_str(), _TRUNCATE);
        info.port = NTP_SERVER_PORT;
        info.responseTime = 0;
        info.status = SERVER_UNKNOWN;
        servers.push_back(info);
    }

    void RemoveServer(const std::wstring& host) {
        std::lock_guard<std::mutex> lock(server_mutex);
        auto it = std::remove_if(servers.begin(), servers.end(),
            [&host](const NTP_SERVER_INFO& s) { return wcscmp(s.serverName, host.c_str()) == 0; });
        servers.erase(it, servers.end());
    }

    // 执行一次同步，成功时保存网络时间
    bool SyncTimeOnce() {
        // 串行化整个会话：定时器回调与手动同步若并发进入，
        // 会竞争 rng / Winsock 初始化等共享状态。
        std::lock_guard<std::mutex> syncLock(m_syncMutex);

        if (!is_initialized.load()) {
            int init_result = InitializeWinsock();
            if (init_result != NTP_ERROR_NONE) {
                return false;
            }
        }

        time_t network_time = 0;
        int success_count = 0;

        for (int retry = 0; retry < MAX_RETRY_COUNT && success_count == 0; ++retry) {
            if (m_cancelFlag.load()) {
                break;
            }

            std::vector<std::wstring> hosts_to_try;
            std::wstring best = GetBestServer();
            if (!best.empty()) {
                hosts_to_try.push_back(best);
            }

            {
                std::lock_guard<std::mutex> lock(server_mutex);
                for (const auto& s : servers) {
                    if (wcscmp(s.serverName, best.c_str()) != 0) {
                        hosts_to_try.push_back(s.serverName);
                    }
                }
            }

            for (const auto& host : hosts_to_try) {
                if (m_cancelFlag.load()) {
                    break;
                }
                int result = SyncOnceWithServer(host, network_time);
                if (result == NTP_ERROR_NONE) {
                    success_count++;

                    // ===== 新增：将网络时间转换为 SYSTEMTIME 并存储 =====
                    if (network_time > 0) {
                        // network_time 是 Unix 时间戳（UTC），需要转换为本地时间
                        // 先转为 FILETIME，再转为 SYSTEMTIME（本地时间）
                        LONGLONG ll = Int32x32To64(network_time, 10000000) + 116444736000000000;
                        FILETIME ft;
                        ft.dwLowDateTime = (DWORD)ll;
                        ft.dwHighDateTime = (DWORD)(ll >> 32);
                        SYSTEMTIME stUtc;
                        FileTimeToSystemTime(&ft, &stUtc);
                        // 转换为本地时间
                        SYSTEMTIME stLocal;
                        SystemTimeToTzSpecificLocalTime(NULL, &stUtc, &stLocal);

                        EnterCriticalSection(&m_timeLock);
                        m_lastNetworkTime = stLocal;
                        m_hasNetworkTime = true;
                        LeaveCriticalSection(&m_timeLock);
                    }

#ifdef _DEBUG
                    std::wcout << L"成功从 " << host << L" 获取网络时间: " << network_time << std::endl;
#endif
                    LOGI(L"NTP: 从 %s 同步成功", host.c_str());
                    break;
                }
                else {
                    LOGV(L"NTP: %s 同步失败 (错误码 %d)", host.c_str(), result);
                    if (retry < MAX_RETRY_COUNT - 1) {
                        if (m_cancelFlag.load()) {
                            break;
                        }
                        ApplyBackoffDelay(retry);
                    }
                }
            }
        }

        if (success_count == 0 && !m_cancelFlag.load()) {
            LOGI(L"NTP: 同步失败（所有服务器均不可用）");
        }
        return success_count > 0;
    }

    // 启动自动同步
    bool StartAutoSync() {
        if (is_auto_syncing.load()) {
            return true;
        }

        if (!CreateTimerQueueTimer(&timer_handle, NULL, TimerCallback, this,
            AUTO_SYNC_INTERVAL * 1000, AUTO_SYNC_INTERVAL * 1000, WT_EXECUTEDEFAULT)) {
            return false;
        }

        is_auto_syncing.store(true);
        return true;
    }

    // 停止自动同步
    void StopAutoSync() {
        if (is_auto_syncing.load()) {
            if (timer_handle) {
                DeleteTimerQueueTimer(NULL, timer_handle, NULL);
                timer_handle = NULL;
            }
            is_auto_syncing.store(false);
        }
    }

    // 获取最后一次网络时间
    bool GetLastNetworkTime(SYSTEMTIME* outTime) {
        if (!outTime) return false;

        EnterCriticalSection(&m_timeLock);
        bool has = m_hasNetworkTime;
        if (has) {
            *outTime = m_lastNetworkTime;
        }
        LeaveCriticalSection(&m_timeLock);

        return has;
    }

    int GetServerInfo(NTP_SERVER_INFO* server_list, int max_count) {
        std::lock_guard<std::mutex> lock(server_mutex);
        size_t count = std::min<size_t>(servers.size(), max_count);
        for (size_t i = 0; i < count; ++i) {
            server_list[i] = servers[i];
        }
        return (int)count;
    }

    int GetClientStatus(int* server_count, int* active_server_count) {
        std::lock_guard<std::mutex> lock(server_mutex);
        if (server_count) *server_count = (int)servers.size();

        if (active_server_count) {
            int active = 0;
            for (const auto& server : servers) {
                if (server.status == SERVER_ACTIVE) active++;
            }
            *active_server_count = active;
        }
        return NTP_ERROR_NONE;
    }

    void CancelPendingOperations() {
        m_cancelFlag.store(true);

        // 关闭活动 socket 以中断阻塞中的 recvfrom。整个“取出并关闭”在
        // m_sockMutex 内完成，与 ReleaseSocket 互斥，避免 double-close
        // 或句柄被复用后误关。
        std::lock_guard<std::mutex> lock(m_sockMutex);
        if (m_activeSock != INVALID_SOCKET) {
            closesocket(m_activeSock);
            m_activeSock = INVALID_SOCKET;
        }
    }

    void ResetCancel() {
        m_cancelFlag.store(false);
    }
};

// 全局NTP客户端实例
static NTPClient* g_ntpClient = nullptr;

// 工具函数
time_t ConvertToUnixTimeImpl(ULONG ntp_sec, ULONG ntp_frac) {
    if (ntp_sec == 0) return 0;
    return (time_t)(ntp_sec - NTP_TIME_OFFSET);
}

// C接口实现
extern "C" {

    NTPLIB_API bool NTP_CALL InitializeNTPClient() {
        if (g_ntpClient != nullptr) return true;
        g_ntpClient = new NTPClient();
        return g_ntpClient != nullptr;
    }

    NTPLIB_API void NTP_CALL CleanupNTPClient() {
        if (g_ntpClient != nullptr) {
            delete g_ntpClient;
            g_ntpClient = nullptr;
        }
    }

    NTPLIB_API bool NTP_CALL SyncTimeOnce() {
        if (!g_ntpClient) return false;
        return g_ntpClient->SyncTimeOnce();
    }

    NTPLIB_API void NTP_CALL StartAutoSync() {
        if (g_ntpClient) g_ntpClient->StartAutoSync();
    }

    NTPLIB_API void NTP_CALL StopAutoSync() {
        if (g_ntpClient) g_ntpClient->StopAutoSync();
    }

    NTPLIB_API bool NTP_CALL AddNtpServer(const wchar_t* server_name) {
        if (!g_ntpClient || !server_name) return false;
        g_ntpClient->AddServer(server_name);
        return true;
    }

    NTPLIB_API void NTP_CALL RemoveNtpServer(const wchar_t* server_name) {
        if (g_ntpClient && server_name) g_ntpClient->RemoveServer(server_name);
    }

    NTPLIB_API time_t NTP_CALL ConvertToUnixTime(ULONG ntp_sec, ULONG ntp_frac) {
        return ConvertToUnixTimeImpl(ntp_sec, ntp_frac);
    }

    NTPLIB_API int NTP_CALL GetClientStatus(int* server_count, int* active_server_count) {
        if (!g_ntpClient) return NTP_SOCKET_ERROR;
        return g_ntpClient->GetClientStatus(server_count, active_server_count);
    }

    NTPLIB_API bool NTP_CALL GetLastNetworkTime(SYSTEMTIME* outTime) {
        if (!g_ntpClient || !outTime) return false;
        return g_ntpClient->GetLastNetworkTime(outTime);
    }

    NTPLIB_API void NTP_CALL CancelPendingOperations() {
        if (g_ntpClient) g_ntpClient->CancelPendingOperations();
    }

    NTPLIB_API void NTP_CALL ResetCancel() {
        if (g_ntpClient) g_ntpClient->ResetCancel();
    }

} // extern "C"