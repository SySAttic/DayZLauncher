
#include "DayZLauncher.h"
#include "ThemeManager.h"
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#include <algorithm>
#include <cctype>
using std::min;
#include <shellapi.h>
#include <objbase.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <regex>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif


extern std::unique_ptr<DayZLauncher> g_launcher;


DayZLauncher::DayZLauncher() : hWnd(nullptr), hTab(nullptr), hServerList(nullptr),
hRefreshBtn(nullptr), hJoinBtn(nullptr), hFavoriteBtn(nullptr),
hFilterEdit(nullptr), hStatusBar(nullptr), hProgressBar(nullptr),
currentTab(0), shouldStopRefresh(false), currentSortColumn(SORT_PING),
sortAscending(true), originalListViewProc(nullptr) {

    InitializeManagers();
}


DayZLauncher::~DayZLauncher() {
    CleanupManagers();
    if (hLogoBitmap) {
        DeleteObject(hLogoBitmap);
        hLogoBitmap = nullptr;
    }
}

void DayZLauncher::InitializeManagers() {
    queryManager = std::make_unique<ServerQueryManager>();
    favoritesManager = std::make_unique<FavoritesManager>();
    configManager = std::make_unique<ConfigManager>();
    serverCache = std::make_unique<ServerCache>();
    threadPool = std::make_unique<ThreadPool>(4);
}

void DayZLauncher::CleanupManagers() {
    if (refreshThread.joinable()) {
        shouldStopRefresh = true;
        refreshThread.join();
    }

    threadPool.reset();
    serverCache.reset();
    trayManager.reset();
    configManager.reset();
    favoritesManager.reset();
    queryManager.reset();
}

bool DayZLauncher::Initialize(HINSTANCE hInstance) {

    RegisterWindowClass(hInstance);

    hWnd = CreateMainWindow(hInstance);
    if (!hWnd) {
        return false;
    }


    if (!queryManager->Initialize()) {
        MessageBox(hWnd, L"Failed to initialize network subsystem", L"Error", MB_OK | MB_ICONERROR);
        return false;
    }


    trayManager = std::make_unique<SystemTrayManager>(hWnd);
    ApplyModernStyling();

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    return true;
}

void DayZLauncher::RegisterWindowClass(HINSTANCE hInstance) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DayZLauncherWindow";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN_ICON));

    RegisterClass(&wc);
}

HWND DayZLauncher::CreateMainWindow(HINSTANCE hInstance) {
    return CreateWindowEx(
        0,
        L"DayZLauncherWindow",
        L"DayZ Server Browser",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800,
        nullptr, nullptr, hInstance, nullptr
    );
}

void DayZLauncher::CreateControls() {
    HINSTANCE hInst = GetModuleHandle(nullptr);
    CreateFilterPanel();


    hTab = CreateWindowEx(
        0, WC_TABCONTROL, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS | TCS_SINGLELINE,
        FILTER_PANEL_WIDTH + 10, 10, 800, 35,
        hWnd, (HMENU)IDC_TAB_CONTROL, hInst, nullptr);

    TCITEM tie = {};
    tie.mask = TCIF_TEXT;


    wchar_t officialText[] = L"Official Servers";
    tie.pszText = officialText;
    TabCtrl_InsertItem(hTab, TAB_OFFICIAL, &tie);

    wchar_t communityText[] = L"Community Servers";
    tie.pszText = communityText;
    TabCtrl_InsertItem(hTab, TAB_COMMUNITY, &tie);

    wchar_t favoritesText[] = L"Favorites";
    tie.pszText = favoritesText;
    TabCtrl_InsertItem(hTab, TAB_FAVORITES, &tie);

    wchar_t lanText[] = L"LAN Servers";
    tie.pszText = lanText;
    TabCtrl_InsertItem(hTab, TAB_LAN, &tie);

    wchar_t settingsText[] = L"Settings";
    tie.pszText = settingsText;
    TabCtrl_InsertItem(hTab, TAB_SETTINGS, &tie);


    hServerList = CreateWindowEx(
        WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        FILTER_PANEL_WIDTH + 10, 50, 800, 400,
        hWnd, (HMENU)IDC_SERVER_LIST, hInst, nullptr);

    SetupEnhancedListView();
    CreateControlPanel();


    hStatusBar = CreateWindowEx(0, STATUSCLASSNAME, L"",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0,
        hWnd, (HMENU)IDC_STATUS_BAR, hInst, nullptr);


    int statusParts[] = { 200, 400, 600, -1 };
    SendMessage(hStatusBar, SB_SETPARTS, 4, (LPARAM)statusParts);
    UpdateStatusBar("Ready - Click Refresh to load servers");
    CreateSettingsControls();

    hImageControl = CreateWindow(
        L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE,
        20, 420, 200, 200, 
        hFilterPanel, 
        (HMENU)IDC_LOGO_IMAGE,
        GetModuleHandle(nullptr), nullptr
    );

    hLogoBitmap = LoadPNGFromFile(L"IMG_5054.BMP", 200, 200);

    if (hLogoBitmap && hImageControl) {
        SendMessage(hImageControl, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hLogoBitmap);
    }



    if (favoritesManager) {
        favoritesManager->LoadFavorites();


        if (currentTab == TAB_FAVORITES) {
            PopulateServerList();
        }
    }
}


void DayZLauncher::CreateSettingsControls() {
    HINSTANCE hInst = GetModuleHandle(nullptr);

    UpdateStatusBar("Creating settings controls...");

    hSmallFont = CreateFont(
        14,                        
        0,                       
        0,                        
        0,                         
        FW_NORMAL,                
        FALSE,                     
        FALSE,                    
        FALSE,                     
        DEFAULT_CHARSET,        
        OUT_DEFAULT_PRECIS,        
        CLIP_DEFAULT_PRECIS,    
        DEFAULT_QUALITY,           
        DEFAULT_PITCH | FF_SWISS,  
        L"Segoe UI"               
    );

   
    hProfileNameLabel = CreateWindow(L"STATIC", L"Profile Name:", WS_CHILD,
        50, 100, 120, 25, hWnd, nullptr, hInst, nullptr);

    hProfilePathLabel = CreateWindow(L"STATIC", L"Profile Path:", WS_CHILD,
        50, 140, 120, 25, hWnd, nullptr, hInst, nullptr);

    hDayZPathLabel = CreateWindow(L"STATIC", L"DayZ Path:", WS_CHILD,
        50, 180, 120, 25, hWnd, nullptr, hInst, nullptr);

    hQueryDelayLabel = CreateWindow(L"STATIC", L"Query Delay (ms):", WS_CHILD,
        50, 220, 150, 25, hWnd, nullptr, hInst, nullptr);

   
    hProfileNameEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | ES_LEFT | ES_AUTOHSCROLL,
        180, 100, 300, 25, hWnd, (HMENU)IDC_PROFILE_NAME_EDIT, hInst, nullptr);

    hProfilePathEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | ES_LEFT | ES_AUTOHSCROLL,
        180, 140, 400, 25, hWnd, (HMENU)IDC_PROFILE_PATH_EDIT, hInst, nullptr);

   
    hDayZPathEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | ES_LEFT | ES_AUTOHSCROLL,
        180, 180, 400, 25, hWnd, (HMENU)IDC_DAYZ_PATH_EDIT, hInst, nullptr);

    OutputDebugStringA("DayZ control created EMPTY - will be filled by LoadSettingsValues()\n");

   
    hBrowseProfileBtn = CreateWindow(L"BUTTON", L"Browse...",
        WS_CHILD | BS_PUSHBUTTON,
        590, 140, 80, 25, hWnd, (HMENU)IDC_BROWSE_PROFILE_BTN, hInst, nullptr);

    hBrowseDayZBtn = CreateWindow(L"BUTTON", L"Browse...",
        WS_CHILD | BS_PUSHBUTTON,
        590, 180, 80, 25, hWnd, (HMENU)IDC_BROWSE_DAYZ_BTN, hInst, nullptr);

    hQueryDelayEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"100",
        WS_CHILD | ES_LEFT | ES_NUMBER,
        210, 220, 60, 25, hWnd, (HMENU)IDC_QUERY_DELAY_EDIT, hInst, nullptr);

    hSaveSettingsBtn = CreateWindow(L"BUTTON", L"SAVE",
        WS_CHILD | BS_PUSHBUTTON,
        50, 300, 70, 25, hWnd, (HMENU)IDC_SAVE_SETTINGS_BTN, hInst, nullptr);

    hReloadSettingsBtn = CreateWindow(L"BUTTON", L"RELOAD",
        WS_CHILD | BS_PUSHBUTTON,
        130, 300, 70, 25, hWnd, (HMENU)IDC_RELOAD_SETTINGS_BTN, hInst, nullptr);


    hColorBgLabel = CreateWindow(L"STATIC", L"Window BG (Hex):", WS_CHILD,
        50, 370, 130, 20, hWnd, nullptr, hInst, nullptr);

    hColorTextLabel = CreateWindow(L"STATIC", L"Text Color (Hex):", WS_CHILD,
        50, 400, 130, 20, hWnd, nullptr, hInst, nullptr);

    hColorButtonLabel = CreateWindow(L"STATIC", L"Button BG (Hex):", WS_CHILD,
        50, 430, 130, 20, hWnd, nullptr, hInst, nullptr);

  
    hColorBgEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"#1e1e1e",
        WS_CHILD | ES_LEFT, 180, 370, 100, 22, hWnd, nullptr, hInst, nullptr);

    hColorTextEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"#ffffff",
        WS_CHILD | ES_LEFT, 180, 400, 100, 22, hWnd, nullptr, hInst, nullptr);

    hColorButtonEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"#2d2d2d",
        WS_CHILD | ES_LEFT, 180, 430, 100, 22, hWnd, nullptr, hInst, nullptr);

   
    hApplyThemeBtn = CreateWindow(L"BUTTON", L"Apply Theme",
        WS_CHILD | BS_PUSHBUTTON,
        300, 400, 120, 30, hWnd, (HMENU)IDC_APPLY_THEME_BTN, hInst, nullptr);

    
    UpdateStatusBar("Settings controls created empty - ready for loading");
}


void DayZLauncher::LoadSettingsValues() {
    if (!configManager) {
        UpdateStatusBar("ERROR: ConfigManager is NULL during load!");
        OutputDebugStringA("ERROR: ConfigManager is NULL during load!\n");
        return;
    }

    OutputDebugStringA("=== LOADING SETTINGS VALUES ===\n");


    std::string profileName = configManager->GetString("profileName", "");
    if (hProfileNameEdit && IsWindow(hProfileNameEdit)) {
        SetWindowTextA(hProfileNameEdit, profileName.c_str());
        OutputDebugStringA(("Loaded profile name: '" + profileName + "'\n").c_str());
    }


    std::string profilePath = configManager->GetString("profilePath", "");
    if (hProfilePathEdit && IsWindow(hProfilePathEdit)) {
        std::wstring wProfilePath = StringToWString(profilePath);
        SetWindowText(hProfilePathEdit, wProfilePath.c_str());
        OutputDebugStringA(("Loaded profile path (Unicode): '" + profilePath + "'\n").c_str());
    }

 
    std::string dayzPath = configManager->GetString("dayzPath", "");
    if (hDayZPathEdit && IsWindow(hDayZPathEdit)) {
        std::wstring wDayzPath = StringToWString(dayzPath);
        SetWindowText(hDayZPathEdit, wDayzPath.c_str());
        OutputDebugStringA(("Loaded DayZ path (Unicode): '" + dayzPath + "'\n").c_str());
    }

  
    int queryDelay = configManager->GetInt("queryDelay", 100);
    if (hQueryDelayEdit && IsWindow(hQueryDelayEdit)) {
        SetWindowTextA(hQueryDelayEdit, std::to_string(queryDelay).c_str());
        OutputDebugStringA(("Loaded query delay: " + std::to_string(queryDelay) + "\n").c_str());
    }

    UpdateStatusBar("Settings loaded with Unicode methods!");
}

void DayZLauncher::OnSettingsChanged() {
    SaveSettingsValues();
}


void DayZLauncher::SaveSettingsValues() {
    OutputDebugStringA("=== SAVE SETTINGS VALUES CALLED ===\n");

    if (!configManager) {
        UpdateStatusBar("FATAL: ConfigManager is NULL!");
        return;
    }

   
    if (hProfileNameEdit && IsWindow(hProfileNameEdit)) {
        char buffer[1024] = { 0 };
        int length = GetWindowTextA(hProfileNameEdit, buffer, sizeof(buffer));
        if (length > 0) {
            std::string profileName = std::string(buffer);
            configManager->SetString("profileName", profileName);
            OutputDebugStringA(("Profile Name saved: '" + profileName + "'\n").c_str());
        }
    }


    if (hProfilePathEdit && IsWindow(hProfilePathEdit)) {
        wchar_t buffer[1024] = { 0 };
        int length = GetWindowText(hProfilePathEdit, buffer, sizeof(buffer) / sizeof(wchar_t));
        if (length > 0) {
            std::string profilePath = WStringToString(std::wstring(buffer));
            configManager->SetString("profilePath", profilePath);
            OutputDebugStringA(("Profile Path saved (Unicode): '" + profilePath + "'\n").c_str());
        }
    }


    if (hDayZPathEdit && IsWindow(hDayZPathEdit)) {
        wchar_t buffer[1024] = { 0 };
        int length = GetWindowText(hDayZPathEdit, buffer, sizeof(buffer) / sizeof(wchar_t));
        if (length > 0) {
            std::string dayzPath = WStringToString(std::wstring(buffer));

           
            if (!dayzPath.empty() && dayzPath.front() == '"' && dayzPath.back() == '"') {
                dayzPath = dayzPath.substr(1, dayzPath.length() - 2);
            }

     
            if (dayzPath.find("DayZ_x64.exe") == std::string::npos) {
    
                if (!dayzPath.empty() && (dayzPath.back() == '\\' || dayzPath.back() == '/')) {
                    dayzPath.pop_back();
                }
                dayzPath += "\\DayZ_x64.exe";
            }

            configManager->SetString("dayzPath", dayzPath);
            OutputDebugStringA(("DayZ Path saved (Fixed): '" + dayzPath + "'\n").c_str());
        }
        else {
            OutputDebugStringA("DayZ Path - No text in control!\n");
        }
    }
    else {
        OutputDebugStringA("DayZ Path - Control not valid!\n");
    }


    if (hQueryDelayEdit && IsWindow(hQueryDelayEdit)) {
        char buffer[64] = { 0 };
        GetWindowTextA(hQueryDelayEdit, buffer, sizeof(buffer));
        try {
            int delay = std::stoi(buffer);
            configManager->SetInt("queryDelay", delay);
            OutputDebugStringA(("Query delay saved: " + std::to_string(delay) + "\n").c_str());
        }
        catch (...) {
            configManager->SetInt("queryDelay", 100);
        }
    }


    configManager->SaveConfig();

 
    std::string savedDayZ = configManager->GetString("dayzPath", "EMPTY");
    std::string savedProfile = configManager->GetString("profilePath", "EMPTY");
    std::string savedProfileName = configManager->GetString("profileName", "EMPTY");

    OutputDebugStringA(("VERIFICATION - DayZ: '" + savedDayZ + "'\n").c_str());
    OutputDebugStringA(("VERIFICATION - Profile: '" + savedProfile + "'\n").c_str());
    OutputDebugStringA(("VERIFICATION - ProfileName: '" + savedProfileName + "'\n").c_str());

    UpdateStatusBar("Settings saved! DayZ: '" + savedDayZ.substr(savedDayZ.find_last_of('\\') + 1) + "'");
}



void DayZLauncher::TestDayZPathControl() {
    OutputDebugStringA("=== TESTING DAYZ PATH CONTROL ===\n");

    if (!hDayZPathEdit || !IsWindow(hDayZPathEdit)) {
        OutputDebugStringA("ERROR: DayZ Path control is invalid!\n");
        return;
    }

    
    std::wstring testPath = L"D:\\SteamLibrary\\steamapps\\common\\DayZ\\DayZ_x64.exe";
    SetWindowText(hDayZPathEdit, testPath.c_str());
    OutputDebugStringA("Test 1: Set test path directly\n");


    wchar_t buffer[1024] = { 0 };
    int length = GetWindowText(hDayZPathEdit, buffer, sizeof(buffer) / sizeof(wchar_t));

    if (length > 0) {
        std::string readBack = WStringToString(std::wstring(buffer));
        OutputDebugStringA(("Test 2: Read back: '" + readBack + "'\n").c_str());


        configManager->SetString("dayzPath", readBack);
        configManager->SaveConfig();

  
        std::string verified = configManager->GetString("dayzPath", "NOT_FOUND");
        OutputDebugStringA(("Test 4: Verified in config: '" + verified + "'\n").c_str());

        UpdateStatusBar("DayZ Path test completed - check debug output");
    }
    else {
        OutputDebugStringA("Test 2: FAILED - Could not read back from control!\n");
    }
}


void DayZLauncher::ShowSettingsTab(bool show) {
    int showCmd = show ? SW_SHOW : SW_HIDE;


    if (hProfileNameEdit) ShowWindow(hProfileNameEdit, showCmd);
    if (hProfilePathEdit) ShowWindow(hProfilePathEdit, showCmd);
    if (hDayZPathEdit) ShowWindow(hDayZPathEdit, showCmd);
    if (hBrowseProfileBtn) ShowWindow(hBrowseProfileBtn, showCmd);
    if (hBrowseDayZBtn) ShowWindow(hBrowseDayZBtn, showCmd);
    if (hQueryDelayEdit) ShowWindow(hQueryDelayEdit, showCmd);
    if (hColorBgLabel) ShowWindow(hColorBgLabel, showCmd);
    if (hColorTextLabel) ShowWindow(hColorTextLabel, showCmd);
    if (hColorButtonLabel) ShowWindow(hColorButtonLabel, showCmd);

    if (hColorBgEdit) ShowWindow(hColorBgEdit, showCmd);
    if (hColorTextEdit) ShowWindow(hColorTextEdit, showCmd);
    if (hColorButtonEdit) ShowWindow(hColorButtonEdit, showCmd);

    if (hApplyThemeBtn) ShowWindow(hApplyThemeBtn, showCmd);

    HWND hChild = GetWindow(hWnd, GW_CHILD);
    while (hChild) {
        wchar_t className[256];
        GetClassName(hChild, className, 256);

        if (wcscmp(className, L"STATIC") == 0) {
     
            wchar_t windowText[256];
            GetWindowText(hChild, windowText, 256);

        
            if (wcsstr(windowText, L"Profile") ||
                wcsstr(windowText, L"DayZ") ||
                wcsstr(windowText, L"Query") ||
                wcsstr(windowText, L"Delay") ||
                wcsstr(windowText, L"Color") ||                   
                wcsstr(windowText, L"BG") ||
                wcsstr(windowText, L"Text"))
            {
                ShowWindow(hChild, showCmd);
            }
        }
        hChild = GetWindow(hChild, GW_HWNDNEXT);
    }
}


