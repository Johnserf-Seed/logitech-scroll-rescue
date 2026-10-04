use crate::wide;
use std::{
    os::windows::process::CommandExt,
    path::PathBuf,
    process::Command,
    ptr, thread,
    time::{Duration, Instant},
};
use windows_sys::Win32::Devices::DeviceAndDriverInstallation::*;

#[derive(Clone, Debug)]
pub struct Device {
    pub id: String,
    pub name: String,
    pub healthy: bool,
}

// Limit the operation to physical Logitech USB nodes, as in the original BAT.
// Strict instance-ID syntax also keeps the UAC helper's argument quoting simple.
pub fn is_logitech_parent(id: &str) -> bool {
    if !id
        .bytes()
        .all(|b| b.is_ascii_alphanumeric() || b"\\&_-".contains(&b))
    {
        return false;
    }
    let upper = id.to_ascii_uppercase();
    let parts: Vec<_> = upper.split('\\').collect();
    parts.len() == 3
        && parts[0] == "USB"
        && parts[1].starts_with("VID_046D&PID_")
        && parts[1].len() >= 17
        && parts[1][13..17].bytes().all(|b| b.is_ascii_hexdigit())
        && !parts[1].contains("&MI_")
        && !parts[2].is_empty()
}

fn property(node: u32, property: u32) -> String {
    let mut buffer = [0_u16; 512];
    let mut bytes = (buffer.len() * 2) as u32;
    // SAFETY: The API receives an aligned, writable UTF-16 buffer of `bytes` length.
    let result = unsafe {
        CM_Get_DevNode_Registry_PropertyW(
            node,
            property,
            ptr::null_mut(),
            buffer.as_mut_ptr().cast(),
            &mut bytes,
            0,
        )
    };
    if result != CR_SUCCESS {
        return String::new();
    }
    String::from_utf16_lossy(&buffer[..buffer.iter().position(|v| *v == 0).unwrap_or(buffer.len())])
}

pub fn list() -> Result<Vec<Device>, String> {
    let class = wide("{36fc9e60-c465-11cf-8056-444553540000}");
    let flags = CM_GETIDLIST_FILTER_PRESENT | CM_GETIDLIST_FILTER_CLASS;
    // Device hotplug can change the required size between these two API calls.
    for _ in 0..4 {
        let mut size = 0;
        let result = unsafe { CM_Get_Device_ID_List_SizeW(&mut size, class.as_ptr(), flags) };
        if result != CR_SUCCESS {
            return Err(format!("无法读取 USB 设备列表（Windows 错误 {result}）。"));
        }
        let mut buffer = vec![0_u16; size.max(2) as usize];
        let result = unsafe {
            CM_Get_Device_ID_ListW(
                class.as_ptr(),
                buffer.as_mut_ptr(),
                buffer.len() as u32,
                flags,
            )
        };
        if result == CR_BUFFER_SMALL {
            continue;
        }
        if result != CR_SUCCESS {
            return Err(format!("无法读取 USB 设备列表（Windows 错误 {result}）。"));
        }
        let mut devices = Vec::new();
        for encoded in buffer.split(|v| *v == 0).filter(|v| !v.is_empty()) {
            let id = String::from_utf16_lossy(encoded);
            if !is_logitech_parent(&id) {
                continue;
            }
            let mut node = 0;
            let mut id_wide = wide(&id);
            if unsafe { CM_Locate_DevNodeW(&mut node, id_wide.as_mut_ptr(), 0) } != CR_SUCCESS {
                continue;
            }
            let mut status = 0;
            let mut problem = 0;
            let healthy = unsafe { CM_Get_DevNode_Status(&mut status, &mut problem, node, 0) }
                == CR_SUCCESS
                && problem == 0;
            let friendly = property(node, CM_DRP_FRIENDLYNAME);
            let description = property(node, CM_DRP_DEVICEDESC);
            let name = if !friendly.is_empty() {
                friendly
            } else if !description.is_empty() {
                description
            } else {
                "Logitech USB device".into()
            };
            devices.push(Device { id, name, healthy });
        }
        devices.sort_by(|a, b| a.id.cmp(&b.id));
        return Ok(devices);
    }
    Err("USB 设备列表正在变化，请稍后刷新。".into())
}

pub fn select(devices: &[Device], ids: &[String], all: bool) -> Result<Vec<Device>, String> {
    if all && !ids.is_empty() {
        return Err("--all 与 --device 不能同时使用。".into());
    }
    if devices.is_empty() {
        return Err("未找到已连接的罗技 USB 设备。".into());
    }
    if all {
        return Ok(devices.to_vec());
    }
    if ids.is_empty() {
        return if devices.len() == 1 {
            Ok(devices.to_vec())
        } else {
            Err("发现多个罗技 USB 设备。请使用 --device 指定设备，或使用 --all。".into())
        };
    }
    let mut selected: Vec<Device> = Vec::new();
    for id in ids {
        if !is_logitech_parent(id) {
            return Err("设备 ID 必须是罗技 USB 主设备，不能是 HID、接口节点或其他品牌。".into());
        }
        let found = devices
            .iter()
            .find(|device| device.id.eq_ignore_ascii_case(id))
            .ok_or_else(|| "指定设备已断开或不可用，请重新查看设备列表。".to_string())?;
        if !selected.iter().any(|device| device.id == found.id) {
            selected.push(found.clone());
        }
    }
    Ok(selected)
}

fn system_program(name: &str) -> Result<PathBuf, String> {
    let mut buffer = [0_u16; 32768];
    let length = unsafe {
        windows_sys::Win32::System::SystemInformation::GetSystemDirectoryW(
            buffer.as_mut_ptr(),
            buffer.len() as u32,
        )
    };
    if length == 0 || length as usize >= buffer.len() {
        return Err("无法定位 Windows 系统目录。".into());
    }
    Ok(PathBuf::from(String::from_utf16_lossy(&buffer[..length as usize])).join(name))
}

pub fn restart(id: &str) -> Result<u32, String> {
    // Re-enumerate immediately before execution; never accept arbitrary PnP IDs.
    select(&list()?, &[id.to_string()], false)?;
    let mut child = Command::new(system_program("pnputil.exe")?)
        .args(["/restart-device", id])
        .creation_flags(0x08000000)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
        .map_err(|error| format!("无法启动设备重启：{error}"))?;
    let deadline = Instant::now() + Duration::from_secs(45);
    let status = loop {
        if let Some(status) = child
            .try_wait()
            .map_err(|error| format!("无法读取重启结果：{error}"))?
        {
            break status;
        }
        if Instant::now() >= deadline {
            let _ = child.kill();
            let _ = child.wait();
            return Err("设备重启超时。请检查设备连接，再刷新列表。".into());
        }
        thread::sleep(Duration::from_millis(100));
    };
    let code = status.code().unwrap_or(1) as u32;
    if code == 3010 {
        return Ok(7);
    }
    if code != 0 {
        return Err(format!("Windows 未完成设备重启（退出码 {code}）。"));
    }
    thread::sleep(Duration::from_millis(700));
    let deadline = Instant::now() + Duration::from_secs(8);
    loop {
        if list()?
            .iter()
            .any(|device| device.id.eq_ignore_ascii_case(id) && device.healthy)
        {
            return Ok(0);
        }
        if Instant::now() >= deadline {
            return Ok(6);
        }
        thread::sleep(Duration::from_millis(300));
    }
}
