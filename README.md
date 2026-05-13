# Authera C++ Native SDK

**Version: 1.1.0** - Fully Tested & Production Ready ✅

The official Zero-Dependency C++ SDK for integrating Authera's advanced Anti-Piracy and Licensing mechanics directly into your high-performance native desktop applications (Windows).

## Features
- **Zero Heavy Dependencies**: Completely avoids `libcurl` and `OpenSSL`. Natively uses Windows' built-in `WinINet` and `Bcrypt` cryptography!
- **Automatic HWID Extraction**: Retrieves the physical Serial Number of the primary OS drive via `GetVolumeInformationA`.
- **HMAC-SHA256 Request Signing**: Mathematical proofing of all API requests exactly like the Node.js and .NET SDKs to prevent MITM attacks!
- **User Login & Password Auth**: Full authentication with usernames and passwords
- **License Key Validation**: Validate traditional license keys (XXXX-YYYY-ZZZZ-1234 format)
- **Hardware ID Binding**: Automatic binding to machine hardware for DRM protection
- **Smart Auto-Updates**: Natively connects to your Cloudflare R2 bucket to auto-download App Releases, caching versions locally in `%APPDATA%` to save bandwidth!

## Version 1.1.0 - What's New
✅ Implemented `CheckForUpdate()` system for downloading app releases.
✅ Smart caching: Locally stores version info in `%APPDATA%` to prevent redundant downloads.
✅ Fully secure: Downloads are protected behind your App's HMAC-SHA256 API Signature.
✅ Fixed redirect bugs by parsing S3/R2 presigned URLs securely.

## Installation

This is designed to be as lightweight as possible. It is NOT provided as a `.dll` to prevent memory tampering or DLL hijacking in your game or application. 

You must copy the Source Code files directly into your project:

1. Copy `AutheraClient.h` and `AutheraClient.cpp` to your Visual Studio Project.
2. Under your Project Properties, ensure your standard libraries are appropriately linked. (The `.cpp` file already uses `#pragma comment(lib)` for `bcrypt.lib`, `wininet.lib`, `ws2_32.lib`, and `shell32.lib`, so you don't even need to configure your Linker if you use MSVC!).
3. Ensure you have copied the `include/json.hpp` file from `nlohmann` into your project directory as well.

## Basic Usage

### Option A: License Keys

```cpp
#include <iostream>
#include "AutheraClient.h"

int main()
{
    // 1. Initialize Client
    Authera::Client authera("APP-UUID-FROM-DASHBOARD", "APP-API-KEY-FROM-DASHBOARD");

    // 2. Validate
    Authera::ValidationResult res = authera.ValidateLicense("XXXX-YYYY-ZZZZ-1234");

    if (res.Valid) {
        std::cout << "[+] License is active! HWID Bound." << std::endl;
        std::cout << "    Expires: " << res.License.expires_at << std::endl;
        
        // Launch Application Logic
    } else {
        std::cerr << "[-] Access Denied: " << res.Error << std::endl;
        return -1;
    }

    return 0;
}
```

### Option B: User Accounts (Username / Password)

```cpp
#include <iostream>
#include "AutheraClient.h"

int main()
{
    Authera::Client authera("APP-UUID-FROM-DASHBOARD", "APP-API-KEY-FROM-DASHBOARD");

    // Pass the raw credentials from your login GUI
    Authera::LoginResult res = authera.LoginUser("customer_123", "secure_password");

    if (res.Valid) {
        std::cout << "[+] Welcome back, " << res.User.username << "!" << std::endl;
        
        // Launch Application Logic
    } else {
        std::cerr << "[-] Login Failed: " << res.Error << std::endl;
        return -1;
    }

    return 0;
}
```

### Option C: Smart Auto-Updates

The SDK includes a built-in auto-updater. It automatically stores your app's version locally in `%APPDATA%/Authera/`. When you call `CheckForUpdate()`, it compares your local version against the latest active release on the server. If a new version exists (or if the user deleted the executable), it downloads it directly into the AppData folder.

```cpp
#include <iostream>
#include "AutheraClient.h"

int main()
{
    Authera::Client authera("APP-UUID-FROM-DASHBOARD", "APP-API-KEY-FROM-DASHBOARD");

    std::cout << "[*] Checking for updates..." << std::endl;
    Authera::UpdateResult res = authera.CheckForUpdate();

    if (res.Success) {
        if (res.UpToDate) {
            std::cout << "[+] You are already on the latest version! (" << res.Version << ")" << std::endl;
        } else if (res.FileDownloaded) {
            std::cout << "[+] Successfully downloaded new update! (" << res.Version << ")" << std::endl;
            std::cout << "[+] File is located at: " << res.FilePath << std::endl;
            
            // You can now execute res.FilePath using ShellExecute or CreateProcess and exit this app!
        }
    } else {
        std::cerr << "[-] Update check failed: " << res.Error << std::endl;
    }

    return 0;
}
```
