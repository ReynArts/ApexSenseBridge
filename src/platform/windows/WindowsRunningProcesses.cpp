#include "platform/RunningProcesses.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

namespace asb::platform {

std::vector<std::wstring> runningExecutableNames() {
    std::vector<std::wstring> names;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return names;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more;
         more = Process32NextW(snapshot, &entry)) {
        names.emplace_back(entry.szExeFile);
    }
    CloseHandle(snapshot);
    return names;
}

} // namespace asb::platform
