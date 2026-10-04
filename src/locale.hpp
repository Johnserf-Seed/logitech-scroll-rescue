#pragma once

enum class Language { Automatic, Chinese, English };
enum class Text {
#define MSG(key, chinese, english) key,
#include "translations.inc"
#undef MSG
    Count
};
enum class MessageFormat { Plain, Count, Error };
struct Message {
    Text key = Text::None;
    unsigned number = 0;
    MessageFormat format = MessageFormat::Plain;
};

void initialize_language();
void set_language(Language language);
Language language_preference();
Language current_language();
bool save_language();
bool parse_language(const wchar_t* value, Language& language);
bool configure_language(int& argc, wchar_t** argv);
const wchar_t* language_tag();
const wchar_t* tr(Text key);
void format_message(const Message& message, wchar_t* output, unsigned capacity);
