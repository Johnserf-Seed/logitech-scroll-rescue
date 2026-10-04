#include "native.hpp"

namespace {
constexpr int WIDTH = 600, HEIGHT = 620;
constexpr unsigned REPAIR = 101, REFRESH = 102, SELECTOR = 103, DEVICE_LIST = 104, MINIMIZE = 105, CLOSE = 106;
constexpr UINT WORK_DONE = WM_APP + 1;
constexpr COLORREF BG = RGB(15, 20, 29), CARD = RGB(24, 32, 45), BORDER = RGB(43, 55, 73);
constexpr COLORREF TEXT = RGB(235, 240, 248), MUTED = RGB(155, 170, 191), GREEN = RGB(105, 231, 178), AMBER = RGB(246, 195, 104), RED = RGB(255, 144, 144);

struct App {
    HWND window, selector, repair, refresh, minimize, close, list, hover;
    HFONT font;
    HBRUSH brush;
    unsigned scale = 100;
    DeviceList devices{};
    int selected = -1;
    bool busy = false, repairing = false;
    wchar_t headline[128] = L"正在查找设备";
    wchar_t detail[512] = L"请稍候，正在读取已连接的罗技 USB 设备。";
    wchar_t logs[2][256]{};
    COLORREF status_color = MUTED;
#ifdef SCROLL_RESCUE_QA
    wchar_t snapshot[32768]{};
    bool qa = false, qa_live = false;
    unsigned ticks = 0, qa_result = 1;
#endif
} app;
WNDPROC button_proc = nullptr;
struct Job { HWND owner; bool restart; wchar_t id[MAX_DEVICE_ID_LEN]; };
struct Outcome { bool restart, success; unsigned code; DeviceList devices; wchar_t error[512]; };

int n(int value) { return MulDiv(value, static_cast<int>(app.scale), 100); }
RECT area(int x, int y, int width, int height) { return {n(x), n(y), n(x + width), n(y + height)}; }
void log(const wchar_t* message) { copy_text(app.logs[0], 256, app.logs[1]); copy_text(app.logs[1], 256, message); }
HFONT font(int size, int weight = 400) { return CreateFontW(-n(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI"); }
void fill(HDC dc, const RECT& rectangle, COLORREF color) {
    const HBRUSH brush = CreateSolidBrush(color); FillRect(dc, &rectangle, brush); DeleteObject(brush);
}
void panel(HDC dc, const RECT& rectangle, COLORREF color, COLORREF border, int radius = 12) {
    const HBRUSH brush = CreateSolidBrush(color); const HPEN pen = CreatePen(PS_SOLID, 1, border);
    const HGDIOBJ old_brush = SelectObject(dc, brush), old_pen = SelectObject(dc, pen);
    RoundRect(dc, rectangle.left, rectangle.top, rectangle.right, rectangle.bottom, n(radius), n(radius));
    SelectObject(dc, old_brush); SelectObject(dc, old_pen); DeleteObject(brush); DeleteObject(pen);
}
void text(HDC dc, const wchar_t* value, RECT rectangle, int size = 14, int weight = 400, COLORREF color = TEXT, UINT flags = DT_SINGLELINE) {
    const HFONT current = font(size, weight); const HGDIOBJ old = SelectObject(dc, current);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    DrawTextW(dc, value, -1, &rectangle, flags | DT_NOPREFIX);
    SelectObject(dc, old); DeleteObject(current);
}
void stroke(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width = 1) {
    const HPEN pen = CreatePen(PS_SOLID, n(width), color); const HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, n(x1), n(y1), nullptr); LineTo(dc, n(x2), n(y2)); SelectObject(dc, old); DeleteObject(pen);
}
void device_label(wchar_t* output, unsigned capacity) {
    if (app.selected >= 0 && static_cast<unsigned>(app.selected) < app.devices.count) {
        const Device& device = app.devices.items[app.selected];
        copy_text(output, capacity, device.name); append_text(output, capacity, device.healthy ? L"  ·  在线" : L"  ·  状态异常");
    } else copy_text(output, capacity, L"选择鼠标对应的 USB 设备…");
}

void draw(HDC dc) {
    fill(dc, area(0, 0, WIDTH, HEIGHT), BG);
    panel(dc, area(22, 15, 22, 22), GREEN, GREEN, 6);
    stroke(dc, 32, 20, 32, 31, RGB(9, 45, 32), 2);
    text(dc, L"SCROLL RESCUE", area(55, 17, 320, 24), 12, 700, TEXT);
    text(dc, L"v0.2", area(422, 18, 62, 22), 11, 400, MUTED, DT_RIGHT | DT_SINGLELINE);
    stroke(dc, 24, 52, 576, 52, BORDER);
    text(dc, L"让滚轮恢复正常。", area(24, 76, 552, 43), 29, 700);
    text(dc, L"退出瓦洛兰特后持续滚动？试着恢复鼠标接收器。", area(26, 127, 550, 25), 14, 400, MUTED);
    panel(dc, area(24, 168, 552, 139), CARD, BORDER);
    text(dc, L"目标设备", area(42, 183, 340, 23), 13, 600);
    wchar_t count[48]{}; append_number(count, 48, app.devices.count); append_text(count, 48, L" 个已连接");
    text(dc, count, area(422, 183, 136, 23), 12, 400, MUTED, DT_SINGLELINE | DT_RIGHT);
    const wchar_t* id = app.selected >= 0 && static_cast<unsigned>(app.selected) < app.devices.count ? app.devices.items[app.selected].id : L"等待选择设备";
    text(dc, id, area(42, 253, 516, 19), 11, 400, MUTED, DT_SINGLELINE | DT_END_ELLIPSIS);
    text(dc, L"恢复时会短暂断连，请确认选择鼠标对应的设备。", area(42, 279, 516, 20), 12, 400, MUTED);
    text(dc, L"按需申请管理员权限 · 建议退出游戏后使用", area(24, 387, 552, 20), 11, 400, MUTED, DT_SINGLELINE | DT_CENTER);
    panel(dc, area(24, 421, 552, 95), CARD, BORDER);
    panel(dc, area(42, 441, 8, 8), app.status_color, app.status_color, 8);
    text(dc, app.headline, area(60, 432, 498, 28), 18, 600, app.status_color);
    text(dc, app.detail, area(42, 465, 516, 41), 13, 400, MUTED, DT_WORDBREAK);
    text(dc, L"操作记录", area(26, 535, 540, 20), 12, 600, MUTED);
    for (int i = 0; i < 2; ++i) if (app.logs[i][0]) {
        wchar_t line[260] = L"·  "; append_text(line, 260, app.logs[i]);
        text(dc, line, area(26, 559 + i * 23, 548, 22), 12, 400, TEXT, DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    text(dc, L"USB 接收器  /  命令行与 BAT 同时保留", area(24, 600, 552, 17), 10, 400, MUTED, DT_SINGLELINE | DT_CENTER);
}

void draw_button(const DRAWITEMSTRUCT& item) {
    const bool enabled = !(item.itemState & ODS_DISABLED), pressed = (item.itemState & ODS_SELECTED) != 0, hovered = app.hover == item.hwndItem;
    fill(item.hDC, item.rcItem, BG);
    if (item.CtlID == MINIMIZE || item.CtlID == CLOSE) {
        if (hovered || pressed) panel(item.hDC, item.rcItem, item.CtlID == CLOSE ? RGB(157, 62, 73) : CARD, BORDER, 8);
        const COLORREF color = enabled ? TEXT : BORDER;
        const int cx = (item.rcItem.left + item.rcItem.right) / 2, cy = (item.rcItem.top + item.rcItem.bottom) / 2;
        const HPEN pen = CreatePen(PS_SOLID, n(1), color); const HGDIOBJ old = SelectObject(item.hDC, pen);
        if (item.CtlID == MINIMIZE) { MoveToEx(item.hDC, cx - n(5), cy + n(2), nullptr); LineTo(item.hDC, cx + n(5), cy + n(2)); }
        else { MoveToEx(item.hDC, cx - n(4), cy - n(4), nullptr); LineTo(item.hDC, cx + n(5), cy + n(5)); MoveToEx(item.hDC, cx + n(4), cy - n(4), nullptr); LineTo(item.hDC, cx - n(5), cy + n(5)); }
        SelectObject(item.hDC, old); DeleteObject(pen);
        if (item.itemState & ODS_FOCUS) DrawFocusRect(item.hDC, &item.rcItem);
        return;
    }
    const bool primary = item.CtlID == REPAIR;
    const COLORREF background = !enabled ? RGB(34, 44, 58) : primary ? (pressed ? RGB(73, 193, 144) : GREEN) : (hovered || pressed ? RGB(34, 47, 65) : CARD);
    panel(item.hDC, item.rcItem, background, primary && enabled ? background : BORDER, item.CtlID == SELECTOR ? 6 : 12);
    if (item.CtlID == SELECTOR) {
        wchar_t label[180]{}; device_label(label, 180);
        RECT rectangle = item.rcItem; rectangle.left += n(10); rectangle.right -= n(32);
        text(item.hDC, label, rectangle, 14, 400, enabled ? TEXT : MUTED, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        rectangle = item.rcItem; rectangle.left = rectangle.right - n(28);
        text(item.hDC, L"⌄", rectangle, 14, 400, MUTED, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    } else {
        const wchar_t* label = primary ? (app.repairing ? L"正在恢复…" : L"恢复滚轮") : (app.busy && !app.repairing ? L"正在扫描…" : L"刷新设备");
        text(item.hDC, label, item.rcItem, 17, 600, !enabled ? MUTED : primary ? RGB(9, 45, 32) : TEXT, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
    if (item.itemState & ODS_FOCUS) { RECT focus = item.rcItem; InflateRect(&focus, -n(4), -n(4)); DrawFocusRect(item.hDC, &focus); }
}

void hide_list() { ShowWindow(app.list, SW_HIDE); }
void controls() {
    const HFONT old = app.font; app.font = font(14);
    const HWND handles[] = {app.selector, app.repair, app.refresh, app.minimize, app.close, app.list};
    for (HWND handle : handles) SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), FALSE);
    if (old) DeleteObject(old);
    MoveWindow(app.selector, n(42), n(211), n(516), n(33), FALSE);
    MoveWindow(app.repair, n(24), n(327), n(376), n(50), FALSE);
    MoveWindow(app.refresh, n(416), n(327), n(160), n(50), FALSE);
    MoveWindow(app.minimize, n(493), n(10), n(36), n(32), FALSE);
    MoveWindow(app.close, n(542), n(10), n(36), n(32), FALSE);
    SendMessageW(app.list, LB_SETITEMHEIGHT, 0, n(46));
    EnableWindow(app.selector, !app.busy && app.devices.count);
    EnableWindow(app.repair, !app.busy && app.selected >= 0);
    EnableWindow(app.refresh, !app.busy); EnableWindow(app.close, !app.repairing);
    wchar_t label[180]{}; device_label(label, 180); SetWindowTextW(app.selector, label);
    InvalidateRect(app.window, nullptr, FALSE);
    for (HWND handle : handles) InvalidateRect(handle, nullptr, FALSE);
}

DWORD WINAPI work(LPVOID parameter) {
    const Job job = *static_cast<Job*>(parameter); HeapFree(GetProcessHeap(), 0, parameter);
    auto outcome = static_cast<Outcome*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Outcome)));
    if (!outcome) { PostMessageW(job.owner, WORK_DONE, 1, 0); return 0; }
    outcome->restart = job.restart;
    if (job.restart) {
        if (IsUserAnAdmin()) outcome->code = restart_device(job.id, outcome->error);
        else {
            DeviceList devices{}, selected{};
            const wchar_t* ids[] = {job.id};
            outcome->code = list_devices(devices, outcome->error) ? select_devices(devices, ids, 1, false, selected, outcome->error) : 2;
            if (!outcome->code) outcome->code = elevate_devices(selected, outcome->error);
        }
    } else outcome->success = list_devices(outcome->devices, outcome->error);
    if (!PostMessageW(job.owner, WORK_DONE, 0, reinterpret_cast<LPARAM>(outcome))) HeapFree(GetProcessHeap(), 0, outcome);
    return 0;
}
void start_work(bool restart) {
    if (app.busy || (restart && app.selected < 0)) return;
    hide_list();
    auto job = static_cast<Job*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Job)));
    if (!job) { copy_text(app.headline, 128, L"操作未完成"); copy_text(app.detail, 512, L"没有足够内存执行操作。"); app.status_color = RED; controls(); return; }
    job->owner = app.window; job->restart = restart;
    if (restart) copy_text(job->id, MAX_DEVICE_ID_LEN, app.devices.items[app.selected].id);
    app.busy = true; app.repairing = restart; app.status_color = restart ? AMBER : MUTED;
    copy_text(app.headline, 128, restart ? L"正在恢复设备" : L"正在查找设备");
    copy_text(app.detail, 512, restart ? L"请允许 Windows 权限提示。鼠标会短暂断连，恢复后请测试滚轮。" : L"请稍候，正在读取已连接的罗技 USB 设备。");
    if (restart) log(L"开始恢复所选设备");
    const HANDLE thread = CreateThread(nullptr, 0, work, job, 0, nullptr);
    if (thread) CloseHandle(thread);
    else {
        HeapFree(GetProcessHeap(), 0, job); app.busy = false; app.repairing = false;
        copy_text(app.headline, 128, L"操作未完成"); copy_text(app.detail, 512, L"无法启动操作，请稍后重试。"); app.status_color = RED;
    }
    controls();
}
void done(Outcome* outcome) {
    app.busy = false; app.repairing = false;
    if (!outcome) {
        copy_text(app.headline, 128, L"操作未完成"); copy_text(app.detail, 512, L"操作意外结束，请刷新后重试。"); app.status_color = RED; controls(); return;
    }
    if (!outcome->restart) {
        wchar_t previous[MAX_DEVICE_ID_LEN]{};
        if (app.selected >= 0) copy_text(previous, MAX_DEVICE_ID_LEN, app.devices.items[app.selected].id);
        app.selected = -1; app.devices = outcome->devices;
        if (outcome->success) {
            for (unsigned i = 0; i < app.devices.count; ++i) if (equal_id(previous, app.devices.items[i].id)) app.selected = static_cast<int>(i);
            if (app.selected < 0 && app.devices.count == 1) app.selected = 0;
            copy_text(app.headline, 128, !app.devices.count ? L"未找到罗技 USB 设备" : app.selected < 0 ? L"请选择设备" : L"准备就绪");
            copy_text(app.detail, 512, !app.devices.count ? L"请插入鼠标接收器，再点击「刷新设备」。" : L"退出游戏后，选择鼠标对应的设备，点击「恢复滚轮」。");
            app.status_color = app.devices.count ? GREEN : AMBER;
            wchar_t message[80] = L"检测到 "; append_number(message, 80, app.devices.count); append_text(message, 80, L" 个罗技 USB 设备"); log(message);
        } else { copy_text(app.headline, 128, L"设备查询失败"); copy_text(app.detail, 512, outcome->error); app.status_color = RED; log(L"查询失败，请刷新后重试"); }
    } else {
        const unsigned code = outcome->code;
        copy_text(app.headline, 128, !code ? L"设备已恢复在线" : code == 4 ? L"已取消操作" : code == 7 ? L"需要重启电脑" : L"恢复未完成");
        copy_text(app.detail, 512, outcome->error[0] ? outcome->error : result_message(code));
        app.status_color = !code ? GREEN : code == 4 || code == 6 || code == 7 ? AMBER : RED;
        log(!code ? L"设备重启完成，请实际测试滚轮" : code == 4 ? L"已取消管理员授权" : L"操作结束，请查看上方结果");
    }
    HeapFree(GetProcessHeap(), 0, outcome); controls();
}
void choose(unsigned index) {
    hide_list(); if (index >= app.devices.count) return;
    app.selected = static_cast<int>(index); app.status_color = GREEN;
    copy_text(app.headline, 128, L"准备就绪"); copy_text(app.detail, 512, L"退出游戏后，点击「恢复滚轮」恢复所选设备。"); controls(); SetFocus(app.selector);
}
void show_list() {
    if (IsWindowVisible(app.list)) { hide_list(); return; }
    SendMessageW(app.list, LB_RESETCONTENT, 0, 0);
    for (unsigned i = 0; i < app.devices.count; ++i) SendMessageW(app.list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(app.devices.items[i].name));
    SendMessageW(app.list, LB_SETCURSEL, app.selected >= 0 ? static_cast<WPARAM>(app.selected) : 0, 0);
    const unsigned rows = app.devices.count < 5 ? app.devices.count : 5;
    SetWindowPos(app.list, HWND_TOP, n(42), n(246), n(516), n(static_cast<int>(rows) * 46 + 4), SWP_SHOWWINDOW);
    SetFocus(app.list);
}
LRESULT CALLBACK child_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_MOUSEMOVE && app.hover != window) {
        if (app.hover) InvalidateRect(app.hover, nullptr, FALSE);
        app.hover = window; InvalidateRect(window, nullptr, FALSE);
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0}; TrackMouseEvent(&tracking);
    } else if (message == WM_MOUSELEAVE && app.hover == window) { app.hover = nullptr; InvalidateRect(window, nullptr, FALSE); }
    return CallWindowProcW(button_proc, window, message, wparam, lparam);
}
HWND make_button(HWND parent, HINSTANCE instance, unsigned id, const wchar_t* label) {
    HWND button = CreateWindowExW(0, L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), instance, nullptr);
    if (button) {
        const LONG_PTR previous = SetWindowLongPtrW(button, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(child_proc));
        if (!button_proc) button_proc = reinterpret_cast<WNDPROC>(previous);
    }
    return button;
}

