#include "native.hpp"

static wchar_t upper_ascii(wchar_t ch) { return ch >= L'a' && ch <= L'z' ? ch - L'a' + L'A' : ch; }

bool valid_device_id(const wchar_t* id) {
    if (!id) return false;
    const unsigned size = length(id);
    if (size < 23 || size >= MAX_DEVICE_ID_LEN) return false;
    const wchar_t prefix[] = L"USB\\VID_046D&PID_";
    for (unsigned i = 0; prefix[i]; ++i) if (upper_ascii(id[i]) != prefix[i]) return false;
    for (unsigned i = 17; i < 21; ++i) {
        const wchar_t ch = upper_ascii(id[i]);
        if (!((ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'F'))) return false;
    }
    unsigned slashes = 0;
    for (unsigned i = 0; i < size; ++i) {
        const wchar_t ch = upper_ascii(id[i]);
        if (ch == L'\\') ++slashes;
        if (!((ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') || ch == L'\\' || ch == L'&' || ch == L'_' || ch == L'-')) return false;
        if (i + 4 <= size && ch == L'&' && upper_ascii(id[i + 1]) == L'M' && upper_ascii(id[i + 2]) == L'I' && id[i + 3] == L'_') return false;
    }
    // PID may have additional hardware suffixes; the instance part must exist.
    if (slashes != 2 || id[size - 1] == L'\\') return false;
    for (unsigned i = 21; i < size; ++i) if (id[i] == L'\\') return i + 1 < size;
    return false;
}

static bool get_property(DEVINST node, ULONG property, wchar_t* output, unsigned capacity) {
    ULONG bytes = capacity * sizeof(wchar_t);
    const CONFIGRET code = CM_Get_DevNode_Registry_PropertyW(node, property, nullptr, output, &bytes, 0);
    output[capacity - 1] = 0;
    if (code != CR_SUCCESS) { output[0] = 0; return false; }
    return output[0] != 0;
}

bool list_devices(DeviceList& output, Message& error) {
    output.count = 0; error = {};
    const wchar_t* usb_class = L"{36fc9e60-c465-11cf-8056-444553540000}";
    const ULONG flags = CM_GETIDLIST_FILTER_CLASS | CM_GETIDLIST_FILTER_PRESENT;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        ULONG size = 0;
        CONFIGRET code = CM_Get_Device_ID_List_SizeW(&size, usb_class, flags);
        if (code != CR_SUCCESS) { error_code(error, Text::ReadDeviceList, code); return false; }
        if (size < 2) size = 2;
        if (size > 262144) { error = {Text::DeviceListTooLarge}; return false; }
        auto buffer = static_cast<wchar_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size * sizeof(wchar_t)));
        if (!buffer) { error = {Text::DeviceListMemory}; return false; }
        code = CM_Get_Device_ID_ListW(usb_class, buffer, size, flags);
        if (code == CR_BUFFER_SMALL) { HeapFree(GetProcessHeap(), 0, buffer); continue; }
        if (code != CR_SUCCESS) { HeapFree(GetProcessHeap(), 0, buffer); error_code(error, Text::ReadDeviceList, code); return false; }
        bool success = true;
        for (ULONG offset = 0; offset < size && buffer[offset];) {
            ULONG end = offset;
            while (end < size && buffer[end]) ++end;
            if (end == size) { error = {Text::InvalidDeviceList}; success = false; break; }
            wchar_t* id = buffer + offset;
            offset = end + 1;
            if (!valid_device_id(id)) continue;
            DEVINST node = 0;
            if (CM_Locate_DevNodeW(&node, id, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) continue;
            if (output.count == MAX_DEVICES) { error = {Text::TooManyDevices}; success = false; break; }
            Device& device = output.items[output.count++];
            copy_text(device.id, MAX_DEVICE_ID_LEN, id);
            device.generic_name = !get_property(node, CM_DRP_FRIENDLYNAME, device.name, 128) && !get_property(node, CM_DRP_DEVICEDESC, device.name, 128);
            ULONG status = 0, problem = 0;
            device.healthy = CM_Get_DevNode_Status(&status, &problem, node, 0) == CR_SUCCESS && problem == 0;
        }
        HeapFree(GetProcessHeap(), 0, buffer);
        if (!success) output.count = 0;
        return success;
    }
    error = {Text::DeviceListChanging}; return false;
}

unsigned select_devices(const DeviceList& devices, const wchar_t* const* ids, unsigned count, bool all, DeviceList& output, Message& error) {
    output.count = 0; error = {};
    if (devices.count > MAX_DEVICES || count > MAX_DEVICES) { error = {Text::DeviceCountUnsupported}; return 2; }
    if (all && count) { error = {Text::ConflictingSelection}; return 2; }
    if (!devices.count) { error = {Text::NoConnectedDevices}; return 3; }
    if (all || (!count && devices.count == 1)) { output = devices; return 0; }
    if (!count) { error = {Text::AmbiguousSelection}; return 2; }
    for (unsigned i = 0; i < count; ++i) {
        if (!valid_device_id(ids[i])) { error = {Text::InvalidDeviceId}; return 2; }
        const Device* found = nullptr;
        for (unsigned j = 0; j < devices.count; ++j) if (equal_id(ids[i], devices.items[j].id)) { found = &devices.items[j]; break; }
        if (!found) { error = {Text::DeviceUnavailable}; return 2; }
        bool duplicate = false;
        for (unsigned j = 0; j < output.count; ++j) if (equal_id(found->id, output.items[j].id)) duplicate = true;
        if (!duplicate) output.items[output.count++] = *found;
    }
    return 0;
}

unsigned restart_device(const wchar_t* id, Message& error) {
    DeviceList present{}; DeviceList selected{};
    if (!list_devices(present, error)) return 2;
    const wchar_t* ids[] = {id};
    const unsigned selection = select_devices(present, ids, 1, false, selected, error);
    if (selection) return selection;
    if (!IsUserAnAdmin()) return 5;
    wchar_t program[MAX_PATH];
    const UINT size = GetSystemDirectoryW(program, MAX_PATH);
    if (!size || size >= MAX_PATH || !append_text(program, MAX_PATH, L"\\pnputil.exe")) { error = {Text::SystemDirectory}; return 1; }
    wchar_t command[1024] = L"\"";
    append_text(command, 1024, program); append_text(command, 1024, L"\" /restart-device \""); append_text(command, 1024, id); append_text(command, 1024, L"\"");
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(program, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) { error_code(error, Text::RestartStartFailed, GetLastError()); return 1; }
    CloseHandle(process.hThread);
    const DWORD wait = WaitForSingleObject(process.hProcess, 45000);
    DWORD code = 1;
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, 1000); CloseHandle(process.hProcess);
        error = {Text::RestartTimeout}; return 1;
    }
    const BOOL got_code = GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    if (!got_code) { error = {Text::RestartResultFailed}; return 1; }
    if (code == ERROR_SUCCESS_REBOOT_REQUIRED) return 7;
    if (code) { error_code(error, Text::WindowsRestartFailed, code); return 1; }
    Sleep(700);
    const ULONGLONG deadline = GetTickCount64() + 8000;
    do {
        if (!list_devices(present, error)) return 2;
        for (unsigned i = 0; i < present.count; ++i) if (equal_id(present.items[i].id, id) && present.items[i].healthy) return 0;
        Sleep(300);
    } while (GetTickCount64() < deadline);
    return 6;
}

