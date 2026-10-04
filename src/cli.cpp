#include "native.hpp"

void write_output(const wchar_t* text) {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0, written = 0;
    if (output && output != INVALID_HANDLE_VALUE && GetConsoleMode(output, &mode)) {
        WriteConsoleW(output, text, length(text), &written, nullptr); return;
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return;
    auto encoded = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(size)));
    if (!encoded) return;
    WideCharToMultiByte(CP_UTF8, 0, text, -1, encoded, size, nullptr, nullptr);
    WriteFile(output, encoded, static_cast<DWORD>(size - 1), &written, nullptr);
    HeapFree(GetProcessHeap(), 0, encoded);
}
static void line(const wchar_t* text) { write_output(text); write_output(L"\r\n"); }
static void line(const Message& message) { wchar_t output[512]{}; format_message(message, output, 512); line(output); }
static void json_string(const wchar_t* text) {
    write_output(L"\"");
    while (*text) {
        const wchar_t ch = *text++;
        if (ch == L'"') write_output(L"\\\"");
        else if (ch == L'\\') write_output(L"\\\\");
        else if (ch < 32) {
            const wchar_t* hex = L"0123456789abcdef";
            wchar_t escaped[] = L"\\u0000";
            escaped[4] = hex[(ch >> 4) & 15]; escaped[5] = hex[ch & 15];
            write_output(escaped);
        } else {
            wchar_t single[] = {ch, 0, 0};
            if (ch >= 0xd800 && ch <= 0xdbff && *text >= 0xdc00 && *text <= 0xdfff) single[1] = *text++;
            write_output(single);
        }
    }
    write_output(L"\"");
}
static void help() {
    line(tr(Text::CliHelp));
}

unsigned run_cli(int argc, wchar_t** argv) {
    if (argc < 2 || equal_text(argv[1], L"--help") || equal_text(argv[1], L"-h") || equal_text(argv[1], L"help")) { help(); return 0; }
    if (equal_text(argv[1], L"--version")) { line(L"Scroll Rescue 0.3.0 (C++)"); return 0; }
    const bool listing = equal_text(argv[1], L"devices");
    const bool repairing = equal_text(argv[1], L"repair");
    if (!listing && !repairing) { line(tr(Text::UnknownCommand)); return 2; }
    bool all = false, dry = false, no_elevate = false, json = false;
    const wchar_t* ids[MAX_DEVICES]{}; unsigned count = 0;
    for (int i = 2; i < argc; ++i) {
        if (equal_text(argv[i], L"--help") || equal_text(argv[i], L"-h")) { help(); return 0; }
        if (listing && equal_text(argv[i], L"--json")) json = true;
        else if (repairing && equal_text(argv[i], L"--all")) all = true;
        else if (repairing && equal_text(argv[i], L"--dry-run")) dry = true;
        else if (repairing && equal_text(argv[i], L"--no-elevate")) no_elevate = true;
        else if (repairing && equal_text(argv[i], L"--device")) {
            if (++i == argc || count == MAX_DEVICES || !valid_device_id(argv[i])) { line(tr(Text::MissingDeviceId)); return 2; }
            ids[count++] = argv[i];
        } else { line(tr(Text::InvalidArgument)); return 2; }
    }
    if (all && count) { line(tr(Text::ConflictingSelection)); return 2; }
    DeviceList devices{}; Message error{};
    if (!list_devices(devices, error)) { line(error); return 2; }
    if (listing) {
        if (json) {
            write_output(L"[");
            for (unsigned i = 0; i < devices.count; ++i) {
                if (i) write_output(L",");
                write_output(L"{\"id\":"); json_string(devices.items[i].id);
                write_output(L",\"name\":"); json_string(device_name(devices.items[i]));
                write_output(L",\"healthy\":"); write_output(devices.items[i].healthy ? L"true}" : L"false}");
            }
            line(L"]");
        } else {
            if (!devices.count) line(tr(Text::NoConnectedDevices));
            for (unsigned i = 0; i < devices.count; ++i) { write_output(device_name(devices.items[i])); line(tr(devices.items[i].healthy ? Text::CliOnline : Text::CliUnhealthy)); write_output(L"  "); line(devices.items[i].id); }
        }
        return 0;
    }
    DeviceList selected{};
    const unsigned selection = select_devices(devices, ids, count, all, selected, error);
    if (selection) { line(error); return selection; }
    if (dry) {
        for (unsigned i = 0; i < selected.count; ++i) {
            line(device_name(selected.items[i])); write_output(L"pnputil /restart-device \""); write_output(selected.items[i].id); line(L"\"");
        }
        line(tr(Text::DryRunDone)); return 0;
    }
    unsigned result = 0;
    if (!IsUserAnAdmin()) {
        if (no_elevate) result = 5;
        else { line(tr(Text::AdminPrompt)); result = elevate_devices(selected, error); }
    } else {
        for (unsigned i = 0; i < selected.count; ++i) {
            write_output(tr(Text::RestartingPrefix)); line(device_name(selected.items[i]));
            const unsigned code = restart_device(selected.items[i].id, error);
            if (code) { result = code; if (code != 7) break; }
        }
    }
    if (error.key != Text::None) line(error);
    line(result_message(result)); return result;
}
