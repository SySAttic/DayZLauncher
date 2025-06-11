#pragma once

#include <windows.h>
#include <commctrl.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <memory>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <deque>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <functional>
#include <queue>
#include <condition_variable>
#include <shlobj.h>
#include <commdlg.h>
#include "ServerQuery.h"
#include "FavoritesManager.h"
#include "resource.h"
#include <regex>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")



class ThreadPool {
private:
    std::vector<std::thread> workers;
    bool stop;

public:
    ThreadPool(size_t threads = 4) : stop(false) {
    
    }

    ~ThreadPool() {
        stop = true;
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }


    template<class F>
    void enqueue(F&& f) {
     
    }
};

class ServerCache {
private:
    std::unordered_map<std::string, ServerInfo> cache;
    std::unordered_map<std::string, std::chrono::time_point<std::chrono::steady_clock>> timestamps;
    std::chrono::minutes cacheTimeout{ 5 };
    std::mutex cacheMutex;

public:
    void CacheServer(const ServerInfo& server);
    ServerInfo* GetCachedServer(const std::string& ip, int port);
    void ClearExpiredEntries();
};

class SystemTrayManager {
private:
    NOTIFYICONDATA nid;
    HWND hWnd;
    bool isTrayVisible;

public:
    SystemTrayManager(HWND hwnd);
    ~SystemTrayManager();

    void ShowTrayIcon();
    void HideTrayIcon();
    void UpdateTrayIcon(const std::wstring& tooltip);
    void ShowTrayMenu(POINT pt);
};

class ConfigManager {
private:
    std::string configFile;
    std::unordered_map<std::string, std::string> config;

public:
    ConfigManager(const std::string& filename = "config.ini");
    void DebugConfigState();
    void LoadConfig();
    void SaveConfig();
    void SetDefaults();
    void DebugPrintAll();
    bool GetBool(const std::string& key, bool defaultValue = false);
    int GetInt(const std::string& key, int defaultValue = 0);
    std::string GetString(const std::string& key, const std::string& defaultValue = "");

    void SetBool(const std::string& key, bool value);
    void SetInt(const std::string& key, int value);
    void SetString(const std::string& key, const std::string& value);
};

class DayZLauncher {
private:

    HWND hWnd;
    HWND hTab;
    HWND hServerList;
    HWND hRefreshBtn;
    HWND hJoinBtn;
    HWND hFavoriteBtn;
    HWND hFilterEdit;
    HWND hStatusBar;
    HWND hProgressBar;
    HWND hMapFilterEdit;
    HWND hShowFavoritesCheck;
    HWND hShowPlayedCheck;
    HWND hShowPasswordCheck;
    HWND hShowOnlineCheck;
    HWND hMinPlayersEdit;
    HWND hMaxPingEdit;


    HWND hFilterPanel;    
    HWND hFilterSearch;
    HWND hFilterMap;
    HWND hFilterVersion;
    HWND hFilterFavorites;
    HWND hFilterPlayed;
    HWND hFilterPassword;
    HWND hFilterModded;
    HWND hFilterOnline;
    HWND hFilterFirstPerson;
    HWND hFilterThirdPerson;
    HWND hFilterNotFull;
    HWND hFilterReset;
    HWND hFilterRefresh;


    HWND hFilterSearchLabel;
    HWND hFilterMapLabel;
    HWND hFilterVersionLabel;
    HWND hFilterOptionsLabel;


    HWND hProfileNameEdit;
    HWND hProfilePathEdit;
    HWND hDayZPathEdit;
    HWND hBrowseProfileBtn;
    HWND hBrowseDayZBtn;
    HWND hQueryDelayEdit;
    HWND hProfileNameLabel;
    HWND hProfilePathLabel;
    HWND hDayZPathLabel;
    HWND hQueryDelayLabel;
    HWND hSaveSettingsBtn;
    HWND hReloadSettingsBtn;
    HWND hTestDayZBtn;
    HWND hForceSaveBtn;
    HWND hEmergencySaveBtn;
    HWND hColorBgEdit;
    HWND hColorTextEdit;
    HWND hColorButtonEdit;
    HWND hApplyThemeBtn;
    HWND hColorBgLabel;
    HWND hColorTextLabel;
    HWND hColorButtonLabel;


    std::vector<ServerInfo> servers;
    std::mutex serverMutex;
    int currentTab = 0;
    std::atomic<bool> isRefreshing{ false };
    std::string filterText;


    DWORD filterFlags = 0;
    std::string searchFilter;
    std::string mapFilter;
    std::string versionFilter;

    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastServerRefresh;
    std::mutex refreshMutex;


    std::vector<std::string> ExtractWorkshopIDs(const std::string& modString);
    bool QueryDZSAServerMods(const std::string& ip, int port, std::vector<std::string>& mods);


    bool QueryMultipleAPIs(std::vector<std::pair<std::string, int>>& servers);
    bool QueryBattleMetricsAPI(std::vector<std::pair<std::string, int>>& servers);
    bool QueryGameTrackerAPI(std::vector<std::pair<std::string, int>>& servers);

  
    std::unique_ptr<ServerQueryManager> queryManager;
    std::unique_ptr<FavoritesManager> favoritesManager;
    std::unique_ptr<SystemTrayManager> trayManager;
    std::unique_ptr<ConfigManager> configManager;
    std::unique_ptr<ServerCache> serverCache;
    std::unique_ptr<ThreadPool> threadPool;


    std::thread refreshThread;
    bool shouldStopRefresh = false;

 
    int currentSortColumn = SORT_PING;
    bool sortAscending = true;

  
    WNDPROC originalListViewProc = nullptr;

