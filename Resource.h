#pragma once


#define IDI_MAIN_ICON           101
#define IDI_OFFICIAL_ICON       102
#define IDI_COMMUNITY_ICON      103
#define IDI_FAVORITE_ICON       104
#define IDI_LAN_ICON           105
#define IDI_REFRESH_ICON       106
#define IDI_JOIN_ICON          107
#define IDI_DETAILS_ICON       108
#define IDI_COPY_ICON          109


#define IDB_TOOLBAR            201
#define IDB_SERVER_TYPES       202
#define IDB_STATUS_ICONS       203


#define IDS_APP_TITLE          301
#define IDS_WINDOW_CLASS       302
#define IDS_ERROR_INIT         303
#define IDS_ERROR_WINSOCK      304
#define IDS_ERROR_DAYZ_PATH    305
#define IDS_STATUS_READY       306
#define IDS_STATUS_REFRESHING  307
#define IDS_STATUS_CONNECTING  308
#define IDS_COLUMN_NAME        309
#define IDS_COLUMN_MAP         310
#define IDS_COLUMN_PLAYERS     311
#define IDS_COLUMN_PING        312
#define IDS_COLUMN_IP          313
#define IDS_COLUMN_VERSION     314
#define IDS_COLUMN_TIME        315
#define IDS_COLUMN_UPTIME      316
#define IDB_LOGO_PNG    333 

#define IDD_ABOUT              401
#define IDD_SERVER_DETAILS     402
#define IDD_ADD_SERVER         403
#define IDD_SETTINGS           404
#define IDD_FILTER_SETTINGS    405

#define IDC_SERVER_NAME        501
#define IDC_SERVER_MAP         502
#define IDC_SERVER_IP          503
#define IDC_SERVER_PLAYERS     504
#define IDC_SERVER_PING        505
#define IDC_SERVER_VERSION     506
#define IDC_SERVER_RULES       507
#define IDC_PLAYER_LIST        508

#define IDC_ADD_IP             601
#define IDC_ADD_PORT           602
#define IDC_ADD_COMMENT        603
#define IDC_ADD_QUERY          604


#define IDC_SETTINGS_AUTOREFRESH    701
#define IDC_SETTINGS_INTERVAL       702
#define IDC_SETTINGS_MINIMIZE_TRAY  703
#define IDC_SETTINGS_DAYZ_PATH      704
#define IDC_SETTINGS_BROWSE         705
#define IDC_SETTINGS_RESET          706


#define IDM_MAIN_MENU          801


#define IDC_TAB_CONTROL         1001
#define IDC_SERVER_LIST         1002
#define IDC_REFRESH_BTN         1003
#define IDC_JOIN_BTN            1004
#define IDC_FAVORITE_BTN        1005
#define IDC_FILTER_EDIT         1006
#define IDC_STATUS_BAR          1007
#define IDC_PROGRESS_BAR        1008
#define IDC_DETAILS_BTN         1009
#define IDC_COPY_BTN            1010
#define IDC_AUTO_REFRESH        1011
#define IDC_MIN_PLAYERS         1012
#define IDC_MAX_PING            1013
#define IDC_SHOW_PASSWORDED     1014
#define IDC_TOOLBAR             1015


#define IDC_MAP_FILTER          1016
#define IDC_SHOW_FAVORITES      1017
#define IDC_SHOW_PLAYED         1018
#define IDC_SHOW_ONLINE         1019
#define IDC_MIN_PLAYERS_EDIT    1020
#define IDC_MAX_PING_EDIT       1021


#define IDC_PROFILE_NAME_EDIT   1022
#define IDC_PROFILE_PATH_EDIT   1023
#define IDC_DAYZ_PATH_EDIT      1024
#define IDC_BROWSE_PROFILE_BTN  1025
#define IDC_BROWSE_DAYZ_BTN     1026
#define IDC_QUERY_DELAY_EDIT    1027
#define IDC_SAVE_SETTINGS_BTN   1045
#define IDC_RELOAD_SETTINGS_BTN 1046
#define IDC_TEST_DAYZ_BTN       1047
#define IDC_FORCE_SAVE_BTN      1048
#define IDC_EMERGENCY_SAVE_BTN  1049
#define IDC_APPLY_THEME_BTN     1050

#define IDC_FILTER_PANEL        3100
#define IDC_FILTER_SEARCH       3101
#define IDC_FILTER_MAP          3102
#define IDC_FILTER_VERSION      3103
#define IDC_FILTER_FAVORITES    3104
#define IDC_FILTER_PLAYED       3105
#define IDC_FILTER_PASSWORD     3106
#define IDC_FILTER_MODDED       3107
#define IDC_FILTER_ONLINE       3108
#define IDC_FILTER_FIRSTPERSON  3109
#define IDC_FILTER_THIRDPERSON  3110
#define IDC_FILTER_NOTFULL      3111
#define IDC_FILTER_RESET        3112
#define IDC_FILTER_REFRESH      3113
#define IDC_LOGO_IMAGE    3200



#define ID_CONTEXT_JOIN         4001
#define ID_CONTEXT_COPY_IP      4002
#define ID_CONTEXT_ADD_FAV      4003
#define ID_CONTEXT_REMOVE_FAV   4004
#define ID_CONTEXT_SERVER_INFO  4005

#define ID_SERVER_REFRESH       4010
#define ID_SERVER_COPY_IP       4011
#define ID_SERVER_COPY_NAME     4012
#define ID_SERVER_VIEW_MODS     4013


#define ID_TRAY_RESTORE         4020
#define ID_TRAY_REFRESH         4021
#define ID_TRAY_EXIT            4022

#define TAB_OFFICIAL            0
#define TAB_COMMUNITY           1
#define TAB_FAVORITES           2
#define TAB_LAN                 3
#define TAB_SETTINGS            4

#define COL_FAVORITE           0
#define COL_NAME               1
#define COL_TIME               2
#define COL_PLAYED             3
#define COL_MAP                4
#define COL_PLAYERS            5
#define COL_PING               6
#define COL_ACTIONS            7


#define SORT_NAME               0
#define SORT_MAP                1
#define SORT_PLAYERS            2
#define SORT_PING               3
#define SORT_IP                 4
#define SORT_VERSION            5


#define FILTER_PANEL_WIDTH     280
#define BUTTON_COLUMN_WIDTH    80
#define MIN_WINDOW_WIDTH       1000
#define MIN_WINDOW_HEIGHT      600

#define FILTER_SHOW_FAVORITES  0x01
#define FILTER_SHOW_PLAYED     0x02
#define FILTER_HIDE_PASSWORD   0x04
#define FILTER_SHOW_MODDED     0x08
#define FILTER_ONLINE_ONLY     0x10
#define FILTER_FIRST_PERSON    0x20
#define FILTER_THIRD_PERSON    0x40
#define FILTER_NOT_FULL        0x80


#define WM_UPDATE_PROGRESS      (WM_USER + 1)
#define WM_REFRESH_COMPLETE     (WM_USER + 2)
#define WM_TRAYICON             (WM_USER + 3)
#define WM_REFRESH_PARTIAL      (WM_USER + 4)

