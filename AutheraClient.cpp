#include "AutheraClient.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "include/json.hpp" // nlohmann/json

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

using json = nlohmann::json;

namespace Authera {

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
        std::string parsedResponse = "";
        
        // Parse Host
        std::wstring host = L"rycbncvtdkldnlyfxtak.supabase.co";
        std::wstring path = std::wstring(urlPath.begin(), urlPath.end());

        HINTERNET hSession = WinHttpOpen(L"Authera SDK/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSession) {
            HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (hConnect) {
                HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                if (hRequest) {
                    
                    std::string headerStr = "Content-Type: application/json\r\nX-Authera-Signature: " + signature + "\r\n";
                    std::wstring headers(headerStr.begin(), headerStr.end());
                    
                    if (WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1, (LPVOID)payloadJson.c_str(), (DWORD)payloadJson.length(), (DWORD)payloadJson.length(), 0)) {
                        if (WinHttpReceiveResponse(hRequest, NULL)) {
                            DWORD size = 0;
                            DWORD downloaded = 0;
                            do {
                                WinHttpQueryDataAvailable(hRequest, &size);
                                if (size == 0) break;

                                std::vector<char> outBuffer(size + 1, 0);
                                if (WinHttpReadData(hRequest, (LPVOID)outBuffer.data(), size, &downloaded)) {
                                    parsedResponse.append(outBuffer.data(), downloaded);
                                }
                            } while (size > 0);
                        }
                    }
                    WinHttpCloseHandle(hRequest);
                }
                WinHttpCloseHandle(hConnect);
            }
            WinHttpCloseHandle(hSession);
        }

        return parsedResponse;
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

        try {
            auto resObj = json::parse(rawResp);
            if (!resObj.contains("valid") || !resObj["valid"].get<bool>()) {
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