#ifdef SCROLL_RESCUE_QA
bool snapshot() {
    const int width = n(WIDTH), height = n(HEIGHT);
    RECT client{}, window{};
    GetClientRect(app.window, &client); GetWindowRect(app.window, &window);
    if ((GetWindowLongPtrW(app.window, GWL_STYLE) & WS_CAPTION) != 0
        || client.right != width || client.bottom != height
        || window.right - window.left != width || window.bottom - window.top != height) return false;
    POINT drag{n(100), n(25)}, close{n(557), n(25)};
    ClientToScreen(app.window, &drag); ClientToScreen(app.window, &close);
    if (SendMessageW(app.window, WM_NCHITTEST, 0, MAKELPARAM(drag.x, drag.y)) != HTCAPTION
        || SendMessageW(app.window, WM_NCHITTEST, 0, MAKELPARAM(close.x, close.y)) != HTCLIENT) return false;
    HDC screen = GetDC(app.window), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height); ReleaseDC(app.window, screen);
    if (!dc || !bitmap) { if (dc) DeleteDC(dc); if (bitmap) DeleteObject(bitmap); return false; }
    HGDIOBJ old = SelectObject(dc, bitmap);
    SendMessageW(app.window, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    SelectObject(dc, old);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    const DWORD bytes = static_cast<DWORD>(width * height * 4);
    void* pixels = HeapAlloc(GetProcessHeap(), 0, bytes);
    const bool captured = pixels && GetDIBits(dc, bitmap, 0, static_cast<UINT>(height), pixels, &info, DIB_RGB_COLORS) == height;
    DeleteObject(bitmap); DeleteDC(dc);
    bool saved = false;
    if (captured) {
        HANDLE file = CreateFileW(app.snapshot, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            BITMAPFILEHEADER header{}; header.bfType = 0x4d42; header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER); header.bfSize = header.bfOffBits + bytes;
            DWORD written = 0;
            saved = WriteFile(file, &header, sizeof(header), &written, nullptr) && written == sizeof(header)
                && WriteFile(file, &info.bmiHeader, sizeof(BITMAPINFOHEADER), &written, nullptr) && written == sizeof(BITMAPINFOHEADER)
                && WriteFile(file, pixels, bytes, &written, nullptr) && written == bytes;
            CloseHandle(file);
        }
    }
    if (pixels) HeapFree(GetProcessHeap(), 0, pixels);
    return saved;
}
#endif

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_NCCALCSIZE: return 0;
    case WM_NCACTIVATE: return TRUE;
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize = {n(WIDTH), n(HEIGHT)};
        info->ptMaxTrackSize = info->ptMinTrackSize;
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_MAXIMIZE || (wparam & 0xfff0) == SC_SIZE) return 0;
        return DefWindowProcW(window, message, wparam, lparam);
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}; ScreenToClient(window, &point);
        if (point.y >= 0 && point.y < n(52) && point.x < n(486)) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_CREATE: {
        app.window = window;
        const HINSTANCE instance = reinterpret_cast<CREATESTRUCTW*>(lparam)->hInstance;
        app.selector = make_button(window, instance, SELECTOR, L"选择设备");
        app.repair = make_button(window, instance, REPAIR, L"恢复滚轮"); app.refresh = make_button(window, instance, REFRESH, L"刷新设备");
        app.minimize = make_button(window, instance, MINIMIZE, L"最小化"); app.close = make_button(window, instance, CLOSE, L"关闭");
        app.list = CreateWindowExW(0, L"LISTBOX", L"设备列表", WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(DEVICE_LIST)), instance, nullptr);
        app.brush = CreateSolidBrush(CARD);
        if (!app.selector || !app.repair || !app.refresh || !app.minimize || !app.close || !app.list || !app.brush) return -1;
