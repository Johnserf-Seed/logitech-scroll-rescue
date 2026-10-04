#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <cfgmgr32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <dwmapi.h>

constexpr unsigned MAX_DEVICES = 32;
struct Device {
    wchar_t id[MAX_DEVICE_ID_LEN];
    wchar_t name[128];
    bool healthy;
};
struct DeviceList { Device items[MAX_DEVICES]; unsigned count; };

inline unsigned length(const wchar_t* text) {
    unsigned result = 0;
    if (text) while (text[result]) ++result;
    return result;
}
inline bool copy_text(wchar_t* output, unsigned capacity, const wchar_t* text) {
    if (!output || !capacity || !text) return false;
    unsigned i = 0;
    while (text[i] && i + 1 < capacity) { output[i] = text[i]; ++i; }
    output[i] = 0;
    return !text[i];
}
inline bool append_text(wchar_t* output, unsigned capacity, const wchar_t* text) {
    const unsigned used = length(output);
    return used < capacity && copy_text(output + used, capacity - used, text);
}
inline void append_number(wchar_t* output, unsigned capacity, unsigned value) {
    wchar_t reversed[11]; unsigned count = 0;
    do { reversed[count++] = static_cast<wchar_t>(L'0' + value % 10); value /= 10; } while (value);
    wchar_t digits[11]; unsigned i = 0;
    while (count) digits[i++] = reversed[--count];
    digits[i] = 0;
    append_text(output, capacity, digits);
}
inline bool equal_text(const wchar_t* a, const wchar_t* b) {
    return a && b && CompareStringOrdinal(a, -1, b, -1, FALSE) == CSTR_EQUAL;
}
inline bool equal_id(const wchar_t* a, const wchar_t* b) {
    return a && b && CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}
inline void error_code(wchar_t* output, const wchar_t* message, unsigned code) {
    copy_text(output, 512, message);
    append_text(output, 512, L"（错误 "); append_number(output, 512, code); append_text(output, 512, L"）。");
}

bool valid_device_id(const wchar_t* id);
bool list_devices(DeviceList& output, wchar_t* error);
unsigned select_devices(const DeviceList& devices, const wchar_t* const* ids, unsigned count, bool all, DeviceList& output, wchar_t* error);
unsigned restart_device(const wchar_t* id, wchar_t* error);
unsigned elevate_devices(const DeviceList& selected, wchar_t* error);
const wchar_t* result_message(unsigned code);
unsigned run_cli(int argc, wchar_t** argv);
unsigned run_gui(HINSTANCE instance);
void write_output(const wchar_t* text);
#ifdef SCROLL_RESCUE_QA
unsigned run_qa(HINSTANCE instance, const wchar_t* path, unsigned percent, bool live);
#endif