void DayZLauncher::OnBrowseProfile() {
    wchar_t folderPath[MAX_PATH] = { 0 };

    BROWSEINFO bi = { 0 };
    bi.hwndOwner = hWnd;
    bi.lpszTitle = L"Select Profile Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
    if (pidl) {
        if (SHGetPathFromIDList(pidl, folderPath)) {
            SetWindowText(hProfilePathEdit, folderPath);
            SaveSettingsValues();
        }
        CoTaskMemFree(pidl);
    }
}




void DayZLauncher::OnBrowseDayZ() {
    wchar_t filePath[MAX_PATH] = { 0 };

    OPENFILENAME ofn = { 0 };
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"DayZ Executable\0DayZ_x64.exe\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = L"Select DayZ Executable (DayZ_x64.exe)";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    ofn.lpstrInitialDir = L"D:\\SteamLibrary\\steamapps\\common\\DayZ";

    if (GetOpenFileName(&ofn)) {
        std::wstring selectedPath(filePath);

 
        if (selectedPath.find(L"DayZ_x64.exe") == std::wstring::npos) {
            MessageBox(hWnd, L"Please select DayZ_x64.exe", L"Invalid File", MB_OK | MB_ICONWARNING);
            return;
        }

     
        SetWindowText(hDayZPathEdit, selectedPath.c_str());

       
        std::string dayzPath = WStringToString(selectedPath);

    
        if (!dayzPath.empty() && dayzPath.front() == '"' && dayzPath.back() == '"') {
            dayzPath = dayzPath.substr(1, dayzPath.length() - 2);
        }

 
        if (configManager) {
            configManager->SetString("dayzPath", dayzPath);
            configManager->SaveConfig();

            OutputDebugStringA(("Saved DayZ path: " + dayzPath + "\n").c_str());
            UpdateStatusBar("DayZ path saved.");
        } else {
            UpdateStatusBar("ERROR: Config manager missing!");
        }
    }
}

void DayZLauncher::SetTestDayZPath() {
    std::wstring testPath = L"D:\\SteamLibrary\\steamapps\\common\\DayZ\\DayZ_x64.exe";

    if (hDayZPathEdit && IsWindow(hDayZPathEdit)) {
        SetWindowText(hDayZPathEdit, testPath.c_str());
        UpdateStatusBar("Set test DayZ path");
        SaveSettingsValues();
    }
    else {
        UpdateStatusBar("ERROR: DayZ edit control not found for test!");
    }
}




void DayZLauncher::SetupEnhancedListView() {
  
    DWORD exStyle = LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
        LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP;
    ListView_SetExtendedListViewStyle(hServerList, exStyle);
    LVCOLUMN lvc = {};
    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    lvc.fmt = LVCFMT_LEFT;
    lvc.cx = 300;
    wchar_t serverNameText[] = L"Server Name";
    lvc.pszText = serverNameText;
    lvc.iSubItem = 0;
    ListView_InsertColumn(hServerList, 0, &lvc);
    lvc.cx = 100;
    wchar_t mapText[] = L"Map";
    lvc.pszText = mapText;
    lvc.iSubItem = 1;
    ListView_InsertColumn(hServerList, 1, &lvc);
    lvc.cx = 80;
    wchar_t playersText[] = L"Players";
    lvc.pszText = playersText;
    lvc.iSubItem = 2;
    ListView_InsertColumn(hServerList, 2, &lvc);
    lvc.cx = 60;
    wchar_t pingText[] = L"Ping";
    lvc.pszText = pingText;
    lvc.iSubItem = 3;
    ListView_InsertColumn(hServerList, 3, &lvc);
    lvc.cx = 120;
    wchar_t ipPortText[] = L"IP:Port";
    lvc.pszText = ipPortText;
    lvc.iSubItem = 4;
    ListView_InsertColumn(hServerList, 4, &lvc);
    lvc.cx = 80;
    wchar_t versionText[] = L"Version";
    lvc.pszText = versionText;
    lvc.iSubItem = 5;
    ListView_InsertColumn(hServerList, 5, &lvc);
}

void DayZLauncher::CreateControlPanel() {
    HINSTANCE hInst = GetModuleHandle(nullptr);


    hRefreshBtn = CreateWindow(L"BUTTON", L"Refresh Servers",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, 470, 120, 30,
        hWnd, (HMENU)IDC_REFRESH_BTN, hInst, nullptr);

    hJoinBtn = CreateWindow(L"BUTTON", L"Join Server",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        150, 470, 120, 30,
        hWnd, (HMENU)IDC_JOIN_BTN, hInst, nullptr);

    hFavoriteBtn = CreateWindow(L"BUTTON", L"Add Favorite",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        280, 470, 120, 30,
        hWnd, (HMENU)IDC_FAVORITE_BTN, hInst, nullptr);


    hProgressBar = CreateWindow(PROGRESS_CLASS, nullptr,
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        20, 510, 770, 15,
        hWnd, (HMENU)IDC_PROGRESS_BAR, hInst, nullptr);

    hTestDayZBtn = CreateWindow(L"BUTTON", L"Test Path",
        WS_CHILD | BS_PUSHBUTTON,
        330, 300, 150, 30, hWnd, (HMENU)IDC_TEST_DAYZ_BTN, hInst, nullptr);


    EnableWindow(hJoinBtn, FALSE);
    EnableWindow(hFavoriteBtn, FALSE);
}


void DayZLauncher::ResizeControls() {
    RECT rect;
    GetClientRect(hWnd, &rect);

    if (currentTab == TAB_SETTINGS) {
     
        if (hTab && IsWindow(hTab)) {
            SetWindowPos(hTab, nullptr, 10, 10, rect.right - 20, 30, SWP_NOZORDER);
        }
        if (hStatusBar && IsWindow(hStatusBar)) {
            SetWindowPos(hStatusBar, nullptr, 0, rect.bottom - 25, rect.right, 25, SWP_NOZORDER);
        }
    }
    else {
       
        if (hFilterPanel && IsWindow(hFilterPanel)) {
            SetWindowPos(hFilterPanel, nullptr, 0, 0, FILTER_PANEL_WIDTH, rect.bottom - 25, SWP_NOZORDER);
        }

        int mainLeft = FILTER_PANEL_WIDTH + 10;
        int mainWidth = rect.right - mainLeft - 10;

        if (hTab && IsWindow(hTab)) {
            SetWindowPos(hTab, nullptr, mainLeft, 10, mainWidth, 30, SWP_NOZORDER);
        }

        if (hServerList && IsWindow(hServerList)) {
            SetWindowPos(hServerList, nullptr, mainLeft, 50, mainWidth, rect.bottom - 150, SWP_NOZORDER);
        }

    
        if (hRefreshBtn && IsWindow(hRefreshBtn)) {
            SetWindowPos(hRefreshBtn, nullptr, mainLeft + 20, rect.bottom - 80, 120, 30, SWP_NOZORDER);
        }
        if (hJoinBtn && IsWindow(hJoinBtn)) {
            SetWindowPos(hJoinBtn, nullptr, mainLeft + 150, rect.bottom - 80, 120, 30, SWP_NOZORDER);
        }
        if (hFavoriteBtn && IsWindow(hFavoriteBtn)) {
            SetWindowPos(hFavoriteBtn, nullptr, mainLeft + 280, rect.bottom - 80, 120, 30, SWP_NOZORDER);
        }

        if (hProgressBar && IsWindow(hProgressBar)) {
            SetWindowPos(hProgressBar, nullptr, mainLeft + 20, rect.bottom - 40, mainWidth - 40, 15, SWP_NOZORDER);
        }

        if (hStatusBar && IsWindow(hStatusBar)) {
            SetWindowPos(hStatusBar, nullptr, 0, rect.bottom - 25, rect.right, 25, SWP_NOZORDER);
        }

    
        if (hImageControl && IsWindow(hImageControl)) {
            int imageSize = 200;  
            int margin = 20;

       
            int filterPanelHeight = rect.bottom - 25; 
            int filterButtonsBottom = 450; 
            int availableHeight = filterPanelHeight - filterButtonsBottom - margin;
            int imageX = margin;
            int imageY = filterButtonsBottom + 10;  
            if (imageY + imageSize > filterPanelHeight - margin) {
                imageY = filterPanelHeight - imageSize - margin;
            }

        
            if (imageY < 420) {
                imageY = 420;
                int maxImageSize = filterPanelHeight - imageY - margin;
                if (maxImageSize < imageSize) {
                    imageSize = maxImageSize;
                    if (imageSize < 100) imageSize = 100; 
                }
            }

            SetWindowPos(hImageControl, nullptr,
                imageX, imageY, imageSize, imageSize,
                SWP_NOZORDER);
        }
    }
}



void DayZLauncher::ApplyModernStyling() {
    ThemeManager::ApplyTheme(hWnd);
}

void DayZLauncher::RefreshServers() {
    if (isRefreshing.load()) return;

    isRefreshing = true;
    UpdateStatusBar("Refreshing servers...");
    UpdateProgressBar(0);

 
    if (refreshThread.joinable()) {
        shouldStopRefresh = true;
        refreshThread.join();
    }

    shouldStopRefresh = false;


    HANDLE hThread = CreateThread(NULL, 0, RefreshServersThread, this, 0, NULL);
    if (hThread) {
        CloseHandle(hThread);
        OutputDebugStringA("RefreshServersThread created successfully!\n");
    }
    else {
        OutputDebugStringA("ERROR: Failed to create RefreshServersThread!\n");
        isRefreshing = false;
        UpdateStatusBar("ERROR: Failed to start refresh thread!");
    }
}

DWORD CALLBACK DayZLauncher::RefreshServersThread(LPVOID lpParam) {
    DayZLauncher* launcher = static_cast<DayZLauncher*>(lpParam);

    OutputDebugStringA("=== REFRESH THREAD STARTED ===\n");

 
    {
        std::lock_guard<std::mutex> lock(launcher->serverMutex);
        launcher->servers.clear();
    }


    PostMessage(launcher->hWnd, WM_USER + 200, 0, (LPARAM)"Querying Steam master servers...");
    PostMessage(launcher->hWnd, WM_UPDATE_PROGRESS, 10, 0);

   
    std::vector<std::pair<std::string, int>> serverAddresses;
    bool foundServers = false;

    OutputDebugStringA("Trying Steam Master Server...\n");
    if (launcher->queryManager->QuerySteamMasterServer(serverAddresses)) {
        foundServers = true;
        OutputDebugStringA(("Found " + std::to_string(serverAddresses.size()) + " servers from Steam master\n").c_str());
        PostMessage(launcher->hWnd, WM_USER + 200, 0, (LPARAM)("Found " + std::to_string(serverAddresses.size()) + " servers from Steam master").c_str());
    }

 
    if (!foundServers) {
        OutputDebugStringA("Trying direct master server...\n");
        PostMessage(launcher->hWnd, WM_USER + 200, 0, (LPARAM)"Trying direct master server...");
        if (launcher->queryManager->QuerySteamMasterServerDirect(serverAddresses)) {
            foundServers = true;
            OutputDebugStringA(("Found " + std::to_string(serverAddresses.size()) + " servers from direct query\n").c_str());
        }
    }


    if (!foundServers || serverAddresses.empty()) {
        OutputDebugStringA("Using fallback server list...\n");
        PostMessage(launcher->hWnd, WM_USER + 200, 0, (LPARAM)"Master servers failed, using backup list...");

        serverAddresses.clear();
        serverAddresses.push_back(std::make_pair(std::string("172.236.0.90"), 4167));   
        serverAddresses.push_back(std::make_pair(std::string("172.236.0.90"), 5113));   
        serverAddresses.push_back(std::make_pair(std::string("85.190.158.18"), 2302));   
        serverAddresses.push_back(std::make_pair(std::string("194.147.90.51"), 2302));   
        serverAddresses.push_back(std::make_pair(std::string("198.143.167.10"), 2302));  
        serverAddresses.push_back(std::make_pair(std::string("139.99.144.41"), 2302));   
    }

    PostMessage(launcher->hWnd, WM_UPDATE_PROGRESS, 20, 0);

    int totalServers = static_cast<int>(serverAddresses.size());
    int processedServers = 0;
    int successfulQueries = 0;

    std::string statusMsg = "Querying " + std::to_string(totalServers) + " servers for details...";
    PostMessage(launcher->hWnd, WM_USER + 200, 0, (LPARAM)statusMsg.c_str());
    OutputDebugStringA((statusMsg + "\n").c_str());

    for (size_t i = 0; i < serverAddresses.size() && !launcher->shouldStopRefresh; ++i) {
        const std::pair<std::string, int>& addr = serverAddresses[i];

        OutputDebugStringA(("Querying server: " + addr.first + ":" + std::to_string(addr.second) + "\n").c_str());

        ServerInfo info;
        A2SInfoResponse response;


        bool serverResponded = false;
        int retries = 2;

        for (int attempt = 0; attempt < retries && !serverResponded; ++attempt) {
            if (launcher->queryManager->QueryServerInfo(addr.first, addr.second, response)) {
                serverResponded = true;
                break;
            }
            if (attempt < retries - 1) {
                Sleep(500); 
            }
        }

        if (serverResponded) {
            OutputDebugStringA(("Server responded: " + response.name + "\n").c_str());

        
            std::string cleanName = response.name;

         
            cleanName.erase(std::remove(cleanName.begin(), cleanName.end(), '\0'), cleanName.end());

   
            for (char& c : cleanName) {
                if (c < 32 || c > 126) {
                    c = ' ';
                }
            }

    
            cleanName.erase(0, cleanName.find_first_not_of(" \t\r\n"));
            cleanName.erase(cleanName.find_last_not_of(" \t\r\n") + 1);

       
            if (cleanName.empty() || cleanName.length() > 200 ||
                response.maxPlayers > 200 || response.maxPlayers < 1) {
                OutputDebugStringA("Skipping server with invalid data\n");
                continue;
            }

 
            info.name = cleanName;
            info.map = response.map.empty() ? "Unknown" : response.map;
            info.ip = addr.first;
            info.port = addr.second;
            info.players = response.players;
            info.maxPlayers = response.maxPlayers;
            info.version = response.version.empty() ? "1.27" : response.version;
            info.hasVAC = (response.vac == 1);
            info.isPassworded = (response.visibility == 1);
            info.folder = response.folder;

       
            info.isOfficial = launcher->DetectOfficialServer(info.name, info.folder);

      
            info.ping = launcher->queryManager->PingServer(addr.first, addr.second);

        
            if (info.ping > 5000) {
                info.ping = -1; 
            }

          
            info.isFavorite = launcher->favoritesManager->IsFavorite(addr.first, addr.second);

         
            info.lastUpdated = time(nullptr);
            info.country = launcher->GetCountryFromIP(addr.first);

     
            {
                std::lock_guard<std::mutex> lock(launcher->serverMutex);
                launcher->servers.push_back(info);
            }

            successfulQueries++;
            OutputDebugStringA(("Successfully added server: " + info.name + "\n").c_str());

        
            if (successfulQueries % 3 == 0) {
                PostMessage(launcher->hWnd, WM_REFRESH_PARTIAL, 0, 0);
            }
        }
        else {
            OutputDebugStringA(("Server did not respond: " + addr.first + ":" + std::to_string(addr.second) + "\n").c_str());
        }

        processedServers++;

      
        int progress = 20 + (processedServers * 75) / totalServers;
        PostMessage(launcher->hWnd, WM_UPDATE_PROGRESS, progress, 0);

      
        if (processedServers % 5 == 0) {
            std::string statusUpdate = "Processed " + std::to_string(processedServers) + "/" +
                std::to_string(totalServers) + " (" +
                std::to_string(successfulQueries) + " responding)";
            PostMessage(launcher->hWnd, WM_USER + 200, 0, (LPARAM)statusUpdate.c_str());
            OutputDebugStringA((statusUpdate + "\n").c_str());
        }

     
        int baseDelay = launcher->configManager->GetInt("queryDelay", 50);
        Sleep(baseDelay);
    }


    {
        std::lock_guard<std::mutex> lock(launcher->serverMutex);
        std::string finalStatus = "Found " + std::to_string(launcher->servers.size()) + " servers";
        PostMessage(launcher->hWnd, WM_USER + 200, 0, (LPARAM)finalStatus.c_str());
        OutputDebugStringA((finalStatus + "\n").c_str());
    }


    PostMessage(launcher->hWnd, WM_UPDATE_PROGRESS, 100, 0);
    PostMessage(launcher->hWnd, WM_REFRESH_COMPLETE, 0, 0);

    OutputDebugStringA("=== REFRESH THREAD COMPLETED ===\n");
    return 0;
}


void DayZLauncher::CreateFilterPanel() {
    HINSTANCE hInst = GetModuleHandle(nullptr);

    hFilterPanel = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, 0, FILTER_PANEL_WIDTH, 600,
        hWnd, (HMENU)IDC_FILTER_PANEL, hInst, nullptr
    );

    int yPos = 15;
    int spacing = 35;
    int checkboxSpacing = 22;

 
    CreateFilterLabel(hFilterSearchLabel, L"Search:", 15, yPos, 100);
    yPos += 22;
    CreateFilterEditBox(hFilterSearch, IDC_FILTER_SEARCH, 15, yPos, 240, 20);
    yPos += spacing;


    CreateFilterLabel(hFilterMapLabel, L"Map:", 15, yPos, 100);
    yPos += 22;
    CreateMapDropdown(15, yPos);
    yPos += spacing;

    
    CreateFilterLabel(hFilterVersionLabel, L"Version:", 15, yPos, 100);
    yPos += 22;
    CreateVersionDropdown(15, yPos);
    yPos += spacing;

  
    CreateFilterLabel(hFilterOptionsLabel, L"Filters:", 15, yPos, 100);
    yPos += 25;


    CreateFilterCheckbox(hFilterFavorites, L"Show favourites only", IDC_FILTER_FAVORITES, 15, yPos, 220);
    yPos += 20; 

    CreateFilterCheckbox(hFilterPlayed, L"Played on", IDC_FILTER_PLAYED, 15, yPos, 220);
    yPos += 20;

    CreateFilterCheckbox(hFilterPassword, L"Is not password protected", IDC_FILTER_PASSWORD, 15, yPos, 220);
    yPos += 20;

    CreateFilterCheckbox(hFilterModded, L"Show modded only", IDC_FILTER_MODDED, 15, yPos, 220);
    yPos += 20;

    CreateFilterCheckbox(hFilterOnline, L"Is online", IDC_FILTER_ONLINE, 15, yPos, 220);
    yPos += 20;

    CreateFilterCheckbox(hFilterFirstPerson, L"First person", IDC_FILTER_FIRSTPERSON, 15, yPos, 220);
    yPos += 20;

    CreateFilterCheckbox(hFilterThirdPerson, L"Third person", IDC_FILTER_THIRDPERSON, 15, yPos, 220);
    yPos += 20;

    CreateFilterCheckbox(hFilterNotFull, L"Is not full", IDC_FILTER_NOTFULL, 15, yPos, 220);
    yPos += 25;
    hFilterReset = CreateWindow(L"BUTTON", L"RESET",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        15, yPos, 110, 25, hFilterPanel, (HMENU)IDC_FILTER_RESET, hInst, nullptr);

    hFilterRefresh = CreateWindow(L"BUTTON", L"REFRESH",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        135, yPos, 110, 25, hFilterPanel, (HMENU)IDC_FILTER_REFRESH, hInst, nullptr);


    SetFilterChecked(hFilterPassword, true);   
    SetFilterChecked(hFilterOnline, true);  
    SetFilterChecked(hFilterFavorites, false);
    SetFilterChecked(hFilterPlayed, false);
    SetFilterChecked(hFilterModded, false);
    SetFilterChecked(hFilterFirstPerson, false);
    SetFilterChecked(hFilterThirdPerson, false);
    SetFilterChecked(hFilterNotFull, false);
}



