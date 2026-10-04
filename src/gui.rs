use crate::{
    cli::result_message,
    device::{self, Device},
    privilege, wide,
};
use std::{
    cell::RefCell,
    mem::size_of,
    ptr,
    sync::mpsc::{self, Receiver},
    thread,
};
use windows_sys::Win32::{
    Foundation::*,
    Graphics::{Dwm::*, Gdi::*},
    System::LibraryLoader::GetModuleHandleW,
    UI::{Controls::*, HiDpi::*, Input::KeyboardAndMouse::*, WindowsAndMessaging::*},
};

const REPAIR: usize = 101;
const REFRESH: usize = 102;
const DEVICES: usize = 103;
const WIDTH: i32 = 740;
const HEIGHT: i32 = 720;
const BG: u32 = rgb(15, 20, 29);
const CARD: u32 = rgb(24, 32, 45);
const BORDER: u32 = rgb(43, 55, 73);
const TEXT: u32 = rgb(235, 240, 248);
const MUTED: u32 = rgb(155, 170, 191);
const GREEN: u32 = rgb(105, 231, 178);
const AMBER: u32 = rgb(246, 195, 104);
const RED: u32 = rgb(255, 144, 144);

const fn rgb(r: u32, g: u32, b: u32) -> u32 {
    r | (g << 8) | (b << 16)
}

enum Event {
    Devices(Result<Vec<Device>, String>),
    Repair(Result<u32, String>),
}

struct Ui {
    scale: f64,
    devices: Vec<Device>,
    selected: Option<usize>,
    busy: bool,
    repairing: bool,
    headline: String,
    detail: String,
    color: u32,
    logs: Vec<String>,
    receiver: Option<Receiver<Event>>,
    combo: HWND,
    repair_button: HWND,
    refresh_button: HWND,
    font: HFONT,
    brush: HBRUSH,
    #[cfg(feature = "visual-qa")]
    qa_snapshot: Option<std::path::PathBuf>,
}

