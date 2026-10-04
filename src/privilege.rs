use crate::{device::is_logitech_parent, wide};
use std::{mem::size_of, ptr};
use windows_sys::Win32::{
    Foundation::*,
    System::{Com::*, Threading::*},
    UI::{Shell::*, WindowsAndMessaging::*},
};

pub fn is_admin() -> bool {
    unsafe { IsUserAnAdmin() != 0 }
}

pub fn elevated_restart(ids: &[String], visible: bool) -> Result<u32, String> {
    if ids.is_empty() || ids.iter().any(|id| !is_logitech_parent(id)) {
        return Err("无效的罗技设备 ID。".into());
    }
    let executable = std::env::current_exe().map_err(|error| error.to_string())?;
    let file: Vec<u16> = executable
        .as_os_str()
        .encode_wide()
        .chain(Some(0))
        .collect();
    // Validated IDs cannot contain quotes, spaces, or shell metacharacters.
    let parameters = wide(&format!(
        "repair --no-elevate {}",
        ids.iter()
            .map(|id| format!("--device \"{id}\""))
            .collect::<Vec<_>>()
            .join(" ")
    ));
    let verb = wide("runas");
    let mut info = SHELLEXECUTEINFOW {
        cbSize: size_of::<SHELLEXECUTEINFOW>() as u32,
        fMask: SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI,
        lpVerb: verb.as_ptr(),
        lpFile: file.as_ptr(),
        lpParameters: parameters.as_ptr(),
        nShow: if visible { SW_SHOWNORMAL } else { SW_HIDE },
        ..unsafe { std::mem::zeroed() }
    };
    // SAFETY: All pointers stay alive until ShellExecuteExW has consumed them.
    unsafe {
        let initialized = CoInitializeEx(
            ptr::null(),
            (COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE) as u32,
        ) >= 0;
        if ShellExecuteExW(&mut info) == 0 {
            let error = GetLastError();
            if initialized {
                CoUninitialize();
            }
            return if error == ERROR_CANCELLED {
                Ok(4)
            } else {
                Err(format!("无法申请管理员权限（Windows 错误 {error}）。"))
            };
        }
        if initialized {
            CoUninitialize();
        }
        if info.hProcess.is_null() {
            return Err("无法读取管理员进程的状态。".into());
        }
        let wait = WaitForSingleObject(info.hProcess, (ids.len() as u32 + 1).saturating_mul(60000));
        if wait != WAIT_OBJECT_0 {
            CloseHandle(info.hProcess);
            return Err("等待管理员进程结果超时或失败。请等待设备恢复后再尝试。".into());
        }
        let mut code = 1;
        let success = GetExitCodeProcess(info.hProcess, &mut code);
        CloseHandle(info.hProcess);
        if success == 0 {
            return Err("无法读取管理员进程的退出码。".into());
        }
        Ok(code)
    }
}

use std::os::windows::ffi::OsStrExt;