void DayZLauncher::CreateVersionDropdown(int x, int y) {
    HINSTANCE hInst = GetModuleHandle(nullptr);

    hFilterVersion = CreateWindow(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_SORT | WS_VSCROLL,
        x, y, 240, 200,
        hFilterPanel, (HMENU)IDC_FILTER_VERSION, hInst, nullptr);

    SendMessage(hFilterVersion, CB_ADDSTRING, 0, (LPARAM)L"All Versions");

    const wchar_t* versions[] = {
        L"1.0.150192", L"1.10.153398", L"1.15.154355", L"1.18.154960", L"1.18.155001",
        L"1.18.155069", L"1.19.155430", L"1.20.155981", L"1.21.156300", L"1.22.156718",
        L"1.23.156951", L"1.23.157045", L"1.24.157623", L"1.24.157828", L"1.25.158199",
        L"1.25.158396", L"1.25.158593", L"1.26.158898", L"1.26.158964", L"1.26.159040",
        L"1.27.159420", L"1.27.159586", L"1.27.159674", L"1.28.159876", L"1.28.159992",
        L"1.28.160049"
    };

    for (int i = 0; i < sizeof(versions) / sizeof(versions[0]); i++) {
        SendMessage(hFilterVersion, CB_ADDSTRING, 0, (LPARAM)versions[i]);
    }

    SendMessage(hFilterVersion, CB_SETCURSEL, 0, 0);
}




bool DayZLauncher::PassesFilters(const ServerInfo& server) const {

    if (!searchFilter.empty()) {
        std::string serverName = server.name;
        std::string serverMap = server.map;
        std::string serverIP = server.ip;
        std::transform(serverName.begin(), serverName.end(), serverName.begin(), ::tolower);
        std::transform(serverMap.begin(), serverMap.end(), serverMap.begin(), ::tolower);
        std::transform(serverIP.begin(), serverIP.end(), serverIP.begin(), ::tolower);

        bool found = (serverName.find(searchFilter) != std::string::npos) ||
            (serverMap.find(searchFilter) != std::string::npos) ||
            (serverIP.find(searchFilter) != std::string::npos);

        if (!found) return false;
    }


    if (!mapFilter.empty()) {
        std::string serverMap = server.map;
        std::transform(serverMap.begin(), serverMap.end(), serverMap.begin(), ::tolower);

        if (serverMap != mapFilter) return false;
    }


    if (!versionFilter.empty()) {
        if (server.version.find(versionFilter) == std::string::npos) return false;
    }


    if (filterFlags & FILTER_SHOW_FAVORITES) {
        if (!server.isFavorite) return false;
    }
    if (filterFlags & FILTER_HIDE_PASSWORD) {
        if (server.isPassworded) return false;
    }
    if (filterFlags & FILTER_ONLINE_ONLY) {
        if (server.ping == -1) return false;
    }
    if (filterFlags & FILTER_NOT_FULL) {
        if (server.players >= server.maxPlayers && server.maxPlayers > 0) return false;
    }
    if (filterFlags & FILTER_SHOW_MODDED) {
        if (server.mods.empty()) return false;
    }
    if (filterFlags & FILTER_SHOW_PLAYED) {
        bool hasPlayed = false;
        if (favoritesManager) {
            const auto& history = favoritesManager->GetRecentServers();
            for (const auto& hist : history) {
                if (hist.ip == server.ip && hist.port == server.port && hist.connectionCount > 0) {
                    hasPlayed = true;
                    break;
                }
            }
        }
        if (!hasPlayed) return false;
    }

    return true;
}


void DayZLauncher::UpdateFilteredServerList() {
    if (!hServerList || !IsWindow(hServerList)) return;

    OutputDebugStringA("=== UPDATE FILTERED SERVER LIST ===\n");


    ListView_DeleteAllItems(hServerList);

    std::lock_guard<std::mutex> lock(serverMutex);

    if (servers.empty()) {
        UpdateStatusBar("No servers loaded - click 'Refresh Servers' to load servers");
        return;
    }

    int filteredCount = 0;
    int totalCount = static_cast<int>(servers.size());

    OutputDebugStringA(("Total servers available: " + std::to_string(totalCount) + "\n").c_str());
    OutputDebugStringA(("Current filters: search='" + searchFilter + "', map='" + mapFilter + "', flags=" + std::to_string(filterFlags) + "\n").c_str());

    for (const auto& server : servers) {
        if (PassesFilters(server)) {
       
            LVITEM lvi = {};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = filteredCount;
            lvi.iSubItem = 0;

            std::wstring serverName = StringToWString(server.name);
            lvi.pszText = const_cast<LPWSTR>(serverName.c_str());
            int listItemIndex = ListView_InsertItem(hServerList, &lvi);

            if (listItemIndex != -1) {
              
                SetListViewItemText(listItemIndex, 1, StringToWString(server.map));

                std::wstring playerText = std::to_wstring(server.players) + L"/" + std::to_wstring(server.maxPlayers);
                SetListViewItemText(listItemIndex, 2, playerText);

                std::wstring pingText = (server.ping == -1) ? L"N/A" : std::to_wstring(server.ping) + L"ms";
                SetListViewItemText(listItemIndex, 3, pingText);

                std::string address = server.ip + ":" + std::to_string(server.port);
                SetListViewItemText(listItemIndex, 4, StringToWString(address));

                SetListViewItemText(listItemIndex, 5, StringToWString(server.version));

                filteredCount++;
            }
        }
    }

    std::string statusMsg = "Showing " + std::to_string(filteredCount) + " of " + std::to_string(totalCount) + " servers";
    UpdateStatusBar(statusMsg);

    OutputDebugStringA(("Filter result: " + std::to_string(filteredCount) + " servers pass filters\n").c_str());
}



void DayZLauncher::CreateMapDropdown(int x, int y) {
    HINSTANCE hInst = GetModuleHandle(nullptr);


    hFilterMap = CreateWindow(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_SORT | WS_VSCROLL,
        x, y, 240, 200,
        hFilterPanel, (HMENU)IDC_FILTER_MAP, hInst, nullptr);


    SendMessage(hFilterMap, CB_ADDSTRING, 0, (LPARAM)L"All Maps");


    const wchar_t* maps[] = {
        L"Alteria", L"Anastara", L"Andromeda_map", L"Antonia", L"Arcadia", L"Arland",
        L"Armst_czoremake", L"Arsteinen", L"Arsteinen_snow", L"Ashesandblood_map", L"Avalon",
        L"Badg_nyheim", L"Banov", L"Banovfrost", L"Barrington", L"Bearsland", L"Belozersknpson",
        L"Bitterroot", L"Broumovsko", L"Burnham", L"Burnhamwinter", L"Caldera", L"Chernarus",
        L"Chernarus2", L"Chernarus+", L"Chernarusplus", L"Chernarusplusgloom", L"Chernarusredux",
        L"Dayz24_battleroyale_map", L"Dayz24_deathmatch_summer_map", L"Deadfall", L"Deerisle",
        L"Dimaiden", L"Dma_maps_old", L"Dmmap", L"Doorcounty", L"Dz_map", L"Eden", L"Elmar_island",
        L"Enoch", L"Esseker", L"Eternal", L"Exclusion_zone", L"Exclusionzone", L"Exclusionzoneplus",
        L"Fallout_map", L"Fallujah", L"Fenix_emptiness", L"Fenwickharbor", L"Flatpack", L"Fogfall",
        L"Frostskod", L"Green county", L"Greencounty", L"Hashima", L"Hom_worldmap", L"Hrp_zone",
        L"Iztek", L"Japan", L"Kalksee", L"Kuba", L"Lincoln_county_war", L"Livonia", L"Lux",
        L"Mecklenburg", L"Medieval_dayz", L"Melkart", L"Metropolis", L"Muerta_islands",
        L"Mysteryisland", L"Namalsk", L"Newsland", L"Newyork", L"Nihchernobyl", L"Noobless",
        L"Northtakistan", L"Nukezone", L"Nyheim", L"Onforin", L"Osek", L"Pnw", L"Pripyat",
        L"Pulsemetromap", L"Queenstown_newzealand", L"Raman", L"Rhengau", L"Rio_map", L"Ros",
        L"Rostow", L"Sahinkaya", L"Sahrani", L"Sakhal", L"Sanfranciscobayarea", L"Santacruz",
        L"Sarov", L"Shauwaki_islands", L"Siberia", L"Stuartisland", L"Swansisland", L"Tsal",
        L"Takistanplus", L"Tavllave", L"Taviana", L"Theprojectialland", L"Thezone", L"Togenia",
        L"Tpdm", L"Underworld", L"Utes", L"Valning", L"Vela", L"Visterrains_islands_map",
        L"Volcano", L"Wabamun", L"Wastelandz", L"Yiprit", L"Zaha", L"Zelador", L"Zentkamap", L"Zona"
    };

    for (int i = 0; i < sizeof(maps) / sizeof(maps[0]); i++) {
        SendMessage(hFilterMap, CB_ADDSTRING, 0, (LPARAM)maps[i]);
    }

    SendMessage(hFilterMap, CB_SETCURSEL, 0, 0);
}


HBITMAP DayZLauncher::LoadPNGFromFile(const std::wstring& filePath, int targetWidth, int targetHeight) {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;

    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok)
        return nullptr;

    Gdiplus::Bitmap* original = Gdiplus::Bitmap::FromFile(filePath.c_str(), false);
    if (!original || original->GetLastStatus() != Gdiplus::Ok) {
        delete original;
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return nullptr;
    }


    UINT originalWidth = original->GetWidth();
    UINT originalHeight = original->GetHeight();

    float scaleX = (float)targetWidth / originalWidth;
    float scaleY = (float)targetHeight / originalHeight;
    float scale = (scaleX < scaleY) ? scaleX : scaleY; 
    int finalWidth = (int)(originalWidth * scale);
    int finalHeight = (int)(originalHeight * scale);
    int offsetX = (targetWidth - finalWidth) / 2;
    int offsetY = (targetHeight - finalHeight) / 2;
    Gdiplus::Bitmap* resized = new Gdiplus::Bitmap(targetWidth, targetHeight, PixelFormat32bppARGB);
    Gdiplus::Graphics* graphics = Gdiplus::Graphics::FromImage(resized);
    graphics->Clear(Gdiplus::Color(255, 255, 255, 255));
    graphics->SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics->SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    graphics->DrawImage(original, offsetX, offsetY, finalWidth, finalHeight);
    HBITMAP hBitmap = nullptr;
    resized->GetHBITMAP(Gdiplus::Color(255, 255, 255), &hBitmap);

    delete graphics;
    delete resized;
    delete original;
    Gdiplus::GdiplusShutdown(gdiplusToken);

    return hBitmap;
}

HBITMAP DayZLauncher::LoadPNGFromResource(HINSTANCE hInstance, int resourceID) {
   
    HRSRC hResource = FindResource(hInstance, MAKEINTRESOURCE(resourceID), L"PNG");
    if (!hResource) return nullptr;


    HGLOBAL hMemory = LoadResource(hInstance, hResource);
    if (!hMemory) return nullptr;


    DWORD dwSize = SizeofResource(hInstance, hResource);
    LPVOID lpData = LockResource(hMemory);
    if (!lpData) return nullptr;

 
    HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, dwSize);
    if (!hBuffer) return nullptr;

    void* pBuffer = GlobalLock(hBuffer);
    memcpy(pBuffer, lpData, dwSize);
    GlobalUnlock(hBuffer);

    IStream* pStream = nullptr;
    if (CreateStreamOnHGlobal(hBuffer, TRUE, &pStream) != S_OK) {
        GlobalFree(hBuffer);
        return nullptr;
    }


    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) != Gdiplus::Ok) {
        pStream->Release();
        return nullptr;
    }


    Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(pStream);
    pStream->Release();

    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        delete bitmap;
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return nullptr;
    }


    HBITMAP hBitmap;
    bitmap->GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hBitmap);

    delete bitmap;
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return hBitmap;
}


void DayZLauncher::CreateFilterCheckbox(HWND& control, const wchar_t* text, int id, int x, int y, int width) {
    HINSTANCE hInst = GetModuleHandle(nullptr);
    control = CreateWindow(L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_LEFTTEXT,
        x, y, width, 20, hFilterPanel, (HMENU)id, hInst, nullptr);
}

void DayZLauncher::CreateFilterEditBox(HWND& control, int id, int x, int y, int width, int height) {
    HINSTANCE hInst = GetModuleHandle(nullptr);
    control = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
        x, y, width, height, hFilterPanel, (HMENU)id, hInst, nullptr);
}

void DayZLauncher::CreateFilterLabel(HWND& control, const wchar_t* text, int x, int y, int width) {
    HINSTANCE hInst = GetModuleHandle(nullptr);
    control = CreateWindow(L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, width, 20, hFilterPanel, nullptr, hInst, nullptr);
}

void DayZLauncher::SetupServerListColumns() {

    while (ListView_DeleteColumn(hServerList, 0));

    LVCOLUMN lvc = {};
    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

    lvc.fmt = LVCFMT_CENTER;
    lvc.cx = 30;
    wchar_t favText[] = L"★";
    lvc.pszText = favText;
    lvc.iSubItem = COL_FAVORITE;
    ListView_InsertColumn(hServerList, COL_FAVORITE, &lvc);
    lvc.fmt = LVCFMT_LEFT;
    lvc.cx = 350;
    wchar_t nameText[] = L"NAME";
    lvc.pszText = nameText;
    lvc.iSubItem = COL_NAME;
    ListView_InsertColumn(hServerList, COL_NAME, &lvc);

    lvc.cx = 80;
    wchar_t timeText[] = L"TIME";
    lvc.pszText = timeText;
    lvc.iSubItem = COL_TIME;
    ListView_InsertColumn(hServerList, COL_TIME, &lvc);
    lvc.cx = 80;
    wchar_t playedText[] = L"PLAYED";
    lvc.pszText = playedText;
    lvc.iSubItem = COL_PLAYED;
    ListView_InsertColumn(hServerList, COL_PLAYED, &lvc);
    lvc.cx = 120;
    wchar_t mapText[] = L"MAP";
    lvc.pszText = mapText;
    lvc.iSubItem = COL_MAP;
    ListView_InsertColumn(hServerList, COL_MAP, &lvc);
    lvc.fmt = LVCFMT_CENTER;
    lvc.cx = 80;
    wchar_t playersText[] = L"PLAYERS";
    lvc.pszText = playersText;
    lvc.iSubItem = COL_PLAYERS;
    ListView_InsertColumn(hServerList, COL_PLAYERS, &lvc);
    lvc.cx = 60;
    wchar_t pingText[] = L"PING";
    lvc.pszText = pingText;
    lvc.iSubItem = COL_PING;
    ListView_InsertColumn(hServerList, COL_PING, &lvc);
    lvc.cx = BUTTON_COLUMN_WIDTH;
    wchar_t actionsText[] = L"";
    lvc.pszText = actionsText;
    lvc.iSubItem = COL_ACTIONS;
    ListView_InsertColumn(hServerList, COL_ACTIONS, &lvc);
}



void DayZLauncher::AddServerToList(const ServerInfo& server, int index) {
    LVITEM lvi = {};
    lvi.mask = LVIF_TEXT;
    lvi.iItem = index;
    lvi.iSubItem = 0;


    wchar_t favStar[2];
    if (server.isFavorite) {
        wcscpy_s(favStar, L"★");
    }
    else {
        wcscpy_s(favStar, L"");
    }

    lvi.pszText = favStar;
    int itemIndex = ListView_InsertItem(hServerList, &lvi);

    if (itemIndex != -1) {
 
        SetListViewItemText(itemIndex, COL_NAME, StringToWString(server.name));

        std::string timeStr = FormatServerTime(server);
        SetListViewItemText(itemIndex, COL_TIME, StringToWString(timeStr));
        std::string playedStr = FormatPlayedStatus(server);
        SetListViewItemText(itemIndex, COL_PLAYED, StringToWString(playedStr));

        SetListViewItemText(itemIndex, COL_MAP, StringToWString(server.map));
        std::wstring playerText = std::to_wstring(server.players) + L"/" + std::to_wstring(server.maxPlayers);
        SetListViewItemText(itemIndex, COL_PLAYERS, playerText);
        std::wstring pingText = (server.ping == -1) ? L"-" : std::to_wstring(server.ping);
        SetListViewItemText(itemIndex, COL_PING, pingText);
        SetListViewItemText(itemIndex, COL_ACTIONS, L"🔄 ▶");
    }
}

std::string DayZLauncher::FormatServerTime(const ServerInfo& server) {

    std::string name = server.name;

    std::regex timeRegex(R"([\(\[](\d{1,2}):(\d{2})[\)\]])");
    std::smatch match;

    if (std::regex_search(name, match, timeRegex)) {
        return match[1].str() + ":" + match[2].str();
    }


    if (name.find("night") != std::string::npos || name.find("Night") != std::string::npos) {
        return "02:00";
    }
    else if (name.find("day") != std::string::npos || name.find("Day") != std::string::npos) {
        return "14:00";
    }

    return "12:00"; 
}

std::string DayZLauncher::FormatPlayedStatus(const ServerInfo& server) {

    if (favoritesManager) {
        const auto& history = favoritesManager->GetRecentServers();
        for (const auto& hist : history) {
            if (hist.ip == server.ip && hist.port == server.port) {
                if (hist.connectionCount > 0) {
                    return std::to_string(hist.connectionCount) + "x";
                }
            }
        }
    }
    return "";
}




std::wstring DayZLauncher::GetFilterText(HWND control) {
    if (!control || !IsWindow(control)) return L"";

    wchar_t buffer[256] = { 0 };
    GetWindowText(control, buffer, 255);
    return std::wstring(buffer);
}

