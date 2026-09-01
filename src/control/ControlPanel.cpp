#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"ApexSenseBridge.ControlPanel";
constexpr int kDiagnosticButton = 101;
constexpr int kRestoreButton = 102;
constexpr int kLogsButton = 103;
constexpr int kFullUninstallButton = 104;
constexpr int kOutputEdit = 201;

std::filesystem::path moduleDirectory() {
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    return length == 0 ? std::filesystem::path{} :
                         std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return L"La sortie du moteur ne peut pas être décodée.";
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), required);
    return result;
}

std::wstring windowsError(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = size && buffer ? std::wstring(buffer, size) :
                                           L"Erreur Windows " + std::to_wstring(code);
    if (buffer) LocalFree(buffer);
    return message;
}

bool runEngineHidden(const std::wstring& arguments,
                     std::wstring& output,
                     DWORD& exitCode) {
    const auto engine = moduleDirectory() / L"ApexSenseBridge.exe";
    if (!std::filesystem::exists(engine)) {
        output = L"ApexSenseBridge.exe est introuvable dans le dossier d'installation.";
        return false;
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
        output = L"Création du canal de diagnostic impossible : " + windowsError(GetLastError());
        return false;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    std::wstring command = L"\"" + engine.wstring() + L"\" " + arguments;
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const auto workingDirectory = moduleDirectory().wstring();
    const BOOL started = CreateProcessW(
        engine.c_str(), commandBuffer.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, workingDirectory.c_str(), &startup, &process);
    CloseHandle(writePipe);
    if (!started) {
        output = L"Démarrage du moteur impossible : " + windowsError(GetLastError());
        CloseHandle(readPipe);
        return false;
    }

    std::string raw;
    std::array<char, 4096> buffer{};
    DWORD read = 0;
    while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                    nullptr) && read != 0) {
        if (raw.size() < 256 * 1024) raw.append(buffer.data(), read);
    }
    WaitForSingleObject(process.hProcess, 65000);
    if (!GetExitCodeProcess(process.hProcess, &exitCode)) exitCode = 0xFFFFFFFFUL;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(readPipe);
    output = utf8ToWide(raw);
    if (output.empty()) output = L"Le moteur n'a produit aucune sortie.";
    return exitCode == 0;
}

std::wstring readMachineString(const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ApexSenseBridge", 0,
                      KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return {};
    }
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t)) {
        RegCloseKey(key);
        return {};
    }
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, name, nullptr, &type,
                        reinterpret_cast<BYTE*>(value.data()), &bytes) != ERROR_SUCCESS) {
        value.clear();
    }
    RegCloseKey(key);
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

void setOutput(HWND window, const std::wstring& value) {
    SetWindowTextW(GetDlgItem(window, kOutputEdit), value.c_str());
}

void useDefaultGuiFont(HWND control) {
    SendMessageW(control, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

void createControls(HWND window) {
    auto addButton = [window](int id, const wchar_t* label, int x, int width) {
        HWND button = CreateWindowExW(0, L"BUTTON", label,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            x, 18, width, 34, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        useDefaultGuiFont(button);
    };
    addButton(kDiagnosticButton, L"Tester l'APEX", 18, 130);
    addButton(kRestoreButton, L"Restaurer HidHide / WGI", 158, 172);
    addButton(kLogsButton, L"Ouvrir les journaux", 340, 145);
    addButton(kFullUninstallButton, L"Désinstallation complète…", 495, 180);

    HWND edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_READONLY,
        18, 68, 657, 330, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOutputEdit)),
        GetModuleHandleW(nullptr), nullptr);
    useDefaultGuiFont(edit);

    std::wstring status = L"ApexSenseBridge " + readMachineString(L"Version") +
                          L"\r\nMoteur : " + (moduleDirectory() / L"ApexSenseBridge.exe").wstring() +
                          L"\r\nusbip-win2 : " + readMachineString(L"UsbipVersion") +
                          L"\r\nHidHide : " + readMachineString(L"HidHideVersion") +
                          L"\r\n\r\nAucun service ni agent permanent n'est actif hors session de jeu.";
    setOutput(window, status);
}

void resizeControls(HWND window) {
    RECT area{};
    GetClientRect(window, &area);
    const int width = (area.right - area.left) - 36;
    const int height = (area.bottom - area.top) - 86;
    MoveWindow(GetDlgItem(window, kOutputEdit), 18, 68,
               width > 100 ? width : 100, height > 80 ? height : 80, TRUE);
}

void openLogs(HWND window) {
    std::array<wchar_t, 32768> localAppData{};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData.data(), static_cast<DWORD>(localAppData.size()));
    if (length == 0 || length >= localAppData.size()) {
        MessageBoxW(window, L"LOCALAPPDATA est indisponible.", L"ApexSenseBridge", MB_ICONERROR);
        return;
    }
    const auto logs = std::filesystem::path(localAppData.data()) / L"ApexSenseBridge" / L"Logs";
    std::error_code error;
    std::filesystem::create_directories(logs, error);
    if (error || reinterpret_cast<INT_PTR>(ShellExecuteW(
            window, L"open", logs.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
        MessageBoxW(window, L"Le dossier des journaux ne peut pas être ouvert.",
                    L"ApexSenseBridge", MB_ICONERROR);
    }
}

void startFullUninstall(HWND window) {
    const int confirmation = MessageBoxW(
        window,
        L"Cette option désinstalle ApexSenseBridge puis demande aussi la suppression des "
        L"pilotes usbip-win2 et HidHide, même s'ils étaient déjà présents. Continuer ?",
        L"Désinstallation complète", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
    if (confirmation != IDYES) return;
    const auto uninstaller = readMachineString(L"UninstallExecutable");
    if (uninstaller.empty() || reinterpret_cast<INT_PTR>(ShellExecuteW(
            window, L"runas", uninstaller.c_str(), L"/REMOVEDEPENDENCIES",
            moduleDirectory().c_str(), SW_SHOWNORMAL)) <= 32) {
        MessageBoxW(window, L"Le programme de désinstallation est introuvable ou n'a pas été autorisé.",
                    L"ApexSenseBridge", MB_ICONERROR);
    }
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message,
                                 WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        createControls(window);
        return 0;
    case WM_SIZE:
        resizeControls(window);
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == kDiagnosticButton || id == kRestoreButton) {
            setOutput(window, id == kDiagnosticButton
                ? L"Lecture de l'état complet de l'APEX pendant deux secondes…"
                : L"Restauration de la visibilité du contrôleur et des réglages WGI…");
            UpdateWindow(window);
            std::wstring output;
            DWORD exitCode = 0;
            const bool success = runEngineHidden(
                id == kDiagnosticButton ? L"input-status --seconds 2 --json"
                                        : L"restore-controller-visibility",
                output, exitCode);
            if (!success) {
                output += L"\r\n\r\nCode de sortie : " + std::to_wstring(exitCode);
            }
            setOutput(window, output);
            return 0;
        }
        if (id == kLogsButton) {
            openLogs(window);
            return 0;
        }
        if (id == kFullUninstallButton) {
            startFullUninstall(window);
            return 0;
        }
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDPIAware();
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass)) return 1;

    HWND window = CreateWindowExW(
        0, kWindowClass, L"ApexSenseBridge — Contrôle et diagnostic",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 715, 470,
        nullptr, nullptr, instance, nullptr);
    if (!window) return 2;
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

#endif