unsigned elevate_devices(const DeviceList& selected, Message& error) {
    if (!selected.count || selected.count > MAX_DEVICES) return 2;
    wchar_t program[32768];
    const DWORD size = GetModuleFileNameW(nullptr, program, 32768);
    if (!size || size >= 32768) { error = {Text::ProgramLocation}; return 1; }
    wchar_t parameters[8192] = L"repair --no-elevate --lang ";
    append_text(parameters, 8192, language_tag());
    for (unsigned i = 0; i < selected.count; ++i) {
        if (!valid_device_id(selected.items[i].id)) return 2;
        if (!append_text(parameters, 8192, L" --device \"") || !append_text(parameters, 8192, selected.items[i].id) || !append_text(parameters, 8192, L"\"")) return 2;
    }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SHELLEXECUTEINFOW info{}; info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"runas"; info.lpFile = program; info.lpParameters = parameters; info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) {
        const DWORD code = GetLastError(); if (SUCCEEDED(initialized)) CoUninitialize();
        if (code == ERROR_CANCELLED) return 4;
        error_code(error, Text::AdminRequestFailed, code); return 1;
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    if (!info.hProcess) { error = {Text::AdminStateFailed}; return 1; }
    const DWORD wait = WaitForSingleObject(info.hProcess, (selected.count + 1) * 60000);
    DWORD code = 1;
    const BOOL success = wait == WAIT_OBJECT_0 && GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    if (!success) { error = {Text::AdminWaitFailed}; return 1; }
    return code;
}

Text result_text(unsigned code) {
    switch (code) {
    case 0: return Text::ResultSuccess;
    case 2: return Text::ResultQueryFailed;
    case 3: return Text::ResultNoDevices;
    case 4: return Text::ResultCancelled;
    case 5: return Text::ResultAdminNeeded;
    case 6: return Text::ResultOffline;
    case 7: return Text::ResultRebootNeeded;
    default: return Text::ResultFailed;
    }
}