bool DayZLauncher::GetFilterChecked(HWND control) {
    if (!control || !IsWindow(control)) {
        OutputDebugStringA("WARNING: Filter control is NULL or invalid!\n");
        return false;
    }

    LRESULT result = SendMessage(control, BM_GETCHECK, 0, 0);
    bool isChecked = (result == BST_CHECKED);


    std::string controlName = "Unknown";
    if (control == hFilterFavorites) controlName = "Favorites";
    else if (control == hFilterPlayed) controlName = "Played";
    else if (control == hFilterPassword) controlName = "Password";
    else if (control == hFilterModded) controlName = "Modded";
    else if (control == hFilterOnline) controlName = "Online";
    else if (control == hFilterFirstPerson) controlName = "FirstPerson";
    else if (control == hFilterThirdPerson) controlName = "ThirdPerson";
    else if (control == hFilterNotFull) controlName = "NotFull";

    OutputDebugStringA(("GetFilterChecked: " + controlName + " = " + (isChecked ? "CHECKED" : "UNCHECKED") + "\n").c_str());

    return isChecked;
}

void DayZLauncher::SetFilterText(HWND control, const std::wstring& text) {
    if (control && IsWindow(control)) {
        SetWindowText(control, text.c_str());
    }
}

void DayZLauncher::SetFilterChecked(HWND control, bool checked) {
    if (control && IsWindow(control)) {
 
        LRESULT result = SendMessage(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);

      
        std::string controlName = "Unknown";
        if (control == hFilterFavorites) controlName = "Favorites";
        else if (control == hFilterPlayed) controlName = "Played";
        else if (control == hFilterPassword) controlName = "Password";
        else if (control == hFilterModded) controlName = "Modded";
        else if (control == hFilterOnline) controlName = "Online";
        else if (control == hFilterFirstPerson) controlName = "FirstPerson";
        else if (control == hFilterThirdPerson) controlName = "ThirdPerson";
        else if (control == hFilterNotFull) controlName = "NotFull";

        OutputDebugStringA(("SetFilterChecked: " + controlName + " = " + (checked ? "CHECKED" : "UNCHECKED") + "\n").c_str());

  
        InvalidateRect(control, NULL, TRUE);
        UpdateWindow(control);
    }

}

bool DayZLauncher::IsServerRefreshNeeded(const std::string& ip, int port) {
    std::lock_guard<std::mutex> lock(refreshMutex);
    std::string key = ip + ":" + std::to_string(port);
    auto it = lastServerRefresh.find(key);

    if (it == lastServerRefresh.end()) {
        return true; 
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second);
    return elapsed.count() > 30; 
}

void DayZLauncher::MarkServerRefreshed(const std::string& ip, int port) {
    std::lock_guard<std::mutex> lock(refreshMutex);
    std::string key = ip + ":" + std::to_string(port);
    lastServerRefresh[key] = std::chrono::steady_clock::now();
}

ServerInfo* DayZLauncher::FindServerByAddress(const std::string& ip, int port) {
    for (auto& server : servers) {
        if (server.ip == ip && server.port == port) {
            return &server;
        }
    }
    return nullptr;
}

void DayZLauncher::ForceSaveDayZPath() {
    OutputDebugStringA("=== FORCE SAVE DAYZ PATH TEST ===\n");

    if (!hDayZPathEdit || !IsWindow(hDayZPathEdit)) {
        OutputDebugStringA("ERROR: DayZ Path control invalid!\n");
        return;
    }

  
    char buffer[1024] = { 0 };
    int length = GetWindowTextA(hDayZPathEdit, buffer, sizeof(buffer));

    OutputDebugStringA(("Control text length: " + std::to_string(length) + "\n").c_str());

    if (length > 0) {
        std::string dayzPath = std::string(buffer);
        OutputDebugStringA(("Control contains: '" + dayzPath + "'\n").c_str());

       
        configManager->SetString("dayzPath", dayzPath);
        OutputDebugStringA("Called SetString...\n");

     
        configManager->SaveConfig();
        OutputDebugStringA("Called SaveConfig...\n");


        configManager->LoadConfig();
        std::string verified = configManager->GetString("dayzPath", "NOT_FOUND");

        OutputDebugStringA(("VERIFICATION: Config now contains: '" + verified + "'\n").c_str());

        if (verified == dayzPath) {
            UpdateStatusBar("SUCCESS: DayZ path saved and verified!");
        }
        else {
            UpdateStatusBar("FAILED: DayZ path not saved correctly!");
        }
    }
    else {
        UpdateStatusBar("ERROR: No text in DayZ path control!");
    }
}

void DayZLauncher::EmergencyManualSave() {
    OutputDebugStringA("=== EMERGENCY MANUAL SAVE ===\n");


    char buffer[1024] = { 0 };
    if (hDayZPathEdit && IsWindow(hDayZPathEdit)) {
        int length = GetWindowTextA(hDayZPathEdit, buffer, sizeof(buffer));
        if (length > 0) {
            std::string dayzPath = std::string(buffer);
            OutputDebugStringA(("Manual save - DayZ path: '" + dayzPath + "'\n").c_str());

  
            std::ofstream file("config.ini", std::ios::app);
            if (file.is_open()) {
                file << "dayzPath=" << dayzPath << std::endl;
                file.close();
                UpdateStatusBar("EMERGENCY: Manually wrote DayZ path to config.ini");
                OutputDebugStringA("Emergency manual save completed\n");
            }
            else {
                UpdateStatusBar("EMERGENCY SAVE FAILED!");
            }
        }
    }
}



bool DayZLauncher::QueryDZSAAPI(std::vector<std::pair<std::string, int>>& servers) {
    try {
        UpdateStatusBar("Querying DZSA API for server list...");

     
        std::wstring host = L"dayzsalauncher.com";
        std::wstring path = L"/api/v1/query/servers";

        std::string response = HttpGet(host, path, 443, true);

        if (response.empty()) {
            UpdateStatusBar("DZSA API returned empty response");
            OutputDebugStringA("DZSA API: Empty response received\n");
            return false;
        }


        std::string debugResp = response.substr(0, 500);
        OutputDebugStringA(("DZSA API Response (first 500 chars): " + debugResp + "\n").c_str());


        size_t dataStart = std::string::npos;

        std::vector<std::string> dataPatterns = {
            "\"data\":[",     
            "\"servers\":[",   
            "\"result\":[",    
            "[{\"ip\""        
        };

        for (const std::string& pattern : dataPatterns) {
            dataStart = response.find(pattern);
            if (dataStart != std::string::npos) {
                OutputDebugStringA(("Found data pattern: " + pattern + "\n").c_str());
                break;
            }
        }

        if (dataStart == std::string::npos) {
            OutputDebugStringA("DZSA API: No recognizable data pattern found\n");

            dataStart = response.find("[{");
            if (dataStart == std::string::npos) {
                UpdateStatusBar("DZSA API response format not recognized");
                return false;
            }
            OutputDebugStringA("DZSA API: Found fallback array pattern\n");
        }


        size_t arrayStart = response.find('[', dataStart);
        if (arrayStart == std::string::npos) {
            UpdateStatusBar("DZSA API: No array found in response");
            return false;
        }

    
        size_t arrayEnd = response.find(']', arrayStart);
        if (arrayEnd == std::string::npos) {
            UpdateStatusBar("DZSA API: Malformed array in response");
            return false;
        }

        std::string serversJson = response.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
        OutputDebugStringA(("DZSA API: Extracted servers JSON length: " + std::to_string(serversJson.length()) + "\n").c_str());

      
        size_t pos = 0;
        int serverCount = 0;
        int maxServers = 10000; 

        while (pos < serversJson.length() && serverCount < maxServers) {
         
            size_t objStart = serversJson.find('{', pos);
            if (objStart == std::string::npos) break;

            size_t objEnd = objStart + 1;
            int braceCount = 1;

            while (objEnd < serversJson.length() && braceCount > 0) {
                if (serversJson[objEnd] == '{') braceCount++;
                else if (serversJson[objEnd] == '}') braceCount--;
                objEnd++;
            }

            if (braceCount != 0) {
                OutputDebugStringA("DZSA API: Malformed JSON object\n");
                break;
            }

            std::string serverObj = serversJson.substr(objStart, objEnd - objStart);
            std::string ip = ParseJsonString(serverObj, "ip");
            std::string portStr = ParseJsonString(serverObj, "port");

            if (ip.empty()) {
                ip = ParseJsonString(serverObj, "address");
                if (!ip.empty() && ip.find(':') != std::string::npos) {
                   
                    size_t colonPos = ip.find(':');
                    portStr = ip.substr(colonPos + 1);
                    ip = ip.substr(0, colonPos);
                }
            }

            if (ip.empty()) ip = ParseJsonString(serverObj, "host");
            if (portStr.empty()) portStr = ParseJsonString(serverObj, "queryPort");
            if (portStr.empty()) portStr = ParseJsonString(serverObj, "gamePort");

         
            if (!ip.empty() && !portStr.empty()) {
                try {
                    int port = std::stoi(portStr);
                    if (port > 0 && port <= 65535) {
                    
                        if (ip.find('.') != std::string::npos && ip.length() >= 7) {
                            servers.emplace_back(ip, port);
                            serverCount++;

                            if (serverCount <= 5) {
                                OutputDebugStringA(("DZSA Server: " + ip + ":" + std::to_string(port) + "\n").c_str());
                            }
                        }
                    }
                }
                catch (const std::exception& e) {
                    OutputDebugStringA(("DZSA API: Invalid port: " + portStr + "\n").c_str());
                }
            }

            pos = objEnd;
        }

        UpdateStatusBar("DZSA API returned " + std::to_string(serverCount) + " servers");
        OutputDebugStringA(("DZSA API: Successfully parsed " + std::to_string(serverCount) + " servers\n").c_str());

        return serverCount > 0;

    }
    catch (const std::exception& e) {
        UpdateStatusBar("DZSA API query failed - using fallback methods");
        OutputDebugStringA(("DZSA API exception: " + std::string(e.what()) + "\n").c_str());
        return false;
    }
    catch (...) {
        UpdateStatusBar("DZSA API query failed - unknown error");
        OutputDebugStringA("DZSA API: Unknown exception occurred\n");
        return false;
    }
}



std::string DayZLauncher::HttpGet(const std::wstring& host, const std::wstring& path, int port, bool useSSL) {
    std::string result;

    HINTERNET hSession = WinHttpOpen(L"DayZ-Launcher/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);

    if (!hSession) return result;

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return result;
    }

    DWORD flags = useSSL ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    DWORD timeout = 10000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {

        if (WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD dwSize = 0;
            DWORD dwDownloaded = 0;

            do {
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                if (dwSize == 0) break;

                std::vector<char> buffer(dwSize + 1);
                if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                    buffer[dwDownloaded] = '\0';
                    result += buffer.data();
                }
            } while (dwSize > 0);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return result;
}

std::string DayZLauncher::ParseJsonString(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":\"";
    size_t start = json.find(searchKey);
    if (start == std::string::npos) return "";

    start += searchKey.length();
    size_t end = json.find("\"", start);
    if (end == std::string::npos) return "";

    return json.substr(start, end - start);
}

std::vector<std::string> DayZLauncher::ParseJsonArray(const std::string& json, const std::string& arrayKey) {
    std::vector<std::string> result;

    std::string searchKey = "\"" + arrayKey + "\":[";
    size_t start = json.find(searchKey);
    if (start == std::string::npos) return result;

    start += searchKey.length();
    size_t end = json.find("]", start);
    if (end == std::string::npos) return result;

    std::string arrayContent = json.substr(start, end - start);


    size_t pos = 0;
    while (pos < arrayContent.length()) {
        size_t itemStart = arrayContent.find("\"", pos);
        if (itemStart == std::string::npos) break;
        itemStart++;

        size_t itemEnd = arrayContent.find("\"", itemStart);
        if (itemEnd == std::string::npos) break;

        std::string item = arrayContent.substr(itemStart, itemEnd - itemStart);
        if (!item.empty()) {
            result.push_back(item);
        }

        pos = itemEnd + 1;
    }

    return result;
}

std::vector<std::string> DayZLauncher::QueryBattleMetricsMods(const std::string& ip, int port) {
    std::vector<std::string> mods;

    try {
      
        std::wstring host = L"api.battlemetrics.com";
        std::wstring path = L"/servers?filter[game]=dayz&filter[search]=" +
            StringToWString(ip + ":" + std::to_string(port));

        std::string response = HttpGet(host, path, 443, true);

        if (response.empty()) return mods;

        size_t serverStart = response.find("\"data\":[{");
        if (serverStart == std::string::npos) return mods;

     
        size_t modsStart = response.find("\"mods\":", serverStart);
        if (modsStart != std::string::npos) {
     
            size_t arrayStart = response.find("[", modsStart);
            size_t arrayEnd = response.find("]", arrayStart);

            if (arrayStart != std::string::npos && arrayEnd != std::string::npos) {
                std::string modsJson = response.substr(arrayStart + 1, arrayEnd - arrayStart - 1);

            
                size_t pos = 0;
                while (pos < modsJson.length()) {
                    size_t objStart = modsJson.find("{", pos);
                    if (objStart == std::string::npos) break;

                    size_t objEnd = modsJson.find("}", objStart);
                    if (objEnd == std::string::npos) break;

                    std::string modObj = modsJson.substr(objStart, objEnd - objStart + 1);

                    std::string modId = ParseJsonString(modObj, "id");
                    if (!modId.empty()) {
                        if (modId[0] != '@') modId = "@" + modId;
                        mods.push_back(modId);
                    }

                    pos = objEnd + 1;
                }
            }
        }

    
        size_t detailsStart = response.find("\"details\":", serverStart);
        if (detailsStart != std::string::npos) {
            std::string workshopIds = ParseJsonString(response.substr(detailsStart), "workshop");
            if (!workshopIds.empty()) {
                std::vector<std::string> workshopMods = ParseRealModIDs(workshopIds);
                mods.insert(mods.end(), workshopMods.begin(), workshopMods.end());
            }
        }

    }
    catch (...) {
       
    }

    return mods;
}





std::vector<std::string> DayZLauncher::ParseRealModIDs(const std::string& modString) {
    std::vector<std::string> realMods;

    if (modString.empty() || modString == "0" || modString == "none" ||
        modString == "vanilla" || modString == "null") {
        return realMods;
    }

    OutputDebugStringA(("ParseRealModIDs input: '" + modString + "'\n").c_str());

 
    std::vector<std::string> parts;
    std::string current = modString;

 
    std::vector<char> separators = { ';', ',', '|', ' ', '\n', '\t' };

    for (char sep : separators) {
        if (current.find(sep) != std::string::npos) {
            std::stringstream ss(current);
            std::string part;

            while (std::getline(ss, part, sep)) {
        
                part.erase(0, part.find_first_not_of(" \t\r\n\"'"));
                part.erase(part.find_last_not_of(" \t\r\n\"'") + 1);

                if (!part.empty() && part != "0" && part != "none") {
                    parts.push_back(part);
                }
            }
            break;
        }
    }


    if (parts.empty()) {
        std::string cleaned = current;
        cleaned.erase(0, cleaned.find_first_not_of(" \t\r\n\"'"));
        cleaned.erase(cleaned.find_last_not_of(" \t\r\n\"'") + 1);
        if (!cleaned.empty()) {
            parts.push_back(cleaned);
        }
    }


    for (const std::string& part : parts) {
        if (part.empty() || part == "0" || part == "none") continue;

        OutputDebugStringA(("Processing mod part: '" + part + "'\n").c_str());


        if (part.length() >= 9 && part.length() <= 10 &&
            std::all_of(part.begin(), part.end(), ::isdigit)) {
            realMods.push_back("@" + part);
            OutputDebugStringA(("Added Workshop ID: @" + part + "\n").c_str());
            continue;
        }

     
        if (part.length() >= 10 && part[0] == '@' &&
            std::all_of(part.begin() + 1, part.end(), ::isdigit)) {
            realMods.push_back(part);
            OutputDebugStringA(("Added prefixed Workshop ID: " + part + "\n").c_str());
            continue;
        }


        std::string lowerPart = part;
        std::transform(lowerPart.begin(), lowerPart.end(), lowerPart.begin(), ::tolower);

 
        if (lowerPart.find("expansion") != std::string::npos ||
            lowerPart.find("trader") != std::string::npos ||
            lowerPart.find("cf") != std::string::npos ||
            lowerPart.find("code") != std::string::npos ||
            lowerPart.find("basebuilding") != std::string::npos ||
            lowerPart.find("unlimited") != std::string::npos ||
            lowerPart.find("stamina") != std::string::npos ||
            lowerPart.find("weapons") != std::string::npos ||
            lowerPart.find("vehicles") != std::string::npos ||
            lowerPart.find("clothing") != std::string::npos ||
            (part.length() >= 8 && part.find_first_of("0123456789") != std::string::npos)) {

            std::string modName = part;
            if (modName[0] != '@') modName = "@" + modName;
            realMods.push_back(modName);
            OutputDebugStringA(("Added named mod: " + modName + "\n").c_str());
            continue;
        }

    
        bool hasLetter = false;
        bool hasNumber = false;
        for (char c : part) {
            if (std::isalpha(c)) hasLetter = true;
            if (std::isdigit(c)) hasNumber = true;
        }

        if (hasLetter && part.length() >= 3) {
            std::string modName = part;
            if (modName[0] != '@') modName = "@" + modName;
            realMods.push_back(modName);
            OutputDebugStringA(("Added generic mod: " + modName + "\n").c_str());
        }
    }

    OutputDebugStringA(("ParseRealModIDs found " + std::to_string(realMods.size()) + " valid mods\n").c_str());
    return realMods;
}


std::vector<std::string> DayZLauncher::QueryGameTrackerMods(const std::string& ip, int port) {
    std::vector<std::string> mods;

    try {
  
        std::wstring host = L"api.gametracker.com";
        std::wstring path = L"/servers/" + StringToWString(ip + ":" + std::to_string(port));

        std::string response = HttpGet(host, path, 443, true);

        if (!response.empty()) {
           
            std::string modsData = ParseJsonString(response, "mods");
            if (!modsData.empty()) {
                mods = ParseRealModIDs(modsData);
            }
        }
    }
    catch (...) {
     
    }

    return mods;
}



std::vector<std::string> DayZLauncher::QueryBattlEyeInfo(const std::string& ip, int port) {
    std::vector<std::string> mods;


    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return mods;

    DWORD timeout = 3000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port + 2); 
    inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr);
    std::vector<uint8_t> query = { 0xFF, 0x00, 0x6D, 0x6F, 0x64, 0x73 }; 

    if (sendto(sock, (char*)query.data(), query.size(), 0,
        (sockaddr*)&serverAddr, sizeof(serverAddr)) != SOCKET_ERROR) {

        std::vector<uint8_t> buffer(1400);
        int received = recvfrom(sock, (char*)buffer.data(), buffer.size(), 0, nullptr, nullptr);

        if (received > 0) {
            std::string response(buffer.begin(), buffer.begin() + received);
            mods = ParseRealModIDs(response);
        }
    }

    closesocket(sock);
    return mods;
}