impl Ui {
    fn new(scale: f64) -> Self {
        Self {
            scale,
            devices: Vec::new(),
            selected: None,
            busy: false,
            repairing: false,
            headline: "正在查找设备".into(),
            detail: "请稍候，正在读取已连接的罗技 USB 设备。".into(),
            color: MUTED,
            logs: Vec::new(),
            receiver: None,
            combo: ptr::null_mut(),
            repair_button: ptr::null_mut(),
            refresh_button: ptr::null_mut(),
            font: ptr::null_mut(),
            brush: ptr::null_mut(),
            #[cfg(feature = "visual-qa")]
            qa_snapshot: None,
        }
    }
    fn n(&self, value: i32) -> i32 {
        (value as f64 * self.scale).round() as i32
    }
    fn log(&mut self, message: impl Into<String>) {
        self.logs.push(message.into());
        if self.logs.len() > 32 {
            self.logs.remove(0);
        }
    }
    fn refresh(&mut self) {
        self.busy = true;
        self.headline = "正在查找设备".into();
        self.detail = "请稍候，正在读取已连接的罗技 USB 设备。".into();
        self.color = MUTED;
        let (sender, receiver) = mpsc::channel();
        self.receiver = Some(receiver);
        thread::spawn(move || {
            let _ = sender.send(Event::Devices(device::list()));
        });
    }
    fn repair(&mut self) {
        let Some(index) = self.selected else {
            return;
        };
        let id = self.devices[index].id.clone();
        let name = self.devices[index].name.clone();
        self.busy = true;
        self.repairing = true;
        self.headline = "正在恢复设备".into();
        self.detail = "请允许 Windows 权限提示。鼠标会短暂断连，恢复后请测试滚轮。".into();
        self.color = AMBER;
        self.log(format!("开始恢复 {name}"));
        let (sender, receiver) = mpsc::channel();
        self.receiver = Some(receiver);
        thread::spawn(move || {
            let result = if privilege::is_admin() {
                device::restart(&id)
            } else {
                privilege::elevated_restart(&[id], false)
            };
            let _ = sender.send(Event::Repair(result));
        });
    }
    unsafe fn controls(&mut self, window: HWND) {
        unsafe {
            if !self.font.is_null() {
                DeleteObject(self.font);
            }
            self.font = make_font(self.n(16), 400);
            for handle in [self.combo, self.repair_button, self.refresh_button] {
                SendMessageW(handle, WM_SETFONT, self.font as usize, 0);
            }
            SendMessageW(
                self.combo,
                CB_SETITEMHEIGHT,
                usize::MAX,
                self.n(32) as isize,
            );
            SendMessageW(self.combo, CB_SETITEMHEIGHT, 0, self.n(32) as isize);
            MoveWindow(
                self.combo,
                self.n(54),
                self.n(214),
                self.n(632),
                self.n(240),
                1,
            );
            MoveWindow(
                self.repair_button,
                self.n(32),
                self.n(345),
                self.n(474),
                self.n(56),
                1,
            );
            MoveWindow(
                self.refresh_button,
                self.n(522),
                self.n(345),
                self.n(186),
                self.n(56),
                1,
            );
            EnableWindow(self.combo, (!self.busy && !self.devices.is_empty()) as i32);
            EnableWindow(
                self.repair_button,
                (!self.busy && self.selected.is_some()) as i32,
            );
            EnableWindow(self.refresh_button, (!self.busy) as i32);
            let system_menu = GetSystemMenu(window, 0);
            EnableMenuItem(
                system_menu,
                SC_CLOSE,
                MF_BYCOMMAND
                    | if self.repairing {
                        MF_GRAYED
                    } else {
                        MF_ENABLED
                    },
            );
            InvalidateRect(window, ptr::null(), 0);
            for handle in [self.combo, self.repair_button, self.refresh_button] {
                InvalidateRect(handle, ptr::null(), 0);
            }
        }
    }
    unsafe fn poll(&mut self, window: HWND) {
        let event = match self.receiver.as_ref().map(|receiver| receiver.try_recv()) {
            Some(Ok(event)) => event,
            Some(Err(mpsc::TryRecvError::Disconnected)) => {
                self.busy = false;
                self.repairing = false;
                self.receiver = None;
                self.headline = "操作未完成".into();
                self.detail = "操作意外结束，请刷新设备列表后重试。".into();
                self.color = RED;
                unsafe {
                    self.controls(window);
                }
                return;
            }
            _ => return,
        };
        self.busy = false;
        self.repairing = false;
        self.receiver = None;
        match event {
            Event::Devices(result) => {
                let previous = self
                    .selected
                    .and_then(|index| self.devices.get(index))
                    .map(|device| device.id.clone());
                self.selected = None;
                self.devices.clear();
                unsafe {
                    SendMessageW(self.combo, CB_RESETCONTENT, 0, 0);
                }
                let placeholder = wide("选择鼠标对应的 USB 设备…");
                unsafe {
                    SendMessageW(self.combo, CB_ADDSTRING, 0, placeholder.as_ptr() as isize);
                }
                match result {
                    Ok(devices) => {
                        self.devices = devices;
                        for device in &self.devices {
                            let label = wide(&format!(
                                "{}  ·  {}",
                                device.name,
                                if device.healthy {
                                    "在线"
                                } else {
                                    "状态异常"
                                }
                            ));
                            unsafe {
                                SendMessageW(self.combo, CB_ADDSTRING, 0, label.as_ptr() as isize);
                            }
                        }
                        self.selected = previous
                            .as_ref()
                            .and_then(|id| self.devices.iter().position(|device| &device.id == id));
                        if self.selected.is_none() && self.devices.len() == 1 {
                            self.selected = Some(0);
                        }
                        if self.devices.is_empty() {
                            self.headline = "未找到罗技 USB 设备".into();
                            self.detail = "请插入鼠标接收器，然后点击「刷新设备」。".into();
                            self.color = AMBER;
                        } else {
                            self.headline = if self.selected.is_some() {
                                "准备就绪"
                            } else {
                                "请选择设备"
                            }
                            .into();
                            self.detail =
                                "退出游戏后，选择鼠标对应的设备，点击「恢复滚轮」。".into();
                            self.color = GREEN;
                        }
                        self.log(format!("检测到 {} 个罗技 USB 设备", self.devices.len()));
                    }
                    Err(error) => {
                        self.headline = "设备查询失败".into();
                        self.detail = error;
                        self.color = RED;
                        self.log("查询失败，请刷新后重试");
                    }
                }
                unsafe {
                    SendMessageW(
                        self.combo,
                        CB_SETCURSEL,
                        self.selected.map_or(0, |index| index + 1),
                        0,
                    );
                }
            }
            Event::Repair(result) => match result {
                Ok(code) => {
                    self.headline = match code {
                        0 => "设备已恢复在线",
                        4 => "已取消操作",
                        7 => "需要重启电脑",
                        _ => "恢复未完成",
                    }
                    .into();
                    self.detail = result_message(code).into();
                    self.color = match code {
                        0 => GREEN,
                        4 | 6 | 7 => AMBER,
                        _ => RED,
                    };
                    self.log(match code {
                        0 => "设备重启完成，请实际测试滚轮",
                        4 => "已取消管理员授权",
                        _ => "操作结束，请查看上方结果",
                    });
                }
                Err(error) => {
                    self.headline = "恢复未完成".into();
                    self.detail = error;
                    self.color = RED;
                    self.log("操作失败，请查看上方结果");
                }
            },
        }
        unsafe {
            self.controls(window);
        }
    }
}

