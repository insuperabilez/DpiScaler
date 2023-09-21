#include <Windows.h>
#include <conio.h>
#include <cwchar>
#include <fstream>
#include <string>
#include <iostream>
#include <algorithm>
#include <ShellScalingAPI.h>
#include <vector>
#include <Pathcch.h>
#include "DpiHelper.h"
#include <memory>
#include <cassert>
#include <WinUser.h>
#include <wingdi.h>
#pragma comment(lib, "User32.lib")

#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "Pathcch.lib")
#define IDM_EXIT 1
#define IDM_SHOW 2

#define WMU_TRAY_ICON_MESSAGE (WM_USER+1)
#define CLASS_NAME "UnvisibleHandlerWin"

DPI_AWARENESS_CONTEXT oldContext = GetThreadDpiAwarenessContext();
DPI_AWARENESS oldDpiAwareness = GetAwarenessFromDpiAwarenessContext(oldContext);


using namespace std;
vector<int> hotkeys;
DEVICE_SCALE_FACTOR value;
NOTIFYICONDATA nid = {};
DpiHelper::DPIScalingInfo dpiInfo = {};
BOOL IsMyProgramRegisteredForStartup(PCWSTR pszAppName)
{
    HKEY hKey = NULL;
    LONG lResult = 0;
    BOOL fSuccess = TRUE;
    DWORD dwRegType = REG_SZ;
    wchar_t szPathToExe[MAX_PATH] = {};
    DWORD dwSize = sizeof(szPathToExe);

    lResult = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey);

    fSuccess = (lResult == 0);

    if (fSuccess)
    {
        lResult = RegGetValueW(hKey, NULL, pszAppName, RRF_RT_REG_SZ, &dwRegType, szPathToExe, &dwSize);
        fSuccess = (lResult == 0);
    }

    if (fSuccess)
    {
        fSuccess = (wcslen(szPathToExe) > 0) ? TRUE : FALSE;
    }

    if (hKey != NULL)
    {
        RegCloseKey(hKey);
        hKey = NULL;
    }

    return fSuccess;
}
BOOL RegisterMyProgramForStartup(PCWSTR pszAppName, PCWSTR pathToExe, PCWSTR args)
{
    HKEY hKey = NULL;
    LONG lResult = 0;
    BOOL fSuccess = TRUE;
    DWORD dwSize;

    const size_t count = MAX_PATH * 2;
    wchar_t szValue[count] = {};


    wcscpy_s(szValue, count, L"\"");
    wcscat_s(szValue, count, pathToExe);
    wcscat_s(szValue, count, L"\" ");

    if (args != NULL)
    {
        // caller should make sure "args" is quoted if any single argument has a space
        // e.g. (L"-name \"Mark Voidale\"");
        wcscat_s(szValue, count, args);
    }

    lResult = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, 0, (KEY_WRITE | KEY_READ), NULL, &hKey, NULL);

    fSuccess = (lResult == 0);

    if (fSuccess)
    {
        dwSize = (wcslen(szValue) + 1) * 2;
        lResult = RegSetValueExW(hKey, pszAppName, 0, REG_SZ, (BYTE*)szValue, dwSize);
        fSuccess = (lResult == 0);
    }

    if (hKey != NULL)
    {
        RegCloseKey(hKey);
        hKey = NULL;
    }

    return fSuccess;
}
void RegisterProgram()
{
    wchar_t szPathToExe[MAX_PATH];

    GetModuleFileNameW(NULL, szPathToExe, MAX_PATH);
    RegisterMyProgramForStartup(L"DpiScaler", szPathToExe, L"-foobar");
}
std::vector<DISPLAYCONFIG_PATH_INFO> GetPathInfos() {
    for (LONG result = ERROR_INSUFFICIENT_BUFFER;
        result == ERROR_INSUFFICIENT_BUFFER;) {
        uint32_t path_elements, mode_elements;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_elements,
            &mode_elements) != ERROR_SUCCESS) {
            return {};
        }
        std::vector<DISPLAYCONFIG_PATH_INFO> path_infos(path_elements);
        std::vector<DISPLAYCONFIG_MODE_INFO> mode_infos(mode_elements);
        result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_elements,
            path_infos.data(), &mode_elements,
            mode_infos.data(), nullptr);
        if (result == ERROR_SUCCESS) {
            path_infos.resize(path_elements);
            return path_infos;
        }
    }
    return {};
}
bool DpiHelper::GetPathsAndModes(std::vector<DISPLAYCONFIG_PATH_INFO>& pathsV, std::vector<DISPLAYCONFIG_MODE_INFO>& modesV, int flags)
{
    UINT32 numPaths = 0, numModes = 0;
    auto status = GetDisplayConfigBufferSizes(flags, &numPaths, &numModes);
    if (ERROR_SUCCESS != status)
    {
        return false;
    }

    std::unique_ptr<DISPLAYCONFIG_PATH_INFO[]> paths(new DISPLAYCONFIG_PATH_INFO[numPaths]);
    std::unique_ptr<DISPLAYCONFIG_MODE_INFO[]> modes(new DISPLAYCONFIG_MODE_INFO[numModes]);
    status = QueryDisplayConfig(flags, &numPaths, paths.get(), &numModes, modes.get(), nullptr);
    if (ERROR_SUCCESS != status)
    {
        return false;
    }

    for (unsigned int i = 0; i < numPaths; i++)
    {
        pathsV.push_back(paths[i]);
    }

    for (unsigned int i = 0; i < numModes; i++)
    {
        modesV.push_back(modes[i]);
    }

    return true;
}


