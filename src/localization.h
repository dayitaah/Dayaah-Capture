#pragma once

enum class AppLanguage { Spanish, English };

// Mini sistema de idiomas sin meter 14 archivos XML. Cada texto trae español
// e ingles juntos y esta funcion escoge el que toca.
inline const wchar_t* Tr(AppLanguage language, const wchar_t* spanish, const wchar_t* english) {
    return language == AppLanguage::English ? english : spanish;
}