impl Drop for Ui {
    fn drop(&mut self) {
        unsafe {
            if !self.font.is_null() {
                DeleteObject(self.font);
            }
            if !self.brush.is_null() {
                DeleteObject(self.brush);
            }
        }
    }
}

unsafe fn make_font(height: i32, weight: i32) -> HFONT {
    let name = wide("Microsoft YaHei UI");
    unsafe {
        CreateFontW(
            -height,
            0,
            0,
            0,
            weight,
            0,
            0,
            0,
            DEFAULT_CHARSET as u32,
            OUT_DEFAULT_PRECIS as u32,
            CLIP_DEFAULT_PRECIS as u32,
            CLEARTYPE_QUALITY as u32,
            DEFAULT_PITCH as u32,
            name.as_ptr(),
        )
    }
}

unsafe fn card(dc: HDC, rect: RECT, color: u32, border: u32, radius: i32) {
    unsafe {
        let brush = CreateSolidBrush(color);
        let pen = CreatePen(PS_SOLID, 1, border);
        let old_brush = SelectObject(dc, brush);
        let old_pen = SelectObject(dc, pen);
        RoundRect(
            dc,
            rect.left,
            rect.top,
            rect.right,
            rect.bottom,
            radius,
            radius,
        );
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(brush);
        DeleteObject(pen);
    }
}

unsafe fn text(
    dc: HDC,
    value: &str,
    mut rect: RECT,
    height: i32,
    weight: i32,
    color: u32,
    flags: u32,
) {
    unsafe {
        let font = make_font(height, weight);
        let old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT as i32);
        SetTextColor(dc, color);
        let mut value = wide(value);
        DrawTextW(
            dc,
            value.as_mut_ptr(),
            (value.len() - 1) as i32,
            &mut rect,
            flags | DT_NOPREFIX,
        );
        SelectObject(dc, old);
        DeleteObject(font);
    }
}

fn rect(ui: &Ui, x: i32, y: i32, width: i32, height: i32) -> RECT {
    RECT {
        left: ui.n(x),
        top: ui.n(y),
        right: ui.n(x + width),
        bottom: ui.n(y + height),
    }
}

// Keep the small set of native drawing attributes together at each call site.
#[allow(clippy::too_many_arguments)]
unsafe fn button(
    dc: HDC,
    area: RECT,
    label: &str,
    primary: bool,
    enabled: bool,
    pressed: bool,
    focused: bool,
    scale: f64,
) {
    unsafe {
        let background = if !enabled {
            rgb(34, 44, 58)
        } else if primary {
            if pressed { rgb(73, 193, 144) } else { GREEN }
        } else if pressed {
            rgb(43, 56, 75)
        } else {
            CARD
        };
        card(
            dc,
            area,
            background,
            if focused {
                TEXT
            } else if primary && enabled {
                background
            } else {
                BORDER
            },
            (12.0 * scale) as i32,
        );
        text(
            dc,
            label,
            area,
            (18.0 * scale) as i32,
            600,
            if !enabled {
                MUTED
            } else if primary {
                rgb(9, 45, 32)
            } else {
                TEXT
            },
            DT_SINGLELINE | DT_CENTER | DT_VCENTER,
        );
    }
}