#ifdef SCROLL_RESCUE_QA
        if (app.qa && !app.qa_live) {
            app.devices.count = 1; copy_text(app.devices.items[0].id, MAX_DEVICE_ID_LEN, L"USB\\VID_046D&PID_C54D\\DEMO"); copy_text(app.devices.items[0].name, 128, L"LIGHTSPEED Receiver"); app.devices.items[0].healthy = true; app.selected = 0;
            app.status_color = GREEN; copy_text(app.headline, 128, L"准备就绪"); copy_text(app.detail, 512, L"退出游戏后，选择鼠标对应的设备，点击「恢复滚轮」。"); log(L"检测到 1 个罗技 USB 设备"); controls();
        } else
#endif
        start_work(false);
#ifdef SCROLL_RESCUE_QA
        if (app.qa) SetTimer(window, 1, 100, nullptr);
#endif
        return 0;
    }
    case WM_COMMAND: {
        const unsigned id = LOWORD(wparam), notification = HIWORD(wparam);
        if (id == MINIMIZE) { hide_list(); ShowWindow(window, SW_MINIMIZE); return 0; }
        if (id == CLOSE) { SendMessageW(window, WM_CLOSE, 0, 0); return 0; }
        if (app.busy) return 0;
        if (id == REPAIR && notification == BN_CLICKED) start_work(true);
        else if (id == REFRESH && notification == BN_CLICKED) start_work(false);
        else if (id == SELECTOR && notification == BN_CLICKED) show_list();
        else if (id == DEVICE_LIST && notification == LBN_SELCHANGE) {
            const LRESULT selected = SendMessageW(app.list, LB_GETCURSEL, 0, 0);
            if (selected >= 0) choose(static_cast<unsigned>(selected));
        }
        return 0;
    }
    case WORK_DONE: done(reinterpret_cast<Outcome*>(lparam)); return 0;
    case WM_LBUTTONDOWN: hide_list(); return 0;
    case WM_DRAWITEM: {
        const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (item.CtlID == DEVICE_LIST) {
            fill(item.hDC, item.rcItem, item.itemState & ODS_SELECTED ? RGB(43, 62, 76) : CARD);
            if (item.itemID < app.devices.count) {
                RECT top = item.rcItem; top.left += n(10); top.right -= n(10); top.top += n(4); top.bottom = top.top + n(21);
                text(item.hDC, app.devices.items[item.itemID].name, top, 13, 400, TEXT, DT_SINGLELINE | DT_END_ELLIPSIS);
                top.top += n(21); top.bottom = top.top + n(17);
                text(item.hDC, app.devices.items[item.itemID].id, top, 10, 400, MUTED, DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        } else draw_button(item);
        return TRUE;
    }
    case WM_MEASUREITEM: reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->itemHeight = static_cast<UINT>(n(46)); return TRUE;
    case WM_CTLCOLORLISTBOX: SetTextColor(reinterpret_cast<HDC>(wparam), TEXT); SetBkColor(reinterpret_cast<HDC>(wparam), CARD); return reinterpret_cast<LRESULT>(app.brush);
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint), memory = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, n(WIDTH), n(HEIGHT));
        if (memory && bitmap) { HGDIOBJ old = SelectObject(memory, bitmap); draw(memory); BitBlt(dc, 0, 0, n(WIDTH), n(HEIGHT), memory, 0, 0, SRCCOPY); SelectObject(memory, old); }
        else draw(dc);
        if (bitmap) DeleteObject(bitmap); if (memory) DeleteDC(memory); EndPaint(window, &paint); return 0;
    }
    case WM_PRINTCLIENT: draw(reinterpret_cast<HDC>(wparam)); return 0;
    case WM_DPICHANGED: {
        hide_list(); app.scale = MulDiv(LOWORD(wparam), 100, 96);
        const RECT& proposed = *reinterpret_cast<RECT*>(lparam);
        SetWindowPos(window, nullptr, proposed.left, proposed.top, n(WIDTH), n(HEIGHT), SWP_NOZORDER | SWP_NOACTIVATE); controls(); return 0;
    }
