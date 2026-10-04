# Scroll Rescue · 罗技滚轮恢复

一个 Windows 小工具，用来尝试恢复退出瓦洛兰特后持续滚动的罗技鼠标。提供中文 GUI、命令行和 BAT 入口。

恢复操作会重启所选罗技 USB 设备，鼠标可能短暂断连。完成后请实际测试滚轮；工具会报告设备重启结果，不能自动判断滚轮症状是否消失。

## 使用 GUI

1. 解压便携包，双击 `scroll-rescue.exe`。
2. 确认选中的是鼠标对应的 USB 接收器。只有一个罗技 USB 设备时会自动选中；多个设备时请自行选择。
3. 退出游戏，点击 **恢复滚轮**，允许 Windows 管理员权限提示。
4. 等待设备恢复在线，再测试滚轮。

点击 **刷新设备** 可重新查找接收器。支持 Tab 切换控件、Enter 执行已选按钮。

支持 Windows 10 2004 及以上版本、Windows 11，x64。只适用于 USB 连接的罗技设备；蓝牙设备不在本工具的恢复范围内。

## 命令行

在解压目录打开终端：

```powershell
# 查看设备
.\scroll-rescue-cli.exe devices
.\scroll-rescue-cli.exe devices --json

# 预览将执行的命令
.\scroll-rescue-cli.exe repair --dry-run

# 只有一个设备时自动选择，并按需申请管理员权限
.\scroll-rescue-cli.exe repair

# 多个设备时，使用 devices 返回的完整 ID
.\scroll-rescue-cli.exe repair --device 'USB\VID_046D&PID_XXXX\YOUR_DEVICE_ID'

# 明确重启所有已列出的罗技 USB 设备
.\scroll-rescue-cli.exe repair --all

# 用于已经以管理员身份运行的终端或自动化
.\scroll-rescue-cli.exe repair --no-elevate

.\scroll-rescue-cli.exe --help
```

`--dry-run` 只显示目标和 `pnputil /restart-device` 命令。`--device` 可重复使用，指定多个设备。未授权管理员权限时，重启操作不会执行。

GUI 程序也接受相同命令参数；终端中建议使用 `scroll-rescue-cli.exe`，以便正常等待操作并读取退出码。

| 退出码 | 含义 |
| --- | --- |
| 0 | 查询 / 预览成功，或设备重启后恢复在线 |
| 1 | 设备重启、权限申请或进程执行失败 |
| 2 | 参数无效或设备查询失败 |
| 3 | 没有已连接的罗技 USB 设备可恢复 |
| 4 | 用户取消管理员授权 |
| 5 | 需要管理员权限，且指定了 `--no-elevate` |
| 6 | 重启请求完成，但设备尚未恢复在线 |
| 7 | Windows 要求重启电脑后完成恢复 |

## 保留 BAT

便携包中的 `reset.bat` 调用同目录的命令行程序，可直接双击：

```bat
reset.bat
reset.bat --dry-run
reset.bat --all
```

原始独立脚本原样保存在 `legacy/reset-original.bat`，即使没有 Rust 程序也可使用。原始脚本会重启所有符合条件的罗技 USB 主设备；新入口在多个设备时要求明确选择。

原始修复命令仍可在管理员终端使用：

```powershell
pnputil /restart-device '从设备列表复制的完整设备 ID'
```

## 从源码构建

需要 Rust stable 的 `x86_64-pc-windows-msvc` 工具链，以及 Visual Studio Build Tools 的 C++ 桌面开发组件。

```powershell
cargo build --release --locked
.\scripts\package.ps1
```

生成文件：

- `target/release/scroll-rescue.exe`：图形界面。
- `target/release/scroll-rescue-cli.exe`：命令行。
- `dist/scroll-rescue-windows-x64.zip`：便携包。

程序无需安装，双击即可运行。首次恢复操作可能出现 Windows 管理员授权提示。

这是独立工具，与 Logitech 或 Riot Games 无关联。基于已有 BAT 的恢复步骤制作，并未确认症状的具体原因。

Windows 设备重启命令说明：[Microsoft PnPUtil 文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax#restart-device)。
