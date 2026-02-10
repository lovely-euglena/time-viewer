#pragma once

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

 // 修改：EXE项目不需要DLL导入导出，直接定义为空
#ifdef NTPLIB_DLL_EXPORTS
#define NTPLIB_API __declspec(dllexport)
#elif defined(NTPLIB_DLL_IMPORTS)
#define NTPLIB_API __declspec(dllimport)
#else
#define NTPLIB_API  // 静态链接或EXE内部使用，不需要导入导出
#endif

// 调用约定定义
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
 *
 * 高32位表示秒数，低32位表示小数部分（分数）
 */
typedef unsigned long long NTP_TIMESTAMP;

/**
 * @brief NTP数据包结构体（RFC 1305）
 *
 * 所有字段在网络传输中均为大端字节序（Big-Endian），
 * 使用时需进行主机序与网络序转换。
 */
#pragma pack(push, 1) // 1字节对齐，确保结构体大小为48字节
typedef struct {
    UCHAR LI_VN_Mode;              ///< Leap Indicator(2), Version Number(3), Mode(3)
    UCHAR Stratum;                 ///< 层级（Stratum level）
    UCHAR Poll;                    ///< 同步间隔（以2为底的对数，秒）
    CHAR Precision;                ///< 时间精度（以2为底的指数）
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
    wchar_t serverName[256];       ///< 服务器域名或IP（宽字符）
    sockaddr_in serverAddr;        ///< 服务器地址结构
    int port;                      ///< 服务器端口
    DWORD responseTime;            ///< 响应时间（毫秒）
    SERVER_STATUS status;          ///< 服务器当前状态
} NTP_SERVER_INFO;

/**
 * @brief 常量定义
 */
#define NTP_SERVER_PORT         123                     ///< NTP服务默认端口
#define NTP_TIME_OFFSET         2208988800UL            ///< 1900年至1970年间的秒数差
#define MAX_RETRY_COUNT         3                       ///< 最大重试次数
#define TIMEOUT_SECONDS         5                       ///< 网络操作超时时间（秒）
#define AUTO_SYNC_INTERVAL      3600                    ///< 自动同步间隔（3600秒 = 1小时）
#define NTP_SERVER_MAX_COUNT    10                      ///< 最大支持的NTP服务器数量

 /**
  * @brief 初始化NTP客户端
  *
  * 初始化Winsock库并准备NTP客户端运行环境。
  * 必须在调用其他NTP函数前调用此函数。
  *
  * @return 成功返回true，失败返回false
  */
extern "C" NTPLIB_API bool NTP_CALL InitializeNTPClient();

/**
 * @brief 清理NTP客户端资源
 *
 * 释放Winsock资源，停止所有后台线程和定时器。
 * 在程序退出前调用此函数。
 */
extern "C" NTPLIB_API void NTP_CALL CleanupNTPClient();

/**
 * @brief 获取网络时间
 *
 * 向配置的NTP服务器发送请求，获取当前网络时间。
 * 使用多个服务器进行冗余查询，选择最优结果。
 *
 * @param[out] error_code 错误码输出参数
 * @return Unix时间戳（秒），失败时返回0
 */
extern "C" NTPLIB_API time_t NTP_CALL GetNetworkTime(int& error_code);

/**
 * @brief 立即执行一次时间同步
 *
 * 主动向NTP服务器请求时间并更新内部状态。
 * 不修改系统时间，仅用于校准本地时间参考。
 *
 * @return 成功返回true，失败返回false
 */
extern "C" NTPLIB_API bool NTP_CALL SyncTimeOnce();

/**
 * @brief 启动自动时间同步
 *
 * 启动后台定时器，每小时自动执行一次时间同步。
 * 如果已在运行，则不重复启动。
 */
extern "C" NTPLIB_API void NTP_CALL StartAutoSync();

/**
 * @brief 停止自动时间同步
 *
 * 停止后台自动同步定时器。
 * 可通过StartAutoSync重新启动。
 */
extern "C" NTPLIB_API void NTP_CALL StopAutoSync();

/**
 * @brief 添加NTP服务器
 *
 * 将指定的NTP服务器添加到服务器列表中。
 * 支持域名和IP地址格式。
 *
 * @param server_name 服务器名称（宽字符字符串）
 * @return 成功返回true，已达最大数量或格式错误返回false
 */
extern "C" NTPLIB_API bool NTP_CALL AddNtpServer(const wchar_t* server_name);

/**
 * @brief 移除NTP服务器
 *
 * 从服务器列表中移除指定的服务器。
 * 支持按域名或IP地址匹配移除。
 *
 * @param server_name 服务器名称（宽字符字符串）
 */
extern "C" NTPLIB_API void NTP_CALL RemoveNtpServer(const wchar_t* server_name);

/**
 * @brief 将NTP时间戳转换为Unix时间戳
 *
 * 将64位NTP时间戳（高32位秒，低32位小数）转换为
 * 32位Unix时间戳（自1970年1月1日以来的秒数）。
 *
 * @param ntp_sec 高32位秒数（已转换为主机字节序）
 * @param ntp_frac 低32位小数部分（已转换为主机字节序）
 * @return 对应的Unix时间戳
 */
extern "C" NTPLIB_API time_t NTP_CALL ConvertToUnixTime(ULONG ntp_sec, ULONG ntp_frac);

/**
 * @brief 获取当前NTP客户端状态信息
 *
 * 获取客户端运行状态、服务器列表状态等诊断信息。
 * 用于调试和监控目的。
 *
 * @param[out] server_count 当前服务器数量输出
 * @param[out] active_server_count 活跃服务器数量输出
 * @return 总体状态码
 */
extern "C" NTPLIB_API int NTP_CALL GetClientStatus(int* server_count, int* active_server_count);