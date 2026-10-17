#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace Windows::Devices::Radios;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::Bluetooth;

namespace {
constexpr int ID_BT = 1001, ID_SCAN = 1002, ID_SEND = 1003, ID_RECEIVE = 1004;
constexpr int ID_STATUS = 1005, ID_LIST = 1006, ID_LOG = 1007;
HWND g_main{}, g_status{}, g_list{}, g_log{}, g_bt{}, g_scan{};
std::vector<Radio> g_radios;
std::mutex g_mutex;
std::atomic_bool g_busy{false};

void SetStatus(const std::wstring& text) {
    if (g_status) SetWindowTextW(g_status, text.c_str());
}
void Log(const std::wstring& text) {
    if (!g_log) return;
    auto n = GetWindowTextLengthW(g_log);
    SendMessageW(g_log, EM_SETSEL, n, n);
    SendMessageW(g_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>((text + L"\r\n").c_str()));
}
void EnableControls(bool enabled) {
    EnableWindow(g_bt, enabled); EnableWindow(g_scan, enabled);
}

// All WinRT work is performed on a background thread so the UI remains responsive.
void RefreshRadios() {
    if (g_busy.exchange(true)) return;
    EnableControls(false);
    SetStatus(L"Checking Bluetooth radio…");
    std::thread([] {
        try {
            init_apartment(apartment_type::multi_threaded);
            auto radios = Radio::GetRadiosAsync().get();
            std::vector<Radio> bt;
            for (auto const& r : radios) if (r.Kind() == RadioKind::Bluetooth) bt.push_back(r);
            {
                std::scoped_lock lock(g_mutex); g_radios = bt;
            }
            std::wstring state = bt.empty() ? L"Bluetooth adapter not found" :
                (bt.front().State() == RadioState::On ? L"Bluetooth is on" :
                 bt.front().State() == RadioState::Off ? L"Bluetooth is off" : L"Bluetooth is unavailable/disabled");
            PostMessageW(g_main, WM_APP + 1, 0, reinterpret_cast<LPARAM>(new std::wstring(state)));
        } catch (const hresult_error& e) {
            auto msg = std::wstring(L"Could not read Bluetooth status (HRESULT ") + std::to_wstring(static_cast<unsigned long>(e.code())) + L")";
            PostMessageW(g_main, WM_APP + 1, 0, reinterpret_cast<LPARAM>(new std::wstring(msg)));
        }
        g_busy = false;
    }).detach();
}

void ToggleBluetooth() {
    if (g_busy.exchange(true)) return;
    EnableControls(false); SetStatus(L"Requesting Bluetooth permission…");
    std::thread([] {
        try {
            init_apartment(apartment_type::multi_threaded);
            auto access = Radio::RequestAccessAsync().get();
            if (access != RadioAccessStatus::Allowed) {
                PostMessageW(g_main, WM_APP + 1, 0, reinterpret_cast<LPARAM>(new std::wstring(L"Windows denied radio control. Use Windows Quick Settings to enable Bluetooth.")));
            } else {
                auto radios = Radio::GetRadiosAsync().get();
                bool found = false; std::wstring outcome;
                for (auto const& r : radios) if (r.Kind() == RadioKind::Bluetooth) {
                    found = true;
                    auto target = r.State() == RadioState::On ? RadioState::Off : RadioState::On;
                    auto result = r.SetStateAsync(target).get();
                    outcome = result == RadioAccessStatus::Allowed ?
                        (target == RadioState::On ? L"Bluetooth enable requested; verifying…" : L"Bluetooth disable requested; verifying…") :
                        L"Windows/hardware denied the Bluetooth state change.";
                    break;
                }
                if (!found) outcome = L"No Bluetooth radio was found.";
                PostMessageW(g_main, WM_APP + 1, 0, reinterpret_cast<LPARAM>(new std::wstring(outcome)));
            }
        } catch (const hresult_error& e) {
            auto msg = std::wstring(L"Bluetooth control failed (HRESULT ") + std::to_wstring(static_cast<unsigned long>(e.code())) + L").";
            PostMessageW(g_main, WM_APP + 1, 0, reinterpret_cast<LPARAM>(new std::wstring(msg)));
        }
        g_busy = false;
    }).detach();
}

void ScanDevices() {
    if (g_busy.exchange(true)) return;
    EnableControls(false); SetStatus(L"Scanning paired and discoverable Bluetooth devices…");
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    std::thread([] {
        try {
            init_apartment(apartment_type::multi_threaded);
            // Bluetooth device selector enumerates known/paired Bluetooth endpoints. Active discovery
            // visibility is governed by Windows' device-enumeration and privacy policies.
            auto selector = BluetoothDevice::GetDeviceSelector();
            auto devices = DeviceInformation::FindAllAsync(selector).get();
            std::vector<std::wstring> names;
            for (auto const& d : devices) {
                std::wstring name = d.Name().c_str();
                if (name.empty()) name = L"Unnamed Bluetooth device";
                names.push_back(name);
            }
            auto payload = new std::vector<std::wstring>(std::move(names));
            PostMessageW(g_main, WM_APP + 2, 0, reinterpret_cast<LPARAM>(payload));
        } catch (const hresult_error& e) {
            auto msg = std::wstring(L"Device scan failed (HRESULT ") + std::to_wstring(static_cast<unsigned long>(e.code())) + L").";
            PostMessageW(g_main, WM_APP + 1, 0, reinterpret_cast<LPARAM>(new std::wstring(msg)));
        }
        g_busy = false;
    }).detach();
}

void LaunchClassicSend() {
    // Windows' inbox OBEX sender is the interoperable fallback for ordinary phones and PCs.
    // It is a send wizard, not an unattended receiver.
    auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"fsquirt.exe", L"-send", nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        SetStatus(L"Could not start Windows Bluetooth File Transfer (fsquirt.exe).");
        Log(L"Send wizard launch failed. Check that Windows Bluetooth components are installed.");
    } else {
        SetStatus(L"Windows Bluetooth send wizard opened. Select the file and destination device there.");
        Log(L"Opened Windows' built-in OBEX send wizard. A custom direct-send path is planned for the next implementation stage.");
    }
}
void ExplainReceive() {
    MessageBoxW(g_main,
        L"Windows' built-in OBEX receive flow is managed by the system. A desktop app cannot safely take over every incoming Bluetooth file transfer without implementing and advertising an OBEX Object Push service, plus handling pairing, save-location, and consent rules.\n\nFor now, this button opens the standard Windows receiver. The next stage is a persistent, opt-in receiver service with an allow-list and a configurable save folder.",
        L"Auto Receive — setup required", MB_ICONINFORMATION | MB_OK);
    ShellExecuteW(nullptr, L"open", L"fsquirt.exe", nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
            HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); return c;
        };
        make(L"STATIC", L"BluetoothBridge", SS_LEFT, 24, 18, 400, 36, 0);
        make(L"STATIC", L"Simple, reliable Bluetooth file transfer", SS_LEFT, 24, 49, 500, 24, 0);
        g_status = make(L"STATIC", L"Starting…", SS_LEFT, 24, 83, 700, 25, ID_STATUS);
        g_bt = make(L"BUTTON", L"Toggle Bluetooth", BS_PUSHBUTTON, 24, 120, 160, 38, ID_BT);
        g_scan = make(L"BUTTON", L"Scan devices", BS_PUSHBUTTON, 196, 120, 145, 38, ID_SCAN);
        make(L"BUTTON", L"Send files", BS_PUSHBUTTON, 353, 120, 130, 38, ID_SEND);
        make(L"BUTTON", L"Receive files", BS_PUSHBUTTON, 495, 120, 145, 38, ID_RECEIVE);
        make(L"STATIC", L"Devices", SS_LEFT, 24, 177, 300, 22, 0);
        g_list = make(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER, 24, 202, 616, 145, ID_LIST);
        make(L"STATIC", L"Activity", SS_LEFT, 24, 362, 300, 22, 0);
        g_log = make(L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_BORDER, 24, 387, 616, 92, ID_LOG);
        Log(L"Ready. Bluetooth status and device enumeration use Windows APIs.");
        RefreshRadios(); return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BT: ToggleBluetooth(); return 0;
        case ID_SCAN: ScanDevices(); return 0;
        case ID_SEND: LaunchClassicSend(); return 0;
        case ID_RECEIVE: ExplainReceive(); return 0;
        } break;
    case WM_APP + 1: {
        std::unique_ptr<std::wstring> s(reinterpret_cast<std::wstring*>(lp));
        SetStatus(*s); Log(*s); EnableControls(true); return 0;
    }
    case WM_APP + 2: {
        std::unique_ptr<std::vector<std::wstring>> names(reinterpret_cast<std::vector<std::wstring>*>(lp));
        for (auto const& n : *names) SendMessageW(g_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n.c_str()));
        auto summary = names->empty() ? L"No Bluetooth devices returned. Make the target discoverable and scan again." : L"Device enumeration complete.";
        SetStatus(summary); Log(summary); EnableControls(true); return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try { init_apartment(apartment_type::single_threaded); }
    catch (...) { }
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES }; InitCommonControlsEx(&icc);
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = instance; wc.lpszClassName = L"BluetoothBridgeWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&wc)) return 1;
    g_main = CreateWindowExW(0, wc.lpszClassName, L"BluetoothBridge", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 690, 545, nullptr, nullptr, instance, nullptr);
    if (!g_main) return 2;
    ShowWindow(g_main, show); UpdateWindow(g_main);
    MSG m{}; while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    return static_cast<int>(m.wParam);
}
