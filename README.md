# Authera C++ Native SDK

The official Zero-Dependency C++ SDK for integrating Authera's advanced Anti-Piracy and Licensing mechanics directly into your high-performance native desktop applications (Windows).

## Features
- **Zero Heavy Dependencies**: Completely avoids `libcurl` and `OpenSSL`. Natively uses Windows' built-in `WinHttp` and `Bcrypt` cryptography!
- **Automatic HWID Extraction**: Retrieves the physical Serial Number of the primary OS drive via `GetVolumeInformationA`.
- **HMAC-SHA256 Request Signing**: Mathematical proofing of all API requests exactly like the Node.js and .NET SDKs to prevent MITM attacks!

## Installation

This is designed to be as lightweight as possible. It is NOT provided as a `.dll` to prevent memory tampering or DLL hijacking in your game or application. 

You must copy the Source Code files directly into your project:

1. Copy `AutheraClient.h` and `AutheraClient.cpp` to your Visual Studio Project.
2. Under your Project Properties, ensure your standard libraries are appropriately linked. (The `.cpp` file already uses `#pragma comment(lib)` so you don't even need to configure your Linker!).
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