std::vector<std::string> DayZLauncher::DetectModsFromServerName(const std::string& serverName) {
    std::vector<std::string> detectedMods;
    std::string upperName = serverName;
    std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);

    if (upperName.find("MODDED") != std::string::npos) {
        detectedMods.push_back("@Unknown_Mods"); 
    }

    return detectedMods;
}


std::vector<std::string> DayZLauncher::ParseModString(const std::string& modString) {
    std::vector<std::string> mods;

    if (modString.empty() || modString == "0" || modString == "none") {
        return mods;
    }


    std::string current = modString;
    std::vector<char> separators = { ';', ',', '|', ' ' };

    for (char sep : separators) {
        if (current.find(sep) != std::string::npos) {
            size_t pos = 0;
            while ((pos = current.find(sep)) != std::string::npos) {
                std::string mod = current.substr(0, pos);
                current.erase(0, pos + 1);

           
                mod.erase(0, mod.find_first_not_of(" \t\r\n"));
                mod.erase(mod.find_last_not_of(" \t\r\n") + 1);

                if (!mod.empty() && mod != "0") {
                    if (mod[0] != '@') mod = "@" + mod;
                    mods.push_back(mod);
                }
            }
       
            if (!current.empty()) {
                current.erase(0, current.find_first_not_of(" \t\r\n"));
                current.erase(current.find_last_not_of(" \t\r\n") + 1);
                if (!current.empty() && current != "0") {
                    if (current[0] != '@') current = "@" + current;
                    mods.push_back(current);
                }
            }
            break;
        }
    }

    if (mods.empty() && !current.empty()) {
        current.erase(0, current.find_first_not_of(" \t\r\n"));
        current.erase(current.find_last_not_of(" \t\r\n") + 1);
        if (!current.empty() && current != "0") {
            if (current[0] != '@') current = "@" + current;
            mods.push_back(current);
        }
    }

    return mods;
}

void DayZLauncher::SortServersByColumn(int column) {
    if (column < 0 || column > 5) return;
    if (servers.empty()) return;
    if (currentSortColumn == column) {
        sortAscending = !sortAscending;
    }
    else {
        sortAscending = true;
        currentSortColumn = column;
    }



    std::sort(servers.begin(), servers.end(),
        [column, this](const ServerInfo& a, const ServerInfo& b) -> bool {

            try {
                int compareResult = 0;

                switch (column) {
                case SORT_NAME: 
                    compareResult = a.name.compare(b.name);
                    break;
                case SORT_MAP: 
                    compareResult = a.map.compare(b.map);
                    break;
                case SORT_PLAYERS: 
                    if (a.players < b.players) compareResult = -1;
                    else if (a.players > b.players) compareResult = 1;
                    else compareResult = 0;
                    break;
                case SORT_PING:
                    if (a.ping == -1 && b.ping == -1) compareResult = 0;
                    else if (a.ping == -1) compareResult = 1;
                    else if (b.ping == -1) compareResult = -1;
                    else {
                        if (a.ping < b.ping) compareResult = -1;
                        else if (a.ping > b.ping) compareResult = 1;
                        else compareResult = 0;
                    }
                    break;
                case SORT_IP: 
                    compareResult = a.ip.compare(b.ip);
                    break;
                case SORT_VERSION: 
                    compareResult = a.version.compare(b.version);
                    break;
                default:
                    return false; 
                }

          
                if (sortAscending) {
                    return compareResult < 0;
                }
                else {
                    return compareResult > 0;
                }
            }
            catch (...) {
                return false;
            }
        });

    PopulateServerList();
}

void DayZLauncher::PopulateServerList() {
    ApplyFiltersAndUpdate();
}



std::vector<ServerInfo> DayZLauncher::GetFilteredServers() const {
    std::vector<ServerInfo> filtered;

    try {
        if (currentTab == TAB_FAVORITES) {
            const auto& favorites = favoritesManager->GetFavorites();

            for (const auto& favorite : favorites) {
                bool foundLive = false;
                for (const ServerInfo& server : servers) {
                    if (server.ip == favorite.ip && server.port == favorite.port) {
                        ServerInfo favoriteServer = server;
                        favoriteServer.isFavorite = true; 
                        filtered.push_back(favoriteServer);
                        foundLive = true;
                        break;
                    }
                }

           
                if (!foundLive) {
                    ServerInfo offlineServer;
                    offlineServer.name = favorite.name + " (OFFLINE)";
                    offlineServer.ip = favorite.ip;
                    offlineServer.port = favorite.port;
                    offlineServer.map = "Unknown";
                    offlineServer.players = 0;
                    offlineServer.maxPlayers = 0;
                    offlineServer.ping = -1;
                    offlineServer.isOfficial = false;
                    offlineServer.isFavorite = true;
                    offlineServer.version = "Unknown";
                    offlineServer.lastUpdated = time(nullptr);

                    filtered.push_back(offlineServer);
                }
            }
            return filtered;
        }

    
        for (const ServerInfo& server : servers) {
            try {
           
                if (currentTab == TAB_OFFICIAL && !server.isOfficial) continue;
                if (currentTab == TAB_COMMUNITY && server.isOfficial) continue;
                if (currentTab == TAB_LAN) {
                    if (!IsLANAddress(server.ip)) continue;
                }

                if (PassesFilters(server)) {
                    filtered.push_back(server);
                }
            }
            catch (...) {
                continue;
            }
        }
    }
    catch (...) {
        return std::vector<ServerInfo>();
    }

    return filtered;
}


bool DayZLauncher::IsLANAddress(const std::string& ip) const {
    if (ip.substr(0, 3) == "10.") return true;
    if (ip.substr(0, 8) == "192.168.") return true;
    if (ip.substr(0, 4) == "127.") return true;
    if (ip.substr(0, 4) == "172.") {
        size_t secondDot = ip.find('.', 4);
        if (secondDot != std::string::npos) {
            std::string secondOctet = ip.substr(4, secondDot - 4);
            try {
                int octet = std::stoi(secondOctet);
                if (octet >= 16 && octet <= 31) return true;
            }
            catch (...) {}
        }
    }

    return false;
}



void DayZLauncher::CreateFilterControls() {
    HINSTANCE hInst = GetModuleHandle(nullptr);


    CreateWindow(L"STATIC", L"Map:", WS_CHILD | WS_VISIBLE,
        420, 472, 30, 20, hWnd, nullptr, hInst, nullptr);

    hMapFilterEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
        450, 470, 100, 25, hWnd, (HMENU)IDC_MAP_FILTER, hInst, nullptr);


    hShowFavoritesCheck = CreateWindow(L"BUTTON", L"Favorites Only",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        570, 450, 120, 20, hWnd, (HMENU)IDC_SHOW_FAVORITES, hInst, nullptr);

    hShowPasswordCheck = CreateWindow(L"BUTTON", L"Show Password Protected",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        570, 470, 150, 20, hWnd, (HMENU)IDC_SHOW_PASSWORDED, hInst, nullptr);

    hShowOnlineCheck = CreateWindow(L"BUTTON", L"Online Only",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        570, 490, 100, 20, hWnd, (HMENU)IDC_SHOW_ONLINE, hInst, nullptr);


    SendMessage(hShowPasswordCheck, BM_SETCHECK, BST_CHECKED, 0);
    SendMessage(hShowOnlineCheck, BM_SETCHECK, BST_CHECKED, 0);
}

void DayZLauncher::ShowSettingsControls(bool show) {
    int showCmd = show ? SW_SHOW : SW_HIDE;
    if (hProfileNameEdit) ShowWindow(hProfileNameEdit, showCmd);
    if (hProfilePathEdit) ShowWindow(hProfilePathEdit, showCmd);
    if (hDayZPathEdit) ShowWindow(hDayZPathEdit, showCmd);
    if (hBrowseProfileBtn) ShowWindow(hBrowseProfileBtn, showCmd);
    if (hBrowseDayZBtn) ShowWindow(hBrowseDayZBtn, showCmd);
    if (hQueryDelayEdit) ShowWindow(hQueryDelayEdit, showCmd);
    if (hProfileNameLabel) ShowWindow(hProfileNameLabel, showCmd);
    if (hProfilePathLabel) ShowWindow(hProfilePathLabel, showCmd);
    if (hDayZPathLabel) ShowWindow(hDayZPathLabel, showCmd);
    if (hQueryDelayLabel) ShowWindow(hQueryDelayLabel, showCmd);
    if (hSaveSettingsBtn) ShowWindow(hSaveSettingsBtn, showCmd);
    if (hReloadSettingsBtn) ShowWindow(hReloadSettingsBtn, showCmd);
    if (hTestDayZBtn) ShowWindow(hTestDayZBtn, showCmd);
    if (hForceSaveBtn) ShowWindow(hForceSaveBtn, showCmd);
    if (hEmergencySaveBtn) ShowWindow(hEmergencySaveBtn, showCmd);
}

void DayZLauncher::ShowThemeControls(bool show) {
    int showCmd = show ? SW_SHOW : SW_HIDE;
    if (hColorBgEdit) ShowWindow(hColorBgEdit, showCmd);
    if (hColorTextEdit) ShowWindow(hColorTextEdit, showCmd);
    if (hColorButtonEdit) ShowWindow(hColorButtonEdit, showCmd);
    if (hApplyThemeBtn) ShowWindow(hApplyThemeBtn, showCmd);
    if (hColorBgLabel) ShowWindow(hColorBgLabel, showCmd);
    if (hColorTextLabel) ShowWindow(hColorTextLabel, showCmd);
    if (hColorButtonLabel) ShowWindow(hColorButtonLabel, showCmd);
}


void DayZLauncher::OnTabChanged() {
    int newTab = TabCtrl_GetCurSel(hTab);

    if (newTab == -1) return;

    currentTab = newTab;

    if (currentTab == TAB_SETTINGS) {
        UpdateStatusBar("=== SETTINGS TAB SELECTED ===");
        if (hFilterPanel) ShowWindow(hFilterPanel, SW_HIDE);
        if (hServerList) ShowWindow(hServerList, SW_HIDE);
        if (hRefreshBtn) ShowWindow(hRefreshBtn, SW_HIDE);
        if (hJoinBtn) ShowWindow(hJoinBtn, SW_HIDE);
        if (hFavoriteBtn) ShowWindow(hFavoriteBtn, SW_HIDE);
        if (hFilterEdit) ShowWindow(hFilterEdit, SW_HIDE);
        if (hProgressBar) ShowWindow(hProgressBar, SW_HIDE);

    
        ShowSettingsControls(true);
        ShowThemeControls(true);

        UpdateStatusBar("Settings visible!");
    }
    else {

        if (hFilterPanel) ShowWindow(hFilterPanel, SW_SHOW);
        if (hServerList) ShowWindow(hServerList, SW_SHOW);
        if (hRefreshBtn) ShowWindow(hRefreshBtn, SW_SHOW);
        if (hJoinBtn) ShowWindow(hJoinBtn, SW_SHOW);
        if (hFavoriteBtn) ShowWindow(hFavoriteBtn, SW_SHOW);
        if (hFilterEdit) ShowWindow(hFilterEdit, SW_SHOW);
        if (hProgressBar) ShowWindow(hProgressBar, SW_SHOW);
        ShowSettingsControls(false);
        ShowThemeControls(false);
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        InvalidateRect(hWnd, &clientRect, TRUE);
        ResizeControls();
        PopulateServerList();

        UpdateStatusBar("Server tab: " + std::to_string(currentTab));
    }


    RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
}

void DayZLauncher::ShowSettingsLabels(bool show) {
    int showCmd = show ? SW_SHOW : SW_HIDE;
    if (hProfileNameLabel) ShowWindow(hProfileNameLabel, showCmd);
    if (hProfilePathLabel) ShowWindow(hProfilePathLabel, showCmd);
    if (hDayZPathLabel) ShowWindow(hDayZPathLabel, showCmd);
    if (hQueryDelayLabel) ShowWindow(hQueryDelayLabel, showCmd);
    if (hSaveSettingsBtn) ShowWindow(hSaveSettingsBtn, showCmd);
    if (hReloadSettingsBtn) ShowWindow(hReloadSettingsBtn, showCmd);
}



void DayZLauncher::ForceCreateSettingsControls() {
    HINSTANCE hInst = GetModuleHandle(nullptr);

   
    if (!hProfileNameEdit) {
        hProfileNameEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"TestProfile",
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
            180, 100, 200, 25, hWnd, (HMENU)IDC_PROFILE_NAME_EDIT, hInst, nullptr);
    }


    if (!hProfilePathEdit) {
        hProfilePathEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"C:\\Users\\YourName\\Documents\\DayZ",
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
            180, 140, 300, 25, hWnd, (HMENU)IDC_PROFILE_PATH_EDIT, hInst, nullptr);
    }

  
    if (!hDayZPathEdit) {
        hDayZPathEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\DayZ\\DayZ_x64.exe",
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
            180, 180, 300, 25, hWnd, (HMENU)IDC_DAYZ_PATH_EDIT, hInst, nullptr);
    }


    if (!hBrowseProfileBtn) {
        hBrowseProfileBtn = CreateWindow(L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            490, 140, 80, 25, hWnd, (HMENU)IDC_BROWSE_PROFILE_BTN, hInst, nullptr);
    }

    if (!hBrowseDayZBtn) {
        hBrowseDayZBtn = CreateWindow(L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            490, 180, 80, 25, hWnd, (HMENU)IDC_BROWSE_DAYZ_BTN, hInst, nullptr);
    }


    if (!hQueryDelayEdit) {
        hQueryDelayEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"100",
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_NUMBER,
            210, 220, 60, 25, hWnd, (HMENU)IDC_QUERY_DELAY_EDIT, hInst, nullptr);
    }

    UpdateStatusBar("Force created all settings controls!");
}

void DayZLauncher::OnServerSelected() {
    int selected = ListView_GetNextItem(hServerList, -1, LVNI_SELECTED);
    bool hasSelection = (selected != -1);

    EnableWindow(hJoinBtn, hasSelection);
    EnableWindow(hFavoriteBtn, hasSelection);

    if (hasSelection) {
     
        std::vector<ServerInfo> filteredServers = GetFilteredServers();
        if (selected >= 0 && selected < static_cast<int>(filteredServers.size())) {
            const ServerInfo& selectedServer = filteredServers[selected];

       
            if (selectedServer.isFavorite) {
                SetWindowText(hFavoriteBtn, L"Remove Favorite");
                UpdateStatusBar("Selected favorite: " + selectedServer.name + " - Click to remove");
            }
            else {
                SetWindowText(hFavoriteBtn, L"Add Favorite");
                UpdateStatusBar("Selected: " + selectedServer.name + " - Click to add to favorites");
            }
        }
    }
    else {
        SetWindowText(hFavoriteBtn, L"Add Favorite");
        UpdateStatusBar("No server selected");
    }
}


void DayZLauncher::DebugFavorites() {
    if (!favoritesManager) {
        UpdateStatusBar("FavoritesManager is NULL!");
        return;
    }

    const auto& favorites = favoritesManager->GetFavorites();

    std::string msg = "Loaded " + std::to_string(favorites.size()) + " favorites: ";
    for (const auto& fav : favorites) {
        msg += fav.name + " (" + fav.ip + ":" + std::to_string(fav.port) + "), ";
    }

    OutputDebugStringA((msg + "\n").c_str());
    UpdateStatusBar(msg);
}



void DayZLauncher::JoinServer() {
    ServerInfo* selectedServer = GetSelectedServer();
    if (!selectedServer) {
        UpdateStatusBar("No server selected!");
        return;
    }

 
    std::string dayzPathStr = configManager->GetString("dayzPath", "");

    if (dayzPathStr.empty()) {
        MessageBox(hWnd, L"DayZ path not set! Go to Settings tab and set DayZ path.", L"Error", MB_OK);
        return;
    }


    if (GetFileAttributesA(dayzPathStr.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBox(hWnd, L"DayZ executable not found! Check path in Settings.", L"Error", MB_OK);
        return;
    }

    UpdateStatusBar("Launching DayZ Standalone...");


    std::string dayzDir = dayzPathStr;
    size_t lastSlash = dayzDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        dayzDir = dayzDir.substr(0, lastSlash);
    }


    std::vector<std::string> arguments;

    arguments.push_back("-connect=" + selectedServer->ip);
    arguments.push_back("-port=" + std::to_string(selectedServer->port));


    std::string profileName = configManager->GetString("profileName", "");
    if (!profileName.empty()) {
        arguments.push_back("-name=" + profileName); 
    }

 
    std::string profilePath = configManager->GetString("profilePath", "");
    if (!profilePath.empty()) {
        arguments.push_back("-profiles=" + profilePath);  
    }

 
    if (!selectedServer->mods.empty()) {
        std::string modList;
        for (size_t i = 0; i < selectedServer->mods.size(); ++i) {
            if (i > 0) modList += ";";

            std::string mod = selectedServer->mods[i];

     
            if (!mod.empty() && mod[0] == '@') {
                mod = mod.substr(1);
            }

            size_t hyphenPos = mod.find('-');
            if (hyphenPos != std::string::npos) {
                mod = mod.substr(0, hyphenPos);
            }

            if (!mod.empty() && std::all_of(mod.begin(), mod.end(), ::isdigit)) {
                modList += "@" + mod;  
            }
        }

        if (!modList.empty()) {
            arguments.push_back("-mod=" + modList);  
            OutputDebugStringA(("DayZ launching with mods: " + modList + "\n").c_str());
        }
    }


    arguments.push_back("-nolauncher");    
    arguments.push_back("-world=empty"); 
    arguments.push_back("-nosplash");      
    arguments.push_back("-skipIntro");    

 
    std::string cmdLine = "\"" + dayzPathStr + "\"";
    for (const std::string& arg : arguments) {
        cmdLine += " " + arg; 
    }


    OutputDebugStringA(("DayZ Command Line: " + cmdLine + "\n").c_str());
    OutputDebugStringA(("DayZ Working Directory: " + dayzDir + "\n").c_str());


    bool steamLaunchSuccess = LaunchViaSteam(selectedServer);
    if (steamLaunchSuccess) {
        return; 
    }


    UpdateStatusBar("Steam launch failed, trying direct launch...");
    STARTUPINFOA si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW;


    std::vector<char> cmdLineVec(cmdLine.begin(), cmdLine.end());
    cmdLineVec.push_back('\0');


    BOOL success = CreateProcessA(
        nullptr,                    
        cmdLineVec.data(),       
        nullptr,                   
        nullptr,                   
        FALSE,                      
        CREATE_NEW_CONSOLE,      
        nullptr,                   
        dayzDir.c_str(),           
        &si,                       
        &pi                       
    );

    if (success) {
        std::string successMsg = "DayZ launched successfully - connecting to " + selectedServer->name;
        if (!selectedServer->mods.empty()) {
            successMsg += " with " + std::to_string(selectedServer->mods.size()) + " mods";
        }
        UpdateStatusBar(successMsg);
        favoritesManager->AddToHistory(*selectedServer);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

    
        SetTimer(hWnd, 1, 3000, NULL);

    }
    else {
        DWORD error = GetLastError();
        std::string errorMsg = "Failed to launch DayZ! Error code: " + std::to_string(error);

      
        switch (error) {
        case ERROR_FILE_NOT_FOUND: 
            errorMsg += "\nDayZ executable not found at: " + dayzPathStr;
            errorMsg += "\nCheck if DayZ is installed correctly.";
            break;
        case ERROR_ACCESS_DENIED:
            errorMsg += "\nAccess denied. Try running launcher as administrator.";
            break;
        case ERROR_BAD_EXE_FORMAT:
            errorMsg += "\nInvalid executable format. Make sure you selected DayZ_x64.exe";
            break;
        default:
            errorMsg += "\nCheck DayZ path in Settings. Make sure Steam is running.";
            break;
        }

        UpdateStatusBar("Launch failed - Error " + std::to_string(error));
        MessageBoxA(hWnd, errorMsg.c_str(), "DayZ Launch Error", MB_OK | MB_ICONERROR);

    
        OutputDebugStringA(("DayZ launch failed: " + errorMsg + "\n").c_str());
    }
}