unsafe fn paint(dc: HDC, ui: &Ui, preview: bool) {
    unsafe {
        let background = CreateSolidBrush(BG);
        FillRect(dc, &rect(ui, 0, 0, WIDTH, HEIGHT), background);
        DeleteObject(background);
        text(
            dc,
            "SCROLL RESCUE",
            rect(ui, 32, 25, 500, 23),
            ui.n(13),
            700,
            GREEN,
            DT_SINGLELINE,
        );
        text(
            dc,
            "v0.1.0  /  WINDOWS",
            rect(ui, 510, 25, 198, 23),
            ui.n(12),
            400,
            MUTED,
            DT_SINGLELINE | DT_RIGHT,
        );
        text(
            dc,
            "让滚轮恢复正常。",
            rect(ui, 32, 59, 676, 50),
            ui.n(34),
            700,
            TEXT,
            DT_SINGLELINE,
        );
        text(
            dc,
            "瓦洛兰特结束后滚轮停不下来？试着恢复鼠标接收器。",
            rect(ui, 34, 117, 676, 26),
            ui.n(16),
            400,
            MUTED,
            DT_SINGLELINE,
        );
        card(dc, rect(ui, 32, 167, 676, 156), CARD, BORDER, ui.n(16));
        text(
            dc,
            "目标设备",
            rect(ui, 54, 184, 420, 23),
            ui.n(14),
            600,
            TEXT,
            DT_SINGLELINE,
        );
        text(
            dc,
            &format!("{} 个已连接", ui.devices.len()),
            rect(ui, 544, 184, 142, 23),
            ui.n(13),
            400,
            MUTED,
            DT_SINGLELINE | DT_RIGHT,
        );
        let device = ui.selected.and_then(|index| ui.devices.get(index));
        if preview {
            card(
                dc,
                rect(ui, 54, 214, 632, 33),
                rgb(32, 43, 59),
                BORDER,
                ui.n(6),
            );
            text(
                dc,
                device
                    .map(|device| device.name.as_str())
                    .unwrap_or("选择鼠标对应的 USB 设备…"),
                rect(ui, 65, 219, 598, 23),
                ui.n(16),
                400,
                TEXT,
                DT_SINGLELINE,
            );
            text(
                dc,
                "⌄",
                rect(ui, 650, 217, 23, 23),
                ui.n(16),
                400,
                MUTED,
                DT_SINGLELINE | DT_CENTER,
            );
        }
        text(
            dc,
            device
                .map(|device| device.id.as_str())
                .unwrap_or("等待选择设备"),
            rect(ui, 54, 260, 632, 20),
            ui.n(12),
            400,
            MUTED,
            DT_SINGLELINE | DT_END_ELLIPSIS,
        );
        text(
            dc,
            "请选择鼠标对应的设备；恢复时会短暂断连。",
            rect(ui, 54, 290, 632, 21),
            ui.n(13),
            400,
            MUTED,
            DT_SINGLELINE,
        );
        if preview {
            button(
                dc,
                rect(ui, 32, 345, 474, 56),
                "恢复滚轮",
                true,
                !ui.busy && ui.selected.is_some(),
                false,
                false,
                ui.scale,
            );
            button(
                dc,
                rect(ui, 522, 345, 186, 56),
                "刷新设备",
                false,
                !ui.busy,
                false,
                false,
                ui.scale,
            );
        }
        text(
            dc,
            "需要时申请管理员权限 · 建议退出游戏后使用",
            rect(ui, 32, 411, 676, 20),
            ui.n(12),
            400,
            MUTED,
            DT_SINGLELINE | DT_CENTER,
        );
        card(dc, rect(ui, 32, 448, 676, 104), CARD, BORDER, ui.n(14));
        let badge = CreateSolidBrush(ui.color);
        let old = SelectObject(dc, badge);
        let old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, ui.n(54), ui.n(468), ui.n(64), ui.n(478));
        SelectObject(dc, old);
        SelectObject(dc, old_pen);
        DeleteObject(badge);
        text(
            dc,
            &ui.headline,
            rect(ui, 76, 460, 610, 30),
            ui.n(19),
            600,
            ui.color,
            DT_SINGLELINE,
        );
        text(
            dc,
            &ui.detail,
            rect(ui, 54, 497, 632, 42),
            ui.n(14),
            400,
            MUTED,
            DT_WORDBREAK,
        );
        text(
            dc,
            "操作记录",
            rect(ui, 34, 576, 640, 22),
            ui.n(13),
            600,
            MUTED,
            DT_SINGLELINE,
        );
        for (index, log) in ui.logs.iter().rev().take(2).rev().enumerate() {
            text(
                dc,
                &format!("·  {log}"),
                rect(ui, 34, 607 + index as i32 * 26, 672, 24),
                ui.n(13),
                400,
                TEXT,
                DT_SINGLELINE | DT_END_ELLIPSIS,
            );
        }
        text(
            dc,
            "按 Tab 选择按钮 · 按 Enter 执行 · 命令行与 BAT 同时保留",
            rect(ui, 32, 681, 676, 22),
            ui.n(12),
            400,
            MUTED,
            DT_SINGLELINE | DT_CENTER,
        );
    }
}

