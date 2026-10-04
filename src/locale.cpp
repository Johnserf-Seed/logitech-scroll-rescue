#include "native.hpp"

namespace {
Language preference = Language::Automatic;
Language resolved = Language::English;
constexpr wchar_t SETTINGS_KEY[] = L"Software\\ScrollRescue";
const wchar_t* const messages[][2] = {
#define MSG(key, chinese, english) {chinese, english},
#include "translations.inc"
#undef MSG
};
static_assert(sizeof(messages) / sizeof(messages[0]) == static_cast<unsigned>(Text::Count));
}

void set_language(Language language) {
    preference = language;
    resolved = language == Language::Automatic
        ? (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE ? Language::Chinese : Language::English)
        : language;
}
Language language_preference() { return preference; }
Language current_language() { return resolved; }
const wchar_t* language_tag() { return resolved == Language::Chinese ? L"zh-CN" : L"en"; }
bool parse_language(const wchar_t* value, Language& language) {
    if (equal_id(value, L"auto")) language = Language::Automatic;
    else if (equal_id(value, L"zh-CN") || equal_id(value, L"zh")) language = Language::Chinese;
    else if (equal_id(value, L"en") || equal_id(value, L"en-US")) language = Language::English;
    else return false;
    return true;
}
void initialize_language() {
    wchar_t saved[16]{}; DWORD bytes = sizeof(saved);
    Language language = Language::Automatic;
    if (RegGetValueW(HKEY_CURRENT_USER, SETTINGS_KEY, L"Language", RRF_RT_REG_SZ, nullptr, saved, &bytes) == ERROR_SUCCESS)
        parse_language(saved, language);
    set_language(language);
}
bool save_language() {
    const wchar_t* value = preference == Language::Automatic ? L"auto" : language_tag();
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    const LSTATUS result = RegSetValueExW(key, L"Language", 0, REG_SZ, reinterpret_cast<const BYTE*>(value), static_cast<DWORD>((length(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(key); return result == ERROR_SUCCESS;
}
bool configure_language(int& argc, wchar_t** argv) {
    bool seen = false;
    for (int i = 1; i < argc;) {
        if (!equal_text(argv[i], L"--lang")) { ++i; continue; }
        Language language{};
        if (seen || i + 1 >= argc || !parse_language(argv[i + 1], language)) return false;
        seen = true; set_language(language);
        for (int j = i; j + 2 < argc; ++j) argv[j] = argv[j + 2];
        argc -= 2; argv[argc] = nullptr;
    }
    return true;
}
const wchar_t* tr(Text key) {
    const unsigned index = static_cast<unsigned>(key);
    return index < static_cast<unsigned>(Text::Count) ? messages[index][resolved == Language::Chinese ? 0 : 1] : L"";
}
void format_message(const Message& message, wchar_t* output, unsigned capacity) {
    if (!capacity) return;
    output[0] = 0;
    const wchar_t* value = tr(message.key);
    if (message.format == MessageFormat::Count) {
        while (*value) {
            if (value[0] == L'{' && value[1] == L'n' && value[2] == L'}') { append_number(output, capacity, message.number); value += 3; }
            else { const wchar_t part[] = {*value++, 0}; if (!append_text(output, capacity, part)) break; }
        }
    } else copy_text(output, capacity, value);
    if (message.format == MessageFormat::Error) {
        append_text(output, capacity, tr(Text::ErrorPrefix)); append_number(output, capacity, message.number); append_text(output, capacity, tr(Text::ErrorSuffix));
    }
}
