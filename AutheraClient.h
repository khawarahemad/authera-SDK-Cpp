#pragma once

#include <string>

// Include standard structures for Authentication
namespace Authera {

    struct LicenseData {
        std::string id;
        std::string key;
        std::string status;
        std::string hwid;
        std::string expires_at;
    };

    struct UserData {
        std::string id;
        std::string username;
        std::string status;
        std::string hwid;
        std::string expires_at;
    };

    struct ValidationResult {
        bool Valid;
        LicenseData License;
        std::string Error;
    };

    struct LoginResult {
        bool Valid;
        UserData User;
        std::string Error;
    };

    class Client {
    public:
        /// <summary>
        /// Initializes the Authera C++ Native Client.
        /// </summary>
        /// <param name="appId">Your Application UUID from the Authera Dashboard</param>
        /// <param name="clientKey">Your Application API Secret Key</param>
        Client(const std::string& appId, const std::string& clientKey);

        /// <summary>
        /// Validates a License Key natively against Edge Functions using HMAC-SHA256 signatures.
        /// Automatically extracts machine Hardware ID.
        /// </summary>
        ValidationResult ValidateLicense(const std::string& licenseKey);

        /// <summary>
        /// Logs an end-user in using Customer Credentials generated via the App Hub.
        /// Automatically locks the account to their PC Hardware ID on first execution.
        /// </summary>
        LoginResult LoginUser(const std::string& username, const std::string& password);

    private:
        std::string m_AppId;
        std::string m_ClientKey;
        std::string m_BaseUrl = "https://bhdvbvmfnorzuclomnvx.supabase.co/functions/v1";

        std::string GetHardwareId();
        std::string GenerateHmacSha256(const std::string& payload, const std::string& secret);
        std::string SendPostRequest(const std::string& endpoint, const std::string& payloadJson, const std::string& signature);
    };

} // namespace Authera
