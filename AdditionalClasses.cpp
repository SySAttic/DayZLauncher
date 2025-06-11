
#include "DayZLauncher.h"
#include <sstream>
#include <algorithm>


void ServerCache::CacheServer(const ServerInfo& server) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    std::string key = server.ip + ":" + std::to_string(server.port);
    cache[key] = server;
    timestamps[key] = std::chrono::steady_clock::now();
}

ServerInfo* ServerCache::GetCachedServer(const std::string& ip, int port) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    std::string key = ip + ":" + std::to_string(port);

    auto it = cache.find(key);
    if (it != cache.end()) {
        auto timeIt = timestamps.find(key);
        if (timeIt != timestamps.end()) {
            auto age = std::chrono::steady_clock::now() - timeIt->second;
            if (age < cacheTimeout) {
                return &it->second;
            }
        }
    }
    return nullptr;
}

void ServerCache::ClearExpiredEntries() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto now = std::chrono::steady_clock::now();

    for (auto it = timestamps.begin(); it != timestamps.end();) {
        if (now - it->second >= cacheTimeout) {
            cache.erase(it->first);
            it = timestamps.erase(it);
        }
        else {
            ++it;
        }
    }
}


SystemTrayManager::SystemTrayManager(HWND hwnd) : hWnd(hwnd), isTrayVisible(false) {
    ZeroMemory(&nid, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_MAIN_ICON));
    wcscpy_s(nid.szTip, L"DayZ Server Browser");
}

SystemTrayManager::~SystemTrayManager() {
    HideTrayIcon();
}

void SystemTrayManager::ShowTrayIcon() {
    if (!isTrayVisible) {
        Shell_NotifyIcon(NIM_ADD, &nid);
        isTrayVisible = true;
    }
}

void SystemTrayManager::HideTrayIcon() {
    if (isTrayVisible) {
        Shell_NotifyIcon(NIM_DELETE, &nid);
        isTrayVisible = false;
    }
}

void SystemTrayManager::UpdateTrayIcon(const std::wstring& tooltip) {
    if (isTrayVisible) {
        wcscpy_s(nid.szTip, tooltip.c_str());
        Shell_NotifyIcon(NIM_MODIFY, &nid);
    }
}

void SystemTrayManager::ShowTrayMenu(POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, ID_TRAY_RESTORE, L"Restore");
    AppendMenu(hMenu, MF_STRING, ID_TRAY_REFRESH, L"Refresh Servers");
    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
    DestroyMenu(hMenu);
}


ConfigManager::ConfigManager(const std::string& filename) : configFile(filename) {
    OutputDebugStringA(("ConfigManager created with file: " + filename + "\n").c_str());
    SetDefaults();
    LoadConfig(); 
}


void ConfigManager::LoadConfig() {
    OutputDebugStringA(("Attempting to load config from: " + configFile + "\n").c_str());

    SetDefaults();

 
    std::ifstream file(configFile);
    if (!file.is_open()) {
        OutputDebugStringA("Config file not found, using defaults and creating new file\n");
        SaveConfig(); 
        return;
    }

    std::string line;
    int lineCount = 0;
    int loadedCount = 0;

    while (std::getline(file, line)) {
        lineCount++;
        if (!line.empty() && line.find('=') != std::string::npos) {
            size_t pos = line.find('=');
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);

           
            config[key] = value;
            loadedCount++;

            std::string debugMsg = "Loaded: " + key + " = " + value + "\n";
            OutputDebugStringA(debugMsg.c_str());
        }
    }

    std::string summary = "Config loaded: " + std::to_string(lineCount) + " lines, " +
        std::to_string(loadedCount) + " settings loaded, " +
        std::to_string(config.size()) + " total settings\n";
    OutputDebugStringA(summary.c_str());

    file.close();
}


void ConfigManager::DebugConfigState() {
    OutputDebugStringA("=== CONFIG STATE DEBUG ===\n");


    OutputDebugStringA("--- IN MEMORY ---\n");
    for (const auto& pair : config) {
        std::string debugMsg = pair.first + " = '" + pair.second + "'\n";
        OutputDebugStringA(debugMsg.c_str());
    }

 
    OutputDebugStringA("--- IN FILE ---\n");
    std::ifstream file(configFile);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty()) {
                OutputDebugStringA((line + "\n").c_str());
            }
        }
        file.close();
    }
    else {
        OutputDebugStringA("FILE NOT READABLE\n");
    }

    OutputDebugStringA("=== END CONFIG DEBUG ===\n");
}