#ifdef SCROLL_RESCUE_QA
    case WM_TIMER:
        if (!app.busy || ++app.ticks >= 100) {
            app.qa_result = !app.busy && snapshot() ? 0 : 1;
            SendMessageW(window, WM_COMMAND, CLOSE, 0);
            if (IsWindow(window)) { app.qa_result = 1; DestroyWindow(window); }
        }
        return 0;
#endif
    case WM_CLOSE: if (!app.repairing) DestroyWindow(window); return 0;
    case WM_DESTROY: KillTimer(window, 1); PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
}

unsigned launch(HINSTANCE instance) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
#ifdef SCROLL_RESCUE_QA
    if (!app.qa)
#endif
    {
        app.scale = static_cast<unsigned>(MulDiv(GetDpiForSystem(), 100, 96));
        const unsigned max_height = static_cast<unsigned>((work.bottom - work.top - 48) * 100 / HEIGHT);
        const unsigned max_width = static_cast<unsigned>((work.right - work.left - 48) * 100 / WIDTH);
        if (max_height >= 60 && app.scale > max_height) app.scale = max_height;
        if (max_width >= 60 && app.scale > max_width) app.scale = max_width;
    }
    WNDCLASSW window_class{}; window_class.lpfnWndProc = window_proc; window_class.hInstance = instance; window_class.lpszClassName = L"ScrollRescueCppWindow";
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW); window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassW(&window_class)) return 1;
    const HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, window_class.lpszClassName, L"Scroll Rescue · 罗技滚轮恢复", WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, work.left + (work.right - work.left - n(WIDTH)) / 2, work.top + (work.bottom - work.top - n(HEIGHT)) / 2, n(WIDTH), n(HEIGHT), nullptr, nullptr, instance, nullptr);
    if (!window) { if (app.font) DeleteObject(app.font); if (app.brush) DeleteObject(app.brush); return 1; }
    const BOOL dark = TRUE; DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const DWORD corners = 2; DwmSetWindowAttribute(window, 33, &corners, sizeof(corners));
    const MARGINS margin{1, 1, 1, 1}; DwmExtendFrameIntoClientArea(window, &margin);
    SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