DpiHelper::DpiHelper()
{
}


DpiHelper::~DpiHelper()
{
}


DpiHelper::DPIScalingInfo DpiHelper::GetDPIScalingInfo(LUID adapterID, UINT32 sourceID)
{
    DPIScalingInfo dpiInfo = {};

    DpiHelper::DISPLAYCONFIG_SOURCE_DPI_SCALE_GET requestPacket = {};
    requestPacket.header.type = (DISPLAYCONFIG_DEVICE_INFO_TYPE)DpiHelper::DISPLAYCONFIG_DEVICE_INFO_TYPE_CUSTOM::DISPLAYCONFIG_DEVICE_INFO_GET_DPI_SCALE;
    requestPacket.header.size = sizeof(requestPacket);
    assert(0x20 == sizeof(requestPacket));//if this fails => OS has changed somthing, and our reverse enginnering knowledge about the API is outdated
    requestPacket.header.adapterId = adapterID;
    requestPacket.header.id = sourceID;

    auto res = ::DisplayConfigGetDeviceInfo(&requestPacket.header);

    if (requestPacket.curScaleRel < requestPacket.minScaleRel)
    {
        requestPacket.curScaleRel = requestPacket.minScaleRel;
    }
    else if (requestPacket.curScaleRel > requestPacket.maxScaleRel)
    {
        requestPacket.curScaleRel = requestPacket.maxScaleRel;
    }

    std::int32_t minAbs = abs((int)requestPacket.minScaleRel);
    if (DpiHelper::CountOf(DpiVals) >= (size_t)(minAbs + requestPacket.maxScaleRel + 1))
    {//all ok
        dpiInfo.current = DpiVals[minAbs + requestPacket.curScaleRel];
        dpiInfo.recommended = DpiVals[minAbs];
        dpiInfo.maximum = DpiVals[minAbs + requestPacket.maxScaleRel];
        dpiInfo.bInitDone = true;
    }
    else
    {
        //Error! Probably DpiVals array is outdated
        return dpiInfo;
    }


    return dpiInfo;
}


