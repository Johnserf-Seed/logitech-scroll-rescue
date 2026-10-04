#include "native.hpp"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    unsigned result = 0;
    if (argc > 1) {
        const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!output || output == INVALID_HANDLE_VALUE) AttachConsole(ATTACH_PARENT_PROCESS);
#ifdef SCROLL_RESCUE_QA
        if (argc == 5 && equal_text(argv[1], L"--qa-preview")) {
            unsigned percent = 0;
            for (const wchar_t* ch = argv[3]; *ch >= L'0' && *ch <= L'9'; ++ch) percent = percent * 10 + *ch - L'0';
            result = run_qa(instance, argv[2], percent, equal_text(argv[4], L"live"));
        } else
#endif
        result = run_cli(argc, argv);
    } else result = run_gui(instance);
    LocalFree(argv);
    return static_cast<int>(result);
}
