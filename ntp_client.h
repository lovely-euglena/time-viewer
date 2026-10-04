#include <winsock2.h>
#include <windows.h>
#include <time.h>

/**
 * @file ntp_client.h
 * @brief NTP客户端模块头文件
 *
 * 该头文件定义了NTP客户端的核心接口、数据结构和常量，
 * 支持Windows 10及以上系统，兼容C++17标准。
 * 提供网络时间同步功能，包括初始化、时间获取、自动同步等。
 */

 // 静态链接或EXE内部使用，不需要导入导出
#define NTPLIB_API
#define NTP_CALL __stdcall

/**
 * @brief 错误码定义
 */
    typedef enum {
    NTP_ERROR_NONE = 0,           ///< 成功，无错误
    NTP_SOCKET_ERROR,             ///< Socket创建失败
    NTP_RESOLVE_ERROR,            ///< DNS解析失败
    NTP_SEND_ERROR,               ///< 请求发送失败
    NTP_RECV_ERROR,               ///< 响应接收失败
    NTP_TIMEOUT,                  ///< 网络超时
    NTP_INVALID_RESPONSE          ///< 无效的NTP响应包
} NTP_ERROR_CODE;

/**
 * @brief 服务器状态枚举
 */
typedef enum {
    SERVER_ACTIVE,                ///< 服务器活跃
    SERVER_DISABLED,              ///< 服务器已禁用
    SERVER_UNKNOWN                ///< 服务器状态未知
} SERVER_STATUS;

/**
 * @brief NTP时间戳类型（64位）
 */
typedef unsigned long long NTP_TIMESTAMP;

/**
 * @brief NTP数据包结构体（RFC 1305）
 */
#pragma pack(push, 1)
typedef struct {
    UCHAR LI_VN_Mode;              ///< Leap Indicator(2), Version Number(3), Mode(3)
    UCHAR Stratum;                 ///< 层级
    UCHAR Poll;                    ///< 同步间隔
    CHAR Precision;                ///< 时间精度
    ULONG RootDelay;               ///< 根延迟
    ULONG RootDispersion;          ///< 根离散度
    ULONG ReferenceID;             ///< 参考源标识符
    ULONG ReferenceTimestampSec;   ///< 参考时间戳秒部分
    ULONG ReferenceTimestampFrac;  ///< 参考时间戳小数部分
    ULONG OriginateTimestampSec;   ///< 发起时间戳秒部分
    ULONG OriginateTimestampFrac;  ///< 发起时间戳小数部分
    ULONG ReceiveTimestampSec;     ///< 接收时间戳秒部分
    ULONG ReceiveTimestampFrac;   ///< 接收时间戳小数部分
    ULONG TransmitTimestampSec;    ///< 发送时间戳秒部分
    ULONG TransmitTimestampFrac;  ///< 发送时间戳小数部分
} NTP_PACKET;
#pragma pack(pop)

/**
 * @brief NTP服务器信息结构体
 */
typedef struct {
    wchar_t serverName[256];       ///< 服务器域名或IP
    int port;                      ///< 服务器端口
    DWORD responseTime;            ///< 响应时间（毫秒）
    SERVER_STATUS status;          ///< 服务器当前状态
} NTP_SERVER_INFO;

/**
 * @brief 常量定义
 */
#define NTP_SERVER_PORT         123
#define NTP_TIME_OFFSET         2208988800UL
#define MAX_RETRY_COUNT         3
#define TIMEOUT_SECONDS         5
#define AUTO_SYNC_INTERVAL      3600
#define NTP_SERVER_MAX_COUNT    10

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * @brief 初始化NTP客户端
     */
    NTPLIB_API bool NTP_CALL InitializeNTPClient();

    /**
     * @brief 清理NTP客户端资源
     */
    NTPLIB_API void NTP_CALL CleanupNTPClient();

    /**
     * @brief 立即执行一次时间同步
     */
    NTPLIB_API bool NTP_CALL SyncTimeOnce();

    /**
     * @brief 启动自动时间同步
     */
    NTPLIB_API void NTP_CALL StartAutoSync();

    /**
     * @brief 停止自动时间同步
     */
    NTPLIB_API void NTP_CALL StopAutoSync();

    /**
     * @brief 添加NTP服务器
     */
    NTPLIB_API bool NTP_CALL AddNtpServer(const wchar_t* server_name);

    /**
     * @brief 移除NTP服务器
     */
    NTPLIB_API void NTP_CALL RemoveNtpServer(const wchar_t* server_name);

    /**
     * @brief 将NTP时间戳转换为Unix时间戳
     */
    NTPLIB_API time_t NTP_CALL ConvertToUnixTime(ULONG ntp_sec, ULONG ntp_frac);

    /**
     * @brief 获取当前NTP客户端状态信息
     */
    NTPLIB_API int NTP_CALL GetClientStatus(int* server_count, int* active_server_count);

    /**
     * @brief 获取最后一次成功同步的网络时间
     * @param outTime 输出参数，接收获取到的网络时间（本地时间，已转换为系统时区）
     * @return 成功返回true，失败返回false
     */
    NTPLIB_API bool NTP_CALL GetLastNetworkTime(SYSTEMTIME* outTime);

    /**
     * @brief 取消正在进行的所有 NTP 操作
     *
     * 设置取消标志并关闭活动 socket，使阻塞的 recvfrom 立即返回 SOCKET_ERROR，
     * 同步线程可在退出时调用此函数快速中断等待，避免 join 长时间阻塞。
     */
    NTPLIB_API void NTP_CALL CancelPendingOperations();

    /**
     * @brief 重置取消标志，使后续同步操作可正常执行
     */
    NTPLIB_API void NTP_CALL ResetCancel();

#ifdef __cplusplus
}
#endif