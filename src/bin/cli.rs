fn main() {
    std::process::exit(logitech_scroll_rescue::cli::run(
        &std::env::args().skip(1).collect::<Vec<_>>(),
    ));
}