unsafe extern "system" fn window_proc(
    window: HWND,
    message: u32,
    wparam: usize,
    lparam: isize,
) -> isize {
    unsafe {
        if message == WM_NCCREATE {
            let create = &*(lparam as *const CREATESTRUCTW);
            SetWindowLongPtrW(window, GWLP_USERDATA, create.lpCreateParams as isize);
        }
        if message == WM_DESTROY {
            KillTimer(window, 1);
            KillTimer(window, 2);
            PostQuitMessage(0);
            return 0;
        }
        if message == WM_NCDESTROY {
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            return DefWindowProcW(window, message, wparam, lparam);
        }
        if message == WM_ERASEBKGND {
            return 1;
        }
        if !matches!(
            message,
            WM_CREATE
                | WM_TIMER
                | WM_COMMAND
                | WM_DRAWITEM
                | WM_CTLCOLORLISTBOX
                | WM_CTLCOLOREDIT
                | WM_CTLCOLORSTATIC
                | WM_PAINT
                | WM_PRINTCLIENT
                | WM_DPICHANGED
                | WM_CLOSE
        ) {
            return DefWindowProcW(window, message, wparam, lparam);
        }
        let state = GetWindowLongPtrW(window, GWLP_USERDATA) as *const RefCell<Ui>;
        if state.is_null() {
            return DefWindowProcW(window, message, wparam, lparam);
        }
        // Native controls can synchronously send messages back to their parent.
        // A checked borrow prevents reentrant callbacks from aliasing mutable state.
        let Ok(mut ui) = (&*state).try_borrow_mut() else {
            return DefWindowProcW(window, message, wparam, lparam);
        };
        match message {
            WM_CREATE => {
                let instance = GetModuleHandleW(ptr::null());
                let combo_class = wide("COMBOBOX");
                let button_class = wide("BUTTON");
                ui.brush = CreateSolidBrush(rgb(32, 43, 59));
                ui.combo = CreateWindowExW(
                    0,
                    combo_class.as_ptr(),
                    ptr::null(),
                    WS_CHILD
                        | WS_VISIBLE
                        | WS_TABSTOP
                        | WS_VSCROLL
                        | CBS_DROPDOWNLIST as u32
                        | CBS_OWNERDRAWFIXED as u32
                        | CBS_HASSTRINGS as u32,
                    0,
                    0,
                    0,
                    0,
                    window,
                    DEVICES as HMENU,
                    instance,
                    ptr::null(),
                );
                ui.repair_button = CreateWindowExW(
                    0,
                    button_class.as_ptr(),
                    wide("恢复滚轮").as_ptr(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW as u32,
                    0,
                    0,
                    0,
                    0,
                    window,
                    REPAIR as HMENU,
                    instance,
                    ptr::null(),
                );
                ui.refresh_button = CreateWindowExW(
                    0,
                    button_class.as_ptr(),
                    wide("刷新设备").as_ptr(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW as u32,
                    0,
                    0,
                    0,
                    0,
                    window,
                    REFRESH as HMENU,
                    instance,
                    ptr::null(),
                );
                if ui.combo.is_null() || ui.repair_button.is_null() || ui.refresh_button.is_null() {
                    return -1;
                }
                SetWindowTheme(ui.combo, wide("DarkMode_Explorer").as_ptr(), ptr::null());
                SendMessageW(ui.combo, CB_SETMINVISIBLE, 8, 0);
                ui.refresh();
                ui.controls(window);
                SetTimer(window, 1, 100, None);
                0
            }
            WM_TIMER => {
                ui.poll(window);
                #[cfg(feature = "visual-qa")]
                {
                    if wparam == 2 {
                        DestroyWindow(window);
                        return 0;
                    }
                    if !ui.busy
                        && let Some(path) = ui.qa_snapshot.take()
                    {
                        drop(ui);
                        let _ = snapshot_window(window, &path);
                        DestroyWindow(window);
                    }
                }
                0
            }
            WM_COMMAND => {
                if ui.busy {
                    return 0;
                }
                let id = wparam & 0xffff;
                let notification = (wparam >> 16) & 0xffff;
                match id {
                    REPAIR if notification == BN_CLICKED as usize => ui.repair(),
                    REFRESH if notification == BN_CLICKED as usize => ui.refresh(),
                    DEVICES if notification == CBN_SELCHANGE as usize => {
                        let index = SendMessageW(ui.combo, CB_GETCURSEL, 0, 0);
                        ui.selected = if index > 0 {
                            Some(index as usize - 1)
                        } else {
                            None
                        };
                        ui.headline = if ui.selected.is_some() {
                            "准备就绪"
                        } else {
                            "请选择设备"
                        }
                        .into();
                        ui.detail = "退出游戏后，选择鼠标对应的设备，点击「恢复滚轮」。".into();
                        ui.color = GREEN;
                    }
                    _ => {}
                }
                ui.controls(window);
                0
            }
            WM_DRAWITEM => {
                let item = &*(lparam as *const DRAWITEMSTRUCT);
                if item.CtlID as usize == DEVICES {
                    let color = if item.itemState & ODS_SELECTED != 0 {
                        rgb(43, 62, 76)
                    } else {
                        rgb(32, 43, 59)
                    };
                    let brush = CreateSolidBrush(color);
                    FillRect(item.hDC, &item.rcItem, brush);
                    DeleteObject(brush);
                    let label = if item.itemID > 0 && item.itemID != u32::MAX {
                        ui.devices
                            .get(item.itemID as usize - 1)
                            .map(|device| {
                                format!(
                                    "{}  ·  {}",
                                    device.name,
                                    if device.healthy {
                                        "在线"
                                    } else {
                                        "状态异常"
                                    }
                                )
                            })
                            .unwrap_or_default()
                    } else {
                        "选择鼠标对应的 USB 设备…".into()
                    };
                    let mut area = item.rcItem;
                    area.left += ui.n(10);
                    area.right -= ui.n(6);
                    text(
                        item.hDC,
                        &label,
                        area,
                        ui.n(16),
                        400,
                        TEXT,
                        DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS,
                    );
                    if item.itemState & ODS_FOCUS != 0 {
                        DrawFocusRect(item.hDC, &item.rcItem);
                    }
                    return 1;
                }
                let primary = item.CtlID as usize == REPAIR;
                let label = if primary {
                    if ui.repairing {
                        "正在恢复…"
                    } else {
                        "恢复滚轮"
                    }
                } else if ui.busy && !ui.repairing {
                    "正在扫描…"
                } else {
                    "刷新设备"
                };
                button(
                    item.hDC,
                    item.rcItem,
                    label,
                    primary,
                    item.itemState & ODS_DISABLED == 0,
                    item.itemState & ODS_SELECTED != 0,
                    item.itemState & ODS_FOCUS != 0,
                    ui.scale,
                );
                1
            }
            WM_CTLCOLORLISTBOX | WM_CTLCOLOREDIT | WM_CTLCOLORSTATIC => {
                let dc = wparam as HDC;
                SetTextColor(dc, TEXT);
                SetBkColor(dc, rgb(32, 43, 59));
                ui.brush as isize
            }
            WM_PAINT => {
                let mut info: PAINTSTRUCT = std::mem::zeroed();
                let dc = BeginPaint(window, &mut info);
                let memory = CreateCompatibleDC(dc);
                let bitmap = CreateCompatibleBitmap(dc, ui.n(WIDTH), ui.n(HEIGHT));
                if !memory.is_null() && !bitmap.is_null() {
                    let old = SelectObject(memory, bitmap);
                    paint(memory, &ui, false);
                    BitBlt(dc, 0, 0, ui.n(WIDTH), ui.n(HEIGHT), memory, 0, 0, SRCCOPY);
                    SelectObject(memory, old);
                } else {
                    paint(dc, &ui, false);
                }
                if !bitmap.is_null() {
                    DeleteObject(bitmap);
                }
                if !memory.is_null() {
                    DeleteDC(memory);
                }
                EndPaint(window, &info);
                0
            }
            WM_PRINTCLIENT => {
                paint(wparam as HDC, &ui, false);
                0
            }
            WM_DPICHANGED => {
                ui.scale = (wparam & 0xffff) as f64 / 96.0;
                let proposed = &*(lparam as *const RECT);
                let mut area = rect(&ui, 0, 0, WIDTH, HEIGHT);
                AdjustWindowRectExForDpi(
                    &mut area,
                    window_style(),
                    0,
                    WS_EX_CONTROLPARENT,
                    (wparam & 0xffff) as u32,
                );
                SetWindowPos(
                    window,
                    ptr::null_mut(),
                    proposed.left,
                    proposed.top,
                    area.right - area.left,
                    area.bottom - area.top,
                    SWP_NOZORDER | SWP_NOACTIVATE,
                );
                ui.controls(window);
                0
            }
            WM_CLOSE if ui.repairing => 0,
            WM_DESTROY => {
                KillTimer(window, 1);
                PostQuitMessage(0);
                0
            }
            _ => {
                drop(ui);
                DefWindowProcW(window, message, wparam, lparam)
            }
        }
    }
}

fn window_style() -> u32 {
    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN
}

pub fn run() -> Result<(), String> {
    run_internal(None)
}

fn run_internal(qa_snapshot: Option<&std::path::Path>) -> Result<(), String> {
    unsafe {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        let dpi = GetDpiForSystem();
        let mut work: RECT = std::mem::zeroed();
        SystemParametersInfoW(SPI_GETWORKAREA, 0, (&mut work as *mut RECT).cast(), 0);
        let requested = dpi as f64 / 96.0;
        let scale = if work.bottom > work.top {
            requested
                .min((work.bottom - work.top - 64) as f64 / HEIGHT as f64)
                .min((work.right - work.left - 64) as f64 / WIDTH as f64)
                .max(0.6)
        } else {
            requested
        };
        let ui = Box::new(RefCell::new(Ui::new(scale)));
        #[cfg(feature = "visual-qa")]
        {
            ui.borrow_mut().qa_snapshot = qa_snapshot.map(std::path::Path::to_path_buf);
        }
        let instance = GetModuleHandleW(ptr::null());
        let class_name = wide("LogitechScrollRescueWindow");
        let class = WNDCLASSW {
            lpfnWndProc: Some(window_proc),
            hInstance: instance,
            lpszClassName: class_name.as_ptr(),
            hCursor: LoadCursorW(ptr::null_mut(), IDC_ARROW),
            hIcon: LoadIconW(ptr::null_mut(), IDI_APPLICATION),
            ..std::mem::zeroed()
        };
        if RegisterClassW(&class) == 0 {
            return Err("无法注册应用窗口。".into());
        }
        let mut area = rect(&ui.borrow(), 0, 0, WIDTH, HEIGHT);
        AdjustWindowRectExForDpi(&mut area, window_style(), 0, WS_EX_CONTROLPARENT, dpi);
        let width = area.right - area.left;
        let height = area.bottom - area.top;
        let window = CreateWindowExW(
            WS_EX_CONTROLPARENT,
            class_name.as_ptr(),
            wide("Scroll Rescue · 罗技滚轮恢复").as_ptr(),
            window_style(),
            work.left + (work.right - work.left - width) / 2,
            work.top + (work.bottom - work.top - height) / 2,
            width,
            height,
            ptr::null_mut(),
            ptr::null_mut(),
            instance,
            (&*ui as *const RefCell<Ui>).cast(),
        );
        if window.is_null() {
            return Err("无法创建应用窗口。".into());
        }
        let dark: i32 = 1;
        DwmSetWindowAttribute(
            window,
            DWMWA_USE_IMMERSIVE_DARK_MODE as u32,
            (&dark as *const i32).cast(),
            size_of::<i32>() as u32,
        );
        if qa_snapshot.is_none() {
            ShowWindow(window, SW_SHOWNORMAL);
            UpdateWindow(window);
        } else {
            SetTimer(window, 2, 10000, None);
        }
        let mut message: MSG = std::mem::zeroed();
        loop {
            let result = GetMessageW(&mut message, ptr::null_mut(), 0, 0);
            if result == 0 {
                break;
            }
            if result == -1 {
                DestroyWindow(window);
                return Err("窗口消息读取失败。".into());
            }
            let action = {
                let ui = ui.borrow();
                let focus = GetFocus();
                if message.message == WM_KEYDOWN && message.wParam == 13 && !ui.busy {
                    if focus == ui.repair_button {
                        Some(REPAIR)
                    } else if focus == ui.refresh_button {
                        Some(REFRESH)
                    } else {
                        None
                    }
                } else {
                    None
                }
            };
            if let Some(action) = action {
                SendMessageW(window, WM_COMMAND, action, 0);
                continue;
            }
            if IsDialogMessageW(window, &message) == 0 {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        Ok(())
    }
}

// A render-only harness for visual QA; it never enumerates or restarts hardware.
#[cfg(feature = "visual-qa")]
pub fn run_hidden_smoke(path: &std::path::Path) -> Result<(), String> {
    run_internal(Some(path))?;
    if !path.exists() {
        return Err("窗口未完成设备识别或渲染。".into());
    }
    Ok(())
}

#[cfg(feature = "visual-qa")]
unsafe fn snapshot_window(window: HWND, path: &std::path::Path) -> Result<(), String> {
    unsafe {
        let mut area: RECT = std::mem::zeroed();
        GetWindowRect(window, &mut area);
        let width = area.right - area.left;
        let height = area.bottom - area.top;
        let screen = GetDC(window);
        let dc = CreateCompatibleDC(screen);
        let bitmap = CreateCompatibleBitmap(screen, width, height);
        ReleaseDC(window, screen);
        if dc.is_null() || bitmap.is_null() {
            return Err("窗口预览创建失败。".into());
        }
        let old = SelectObject(dc, bitmap);
        SendMessageW(
            window,
            WM_PRINT,
            dc as usize,
            (PRF_CLIENT | PRF_CHILDREN | PRF_NONCLIENT | PRF_ERASEBKGND) as isize,
        );
        SelectObject(dc, old);
        let result = save_bitmap(dc, bitmap, width, height, path);
        DeleteObject(bitmap);
        DeleteDC(dc);
        result
    }
}

#[cfg(feature = "visual-qa")]
pub fn render_preview(path: &std::path::Path, scale: f64) -> Result<(), String> {
    if !(0.5..=3.0).contains(&scale) {
        return Err("无效预览比例。".into());
    }
    let mut ui = Ui::new(scale);
    ui.devices.push(Device {
        id: "USB\\VID_046D&PID_C54D\\DEMO".into(),
        name: "LIGHTSPEED Receiver".into(),
        healthy: true,
    });
    ui.selected = Some(0);
    ui.headline = "准备就绪".into();
    ui.detail = "退出游戏后，选择鼠标对应的设备，点击「恢复滚轮」。".into();
    ui.color = GREEN;
    ui.log("检测到 1 个罗技 USB 设备");
    let width = ui.n(WIDTH);
    let height = ui.n(HEIGHT);
    unsafe {
        let screen = GetDC(ptr::null_mut());
        if screen.is_null() {
            return Err("无法创建设备上下文。".into());
        }
        let dc = CreateCompatibleDC(screen);
        let bitmap = CreateCompatibleBitmap(screen, width, height);
        ReleaseDC(ptr::null_mut(), screen);
        if dc.is_null() || bitmap.is_null() {
            if !dc.is_null() {
                DeleteDC(dc);
            }
            if !bitmap.is_null() {
                DeleteObject(bitmap);
            }
            return Err("无法创建预览图。".into());
        }
        let old = SelectObject(dc, bitmap);
        paint(dc, &ui, true);
        SelectObject(dc, old);
        let result = save_bitmap(dc, bitmap, width, height, path);
        DeleteObject(bitmap);
        DeleteDC(dc);
        result
    }
}

#[cfg(feature = "visual-qa")]
unsafe fn save_bitmap(
    dc: HDC,
    bitmap: HBITMAP,
    width: i32,
    height: i32,
    path: &std::path::Path,
) -> Result<(), String> {
    use std::io::Write;
    unsafe {
        let mut info: BITMAPINFO = std::mem::zeroed();
        info.bmiHeader.biSize = size_of::<BITMAPINFOHEADER>() as u32;
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        let mut pixels = vec![0_u8; width as usize * height as usize * 4];
        let lines = GetDIBits(
            dc,
            bitmap,
            0,
            height as u32,
            pixels.as_mut_ptr().cast(),
            &mut info,
            DIB_RGB_COLORS,
        );
        if lines != height {
            return Err("无法读取预览像素。".into());
        }
        if pixels.iter().all(|value| *value == 0) {
            return Err("窗口预览为空。".into());
        }
        let mut file = std::fs::File::create(path).map_err(|error| error.to_string())?;
        file.write_all(b"BM")
            .and_then(|_| file.write_all(&((54 + pixels.len()) as u32).to_le_bytes()))
            .and_then(|_| file.write_all(&[0; 4]))
            .and_then(|_| file.write_all(&54_u32.to_le_bytes()))
            .map_err(|error| error.to_string())?;
        let header = std::slice::from_raw_parts(
            (&info.bmiHeader as *const BITMAPINFOHEADER).cast::<u8>(),
            40,
        );
        file.write_all(header)
            .and_then(|_| file.write_all(&pixels))
            .map_err(|error| error.to_string())?;
    }
    Ok(())
}
