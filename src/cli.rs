use crate::{device, privilege};

const HELP: &str = "Scroll Rescue 0.1.0 — 罗技滚轮恢复工具\n\n使用方法：\n  scroll-rescue-cli devices [--json]\n  scroll-rescue-cli repair [--device <ID> | --all] [--dry-run] [--no-elevate]\n\n  devices          查看已连接的罗技 USB 主设备\n  --device <ID>    指定设备，可重复使用\n  --all            重启所有列出的罗技 USB 主设备\n  --dry-run        只显示设备及命令\n  --no-elevate     缺少管理员权限时直接退出\n\n只有一个设备时 repair 自动选择；多个设备时需明确指定。\n设备重启期间鼠标会短暂断连。完成后请测试滚轮。\n";

fn json_string(value: &str) -> String {
    let mut result = String::from("\"");
    for character in value.chars() {
        match character {
            '"' => result.push_str("\\\""),
            '\\' => result.push_str("\\\\"),
            '\n' => result.push_str("\\n"),
            '\r' => result.push_str("\\r"),
            '\t' => result.push_str("\\t"),
            ch if ch.is_control() => result.push_str(&format!("\\u{:04x}", ch as u32)),
            ch => result.push(ch),
        }
    }
    result.push('"');
    result
}

pub fn result_message(code: u32) -> &'static str {
    match code {
        0 => "设备已重启并恢复在线。请滚动鼠标，确认问题是否解决。",
        2 => "设备查询或参数验证失败，请刷新设备列表。",
        3 => "未找到已连接的罗技 USB 设备，请检查接收器。",
        4 => "已取消管理员授权，未执行设备重启。",
        5 => "需要管理员权限才能重启设备。",
        6 => "Windows 已完成重启请求，但设备尚未恢复在线，请检查连接。",
        7 => "Windows 要求重启电脑后完成设备恢复，请保存工作后手动重启。",
        _ => "设备重启失败。请检查连接和管理员权限后重试。",
    }
}

pub fn run(args: &[String]) -> i32 {
    if args.is_empty() || matches!(args[0].as_str(), "-h" | "--help" | "help") {
        println!("{HELP}");
        return 0;
    }
    if args[0] == "--version" {
        println!("Scroll Rescue {}", env!("CARGO_PKG_VERSION"));
        return 0;
    }
    let command = args[0].as_str();
    if !matches!(command, "devices" | "repair") {
        eprintln!("未知命令：{command}\n{HELP}");
        return 2;
    }
    let (mut ids, mut all, mut dry_run, mut no_elevate, mut json) =
        (Vec::new(), false, false, false, false);
    let mut index = 1;
    while index < args.len() {
        match args[index].as_str() {
            "--json" if command == "devices" => json = true,
            "--all" if command == "repair" => all = true,
            "--dry-run" if command == "repair" => dry_run = true,
            "--no-elevate" if command == "repair" => no_elevate = true,
            "--device" if command == "repair" => {
                index += 1;
                if index == args.len() {
                    eprintln!("--device 后需要提供设备 ID。");
                    return 2;
                }
                ids.push(args[index].clone());
            }
            "--help" | "-h" => {
                println!("{HELP}");
                return 0;
            }
            other => {
                eprintln!("无效参数：{other}");
                return 2;
            }
        }
        index += 1;
    }
    // Fail malformed input before querying or requesting privileges.
    if (all && !ids.is_empty()) || ids.iter().any(|id| !device::is_logitech_parent(id)) {
        eprintln!("参数冲突或设备 ID 无效。请使用 devices 获取设备 ID。");
        return 2;
    }
    let devices = match device::list() {
        Ok(devices) => devices,
        Err(error) => {
            eprintln!("{error}");
            return 2;
        }
    };
    if command == "devices" {
        if json {
            println!(
                "[{}]",
                devices
                    .iter()
                    .map(|device| format!(
                        "{{\"id\":{},\"name\":{},\"healthy\":{}}}",
                        json_string(&device.id),
                        json_string(&device.name),
                        device.healthy
                    ))
                    .collect::<Vec<_>>()
                    .join(",")
            );
        } else if devices.is_empty() {
            println!("未找到已连接的罗技 USB 设备。");
        } else {
            for device in &devices {
                println!(
                    "{} [{}]\n  {}",
                    device.name,
                    if device.healthy { "在线" } else { "异常" },
                    device.id
                );
            }
        }
        return 0;
    }
    if devices.is_empty() {
        eprintln!("{}", result_message(3));
        return 3;
    }
    let selected = match device::select(&devices, &ids, all) {
        Ok(selected) => selected,
        Err(error) => {
            eprintln!("{error}");
            return 2;
        }
    };
    if dry_run {
        for device in &selected {
            println!("{}\npnputil /restart-device \"{}\"", device.name, device.id);
        }
        println!("预览完成，未执行设备重启。");
        return 0;
    }
    let code = if privilege::is_admin() {
        let mut outcome = 0;
        for device in &selected {
            println!("正在重启：{}", device.name);
            match device::restart(&device.id) {
                Ok(0) => {}
                Ok(code) => {
                    outcome = code;
                    if code != 7 {
                        break;
                    }
                }
                Err(error) => {
                    eprintln!("{error}");
                    return 1;
                }
            }
        }
        outcome
    } else if no_elevate {
        5
    } else {
        println!("请在 Windows 权限提示中允许重启设备。");
        match privilege::elevated_restart(
            &selected
                .iter()
                .map(|device| device.id.clone())
                .collect::<Vec<_>>(),
            true,
        ) {
            Ok(code) => code,
            Err(error) => {
                eprintln!("{error}");
                return 1;
            }
        }
    };
    println!("{}", result_message(code));
    code as i32
}