#ifdef SCROLL_RESCUE_QA
    if (!app.qa)
#endif
    { ShowWindow(window, SW_SHOWNORMAL); UpdateWindow(window); }
    MSG message{};
    while (true) {
        const BOOL got = GetMessageW(&message, nullptr, 0, 0);
        if (!got) break;
        if (got == -1) { DestroyWindow(window); break; }
        if (message.message == WM_KEYDOWN) {
            const HWND focus = GetFocus();
            if (message.wParam == VK_ESCAPE && IsWindowVisible(app.list)) { hide_list(); SetFocus(app.selector); continue; }
            if (message.wParam == VK_RETURN) {
                if (focus == app.list) { const LRESULT index = SendMessageW(app.list, LB_GETCURSEL, 0, 0); if (index >= 0) choose(static_cast<unsigned>(index)); continue; }
                const unsigned id = static_cast<unsigned>(GetDlgCtrlID(focus));
                if (focus && IsWindowEnabled(focus) && id >= REPAIR && id <= CLOSE) { SendMessageW(window, WM_COMMAND, id, 0); continue; }
            }
        }
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (app.font) DeleteObject(app.font); if (app.brush) DeleteObject(app.brush);
#ifdef SCROLL_RESCUE_QA
    return app.qa ? app.qa_result : 0;
#else
    return 0;
#endif
}
}

unsigned run_gui(HINSTANCE instance) { return launch(instance); }
#ifdef SCROLL_RESCUE_QA
unsigned run_qa(HINSTANCE instance, const wchar_t* path, unsigned percent, bool live) {
    if (percent < 60 || percent > 300 || !copy_text(app.snapshot, 32768, path)) return 2;
    app.qa = true; app.qa_live = live; app.scale = percent;
    return launch(instance);
}
#endif
