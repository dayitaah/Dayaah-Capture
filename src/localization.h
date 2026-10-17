#pragma once

enum class AppLanguage { Spanish, English };

inline const wchar_t* Tr(AppLanguage language, const wchar_t* spanish, const wchar_t* english) {
    return language == AppLanguage::English ? english : spanish;
}