bool DpiHelper::SetDPIScaling(LUID adapterID, UINT32 sourceID, UINT32 dpiPercentToSet)
{

    DPIScalingInfo dPIScalingInfo = GetDPIScalingInfo(adapterID, sourceID);

    if (dpiPercentToSet == dPIScalingInfo.current)
    {
        return true;
    }

    if (dpiPercentToSet < dPIScalingInfo.mininum)
    {
        dpiPercentToSet = dPIScalingInfo.mininum;
    }
    else if (dpiPercentToSet > dPIScalingInfo.maximum)
    {
        dpiPercentToSet = dPIScalingInfo.maximum;
    }

    int idx1 = -1, idx2 = -1;

    int i = 0;
    for (const auto& val : DpiVals)
    {
        if (val == dpiPercentToSet)
        {
            idx1 = i;
        }

        if (val == dPIScalingInfo.recommended)
        {
            idx2 = i;
        }
        i++;
    }

    if ((idx1 == -1) || (idx2 == -1))
    {
        //Error cannot find dpi value

        return false;
    }

    int dpiRelativeVal = idx1 - idx2;

    DpiHelper::DISPLAYCONFIG_SOURCE_DPI_SCALE_SET setPacket = {};
    setPacket.header.adapterId = adapterID;
    setPacket.header.id = sourceID;
    setPacket.header.size = sizeof(setPacket);
    assert(0x18 == sizeof(setPacket));//if this fails => OS has changed somthing, and our reverse enginnering knowledge about the API is outdated
    setPacket.header.type = (DISPLAYCONFIG_DEVICE_INFO_TYPE)DpiHelper::DISPLAYCONFIG_DEVICE_INFO_TYPE_CUSTOM::DISPLAYCONFIG_DEVICE_INFO_SET_DPI_SCALE;
    setPacket.scaleRel = (UINT32)dpiRelativeVal;

    auto res = ::DisplayConfigSetDeviceInfo(&setPacket.header);

    if (ERROR_SUCCESS == res)
    {
        return true;
    }
    else
    {
        return false;
    }
    return true;
}


std::wstring DpiHelper::GetDisplayUniqueName(LUID adapterID, UINT32 targetID)
{
    _DISPLAYCONFIG_GET_MONITOR_INTERNAL_INFO mi = {};
    mi.header.adapterId = adapterID;
    mi.header.id = targetID;
    mi.header.size = sizeof(mi);
    mi.header.type = (DISPLAYCONFIG_DEVICE_INFO_TYPE)DpiHelper::DISPLAYCONFIG_DEVICE_INFO_TYPE_CUSTOM::DISPLAYCONFIG_DEVICE_INFO_GET_MONITOR_UNIQUE_NAME;

    LONG res = ::DisplayConfigGetDeviceInfo(&mi.header);
    if (ERROR_SUCCESS == res)
    {
        return std::wstring(mi.monitorUniqueName);
    }

    return std::wstring();
}




std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(' ');
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, last - first + 1);
}

// Функция для обработки нажатия комбинации клавиш
void HandleHotkey(LUID adapterID, UINT32 sourceID)
{

    if (GetAsyncKeyState(VK_CONTROL) & 0x8000 && GetAsyncKeyState(hotkeys[0]) & 0x8000)
    {
       

        DpiHelper::SetDPIScaling(adapterID, sourceID, dpiInfo.current + 25);
        //SetDpiScaling(dpiInfo.current + 25);




    }
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000 && GetAsyncKeyState(hotkeys[2]) & 0x8000)
    {
       

        //SetDpiScaling(dpiInfo.current - 25);
        DpiHelper::SetDPIScaling(adapterID, sourceID, dpiInfo.current-25);

    }



}