void ConfigManager::SaveConfig() {
    OutputDebugStringA("=== ConfigManager::SaveConfig() CALLED ===\n");


    for (const auto& pair : config) {
        std::string debugMsg = "About to save: " + pair.first + " = '" + pair.second + "'\n";
        OutputDebugStringA(debugMsg.c_str());
    }

    std::ofstream file;
    file.open(configFile, std::ofstream::out | std::ofstream::trunc);

    if (!file.is_open()) {
        OutputDebugStringA(("ERROR: Cannot open config file: " + configFile + "\n").c_str());

  
        std::string absPath = "C:\\temp\\config.ini";
        file.open(absPath, std::ofstream::out | std::ofstream::trunc);
        if (file.is_open()) {
            OutputDebugStringA(("Fallback: Using absolute path: " + absPath + "\n").c_str());
            configFile = absPath;
        }
        else {
            OutputDebugStringA("FATAL: Cannot open any config file!\n");
            return;
        }
    }

    int written = 0;
    for (const auto& pair : config) {
        file << pair.first << "=" << pair.second << std::endl;
        file.flush();
        written++;

        std::string writeMsg = "WROTE: " + pair.first + "=" + pair.second + "\n";
        OutputDebugStringA(writeMsg.c_str());
    }

    file.close();

    OutputDebugStringA(("Config save completed. Wrote " + std::to_string(written) + " entries to: " + configFile + "\n").c_str());


    std::ifstream verify(configFile);
    if (verify.is_open()) {
        OutputDebugStringA("=== VERIFICATION: Reading config file back ===\n");
        std::string line;
        while (std::getline(verify, line)) {
            OutputDebugStringA(("File contains: " + line + "\n").c_str());
        }
        verify.close();
    }
    else {
        OutputDebugStringA("ERROR: Cannot verify - file not readable!\n");
    }
}



void ConfigManager::SetDefaults() {
    config["autoRefresh"] = "true";
    config["refreshInterval"] = "30";
    config["maxPing"] = "200";
    config["minPlayers"] = "0";
    config["showPassworded"] = "true";
    config["windowWidth"] = "1200";
    config["windowHeight"] = "800";
    config["dayzPath"] = "";           
    config["profileName"] = "";      
    config["profilePath"] = "";      
    config["queryDelay"] = "100";    

    OutputDebugStringA("Set all default config values\n");
}


bool ConfigManager::GetBool(const std::string& key, bool defaultValue) {
    auto it = config.find(key);
    if (it != config.end()) {
        return it->second == "true";
    }
    return defaultValue;
}

int ConfigManager::GetInt(const std::string& key, int defaultValue) {
    auto it = config.find(key);
    if (it != config.end()) {
        try {
            return std::stoi(it->second);
        }
        catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}

std::string ConfigManager::GetString(const std::string& key, const std::string& defaultValue) {
    auto it = config.find(key);
    if (it != config.end()) {
        return it->second;
    }
    return defaultValue;
}

void ConfigManager::SetBool(const std::string& key, bool value) {
    config[key] = value ? "true" : "false";
}

void ConfigManager::SetInt(const std::string& key, int value) {
    config[key] = std::to_string(value);
}

void ConfigManager::SetString(const std::string& key, const std::string& value) {
    OutputDebugStringA(("SetString called: " + key + " = '" + value + "'\n").c_str());

    config[key] = value;

    auto it = config.find(key);
    if (it != config.end()) {
        OutputDebugStringA(("SetString verified in memory: " + key + " = '" + it->second + "'\n").c_str());
    }
    else {
        OutputDebugStringA(("SetString ERROR: Key not found after setting: " + key + "\n").c_str());
    }
}


void ConfigManager::DebugPrintAll() {
    OutputDebugStringA("=== ALL CONFIG VALUES ===\n");
    for (const auto& pair : config) {
        std::string debugMsg = pair.first + " = " + pair.second + "\n";
        OutputDebugStringA(debugMsg.c_str());
    }
    OutputDebugStringA("=== END CONFIG VALUES ===\n");
}