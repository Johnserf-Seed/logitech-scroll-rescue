#[cfg(not(windows))]
compile_error!("Scroll Rescue supports Windows 10 2004 or newer only.");

pub mod cli;
pub mod device;
pub mod gui;
pub mod privilege;

pub fn wide(text: &str) -> Vec<u16> {
    text.encode_utf16().chain(Some(0)).collect()
}