bool DayZLauncher::LaunchViaSteam(ServerInfo* server) {
    try {
        UpdateStatusBar("Attempting Steam launch...");


        std::string steamCmd = "steam://run/221100//";

 
        steamCmd += "-connect=" + server->ip + "%20";
        steamCmd += "-port=" + std::to_string(server->port) + "%20";

   
        std::string profileName = configManager->GetString("profileName", "");
        if (!profileName.empty()) {
            steamCmd += "-name=" + profileName + "%20";
        }


        if (!server->mods.empty()) {
            std::string modList;
            for (size_t i = 0; i < server->mods.size(); ++i) {
                if (i > 0) modList += ";";

                std::string mod = server->mods[i];
                if (!mod.empty() && mod[0] == '@') {
                    mod = mod.substr(1);
                }

        
                size_t hyphenPos = mod.find('-');
                if (hyphenPos != std::string::npos) {
                    mod = mod.substr(0, hyphenPos);
                }

                if (std::all_of(mod.begin(), mod.end(), ::isdigit)) {
                    modList += "@" + mod;
                }
            }

            if (!modList.empty()) {
                steamCmd += "-mod=" + modList + "%20";
            }
        }

        steamCmd += "-nolauncher";

        OutputDebugStringA(("Steam command: " + steamCmd + "\n").c_str());

        HINSTANCE result = ShellExecuteA(nullptr, "open", steamCmd.c_str(), nullptr, nullptr, SW_SHOW);

        if ((intptr_t)result > 32) {
            UpdateStatusBar("DayZ launched via Steam");
            favoritesManager->AddToHistory(*server);
            SetTimer(hWnd, 1, 3000, NULL); 
            return true;
        }

        OutputDebugStringA("Steam launch failed\n");
        return false;
    }
    catch (...) {
        OutputDebugStringA("Exception in Steam launch\n");
        return false;
    }
}


bool DayZLauncher::DetectOfficialServer(const std::string& name, const std::string& folder) {

    if (name.length() >= 7) {
        bool hasDigits = std::isdigit(name[0]) && std::isdigit(name[1]) &&
            std::isdigit(name[2]) && std::isdigit(name[3]);
        bool hasSeparator = (name.length() > 6) && (name.substr(4, 3) == " | ");

        if (hasDigits && hasSeparator) {
            std::string upperName = name;
            std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);

            bool isOfficialRegion = (upperName.find(" NORTH AMERICA ") != std::string::npos &&
                upperName.find(" SOUTH AMERICA ") != std::string::npos ||
                upperName.find(" EUROPE ") != std::string::npos ||
                upperName.find(" ASIA PACIFIC ") != std::string::npos);

            if (isOfficialRegion) {
                OutputDebugStringA(("Detected official server (pattern): " + name + "\n").c_str());
                return true;
            }
        }
    }



    std::string upperName = name;
    std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);


    if 
        (upperName.find(" NORTH AMERICA ") != std::string::npos &&
            upperName.find(" SOUTH AMERICA ") != std::string::npos ||
                upperName.find(" EUROPE ") != std::string::npos ||
                upperName.find(" ASIA PACIFIC ") != std::string::npos) {

        OutputDebugStringA(("Detected official server (keywords): " + name + "\n").c_str());
        return true;
    }

    return false;
}


bool DayZLauncher::ExtractModsFromRules(const A2SRulesResponse& rules, std::vector<std::string>& mods) {
    mods.clear();


    std::vector<std::string> modFields = {
        "modIds",         
        "requiredMods",    
        "mods",             
        "workshopIds",     
        "modlist",         
        "steam_mods",    
        "@"
    };

    OutputDebugStringA("=== SEARCHING FOR MODS IN SERVER RULES ===\n");

    for (const auto& rule : rules.rules) {
        OutputDebugStringA(("Rule: '" + rule.name + "' = '" + rule.value + "'\n").c_str());

       
        for (const auto& field : modFields) {
            if (rule.name == field && !rule.value.empty() &&
                rule.value != "0" && rule.value != "none" && rule.value != "vanilla") {

                OutputDebugStringA(("FOUND MOD FIELD: " + field + " = " + rule.value + "\n").c_str());

            
                std::vector<std::string> foundMods = ExtractWorkshopIDs(rule.value);

                if (!foundMods.empty()) {
                    mods.insert(mods.end(), foundMods.begin(), foundMods.end());
                    OutputDebugStringA(("Extracted " + std::to_string(foundMods.size()) + " valid Workshop IDs\n").c_str());
                    return true;
                }
            }
        }
    }

    OutputDebugStringA("No valid mod Workshop IDs found in server rules\n");
    return false;
}

std::vector<std::string> DayZLauncher::ExtractWorkshopIDs(const std::string& modString) {
    std::vector<std::string> workshopIDs;

    if (modString.empty() || modString == "0" || modString == "none" || modString == "vanilla") {
        return workshopIDs;
    }

    OutputDebugStringA(("Extracting Workshop IDs from: '" + modString + "'\n").c_str());


    std::vector<std::string> parts;
    std::stringstream ss(modString);
    std::string part;

 
    char separators[] = { ';', ',', '|', ' ', '\n', '\t' };
    std::string workingString = modString;

    for (char sep : separators) {
        if (workingString.find(sep) != std::string::npos) {
            std::stringstream splitStream(workingString);
            parts.clear();

            while (std::getline(splitStream, part, sep)) {
                if (!part.empty()) {
                    parts.push_back(part);
                }
            }
            break;
        }
    }

 
    if (parts.empty()) {
        parts.push_back(workingString);
    }


    for (const std::string& rawPart : parts) {
        std::string cleanPart = rawPart;

   
        cleanPart.erase(0, cleanPart.find_first_not_of(" \t\r\n\"'@"));
        cleanPart.erase(cleanPart.find_last_not_of(" \t\r\n\"'") + 1);

        if (cleanPart.empty()) continue;

        OutputDebugStringA(("Processing part: '" + cleanPart + "'\n").c_str());

        if (cleanPart.length() >= 9 && cleanPart.length() <= 10 &&
            std::all_of(cleanPart.begin(), cleanPart.end(), ::isdigit)) {
            workshopIDs.push_back("@" + cleanPart);
            OutputDebugStringA(("VALID Workshop ID: @" + cleanPart + "\n").c_str());
            continue;
        }

 
        if (cleanPart.length() >= 10 && cleanPart[0] == '@' &&
            std::all_of(cleanPart.begin() + 1, cleanPart.end(), ::isdigit)) {
            workshopIDs.push_back(cleanPart);
            OutputDebugStringA(("VALID @ format: " + cleanPart + "\n").c_str());
            continue;
        }

     
        if (cleanPart.length() >= 10 && cleanPart[0] == '@') {
            size_t hyphenPos = cleanPart.find('-');
            if (hyphenPos != std::string::npos) {
                std::string idPart = cleanPart.substr(1, hyphenPos - 1);
                if (idPart.length() >= 9 && idPart.length() <= 10 &&
                    std::all_of(idPart.begin(), idPart.end(), ::isdigit)) {
                    workshopIDs.push_back("@" + idPart);
                    OutputDebugStringA(("VALID ID from name format: @" + idPart + "\n").c_str());
                    continue;
                }
            }
        }

  
        std::regex workshopRegex(R"(\b(\d{9,10})\b)");
        std::smatch match;
        if (std::regex_search(cleanPart, match, workshopRegex)) {
            std::string foundID = match[1].str();
            workshopIDs.push_back("@" + foundID);
            OutputDebugStringA(("EXTRACTED ID from text: @" + foundID + "\n").c_str());
            continue;
        }

        OutputDebugStringA(("REJECTED (not a valid Workshop ID): '" + cleanPart + "'\n").c_str());
    }

    OutputDebugStringA(("Final result: " + std::to_string(workshopIDs.size()) + " valid Workshop IDs\n").c_str());
    return workshopIDs;
}

bool DayZLauncher::QueryDZSAServerMods(const std::string& ip, int port, std::vector<std::string>& mods) {
    try {

        std::wstring host = L"dayzsalauncher.com";
        std::wstring path = L"/api/v1/query/" + StringToWString(ip) + L"/" + std::to_wstring(port);

        std::string response = HttpGet(host, path, 443, true);

        if (response.empty()) {
            OutputDebugStringA("DZSA server query: Empty response\n");
            return false;
        }

        OutputDebugStringA(("DZSA server response length: " + std::to_string(response.length()) + "\n").c_str());

     
        std::string modsField = ParseJsonString(response, "mods");
        if (!modsField.empty()) {
            mods = ExtractWorkshopIDs(modsField);
            OutputDebugStringA(("DZSA found " + std::to_string(mods.size()) + " mods\n").c_str());
            return !mods.empty();
        }

  
        std::vector<std::string> altFields = { "modIds", "requiredMods", "workshopIds" };
        for (const std::string& field : altFields) {
            std::string altMods = ParseJsonString(response, field);
            if (!altMods.empty()) {
                mods = ExtractWorkshopIDs(altMods);
                if (!mods.empty()) {
                    OutputDebugStringA(("DZSA found mods in " + field + ": " + std::to_string(mods.size()) + "\n").c_str());
                    return true;
                }
            }
        }

        OutputDebugStringA("DZSA: No mod fields found in response\n");
        return false;

    }
    catch (const std::exception& e) {
        OutputDebugStringA(("DZSA server query exception: " + std::string(e.what()) + "\n").c_str());
        return false;
    }
}


bool DayZLauncher::QueryMultipleAPIs(std::vector<std::pair<std::string, int>>& servers) {
    UpdateStatusBar("Trying multiple server APIs...");

  
    if (QueryDZSAAPI(servers) && !servers.empty()) {
        UpdateStatusBar("DZSA API successful: " + std::to_string(servers.size()) + " servers");
        return true;
    }


    if (QueryBattleMetricsAPI(servers) && !servers.empty()) {
        UpdateStatusBar("BattleMetrics API successful: " + std::to_string(servers.size()) + " servers");
        return true;
    }

    if (QueryGameTrackerAPI(servers) && !servers.empty()) {
        UpdateStatusBar("GameTracker API successful: " + std::to_string(servers.size()) + " servers");
        return true;
    }

    UpdateStatusBar("All APIs failed, falling back to Steam master server");
    return false;
}

bool DayZLauncher::QueryBattleMetricsAPI(std::vector<std::pair<std::string, int>>& servers) {
    try {
        UpdateStatusBar("Trying BattleMetrics API...");

        std::wstring host = L"api.battlemetrics.com";
        std::wstring path = L"/servers?filter[game]=dayz&page[size]=100";

        std::string response = HttpGet(host, path, 443, true);

        if (!response.empty()) {
            OutputDebugStringA(("BattleMetrics response length: " + std::to_string(response.length()) + "\n").c_str());

            size_t dataStart = response.find("\"data\":[");
            if (dataStart != std::string::npos) {
         
                size_t pos = dataStart;
                while ((pos = response.find("\"ip\":", pos)) != std::string::npos) {
                    std::string ip = ParseJsonString(response.substr(pos), "ip");
                    std::string portStr = ParseJsonString(response.substr(pos), "port");

                    if (!ip.empty() && !portStr.empty()) {
                        try {
                            int port = std::stoi(portStr);
                            servers.emplace_back(ip, port);
                        }
                        catch (...) {}
                    }
                    pos += 10;
                }
                return !servers.empty();
            }
        }
    }
    catch (...) {
        OutputDebugStringA("BattleMetrics API failed\n");
    }
    return false;
}

bool DayZLauncher::QueryGameTrackerAPI(std::vector<std::pair<std::string, int>>& servers) {
    try {
        UpdateStatusBar("Trying GameTracker API...");

        std::wstring host = L"api.gametracker.com";
        std::wstring path = L"/servers/dayz/?limit=1000";

        std::string response = HttpGet(host, path, 443, true);

        if (!response.empty()) {
            OutputDebugStringA(("GameTracker response length: " + std::to_string(response.length()) + "\n").c_str());
  
            return false;
        }
    }
    catch (...) {
        OutputDebugStringA("GameTracker API failed\n");
    }
    return false;
}

bool DayZLauncher::ServerNameIndicatesMods(const std::string& name) {
    std::string upperName = name;
    std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);


    std::vector<std::string> moddedIndicators = {
        "MODDED", "TRADER", "EXPANSION", "CUSTOM", "RP", "PVP", "PVE",
        "BUILDING++", "UNLIMITED", "STAMINA", "LOOT++", "BASEBUILDING",
        "HELICOPTERS", "CARS++", "WEAPONS++", "ZOMBIES++", "HARDCORE",
        "AIRDROPS", "MISSIONS", "KING OF THE HILL", "KOTH", "EXILE",
        "RAVAGE", "CHERNARUS++", "LIVONIA++", "NAMALSK", "DEER ISLE",
        "ESSEKER", "TAKISTAN", "BANOV", "ROSTOW", "PRIPYAT"
    };

    for (const auto& indicator : moddedIndicators) {
        if (upperName.find(indicator) != std::string::npos) {
            OutputDebugStringA(("Server name indicates mods: " + name + " (found: " + indicator + ")\n").c_str());
            return true;
        }
    }

    return false;
}



std::string DayZLauncher::GetCountryFromIP(const std::string& ip) {
    if (ip.substr(0, 3) == "85.") return "DE";
    if (ip.substr(0, 3) == "194") return "NL";
    if (ip.substr(0, 3) == "185") return "RU";
    if (ip.substr(0, 3) == "198") return "US";
    if (ip.substr(0, 3) == "139") return "CA";
    return "Unknown";
}


void DayZLauncher::AddToFavorites() {
    ServerInfo* selectedServer = GetSelectedServer();
    if (selectedServer && !selectedServer->isFavorite) {
        favoritesManager->AddFavorite(*selectedServer);
        selectedServer->isFavorite = true;
        PopulateServerList();
        UpdateStatusBar("Added " + selectedServer->name + " to favorites");
    }
}

void DayZLauncher::RemoveFromFavorites() {
    ServerInfo* selectedServer = GetSelectedServer();
    if (selectedServer && selectedServer->isFavorite) {
        favoritesManager->RemoveFavorite(selectedServer->ip, selectedServer->port);
        selectedServer->isFavorite = false;
        PopulateServerList();
        UpdateStatusBar("Removed " + selectedServer->name + " from favorites");
    }
}


std::string DayZLauncher::GetDayZInstallPath() {
    std::wstring wpath = GetDayZInstallPathW();
    return WStringToString(wpath);
}

void DayZLauncher::MinimizeToTray() {
    if (trayManager) {
        trayManager->ShowTrayIcon();
        ShowWindow(hWnd, SW_HIDE);
    }
}

void DayZLauncher::RestoreFromTray() {
    if (trayManager) {
        trayManager->HideTrayIcon();
        ShowWindow(hWnd, SW_RESTORE);
        SetForegroundWindow(hWnd);
    }
}



void DayZLauncher::HandleTrayMessage(WPARAM wParam, LPARAM lParam) {
    if (lParam == WM_LBUTTONDBLCLK) {
        RestoreFromTray();
    }
    else if (lParam == WM_RBUTTONUP) {
        if (trayManager) {
            POINT pt;
            GetCursorPos(&pt);
            trayManager->ShowTrayMenu(pt);
        }
    }
}


void DayZLauncher::OnCreate() {

    CreateControls();
}

void DayZLauncher::OnDestroy() {
  
    SaveConfiguration();
}

void DayZLauncher::OnSize(int width, int height) {

    ResizeControls();
}

void DayZLauncher::OnCommand(WPARAM wParam, LPARAM lParam) {
 
}

void DayZLauncher::OnNotify(LPARAM lParam) {

}

void DayZLauncher::OnTimer(WPARAM wParam) {

    if (wParam == 1) {
        ShowWindow(hWnd, SW_MINIMIZE);
    }
}

void DayZLauncher::OnContextMenu(WPARAM wParam, LPARAM lParam) {

    POINT pt = { LOWORD(lParam), HIWORD(lParam) };
    ShowServerContextMenu(pt);
}

void DayZLauncher::OnUpdateProgress(int progress) {

    UpdateProgressBar(progress);
}

void DayZLauncher::OnRefreshComplete() {

    isRefreshing = false;
    PopulateServerList();
    UpdateStatusBar("Ready");
    UpdateProgressBar(0);
}

void DayZLauncher::EnableControls(bool enable) {

    EnableWindow(hRefreshBtn, enable);
    EnableWindow(hJoinBtn, enable);
    EnableWindow(hFavoriteBtn, enable);
}


void DayZLauncher::ShowServerContextMenu(POINT pt) {
    ServerInfo* selectedServer = GetSelectedServer();
    if (!selectedServer) return;

    HMENU hMenu = CreatePopupMenu();

    AppendMenu(hMenu, MF_STRING, ID_CONTEXT_JOIN, L"Join Server");
    AppendMenu(hMenu, MF_STRING, ID_CONTEXT_COPY_IP, L"Copy IP:Port");
    AppendMenu(hMenu, MF_STRING, ID_SERVER_REFRESH, L"Refresh This Server");  
    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);

    if (selectedServer->isFavorite) {
        AppendMenu(hMenu, MF_STRING, ID_CONTEXT_REMOVE_FAV, L"Remove from Favorites");
    }
    else {
        AppendMenu(hMenu, MF_STRING, ID_CONTEXT_ADD_FAV, L"Add to Favorites");
    }

    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hMenu, MF_STRING, ID_CONTEXT_SERVER_INFO, L"Server Information");

    SetForegroundWindow(hWnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);

    switch (cmd) {
    case ID_CONTEXT_JOIN:
        JoinServer();
        break;
    case ID_CONTEXT_COPY_IP:
        CopyServerAddress();
        break;
    case ID_SERVER_REFRESH:
    {
        ServerInfo* selectedServer = g_launcher->GetSelectedServer();
        if (selectedServer) {
            g_launcher->RefreshSingleServer(selectedServer->ip, selectedServer->port);
            g_launcher->UpdateStatusBar("Refreshing server: " + selectedServer->name);
        }
        else {
            g_launcher->UpdateStatusBar("No server selected for refresh");
        }
    }
    break;
    case ID_CONTEXT_ADD_FAV:
    case ID_CONTEXT_REMOVE_FAV:
        ToggleFavorite();
        break;
    case ID_CONTEXT_SERVER_INFO:
        ShowServerDetails();
        break;
    }

    DestroyMenu(hMenu);
}

