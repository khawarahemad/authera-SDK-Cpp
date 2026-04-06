#include "AutheraClient.h"
#include <windows.h>
#include <wininet.h>
#include <bcrypt.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "include/json.hpp" // nlohmann/json

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")

// Define missing WinINet constants if not already defined
#ifndef INTERNET_OPEN_TYPE_DEFAULT_PROXY
#define INTERNET_OPEN_TYPE_DEFAULT_PROXY 1
#endif

#ifndef ERROR_INTERNET_CONNECTION_CLOSED
#define ERROR_INTERNET_CONNECTION_CLOSED 12030
#endif

#ifndef WININET_E_TIMEOUT
#define WININET_E_TIMEOUT 12002
#endif

using json = nlohmann::json;

namespace Authera {

    // Windows Internet error code mapper
    std::string GetWinINetErrorMessage(DWORD dwError) {
        switch (dwError) {
            case ERROR_INTERNET_NAME_NOT_RESOLVED:
                return "ERROR_INTERNET_NAME_NOT_RESOLVED (12007): DNS resolution failed - domain cannot be resolved";
            case ERROR_INTERNET_CONNECTION_RESET:
                return "ERROR_INTERNET_CONNECTION_RESET (12031): Connection was reset by the server";
            case ERROR_INTERNET_CONNECTION_CLOSED:
                return "ERROR_INTERNET_CONNECTION_CLOSED (12030): Connection was closed by the server";
            case ERROR_INTERNET_TIMEOUT:
                return "ERROR_INTERNET_TIMEOUT (12002): Operation timed out";
            case ERROR_INTERNET_INVALID_URL:
                return "ERROR_INTERNET_INVALID_URL (12005): URL format is invalid";
            case ERROR_INTERNET_OPERATION_CANCELLED:
                return "ERROR_INTERNET_OPERATION_CANCELLED (12017): Operation was cancelled by user or system";
            case ERROR_INTERNET_SEC_CERT_CN_INVALID:
                return "ERROR_INTERNET_SEC_CERT_CN_INVALID (12038): Certificate CN does not match hostname";
            case ERROR_INTERNET_SEC_INVALID_CERT:
                return "ERROR_INTERNET_SEC_INVALID_CERT (12055): Invalid or untrusted certificate";
            case ERROR_INTERNET_CANNOT_CONNECT:
                return "ERROR_INTERNET_CANNOT_CONNECT (12029): Cannot establish connection to server";
            case ERROR_INTERNET_INVALID_OPERATION:
                return "ERROR_INTERNET_INVALID_OPERATION (12043): Invalid operation for current state";
            case WININET_E_TIMEOUT:
                return "WININET_E_TIMEOUT: Network timeout occurred";
            case ERROR_NOT_ENOUGH_MEMORY:
                return "ERROR_NOT_ENOUGH_MEMORY: Insufficient memory for operation";
            default:
                return "Unknown error code: " + std::to_string(dwError);
        }
    }

    Client::Client(const std::string& appId, const std::string& clientKey)
        : m_AppId(appId), m_ClientKey(clientKey)
    {
    }

    std::string Client::GetHardwareId() {
        DWORD serialNumber = 0;
        // Fetch the volume serial number of the C: drive as a stable hardware identifier
        if (GetVolumeInformationA("C:\\", NULL, 0, &serialNumber, NULL, NULL, NULL, 0)) {
            std::stringstream stream;
            stream << std::hex << std::uppercase << serialNumber;
            return stream.str();
        }
        return "UNKNOWN_HWID";
    }