    std::string HttpGet(const std::wstring& host, const std::wstring& path, int port = 443, bool useSSL = true);
    std::string ParseJsonString(const std::string& json, const std::string& key);
    std::vector<std::string> ParseJsonArray(const std::string& json, const std::string& arrayKey);
    std::vector<std::string> QueryBattleMetricsMods(const std::string& ip, int port);
    std::vector<std::string> QueryGameTrackerMods(const std::string& ip, int port);
    bool QueryDZSAAPI(std::vector<std::pair<std::string, int>>& servers);

  
    std::vector<std::string> ParseModString(const std::string& modString);
    std::vector<std::string> DetectModsFromServerName(const std::string& serverName);
    std::vector<std::string> ParseRealModIDs(const std::string& modString);
    std::vector<std::string> QueryBattlEyeInfo(const std::string& ip, int port);

    bool DetectOfficialServer(const std::string& name, const std::string& folder);
    bool ExtractModsFromRules(const A2SRulesResponse& rules, std::vector<std::string>& mods);
    bool ServerNameIndicatesMods(const std::string& name);
    std::string GetCountryFromIP(const std::string& ip);

public:
    DayZLauncher();
    ~DayZLauncher();

    bool LaunchViaSteam(ServerInfo* server);

    void ForceSaveDayZPath();
    void EmergencyManualSave();
    void ShowServerContextMenu(POINT pt);
    void ApplyUserTheme();
    void ShowSettingsControls(bool show);
    void ShowThemeControls(bool show);
    void CreateVersionDropdown(int x, int y);


    void CreateFilterPanel();
    void UpdateFilteredServerList();
    void CreateMapDropdown(int x, int y);
    void ResetFilters();
    void RefreshSingleServer(const std::string& ip, int port);
    void OnFilterChanged();

  
    void SetupServerListColumns();
    void AddServerToList(const ServerInfo& server, int index);
    std::string FormatServerTime(const ServerInfo& server);
    std::string FormatPlayedStatus(const ServerInfo& server);

 
    bool Initialize(HINSTANCE hInstance);
    void SortServersByColumn(int column);
    void CreateFilterControls();

    void ForceCreateSettingsControls();
    void ShowSettingsLabels(bool show);
    void CreateSettingsControls();
    void LoadSettingsValues();
    void SaveSettingsValues();
    void ShowSettingsTab(bool show);
    void OnBrowseProfile();
    void OnBrowseDayZ();
    void TestDayZPathControl();
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void DebugServerDetection();
    void DebugFavorites();
    void CreateControls();
    void SetupEnhancedListView();
    void CreateControlPanel();
    void ResizeControls();
    void ApplyModernStyling();
    void TestLoadSettings();


    void RefreshServers();
    void PopulateServerList();
    void FilterServers();
    void OnTabChanged();
    void OnServerSelected();
    void OnSettingsChanged();
    void SetTestDayZPath();

    void JoinServer();
    void ToggleFavorite();
    void AddToFavorites();
    void RemoveFromFavorites();
    void CopyServerAddress();
    void ShowServerDetails();
    void SortByColumn(int column);
    void UpdateStatusBar(const std::string& text);
    void UpdateProgressBar(int progress);
    ServerInfo* GetSelectedServer();
    std::vector<ServerInfo> GetFilteredServers() const;
    void LoadConfiguration();
    void SaveConfiguration();
    std::wstring GetDayZInstallPathW();
    std::string GetDayZInstallPath();
    void MinimizeToTray();
    void RestoreFromTray();
    void HandleTrayMessage(WPARAM wParam, LPARAM lParam);
    void OnCreate();
    void OnDestroy();
    void OnSize(int width, int height);
    void OnCommand(WPARAM wParam, LPARAM lParam);
    void OnNotify(LPARAM lParam);
    void OnTimer(WPARAM wParam);
    void OnContextMenu(WPARAM wParam, LPARAM lParam);
    void OnUpdateProgress(int progress);
    void OnRefreshComplete();
    void ApplyFiltersAndUpdate();
    bool PassesAllFilters(const ServerInfo& server) const;

private:

    HFONT hSmallFont;
    bool PassesFilters(const ServerInfo& server) const;
    void CreateFilterCheckbox(HWND& control, const wchar_t* text, int id, int x, int y, int width = 200);
    void CreateFilterEditBox(HWND& control, int id, int x, int y, int width = 200, int height = 25);
    void CreateFilterLabel(HWND& control, const wchar_t* text, int x, int y, int width = 200);
    HWND hImageControl;          
    HBITMAP hLogoBitmap;         
    static HBITMAP LoadPNGFromResource(HINSTANCE hInstance, int resourceID);
    static HBITMAP LoadPNGFromFile(const std::wstring& filePath, int targetWidth, int targetHeight);
    ServerInfo* FindServerByAddress(const std::string& ip, int port);
    bool IsServerRefreshNeeded(const std::string& ip, int port);
    void MarkServerRefreshed(const std::string& ip, int port);
    std::wstring GetFilterText(HWND control);
    bool GetFilterChecked(HWND control);
    void SetFilterText(HWND control, const std::wstring& text);
    void SetFilterChecked(HWND control, bool checked);
    void SetListViewItemText(int item, int subItem, const std::wstring& text);
    std::wstring StringToWString(const std::string& str) const;
    std::string WStringToString(const std::wstring& wstr);
    void EnableControls(bool enable);
    void UpdateServerCount();
    void RegisterWindowClass(HINSTANCE hInstance);
    HWND CreateMainWindow(HINSTANCE hInstance);
    void InitializeManagers();
    void CleanupManagers();
    bool IsLANAddress(const std::string& ip) const;
    static DWORD CALLBACK RefreshServersThread(LPVOID lpParam);

};

extern std::unique_ptr<DayZLauncher> g_launcher;