void DayZLauncher::CopyServerAddress() {
    ServerInfo* selectedServer = GetSelectedServer();
    if (!selectedServer) return;

    std::string address = selectedServer->ip + ":" + std::to_string(selectedServer->port);

    if (OpenClipboard(hWnd)) {
        EmptyClipboard();

        HGLOBAL hClipboard = GlobalAlloc(GMEM_DDESHARE, address.length() + 1);
        if (hClipboard) {
            strcpy_s(static_cast<char*>(GlobalLock(hClipboard)), address.length() + 1, address.c_str());
            GlobalUnlock(hClipboard);
            SetClipboardData(CF_TEXT, hClipboard);
        }

        CloseClipboard();
        UpdateStatusBar("Copied " + address + " to clipboard");
    }
}

void DayZLauncher::ShowServerDetails() {
    ServerInfo* selectedServer = GetSelectedServer();
    if (!selectedServer) return;

    std::wstring details = L"Server Information\n\n";
    details += L"Name: " + StringToWString(selectedServer->name) + L"\n";
    details += L"IP:Port: " + StringToWString(selectedServer->ip) + L":" + std::to_wstring(selectedServer->port) + L"\n";
    details += L"Map: " + StringToWString(selectedServer->map) + L"\n";
    details += L"Players: " + std::to_wstring(selectedServer->players) + L"/" + std::to_wstring(selectedServer->maxPlayers) + L"\n";


    details += L"Ping: ";
    if (selectedServer->ping == -1) {
        details += L"N/A";
    }
    else {
        details += std::to_wstring(selectedServer->ping) + L"ms";
    }
    details += L"\n";

    details += L"Version: " + StringToWString(selectedServer->version) + L"\n";


    details += L"VAC: ";
    details += selectedServer->hasVAC ? L"Yes" : L"No";
    details += L"\n";

    details += L"Password: ";
    details += selectedServer->isPassworded ? L"Yes" : L"No";
    details += L"\n";

    details += L"Type: ";
    details += selectedServer->isOfficial ? L"Official" : L"Community";
    details += L"\n";

    if (!selectedServer->mods.empty()) {
        details += L"\nMods (" + std::to_wstring(selectedServer->mods.size()) + L"):\n";
        for (const auto& mod : selectedServer->mods) {
            details += L"  • " + StringToWString(mod) + L"\n";
        }
    }
    else {
        details += L"\nMods: None (Vanilla)\n";
    }

    MessageBox(hWnd, details.c_str(), L"Server Details", MB_OK | MB_ICONINFORMATION);
}





void DayZLauncher::ToggleFavorite() {
    int selected = ListView_GetNextItem(hServerList, -1, LVNI_SELECTED);
    if (selected == -1) {
        UpdateStatusBar("No server selected!");
        return;
    }

    std::vector<ServerInfo> filteredServers = GetFilteredServers();
    if (selected >= 0 && selected < static_cast<int>(filteredServers.size())) {
        const ServerInfo& selectedServer = filteredServers[selected];

        if (selectedServer.isFavorite) {
        
            favoritesManager->RemoveFavorite(selectedServer.ip, selectedServer.port);
            SetWindowText(hFavoriteBtn, L"Add Favorite");
            UpdateStatusBar("Removed " + selectedServer.name + " from favorites");
        }
        else {
       
            favoritesManager->AddFavorite(selectedServer);
            SetWindowText(hFavoriteBtn, L"Remove Favorite");
            UpdateStatusBar("Added " + selectedServer.name + " to favorites");
        }


        PopulateServerList();
    }
}


void DayZLauncher::FilterServers() {
    PopulateServerList();
}


ServerInfo* DayZLauncher::GetSelectedServer() {
    int selected = ListView_GetNextItem(hServerList, -1, LVNI_SELECTED);
    if (selected == -1) return nullptr;

    std::lock_guard<std::mutex> lock(serverMutex);


    if (currentTab == TAB_FAVORITES) {
        const auto& favorites = favoritesManager->GetFavorites();
        if (selected >= 0 && selected < static_cast<int>(favorites.size())) {
            const auto& favorite = favorites[selected];

        
            for (auto& server : servers) {
                if (server.ip == favorite.ip && server.port == favorite.port) {
                    return &server;
                }
            }

   
            static ServerInfo tempServer;
            tempServer.name = favorite.name;
            tempServer.ip = favorite.ip;
            tempServer.port = favorite.port;
            tempServer.isFavorite = true;
            tempServer.ping = -1;
            tempServer.players = 0;
            tempServer.maxPlayers = 0;
            tempServer.map = "Unknown";
            tempServer.version = "Unknown";
            return &tempServer;
        }
        return nullptr;
    }


    std::vector<ServerInfo> filteredServers = GetFilteredServers();
    if (selected >= 0 && selected < static_cast<int>(filteredServers.size())) {
        for (auto& server : servers) {
            if (server.ip == filteredServers[selected].ip &&
                server.port == filteredServers[selected].port) {
                return &server;
            }
        }
    }

    return nullptr;
}