    std::string Client::GenerateHmacSha256(const std::string& payload, const std::string& secret) {
        BCRYPT_ALG_HANDLE hAlg = NULL;
        BCRYPT_HASH_HANDLE hHash = NULL;
        std::string result = "";

        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG) >= 0) {
            std::vector<UCHAR> secretBytes(secret.begin(), secret.end());
            if (BCryptCreateHash(hAlg, &hHash, NULL, 0, secretBytes.data(), (ULONG)secretBytes.size(), 0) >= 0) {
                std::vector<UCHAR> dataBytes(payload.begin(), payload.end());
                if (BCryptHashData(hHash, dataBytes.data(), (ULONG)dataBytes.size(), 0) >= 0) {
                    DWORD hashLen = 0;
                    DWORD resultLen = 0;
                    if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen, sizeof(DWORD), &resultLen, 0) >= 0) {
                        std::vector<UCHAR> hashResult(hashLen);
                        if (BCryptFinishHash(hHash, hashResult.data(), hashLen, 0) >= 0) {
                            std::stringstream ss;
                            for (size_t i = 0; i < hashResult.size(); ++i) {
                                ss << std::hex << std::setw(2) << std::setfill('0') << (int)hashResult[i];
                            }
                            result = ss.str();
                        }
                    }
                }
                BCryptDestroyHash(hHash);
            }
            BCryptCloseAlgorithmProvider(hAlg, 0);
        }
        return result;
    }

    std::string Client::SendPostRequest(const std::string& urlPath, const std::string& payloadJson, const std::string& signature) {
        std::string responseData = "";

        // Extract hostname from m_BaseUrl (e.g., "https://bhdvbvmfnorzuclomnvx.supabase.co/functions/v1")
        // Result should be: "bhdvbvmfnorzuclomnvx.supabase.co"
        size_t protocolEnd = m_BaseUrl.find("://");
        if (protocolEnd == std::string::npos) {
            std::cerr << "[ERROR] Invalid m_BaseUrl format: " << m_BaseUrl << std::endl;
            return "";
        }
        
        size_t hostStart = protocolEnd + 3;
        size_t pathStart = m_BaseUrl.find("/", hostStart);
        std::string hostname = m_BaseUrl.substr(hostStart, pathStart - hostStart);
        
        std::cerr << "[DEBUG] Using m_BaseUrl: " << m_BaseUrl << std::endl;
        std::cerr << "[DEBUG] Extracted hostname: " << hostname << std::endl;

        // Internet open - try system proxy first, then fallback to direct
        HINTERNET hInternet = InternetOpenA(
            "Authera SDK/1.0",
            INTERNET_OPEN_TYPE_PRECONFIG,  // Use system proxy settings
            NULL,
            NULL,
            0
        );

        if (!hInternet) {
            // Fallback: Try with default proxy
            hInternet = InternetOpenA(
                "Authera SDK/1.0",
                INTERNET_OPEN_TYPE_DEFAULT_PROXY,
                NULL,
                NULL,
                0
            );
        }

        if (!hInternet) {
            // Last resort: Try direct connection
            hInternet = InternetOpenA(
                "Authera SDK/1.0",
                INTERNET_OPEN_TYPE_DIRECT,
                NULL,
                NULL,
                0
            );
        }

        if (!hInternet) {
            DWORD dwError = GetLastError();
            std::cerr << "[ERROR] InternetOpen failed: " << GetWinINetErrorMessage(dwError) << std::endl;
            return "";
        }
        
        std::cerr << "[DEBUG] Internet session opened successfully" << std::endl;

        // Connect to server using extracted hostname
        HINTERNET hConnect = InternetConnectA(
            hInternet,
            hostname.c_str(),
            INTERNET_DEFAULT_HTTPS_PORT,
            NULL,
            NULL,
            INTERNET_SERVICE_HTTP,
            0,
            0
        );

        if (!hConnect) {
            DWORD dwError = GetLastError();
            std::cerr << "[ERROR] InternetConnect failed: " << GetWinINetErrorMessage(dwError) << std::endl;
            std::cerr << "[ERROR] Cannot connect to " << hostname << ":443" << std::endl;
            InternetCloseHandle(hInternet);
            return "";
        }

        std::cerr << "[DEBUG] Connected to " << hostname << " successfully" << std::endl;

        // Open request handle with URL path
        DWORD dwFlags = INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD;
        HINTERNET hRequest = HttpOpenRequestA(
            hConnect,
            "POST",
            urlPath.c_str(),
            "HTTP/1.1",
            NULL,
            NULL,
            dwFlags,
            0
        );

        if (!hRequest) {
            DWORD dwError = GetLastError();
            std::cerr << "[ERROR] HttpOpenRequest failed: " << GetWinINetErrorMessage(dwError) << std::endl;
            InternetCloseHandle(hConnect);
            InternetCloseHandle(hInternet);
            return "";
        }

        std::cerr << "[DEBUG] Request handle created for POST " << urlPath << std::endl;

        // Set timeout values for connection
        DWORD dwTimeout = 30000; // 30 seconds
        InternetSetOptionA(hRequest, INTERNET_OPTION_CONNECT_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
        InternetSetOptionA(hRequest, INTERNET_OPTION_SEND_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
        InternetSetOptionA(hRequest, INTERNET_OPTION_RECEIVE_TIMEOUT, &dwTimeout, sizeof(dwTimeout));

        // Set headers - proper format for HttpSendRequestA
        // Headers should be null-terminated and separated by \r\n, with final \r\n
        std::string contentLengthHeader = "Content-Length: " + std::to_string(payloadJson.length()) + "\r\n";
        std::string headersStr = "Content-Type: application/json\r\n" + contentLengthHeader + "X-Authera-Signature: " + signature + "\r\n";
        
        std::cerr << "[DEBUG] Headers being sent:" << std::endl;
        std::cerr << "[DEBUG]   Content-Type: application/json" << std::endl;
        std::cerr << "[DEBUG]   Content-Length: " << payloadJson.length() << std::endl;
        std::cerr << "[DEBUG]   X-Authera-Signature: " << signature << std::endl;
        
        // Send request
        std::cerr << "[DEBUG] Sending request to: " << urlPath << std::endl;
        std::cerr << "[DEBUG] Payload: " << payloadJson << std::endl;
        std::cerr << "[DEBUG] Payload size: " << payloadJson.length() << " bytes" << std::endl;
        
        BOOL bSend = HttpSendRequestA(
            hRequest,
            headersStr.c_str(),
            (DWORD)-1,  // -1 means lpszHeaders is null-terminated
            (LPVOID)payloadJson.c_str(),
            (DWORD)payloadJson.length()
        );

        if (!bSend) {
            DWORD dwError = GetLastError();
            std::cerr << "[ERROR] HttpSendRequest failed!" << std::endl;
            std::cerr << "[ERROR] " << GetWinINetErrorMessage(dwError) << std::endl;
            
            // Special diagnostic for DNS failures
            if (dwError == ERROR_INTERNET_NAME_NOT_RESOLVED) {
                std::cerr << "[CRITICAL] DNS RESOLUTION FAILED!" << std::endl;
                std::cerr << "[CRITICAL] Domain 'rycbncvtdkldnlyfxtak.supabase.co' cannot be resolved" << std::endl;
                std::cerr << "[CRITICAL] Check: " << std::endl;
                std::cerr << "  1. Internet connectivity" << std::endl;
                std::cerr << "  2. DNS server configuration" << std::endl;
                std::cerr << "  3. Network firewall/proxy blocking DNS" << std::endl;
                std::cerr << "  4. Windows Defender blocking DNS resolution" << std::endl;
                std::cerr << "  5. run 'nslookup rycbncvtdkldnlyfxtak.supabase.co' in command prompt" << std::endl;
            }
            
            InternetCloseHandle(hRequest);
            InternetCloseHandle(hConnect);
            InternetCloseHandle(hInternet);
            return "";
        }
        
        std::cerr << "[DEBUG] Request sent successfully" << std::endl;

        // Get HTTP status code
        DWORD dwStatusCode = 0;
        DWORD dwStatusCodeSize = sizeof(dwStatusCode);
        if (HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, 
                          &dwStatusCode, &dwStatusCodeSize, NULL)) {
            std::cerr << "[DEBUG] HTTP Status Code: " << dwStatusCode << std::endl;
        }

        // Read response
        const DWORD BUFFER_SIZE = 4096;
        char szBuffer[BUFFER_SIZE];
        DWORD dwBytesRead = 0;

        while (InternetReadFile(hRequest, szBuffer, BUFFER_SIZE, &dwBytesRead)) {
            if (dwBytesRead == 0) {
                break;
            }
            responseData.append(szBuffer, dwBytesRead);
        }
        
        if (responseData.empty()) {
            DWORD dwError = GetLastError();
            if (dwError != 0) {
                std::cerr << "[ERROR] InternetReadFile failed: " << GetWinINetErrorMessage(dwError) << std::endl;
            }
        }
        
        std::cerr << "[DEBUG] Response received: " << responseData.length() << " bytes" << std::endl;

        // Cleanup
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        
        std::cerr << "[DEBUG] Connection closed" << std::endl;

        return responseData;
    }

    ValidationResult Client::ValidateLicense(const std::string& licenseKey) {
        ValidationResult result = { false, {}, "" };

        std::string hwid = GetHardwareId();
        auto nowRaw = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()).time_since_epoch().count();

        json payload;
        payload["app_id"] = m_AppId;
        payload["license_key"] = licenseKey;
        payload["hwid"] = hwid;
        payload["timestamp"] = nowRaw;

        std::string payloadStr = payload.dump();
        std::string sig = GenerateHmacSha256(payloadStr, m_ClientKey);

        std::string rawResp = SendPostRequest("/functions/v1/license-validate", payloadStr, sig);

        if (rawResp.empty()) {
            result.Error = "Connection Failed";
            return result;
        }

        try {
            auto resObj = json::parse(rawResp);
            if (!resObj.contains("valid") || !resObj["valid"].get<bool>()) {
                result.Error = resObj.value("error", "Unknown validation error");
                return result;
            }

            result.Valid = true;
            if (resObj.contains("license")) {
                auto lic = resObj["license"];
                result.License.id = lic.value("id", "");
                result.License.key = lic.value("key", "");
                result.License.status = lic.value("status", "");
                result.License.hwid = hwid; // Reflected
                result.License.expires_at = lic.contains("expires_at") && !lic["expires_at"].is_null() ? lic["expires_at"].get<std::string>() : "";
            }
        }
        catch (const std::exception& e) {
            result.Error = std::string("JSON Parse Error: ") + e.what();
        }

        return result;
    }

    LoginResult Client::LoginUser(const std::string& username, const std::string& password) {
        LoginResult result = { false, {}, "" };

        std::string hwid = GetHardwareId();
        auto nowRaw = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()).time_since_epoch().count();

        json payload;
        payload["app_id"] = m_AppId;
        payload["username"] = username;
        payload["password"] = password;
        payload["hwid"] = hwid;
        payload["timestamp"] = nowRaw;

        std::string payloadStr = payload.dump();
        std::string sig = GenerateHmacSha256(payloadStr, m_ClientKey);

        std::string rawResp = SendPostRequest("/functions/v1/user-login", payloadStr, sig);

        if (rawResp.empty()) {
            result.Error = "Connection Failed";
            return result;
        }

        // Debug: Print raw response
        std::cerr << "[DEBUG] Raw response data: " << rawResp << std::endl;

        try {
            auto resObj = json::parse(rawResp);
            std::cerr << "[DEBUG] Parsed JSON successfully" << std::endl;
            if (!resObj.contains("valid") || !resObj["valid"].get<bool>()) {
                std::cerr << "[DEBUG] Response does not have valid=true" << std::endl;
                result.Error = resObj.value("error", "Unknown login error");
                return result;
            }

            result.Valid = true;
            if (resObj.contains("user")) {
                auto usr = resObj["user"];
                result.User.id = usr.value("id", "");
                result.User.username = usr.value("username", "");
                result.User.status = usr.value("status", "");
                result.User.hwid = hwid;
                result.User.expires_at = usr.contains("expires_at") && !usr["expires_at"].is_null() ? usr["expires_at"].get<std::string>() : "";
            }
        }
        catch (const std::exception& e) {
            result.Error = std::string("JSON Parse Error: ") + e.what();
        }

        return result;
    }

} // namespace Authera