// Функция для обработки сообщений трея
LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{

    DISPLAYCONFIG_DEVICE_INFO_HEADER deviceInfoHeader = {  };
    deviceInfoHeader.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;

    DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceDeviceName = {  };
    sourceDeviceName.header = deviceInfoHeader;

    std::vector<DISPLAYCONFIG_PATH_INFO> path_infos = GetPathInfos();
    DISPLAYCONFIG_SOURCE_DEVICE_NAME device_name = {};
    for (const auto& info : path_infos) {

        device_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        device_name.header.size = sizeof(device_name);
        device_name.header.adapterId = info.sourceInfo.adapterId;
        device_name.header.id = info.sourceInfo.id;

    }

    dpiInfo = DpiHelper::GetDPIScalingInfo(device_name.header.adapterId, device_name.header.id);
    wchar_t buf1[128];
    wstring stemp;
    std::wstring curdpi;
    std::wstring test1;
    LPCWSTR sw;
    static HMENU hMenu;


    switch (msg)
    {

    case WM_CREATE:
        stemp = L"Current DPI: ";
        curdpi = to_wstring(dpiInfo.current);
        test1 = stemp + curdpi;
        sw = test1.c_str();
        hMenu = CreatePopupMenu();
        AppendMenu(hMenu, MF_DISABLED, IDM_SHOW, sw);
        AppendMenu(hMenu, MF_STRING, IDM_EXIT, L"Выход");
        SetMenuDefaultItem(hMenu, IDM_EXIT, FALSE);
    case WM_USER + 1:

        if (lParam == WM_RBUTTONDOWN) {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            break;
        }
        break;
    case WM_HOTKEY:

        HandleHotkey(device_name.header.adapterId, device_name.header.id);

        Sleep(100);
        stemp = L"Current DPI: ";
        curdpi = to_wstring(dpiInfo.current);
        test1 = stemp + curdpi;
        sw = test1.c_str();

        ModifyMenu(hMenu, 0,
            MF_BYPOSITION, 1, sw);

        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDM_EXIT)
        {
            // Обработка команды "Выход"

            DestroyWindow(hwnd);
        }

        break;
    case WM_CLOSE:
        // Shell_NotifyIcon(NIM_DELETE, &nid);
        ShowWindow(hwnd, SW_HIDE);
        break;
    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        DestroyWindow(hwnd);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR szCmdLine, int iCmdShow)
{
    setlocale(LC_ALL, "Russian");


    TCHAR exePath[MAX_PATH];
    HMODULE hModule = GetModuleHandleW(NULL);
    GetModuleFileNameW(hModule, exePath, MAX_PATH);
    PathCchRemoveFileSpec(exePath, MAX_PATH);
    wstring path(exePath);
    wstring iniName = L"\\config.ini";
    wstring iniPath = path + iniName;
    std::wifstream iniFile(iniPath);

    RegisterProgram();
    IsMyProgramRegisteredForStartup(L"DpiScaler");
    wstring check = L"[HOTKEYS]";
    if (iniFile.is_open()) {


        std::wstring line1;
        std::vector<std::string> tokens;
        while (std::getline(iniFile, line1)) {
            if (line1 != check) {
                string line(line1.begin(), line1.end());
                
                //size_t pos = 0;
                //std::string delimiter = "CTRL+";
                //std::size_t pos1 = line.find('=');
                size_t pos = line.find_last_of("+") + 1;
                line = line.substr(pos);
                /* while ((pos = line.find(delimiter)) != std::string::npos) {
                     std::string token = line.substr(0, pos);
                     tokens.push_back(token);
                     line.erase(0, pos + delimiter.length());

                 } */

                tokens.push_back(line);
            }
            // Вывод разделенных токенов
            for (const string& token : tokens) {
                string a = token;

                hotkeys.push_back(stoi(trim(token)));

               

            }

        }
    }
    else {
        std::wofstream file(iniPath);
        file << "[HOTKEYS]\n";
        file << "INCREASE = LCTRL+65\n";
        file << "DECREASE = LCTRL+66\n";

        cout << "Конфиг создан" << endl;
        file.close();

        hotkeys.push_back(65);
        hotkeys.push_back(0);
        hotkeys.push_back(66);


    }
    int t = 0;
    for (auto& i : hotkeys)
    {
        t = i;
    }
    auto test1 = hotkeys[0];
    auto test2 = hotkeys[1];

    setlocale(LC_ALL, "Russian");
    // Определение класса окна трея
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"DpiScaler";
    // Регистрация класса окна трея
    RegisterClass(&wc);

    // Создание окна трея
    HWND hwnd = CreateWindowEx(
        0,
        L"DpiScaler",
        L"DpiScaler",
        0,
        0,
        0,
        0,
        0,
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );




    HICON hIcon = (HICON)LoadImage(NULL, L"icon.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE);
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 0;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 1;
    nid.hIcon = hIcon;
    wcscpy_s(nid.szTip, L"DpiScaler");
    Shell_NotifyIcon(NIM_ADD, &nid);

    bool t1 = RegisterHotKey(hwnd, 1, MOD_CONTROL, hotkeys[0]);
    bool t2 = RegisterHotKey(hwnd, 2, MOD_CONTROL, hotkeys[2]);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {


        TranslateMessage(&msg);
        DispatchMessage(&msg);


    }

    // Удаление регистрации горячих клавиш
    UnregisterHotKey(hwnd, 1);
    UnregisterHotKey(hwnd, 2);


    return 0;
}