void DayZLauncher::OnFilterChanged() {
    OutputDebugStringA("=== OnFilterChanged START ===\n");


    searchFilter.clear();
    mapFilter.clear();
    versionFilter.clear();
    filterFlags = 0;

    if (hFilterSearch && IsWindow(hFilterSearch)) {
        wchar_t searchBuffer[256] = { 0 };
        int textLength = GetWindowText(hFilterSearch, searchBuffer, 255);
        if (textLength > 0) {
            searchFilter = WStringToString(std::wstring(searchBuffer));
            std::transform(searchFilter.begin(), searchFilter.end(), searchFilter.begin(), ::tolower);
        }
    }

    if (hFilterMap && IsWindow(hFilterMap)) {
        int selectedMapIndex = SendMessage(hFilterMap, CB_GETCURSEL, 0, 0);
        if (selectedMapIndex > 0) { 
            wchar_t mapName[256] = { 0 };
            int result = SendMessage(hFilterMap, CB_GETLBTEXT, selectedMapIndex, (LPARAM)mapName);
            if (result != CB_ERR && wcslen(mapName) > 0) {
                mapFilter = WStringToString(std::wstring(mapName));
     
                std::transform(mapFilter.begin(), mapFilter.end(), mapFilter.begin(), ::tolower);
                OutputDebugStringA(("Map filter set to: '" + mapFilter + "'\n").c_str());
            }
        }
    }

  
    if (hFilterVersion && IsWindow(hFilterVersion)) {
        int selectedVersionIndex = SendMessage(hFilterVersion, CB_GETCURSEL, 0, 0);
        if (selectedVersionIndex > 0) {
            wchar_t versionName[256] = { 0 };
            int result = SendMessage(hFilterVersion, CB_GETLBTEXT, selectedVersionIndex, (LPARAM)versionName);
            if (result != CB_ERR && wcslen(versionName) > 0) {
                versionFilter = WStringToString(std::wstring(versionName));
            }
        }
    }

    if (hFilterFavorites && IsWindow(hFilterFavorites) && SendMessage(hFilterFavorites, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        filterFlags |= FILTER_SHOW_FAVORITES;
    }
    if (hFilterPassword && IsWindow(hFilterPassword) && SendMessage(hFilterPassword, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        filterFlags |= FILTER_HIDE_PASSWORD;
    }
    if (hFilterOnline && IsWindow(hFilterOnline) && SendMessage(hFilterOnline, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        filterFlags |= FILTER_ONLINE_ONLY;
    }
    if (hFilterNotFull && IsWindow(hFilterNotFull) && SendMessage(hFilterNotFull, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        filterFlags |= FILTER_NOT_FULL;
    }
    if (hFilterModded && IsWindow(hFilterModded) && SendMessage(hFilterModded, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        filterFlags |= FILTER_SHOW_MODDED;
    }
    if (hFilterPlayed && IsWindow(hFilterPlayed) && SendMessage(hFilterPlayed, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        filterFlags |= FILTER_SHOW_PLAYED;
    }
    if (hFilterFirstPerson && IsWindow(hFilterFirstPerson) && SendMessage(hFilterFirstPerson, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        filterFlags |= FILTER_FIRST_PERSON;
    }
    if (hFilterThirdPerson && IsWindow(hFilterThirdPerson) && SendMessage(hFilterThirdPerson, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        filterFlags |= FILTER_THIRD_PERSON;
    }

    ApplyFiltersAndUpdate();

    OutputDebugStringA("=== OnFilterChanged END ===\n");
}


bool DayZLauncher::PassesAllFilters(const ServerInfo& server) const {

    if (!searchFilter.empty()) {
        std::string serverName = server.name;
        std::string serverMap = server.map;
        std::string serverIP = server.ip;

        std::transform(serverName.begin(), serverName.end(), serverName.begin(), ::tolower);
        std::transform(serverMap.begin(), serverMap.end(), serverMap.begin(), ::tolower);
        std::transform(serverIP.begin(), serverIP.end(), serverIP.begin(), ::tolower);

        bool found = (serverName.find(searchFilter) != std::string::npos) ||
            (serverMap.find(searchFilter) != std::string::npos) ||
            (serverIP.find(searchFilter) != std::string::npos);

        if (!found) {
            return false;
        }
    }

   
    if (!mapFilter.empty()) {
        std::string serverMap = server.map;
        std::transform(serverMap.begin(), serverMap.end(), serverMap.begin(), ::tolower);

        if (serverMap.find("chernarus") != std::string::npos) {
            OutputDebugStringA(("Testing map filter: server='" + serverMap + "' vs filter='" + mapFilter + "'\n").c_str());
        }

        if (serverMap != mapFilter) {
            return false;
        }
    }

    if (!versionFilter.empty()) {
        if (server.version.find(versionFilter) == std::string::npos) {
            return false;
        }
    }


    if (filterFlags & FILTER_SHOW_FAVORITES && !server.isFavorite) return false;
    if (filterFlags & FILTER_HIDE_PASSWORD && server.isPassworded) return false;
    if (filterFlags & FILTER_ONLINE_ONLY && server.ping == -1) return false;
    if (filterFlags & FILTER_NOT_FULL && server.players >= server.maxPlayers && server.maxPlayers > 0) return false;
    if (filterFlags & FILTER_SHOW_MODDED && server.mods.empty()) return false;

    if (filterFlags & FILTER_SHOW_PLAYED) {
        bool hasPlayed = false;
        if (favoritesManager) {
            const auto& history = favoritesManager->GetRecentServers();
            for (const auto& hist : history) {
                if (hist.ip == server.ip && hist.port == server.port && hist.connectionCount > 0) {
                    hasPlayed = true;
                    break;
                }
            }
        }
        if (!hasPlayed) return false;
    }

    return true;
}


void DayZLauncher::ApplyFiltersAndUpdate() {
    if (!hServerList || !IsWindow(hServerList)) return;

    OutputDebugStringA("=== ApplyFiltersAndUpdate START ===\n");
    ListView_DeleteAllItems(hServerList);

    std::lock_guard<std::mutex> lock(serverMutex);

  
    if (currentTab == TAB_FAVORITES) {
        if (!favoritesManager) {
            UpdateStatusBar("No favorites manager");
            return;
        }

        const auto& favorites = favoritesManager->GetFavorites();
        int favCount = 0;

        for (const auto& favorite : favorites) {
   
            LVITEM lvi = {};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = favCount;
            lvi.iSubItem = 0;

            std::wstring serverName = StringToWString(favorite.name);
            lvi.pszText = const_cast<LPWSTR>(serverName.c_str());
            int itemIndex = ListView_InsertItem(hServerList, &lvi);

            if (itemIndex != -1) {
                SetListViewItemText(itemIndex, 1, L"Unknown");
                SetListViewItemText(itemIndex, 2, L"0/0");
                SetListViewItemText(itemIndex, 3, L"N/A");
                std::string address = favorite.ip + ":" + std::to_string(favorite.port);
                SetListViewItemText(itemIndex, 4, StringToWString(address));
                SetListViewItemText(itemIndex, 5, L"Unknown");
                favCount++;
            }
        }

        UpdateStatusBar("Showing " + std::to_string(favCount) + " favorites");
        return;
    }


    if (servers.empty()) {
        UpdateStatusBar("No servers loaded - click 'Refresh Servers' to load servers");
        return;
    }

    int filteredCount = 0;
    int totalCount = static_cast<int>(servers.size());
    int testedCount = 0;

    OutputDebugStringA(("Starting to filter " + std::to_string(totalCount) + " servers\n").c_str());

    for (const auto& server : servers) {
        testedCount++;


        if (currentTab == TAB_OFFICIAL && !server.isOfficial) continue;
        if (currentTab == TAB_COMMUNITY && server.isOfficial) continue;
        if (currentTab == TAB_LAN && !IsLANAddress(server.ip)) continue;

   
        if (PassesAllFilters(server)) {
            LVITEM lvi = {};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = filteredCount;
            lvi.iSubItem = 0;

            std::wstring serverName = StringToWString(server.name);
            lvi.pszText = const_cast<LPWSTR>(serverName.c_str());
            int listItemIndex = ListView_InsertItem(hServerList, &lvi);

            if (listItemIndex != -1) {
                SetListViewItemText(listItemIndex, 1, StringToWString(server.map));
                std::wstring playerText = std::to_wstring(server.players) + L"/" + std::to_wstring(server.maxPlayers);
                SetListViewItemText(listItemIndex, 2, playerText);
                std::wstring pingText = (server.ping == -1) ? L"N/A" : std::to_wstring(server.ping) + L"ms";
                SetListViewItemText(listItemIndex, 3, pingText);
                std::string address = server.ip + ":" + std::to_string(server.port);
                SetListViewItemText(listItemIndex, 4, StringToWString(address));
                SetListViewItemText(listItemIndex, 5, StringToWString(server.version));
                filteredCount++;
            }

 
            if (filteredCount <= 5) {
                OutputDebugStringA(("PASSED FILTER: " + server.name + " (map: " + server.map + ")\n").c_str());
            }
        }
        else if (testedCount <= 5) {
            OutputDebugStringA(("FAILED FILTER: " + server.name + " (map: " + server.map + ")\n").c_str());
        }
    }

    std::string statusMsg = "Showing " + std::to_string(filteredCount) + " of " + std::to_string(totalCount) + " servers";
    UpdateStatusBar(statusMsg);

    OutputDebugStringA(("FINAL RESULT: " + std::to_string(filteredCount) + " servers pass filters\n").c_str());
    OutputDebugStringA("=== ApplyFiltersAndUpdate END ===\n");
}





void DayZLauncher::RefreshSingleServer(const std::string& ip, int port) {
    if (IsServerRefreshNeeded(ip, port)) {
        std::thread([this, ip, port]() {
            A2SInfoResponse response;
            if (queryManager->QueryServerInfo(ip, port, response)) {
                std::lock_guard<std::mutex> lock(serverMutex);
                for (auto& server : servers) {
                    if (server.ip == ip && server.port == port) {
                        server.name = response.name;
                        server.players = response.players;
                        server.maxPlayers = response.maxPlayers;
                        server.ping = queryManager->PingServer(ip, port);
                        server.lastUpdated = time(nullptr);
                        PostMessage(hWnd, WM_REFRESH_PARTIAL, 0, 0);
                        break;
                    }
                }
            }
            MarkServerRefreshed(ip, port);
            }).detach();
    }
}


void DayZLauncher::ResetFilters() {
    OutputDebugStringA("=== RESET FILTERS START ===\n");


    searchFilter.clear();
    mapFilter.clear();
    versionFilter.clear();
    filterFlags = 0;

   
    if (hFilterSearch && IsWindow(hFilterSearch)) {
        SetWindowText(hFilterSearch, L"");
        OutputDebugStringA("Cleared search box\n");
    }

  
    if (hFilterMap && IsWindow(hFilterMap)) {
        SendMessage(hFilterMap, CB_SETCURSEL, 0, 0);
        OutputDebugStringA("Reset map dropdown\n");
    }
    if (hFilterVersion && IsWindow(hFilterVersion)) {
        SendMessage(hFilterVersion, CB_SETCURSEL, 0, 0);
        OutputDebugStringA("Reset version dropdown\n");
    }


    if (hFilterFavorites && IsWindow(hFilterFavorites)) {
        SendMessage(hFilterFavorites, BM_SETCHECK, BST_UNCHECKED, 0);
        OutputDebugStringA("Unchecked favorites\n");
    }
    if (hFilterPlayed && IsWindow(hFilterPlayed)) {
        SendMessage(hFilterPlayed, BM_SETCHECK, BST_UNCHECKED, 0);
        OutputDebugStringA("Unchecked played\n");
    }
    if (hFilterPassword && IsWindow(hFilterPassword)) {
        SendMessage(hFilterPassword, BM_SETCHECK, BST_UNCHECKED, 0);
        OutputDebugStringA("Unchecked password\n");
    }
    if (hFilterModded && IsWindow(hFilterModded)) {
        SendMessage(hFilterModded, BM_SETCHECK, BST_UNCHECKED, 0);
        OutputDebugStringA("Unchecked modded\n");
    }
    if (hFilterOnline && IsWindow(hFilterOnline)) {
        SendMessage(hFilterOnline, BM_SETCHECK, BST_UNCHECKED, 0);
        OutputDebugStringA("Unchecked online\n");
    }
    if (hFilterFirstPerson && IsWindow(hFilterFirstPerson)) {
        SendMessage(hFilterFirstPerson, BM_SETCHECK, BST_UNCHECKED, 0);
        OutputDebugStringA("Unchecked first person\n");
    }
    if (hFilterThirdPerson && IsWindow(hFilterThirdPerson)) {
        SendMessage(hFilterThirdPerson, BM_SETCHECK, BST_UNCHECKED, 0);
        OutputDebugStringA("Unchecked third person\n");
    }
    if (hFilterNotFull && IsWindow(hFilterNotFull)) {
        SendMessage(hFilterNotFull, BM_SETCHECK, BST_UNCHECKED, 0);
        OutputDebugStringA("Unchecked not full\n");
    }

    ApplyFiltersAndUpdate();

    UpdateStatusBar("All filters reset - showing all servers");
    OutputDebugStringA("=== RESET FILTERS END ===\n");
}



void DayZLauncher::SetListViewItemText(int item, int subItem, const std::wstring& text) {
    ListView_SetItemText(hServerList, item, subItem, const_cast<LPWSTR>(text.c_str()));
}

std::wstring DayZLauncher::StringToWString(const std::string& str) const {
    if (str.empty()) return std::wstring();

    int size = MultiByteToWideChar(CP_UTF8, 0, &str[0], static_cast<int>(str.size()), nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], static_cast<int>(str.size()), &result[0], size);
    return result;
}

std::string DayZLauncher::WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();

    int size = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), &result[0], size, nullptr, nullptr);
    return result;
}

std::wstring DayZLauncher::GetDayZInstallPathW() {
    std::vector<std::wstring> steamPaths = {
        L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\DayZ\\DayZ_x64.exe",
        L"C:\\Program Files\\Steam\\steamapps\\common\\DayZ\\DayZ_x64.exe"
    };

    for (const auto& path : steamPaths) {
        if (GetFileAttributes(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return path;
        }
    }

    return L"";
}

void DayZLauncher::DebugServerDetection() {
    std::lock_guard<std::mutex> lock(serverMutex);

    int officialCount = 0;
    int communityCount = 0;

    for (const auto& server : servers) {
        if (server.isOfficial) {
            officialCount++;
        }
        else {
            communityCount++;
        }
    }

    std::string debugMsg = "Server counts - Official: " + std::to_string(officialCount) +
        ", Community: " + std::to_string(communityCount) +
        ", Total: " + std::to_string(servers.size());

    OutputDebugStringA((debugMsg + "\n").c_str());
    UpdateStatusBar(debugMsg);
}

void DayZLauncher::UpdateStatusBar(const std::string& text) {
    std::wstring wtext = StringToWString(text);
    SendMessage(hStatusBar, SB_SETTEXT, 0, (LPARAM)wtext.c_str());
}

void DayZLauncher::UpdateProgressBar(int progress) {
    SendMessage(hProgressBar, PBM_SETPOS, progress, 0);
}

void DayZLauncher::UpdateServerCount() {
    int listCount = ListView_GetItemCount(hServerList);
    std::wstring statusText = L"Showing " + std::to_wstring(listCount) + L" servers";
    SendMessage(hStatusBar, SB_SETTEXT, 1, (LPARAM)statusText.c_str());
}

void DayZLauncher::LoadConfiguration() {
    configManager->LoadConfig();
}

void DayZLauncher::SaveConfiguration() {
    configManager->SaveConfig();
}

void DayZLauncher::SortByColumn(int column) {
    currentSortColumn = column;
    sortAscending = !sortAscending;
    PopulateServerList();
}



void DayZLauncher::TestLoadSettings() {
    UpdateStatusBar("=== TESTING SETTINGS LOAD ===");
    configManager->DebugConfigState();
    configManager->LoadConfig();
    UpdateStatusBar("=== AFTER RELOAD ===");
    configManager->DebugConfigState();
    std::string profileName = configManager->GetString("profileName", "NOT_FOUND");
    std::string profilePath = configManager->GetString("profilePath", "NOT_FOUND");
    std::string dayzPath = configManager->GetString("dayzPath", "NOT_FOUND");

    UpdateStatusBar("Config has: Profile='" + profileName + "', DayZ='" + dayzPath + "', ProfilePath='" + profilePath + "'");
    LoadSettingsValues();
}

void DayZLauncher::ApplyUserTheme() {
    auto GetHexColor = [](HWND hwnd) -> COLORREF {
        char buffer[16] = {};
        GetWindowTextA(hwnd, buffer, sizeof(buffer));
        std::string hex = buffer;

    
        if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);

        if (hex.length() == 6) {
            try {
                int r = std::stoi(hex.substr(0, 2), nullptr, 16);
                int g = std::stoi(hex.substr(2, 2), nullptr, 16);
                int b = std::stoi(hex.substr(4, 2), nullptr, 16);
                return RGB(r, g, b);
            }
            catch (...) {
                return RGB(255, 255, 255);
            }
        }
        return RGB(255, 255, 255);
        };


    COLORREF bg = GetHexColor(hColorBgEdit);
    COLORREF text = GetHexColor(hColorTextEdit);
    COLORREF btn = GetHexColor(hColorButtonEdit);
    ThemeManager::SetColor("windowBg", bg);
    ThemeManager::SetColor("text", text);
    ThemeManager::SetColor("buttonBg", btn);
    ApplyModernStyling();
    UpdateStatusBar("Theme applied!");
}

LRESULT CALLBACK DayZLauncher::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (!g_launcher) return DefWindowProc(hwnd, uMsg, wParam, lParam);

    switch (uMsg) {
    case WM_CREATE:
        g_launcher->hWnd = hwnd;
        g_launcher->CreateControls();
        g_launcher->favoritesManager->LoadFavorites();
        PostMessage(hwnd, WM_USER + 100, 0, 0);
        PostMessage(hwnd, WM_USER + 101, 0, 0);
        g_launcher->DebugFavorites();
        return 0;

    case WM_USER + 100:
        g_launcher->LoadSettingsValues();
        g_launcher->UpdateStatusBar("Settings loaded after initialization");
        return 0;

    case WM_USER + 101:
        OutputDebugStringA("=== FORCE RELOAD TRIGGERED ===\n");
        g_launcher->configManager->LoadConfig();
        g_launcher->LoadSettingsValues();
        g_launcher->UpdateStatusBar("FORCE RELOAD: Settings loaded!");
        return 0;

    case WM_USER + 200:
        if (lParam) {
            std::string statusText = reinterpret_cast<const char*>(lParam);
            g_launcher->UpdateStatusBar(statusText);
        }
        return 0;

    case WM_SIZE:
        g_launcher->ResizeControls();
        return 0;

    case WM_REFRESH_PARTIAL:
        g_launcher->PopulateServerList();
        return 0;

    case WM_COMMAND: {
        int controlId = LOWORD(wParam);
        int notificationCode = HIWORD(wParam);

        switch (controlId) {
        case IDC_REFRESH_BTN:
            g_launcher->RefreshServers();
            break;

        case IDC_FILTER_REFRESH:
            OutputDebugStringA("MANUAL FILTER REFRESH CLICKED\n");
            g_launcher->OnFilterChanged();
            break;

        case IDC_FILTER_RESET:
            OutputDebugStringA("FILTER RESET CLICKED\n");
            g_launcher->ResetFilters();
            break;


        case ID_SERVER_REFRESH:
        {
            ServerInfo* selectedServer = g_launcher->GetSelectedServer();
            if (selectedServer) {
                g_launcher->RefreshSingleServer(selectedServer->ip, selectedServer->port);
            }
        }
        break;
        case IDC_FILTER_SEARCH:
            if (notificationCode == EN_CHANGE) {
                OutputDebugStringA("Search text changed - applying filters\n");
                g_launcher->OnFilterChanged();
            }
            break;

        case IDC_FILTER_MAP:
            if (notificationCode == CBN_SELCHANGE) {
                OutputDebugStringA("Map selection changed - applying filters\n");
                g_launcher->OnFilterChanged();
            }
            break;

        case IDC_FILTER_VERSION:
            if (notificationCode == CBN_SELCHANGE) {
                OutputDebugStringA("Version selection changed - applying filters\n");
                g_launcher->OnFilterChanged();
            }
            break;

case IDC_FILTER_FAVORITES:
case IDC_FILTER_PLAYED:
case IDC_FILTER_PASSWORD:
case IDC_FILTER_MODDED:
case IDC_FILTER_ONLINE:
case IDC_FILTER_FIRSTPERSON:
case IDC_FILTER_THIRDPERSON:
case IDC_FILTER_NOTFULL:
    OutputDebugStringA("Checkbox filter changed - applying filters\n");
    g_launcher->OnFilterChanged();
    break;



        case IDC_JOIN_BTN:
            g_launcher->JoinServer();
            break;

        case IDC_FAVORITE_BTN:
            g_launcher->ToggleFavorite();
            break;


        case IDC_SAVE_SETTINGS_BTN:
            g_launcher->SaveSettingsValues();
            MessageBox(hwnd, L"Settings saved! Check config.ini", L"Save Test", MB_OK);
            break;

        case IDC_RELOAD_SETTINGS_BTN:
            g_launcher->TestLoadSettings();
            MessageBox(hwnd, L"Settings reloaded! Check status bar for details.", L"Reload Test", MB_OK);
            break;

        case IDC_BROWSE_PROFILE_BTN:
            g_launcher->OnBrowseProfile();
            break;

        case IDC_BROWSE_DAYZ_BTN:
            g_launcher->OnBrowseDayZ();
            break;

        case IDC_TEST_DAYZ_BTN:
            g_launcher->TestDayZPathControl();
            break;

        case IDC_FORCE_SAVE_BTN:
            g_launcher->ForceSaveDayZPath();
            break;

        case IDC_EMERGENCY_SAVE_BTN:
            g_launcher->EmergencyManualSave();
            break;


        case IDC_PROFILE_NAME_EDIT:
        case IDC_PROFILE_PATH_EDIT:
        case IDC_DAYZ_PATH_EDIT:
        case IDC_QUERY_DELAY_EDIT:
            if (notificationCode == EN_CHANGE) {
                g_launcher->SaveSettingsValues();
            }
            break;



        case IDC_SHOW_FAVORITES:
        case IDC_SHOW_PASSWORDED:
        case IDC_SHOW_ONLINE:
            g_launcher->FilterServers();
            break;

        case ID_TRAY_RESTORE:
            g_launcher->RestoreFromTray();
            break;

        case ID_TRAY_REFRESH:
            g_launcher->RefreshServers();
            break;

        case ID_TRAY_EXIT:
            PostMessage(hwnd, WM_CLOSE, 0, 0);
            break;

        case ID_CONTEXT_JOIN:
            g_launcher->JoinServer();
            break;

        case ID_CONTEXT_COPY_IP:
            g_launcher->CopyServerAddress();
            break;

        case ID_CONTEXT_ADD_FAV:
        case ID_CONTEXT_REMOVE_FAV:
            g_launcher->ToggleFavorite();
            break;

        case ID_CONTEXT_SERVER_INFO:
            g_launcher->ShowServerDetails();
            break;

        default:
            OutputDebugStringA(("Unhandled WM_COMMAND: ID=" + std::to_string(controlId) + "\n").c_str());
            break;
        }
        return 0;
    }

    case WM_TIMER:
        if (wParam == 1) {
            KillTimer(hwnd, 1);
            ShowWindow(hwnd, SW_MINIMIZE);
            OutputDebugStringA("Application minimized after successful DayZ launch\n");
        }
        return 0;

    case WM_NOTIFY: {
        LPNMHDR nmhdr = (LPNMHDR)lParam;
        if (!nmhdr) return 0;

        if (nmhdr->idFrom == IDC_TAB_CONTROL && nmhdr->code == TCN_SELCHANGE) {
            int selectedTab = TabCtrl_GetCurSel(g_launcher->hTab);
            std::string debugMsg = "Tab changed to: " + std::to_string(selectedTab);
            g_launcher->UpdateStatusBar(debugMsg);
            g_launcher->OnTabChanged();
        }
        else if (nmhdr->idFrom == IDC_SERVER_LIST && nmhdr->code == LVN_ITEMCHANGED) {
            g_launcher->OnServerSelected();
        }
        else if (nmhdr->idFrom == IDC_SERVER_LIST && nmhdr->code == NM_DBLCLK) {
            g_launcher->JoinServer();
        }
        else if (nmhdr->idFrom == IDC_SERVER_LIST && nmhdr->code == LVN_COLUMNCLICK) {
            LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
            if (pnmv && pnmv->iSubItem >= 0 && pnmv->iSubItem <= 5) {
                g_launcher->SortServersByColumn(pnmv->iSubItem);
            }
        }
        else if (nmhdr->idFrom == IDC_SERVER_LIST && nmhdr->code == NM_RCLICK) {
            POINT pt;
            GetCursorPos(&pt);
            g_launcher->ShowServerContextMenu(pt);
        }
        return 0;
    }

    case WM_UPDATE_PROGRESS:
        g_launcher->UpdateProgressBar(static_cast<int>(wParam));
        return 0;

    case WM_REFRESH_COMPLETE:
        g_launcher->isRefreshing = false;
        g_launcher->PopulateServerList();
        g_launcher->UpdateStatusBar("Ready");
        g_launcher->UpdateProgressBar(0);
        return 0;

    case WM_TRAYICON:
        g_launcher->HandleTrayMessage(wParam, lParam);
        return 0;

    case WM_CONTEXTMENU: {
        HWND hControl = (HWND)wParam;
        if (hControl == g_launcher->hServerList) {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            g_launcher->ShowServerContextMenu(pt);
        }
        return 0;
    }

    case WM_SYSCOMMAND:
        if (wParam == SC_MINIMIZE) {
            g_launcher->MinimizeToTray();
            return 0;
        }
        break;

    case WM_CLOSE:
        g_launcher->SaveConfiguration();
        g_launcher->favoritesManager->SaveFavorites();


        if (g_launcher->trayManager) {
            g_launcher->trayManager->HideTrayIcon();
        }

        if (g_launcher->isRefreshing.load()) {
            int result = MessageBox(hwnd,
                L"Server refresh is in progress. Are you sure you want to exit?",
                L"Confirm Exit",
                MB_YESNO | MB_ICONQUESTION);
            if (result == IDNO) {
                return 0; 
            }
        }

        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        g_launcher->SaveConfiguration();
        g_launcher->favoritesManager->SaveFavorites();
        PostQuitMessage(0);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        HWND hStatic = (HWND)lParam;
        SetTextColor(hdcStatic, RGB(0, 0, 0)); 
        SetBkColor(hdcStatic, RGB(240, 240, 240)); 

        static HBRUSH hbrStatic = CreateSolidBrush(RGB(240, 240, 240));
        return (LRESULT)hbrStatic;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = (HDC)wParam;
        HWND hEdit = (HWND)lParam;

        SetTextColor(hdcEdit, RGB(0, 0, 0));
        SetBkColor(hdcEdit, RGB(255, 255, 255));

        static HBRUSH hbrEdit = CreateSolidBrush(RGB(255, 255, 255));
        return (LRESULT)hbrEdit;
    }

    case WM_CTLCOLORLISTBOX: {
        HDC hdcListBox = (HDC)wParam;
        HWND hListBox = (HWND)lParam;

        SetTextColor(hdcListBox, RGB(0, 0, 0)); 
        SetBkColor(hdcListBox, RGB(255, 255, 255)); 

        static HBRUSH hbrListBox = CreateSolidBrush(RGB(255, 255, 255));
        return (LRESULT)hbrListBox;
    }

    case WM_CTLCOLORBTN: {
        HDC hdcButton = (HDC)wParam;
        HWND hButton = (HWND)lParam;

        SetTextColor(hdcButton, RGB(0, 0, 0)); 
        SetBkColor(hdcButton, RGB(240, 240, 240));

        static HBRUSH hbrButton = CreateSolidBrush(RGB(240, 240, 240));
        return (LRESULT)hbrButton;
    }

    case WM_GETMINMAXINFO: {
        LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
        lpMMI->ptMinTrackSize.x = MIN_WINDOW_WIDTH;
        lpMMI->ptMinTrackSize.y = MIN_WINDOW_HEIGHT;
        return 0;
    }

    case WM_MOUSEWHEEL: {
        HWND hFocus = GetFocus();
        if (hFocus == g_launcher->hServerList) {
            return SendMessage(g_launcher->hServerList, uMsg, wParam, lParam);
        }
        break;
    }

    case WM_KEYDOWN:
        switch (wParam) {
        case VK_F5:
            g_launcher->RefreshServers();
            return 0;

        case VK_ESCAPE:
            g_launcher->MinimizeToTray();
            return 0;

        case VK_RETURN:
            if (GetFocus() == g_launcher->hServerList) {
                g_launcher->JoinServer();
                return 0;
            }
            break;

        case VK_DELETE:
            if (GetFocus() == g_launcher->hServerList) {
                ServerInfo* selectedServer = g_launcher->GetSelectedServer();
                if (selectedServer && selectedServer->isFavorite) {
                    g_launcher->ToggleFavorite();
                }
                return 0;
            }
            break;
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) {
            if (g_launcher->trayManager) {
                g_launcher->trayManager->HideTrayIcon();
            }
        }
        break;

    case WM_POWERBROADCAST:
        if (wParam == PBT_APMSUSPEND) {
            g_launcher->SaveConfiguration();
            g_launcher->favoritesManager->SaveFavorites();
        }
        else if (wParam == PBT_APMRESUMEAUTOMATIC) {
            if (g_launcher->configManager->GetBool("autoRefresh", false)) {
                PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_REFRESH_BTN, BN_CLICKED), 0);
            }
        }
        break;

    case WM_ENDSESSION:
        if (wParam) {
            g_launcher->SaveConfiguration();
            g_launcher->favoritesManager->SaveFavorites();
        }
        break;

    case WM_DROPFILES: {

        HDROP hDrop = (HDROP)wParam;
        wchar_t fileName[MAX_PATH];

        if (DragQueryFile(hDrop, 0, fileName, MAX_PATH)) {
            std::string fileStr = g_launcher->WStringToString(fileName);

            if (fileStr.find(".json") != std::string::npos ||
                fileStr.find(".txt") != std::string::npos) {

                int result = MessageBox(hwnd,
                    L"Import favorites from this file?",
                    L"Import Favorites",
                    MB_YESNO | MB_ICONQUESTION);

                if (result == IDYES) {
                    if (g_launcher->favoritesManager->ImportFavorites(fileStr)) {
                        g_launcher->UpdateStatusBar("Favorites imported successfully");
                        g_launcher->PopulateServerList();
                    }
                    else {
                        MessageBox(hwnd, L"Failed to import favorites", L"Import Error", MB_OK | MB_ICONERROR);
                    }
                }
            }
        }

        DragFinish(hDrop);
        return 0;
    }

    case WM_COPYDATA: {
        PCOPYDATASTRUCT pcds = (PCOPYDATASTRUCT)lParam;
        if (pcds && pcds->dwData == 1) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            FlashWindow(hwnd, TRUE);
        }
        return TRUE;
    }

    case WM_DISPLAYCHANGE:
        g_launcher->ResizeControls();
        return 0;

    case WM_SETTINGCHANGE:
        if (lParam && wcscmp((LPCWSTR)lParam, L"ImmersiveColorSet") == 0) {
            g_launcher->ApplyModernStyling();
        }
        return 0;


    case WM_USER + 300: 
        if (wParam >= 0 && wParam <= 100) {
            g_launcher->UpdateProgressBar(static_cast<int>(wParam));
        }
        return 0;

    case WM_USER + 301: 
        if (lParam) {
            g_launcher->PopulateServerList();
        }
        return 0;

    case WM_USER + 302: 
        if (lParam) {
            std::string errorMsg = reinterpret_cast<const char*>(lParam);
            g_launcher->UpdateStatusBar("Error: " + errorMsg);
        }
        return 0;

    case WM_USER + 303: 
        if (wParam == 1) {
            g_launcher->UpdateStatusBar("Network connection established");
        }
        else {
            g_launcher->UpdateStatusBar("Network connection lost");
        }
        return 0;

    default:
        break;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}