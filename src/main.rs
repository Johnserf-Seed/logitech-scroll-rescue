#![windows_subsystem = "windows"]

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if !args.is_empty() {
        // The elevated helper shares the same validation and exit codes as the CLI.
        unsafe {
            windows_sys::Win32::System::Console::AttachConsole(u32::MAX);
        }
        std::process::exit(logitech_scroll_rescue::cli::run(&args));
    }
    if let Err(error) = logitech_scroll_rescue::gui::run() {
        let message = logitech_scroll_rescue::wide(&error);
        let title = logitech_scroll_rescue::wide("Scroll Rescue");
        unsafe {
            windows_sys::Win32::UI::WindowsAndMessaging::MessageBoxW(
                std::ptr::null_mut(),
                message.as_ptr(),
                title.as_ptr(),
                0x10,
            );
        }
    }
}
