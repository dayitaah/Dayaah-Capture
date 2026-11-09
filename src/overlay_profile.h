#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <codecvt>
#endif

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>

#include "overlay_model.h"

namespace overlay_profile {

// Perfil portable y legible. Se guarda como JSON al lado del programa para que
// puedas respaldarlo, compartirlo o abrirlo y decir "ahh, era esto".

inline std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
#ifdef _WIN32
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                            static_cast<int>(value.size()),
                                            nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string output(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        output.data(), length, nullptr, nullptr);
    return output;
#else
    try { return std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(value); }
    catch (const std::range_error&) { return {}; }
#endif
}

inline std::wstring FromUtf8(const std::string& value) {
    if (value.empty()) return {};
#ifdef _WIN32
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring output(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), output.data(), length);
    return output;
#else
    try { return std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(value); }
    catch (const std::range_error&) { return {}; }
#endif
}

inline std::string Escape(const std::wstring& wide) {
    const std::string value = ToUtf8(wide);
    std::string output;
    output.reserve(value.size() + 8);
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20) {
                char escaped[7]{};
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", character);
                output += escaped;
            } else {
                output.push_back(static_cast<char>(character));
            }
        }
    }
    return output;
}

inline const char* TypeName(OverlayItemType value) {
    switch (value) {
    case OverlayItemType::Clock: return "clock";
    case OverlayItemType::Image: return "image";
    case OverlayItemType::Stats: return "stats";
    case OverlayItemType::Crosshair: return "crosshair";
    default: return "text";
    }
}

inline const char* AnchorName(OverlayAnchor value) {
    switch (value) {
    case OverlayAnchor::TopRight: return "top-right";
    case OverlayAnchor::BottomLeft: return "bottom-left";
    case OverlayAnchor::BottomRight: return "bottom-right";
    case OverlayAnchor::Center: return "center";
    default: return "top-left";
    }
}

inline const char* AlignmentName(OverlayAlignment value) {
    switch (value) {
    case OverlayAlignment::Center: return "center";
    case OverlayAlignment::Right: return "right";
    default: return "left";
    }
}

inline std::string Serialize(const OverlayConfig& config) {
    // Locale clasico obliga a usar punto decimal aunque Windows este en español.
    // Si salieran comas, el JSON se haria bolas con sus propios separadores.
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(5);
    output << "{\n  \"schema\": 1,\n  \"enabled\": "
           << (config.enabled ? "true" : "false") << ",\n  \"items\": [";
    for (size_t index = 0; index < config.items.size(); ++index) {
        const OverlayItem& item = config.items[index];
        output << (index ? ",\n" : "\n")
               << "    {\"id\": " << item.id
               << ", \"type\": \"" << TypeName(item.type)
               << "\", \"visible\": " << (item.visible ? "true" : "false")
               << ", \"name\": \"" << Escape(item.name)
               << "\", \"text\": \"" << Escape(item.text)
               << "\", \"imagePath\": \"" << Escape(item.imagePath)
               << "\", \"fontFamily\": \"" << Escape(item.fontFamily)
               << "\", \"fontSize\": " << item.fontSize
               << ", \"opacity\": " << item.opacity
               << ", \"argb\": " << item.argb
               << ", \"anchor\": \"" << AnchorName(item.anchor)
               << "\", \"alignment\": \"" << AlignmentName(item.alignment)
               << "\", \"x\": " << item.x << ", \"y\": " << item.y
               << ", \"width\": " << item.width << ", \"height\": " << item.height
               << ", \"clock24h\": " << (item.clock24h ? "true" : "false") << "}";
    }
    output << (config.items.empty() ? "" : "\n") << "  ]\n}\n";
    return output.str();
}

inline bool ReadStringToken(const std::string& source, size_t& position, std::string& output) {
    // Parser pequeño hecho para NUESTRO esquema. Acepta escapes JSON de verdad,
    // incluido Unicode, sin cargar una dependencia enorme solo para este archivo.
    while (position < source.size() && (source[position] == ' ' || source[position] == '\t' ||
           source[position] == '\r' || source[position] == '\n')) ++position;
    if (position >= source.size() || source[position++] != '"') return false;
    output.clear();
    while (position < source.size()) {
        char character = source[position++];
        if (character == '"') return true;
        if (character != '\\') {
            if (static_cast<unsigned char>(character) < 0x20) return false;
            output.push_back(character);
            continue;
        }
        if (position >= source.size()) return false;
        const char escaped = source[position++];
        if (escaped == '"' || escaped == '\\' || escaped == '/') output.push_back(escaped);
        else if (escaped == 'b') output.push_back('\b');
        else if (escaped == 'f') output.push_back('\f');
        else if (escaped == 'n') output.push_back('\n');
        else if (escaped == 'r') output.push_back('\r');
        else if (escaped == 't') output.push_back('\t');
        else if (escaped == 'u' && position + 4 <= source.size()) {
            auto readHex = [&]() -> int {
                if (position + 4 > source.size()) return -1;
                int code = 0;
                for (int i = 0; i < 4; ++i) {
                    const char digit = source[position++];
                    int number = -1;
                    if (digit >= '0' && digit <= '9') number = digit - '0';
                    else if (digit >= 'a' && digit <= 'f') number = digit - 'a' + 10;
                    else if (digit >= 'A' && digit <= 'F') number = digit - 'A' + 10;
                    if (number < 0) return -1;
                    code = code * 16 + number;
                }
                return code;
            };
            int code = readHex();
            if (code < 0) return false;
            if (code >= 0xd800 && code <= 0xdbff) {
                if (source.compare(position, 2, "\\u") != 0) return false;
                position += 2;
                const int low = readHex();
                if (low < 0xdc00 || low > 0xdfff) return false;
                code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
            } else if (code >= 0xdc00 && code <= 0xdfff) return false;
            if (code < 0x80) output.push_back(static_cast<char>(code));
            else if (code < 0x800) {
                output.push_back(static_cast<char>(0xc0 | (code >> 6)));
                output.push_back(static_cast<char>(0x80 | (code & 0x3f)));
            } else if (code < 0x10000) {
                output.push_back(static_cast<char>(0xe0 | (code >> 12)));
                output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
                output.push_back(static_cast<char>(0x80 | (code & 0x3f)));
            } else {
                output.push_back(static_cast<char>(0xf0 | (code >> 18)));
                output.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
                output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
                output.push_back(static_cast<char>(0x80 | (code & 0x3f)));
            }
        } else return false;
    }
    return false;
}

inline bool FindValue(const std::string& object, const char* key, size_t& position) {
    unsigned objectDepth = 0;
    unsigned arrayDepth = 0;
    size_t cursor = 0;
    while (cursor < object.size()) {
        const char character = object[cursor];
        if (character == '"') {
            std::string token;
            if (!ReadStringToken(object, cursor, token)) return false;
            size_t colon = cursor;
            while (colon < object.size() && std::isspace(static_cast<unsigned char>(object[colon]))) {
                ++colon;
            }
            if (objectDepth == 1 && !arrayDepth && token == key &&
                colon < object.size() && object[colon] == ':') {
                position = colon + 1;
                while (position < object.size() &&
                       std::isspace(static_cast<unsigned char>(object[position]))) ++position;
                return position < object.size();
            }
            continue;
        }
        if (character == '{') ++objectDepth;
        else if (character == '}' && objectDepth) --objectDepth;
        else if (character == '[') ++arrayDepth;
        else if (character == ']' && arrayDepth) --arrayDepth;
        ++cursor;
    }
    return false;
}

inline size_t FindObjectEnd(const std::string& source, size_t start) {
    bool inString = false;
    bool escaped = false;
    unsigned depth = 0;
    for (size_t position = start; position < source.size(); ++position) {
        const char character = source[position];
        if (inString) {
            if (escaped) escaped = false;
            else if (character == '\\') escaped = true;
            else if (character == '"') inString = false;
            continue;
        }
        if (character == '"') inString = true;
        else if (character == '{') ++depth;
        else if (character == '}' && depth && --depth == 0) return position;
    }
    return std::string::npos;
}

inline bool GetString(const std::string& object, const char* key, std::wstring& value) {
    size_t position = 0;
    std::string utf8;
    if (!FindValue(object, key, position) || !ReadStringToken(object, position, utf8)) return false;
    value = FromUtf8(utf8);
    return utf8.empty() || !value.empty();
}

inline bool GetBool(const std::string& object, const char* key, bool& value) {
    size_t position = 0;
    if (!FindValue(object, key, position)) return false;
    if (object.compare(position, 4, "true") == 0) { value = true; return true; }
    if (object.compare(position, 5, "false") == 0) { value = false; return true; }
    return false;
}

template <typename T> inline bool GetNumber(const std::string& object, const char* key, T& value) {
    size_t position = 0;
    if (!FindValue(object, key, position)) return false;
    std::istringstream input(object.substr(position));
    input.imbue(std::locale::classic());
    long double number = 0;
    if (!(input >> number) || !std::isfinite(number) ||
        number < static_cast<long double>(std::numeric_limits<T>::lowest()) ||
        number > static_cast<long double>(std::numeric_limits<T>::max())) return false;
    if (std::numeric_limits<T>::is_integer && std::floor(number) != number) return false;
    value = static_cast<T>(number);
    return true;
}

inline bool Parse(const std::string& json, OverlayConfig& config, std::wstring& error) {
    // Parseamos en una copia temporal. La configuracion buena solo se reemplaza
    // al final; un JSON mocho no destruye lo que ya estaba funcionando.
    OverlayConfig parsed{};
    const size_t start = json.find_first_not_of(" \t\r\n");
    if (start == std::string::npos || json[start] != '{' ||
        FindObjectEnd(json, start) != json.find_last_not_of(" \t\r\n")) {
        error = L"El perfil JSON está incompleto o contiene datos extra.";
        return false;
    }
    std::uint32_t schema = 0;
    if (!GetNumber(json, "schema", schema) || schema != 1) {
        error = L"El perfil usa un esquema desconocido o no declara schema 1.";
        return false;
    }
    size_t enabledPosition = 0;
    if (FindValue(json, "enabled", enabledPosition) &&
        !GetBool(json, "enabled", parsed.enabled)) {
        error = L"El campo enabled del perfil no es válido.";
        return false;
    }
    size_t itemsPosition = 0;
    if (!FindValue(json, "items", itemsPosition) || json[itemsPosition] != '[') {
        error = L"El perfil no contiene una lista items válida.";
        return false;
    }
    size_t cursor = itemsPosition + 1;
    while (cursor < json.size()) {
        while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) ++cursor;
        if (cursor >= json.size() || json[cursor] == ']') break;
        if (json[cursor] != '{') { error = L"Objeto de overlay inválido."; return false; }
        const size_t end = FindObjectEnd(json, cursor);
        if (end == std::string::npos) { error = L"Objeto de overlay incompleto."; return false; }
        const std::string object = json.substr(cursor, end - cursor + 1);
        std::uint64_t id = 0;
        std::wstring type;
        if (!GetNumber(object, "id", id) || !GetString(object, "type", type)) {
            error = L"Cada overlay necesita al menos id y type válidos.";
            return false;
        }
        OverlayItemType itemType = OverlayItemType::Text;
        if (type == L"text") itemType = OverlayItemType::Text;
        else if (type == L"clock") itemType = OverlayItemType::Clock;
        else if (type == L"image") itemType = OverlayItemType::Image;
        else if (type == L"stats") itemType = OverlayItemType::Stats;
        else if (type == L"crosshair") itemType = OverlayItemType::Crosshair;
        else {
            error = L"Un overlay contiene un type desconocido.";
            return false;
        }
        OverlayItem item = MakeDefaultOverlayItem(itemType, id);
        std::wstring anchor;
        std::wstring alignment;

        // Las versiones nuevas pueden agregar campos sin romper perfiles viejos.
        // Si un valor opcional falta usamos el default bonito del tipo; si esta
        // presente pero viene chueco, ahi si avisamos en vez de adivinar.
        auto optionalString = [&](const char* key, std::wstring& value) {
            size_t position = 0;
            return !FindValue(object, key, position) || GetString(object, key, value);
        };
        auto optionalBool = [&](const char* key, bool& value) {
            size_t position = 0;
            return !FindValue(object, key, position) || GetBool(object, key, value);
        };
        auto optionalNumber = [&](const char* key, auto& value) {
            size_t position = 0;
            return !FindValue(object, key, position) || GetNumber(object, key, value);
        };
        if (!optionalBool("visible", item.visible) || !optionalString("name", item.name) ||
            !optionalString("text", item.text) || !optionalString("imagePath", item.imagePath) ||
            !optionalString("fontFamily", item.fontFamily) ||
            !optionalNumber("fontSize", item.fontSize) ||
            !optionalNumber("opacity", item.opacity) || !optionalNumber("argb", item.argb) ||
            !optionalString("anchor", anchor) || !optionalString("alignment", alignment) ||
            !optionalNumber("x", item.x) || !optionalNumber("y", item.y) ||
            !optionalNumber("width", item.width) || !optionalNumber("height", item.height) ||
            !optionalBool("clock24h", item.clock24h)) {
            error = L"Un elemento del perfil contiene un valor opcional inválido.";
            return false;
        }
        if (!anchor.empty()) {
            if (anchor == L"top-left") item.anchor = OverlayAnchor::TopLeft;
            else if (anchor == L"top-right") item.anchor = OverlayAnchor::TopRight;
            else if (anchor == L"bottom-left") item.anchor = OverlayAnchor::BottomLeft;
            else if (anchor == L"bottom-right") item.anchor = OverlayAnchor::BottomRight;
            else if (anchor == L"center") item.anchor = OverlayAnchor::Center;
            else { error = L"Un overlay contiene un anchor desconocido."; return false; }
        }
        if (!alignment.empty()) {
            if (alignment == L"left") item.alignment = OverlayAlignment::Left;
            else if (alignment == L"center") item.alignment = OverlayAlignment::Center;
            else if (alignment == L"right") item.alignment = OverlayAlignment::Right;
            else { error = L"Un overlay contiene un alignment desconocido."; return false; }
        }
        ClampOverlayItem(item);
        parsed.items.push_back(std::move(item));
        cursor = end + 1;
        while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) ++cursor;
        if (cursor < json.size() && json[cursor] == ']') break;
        if (cursor >= json.size() || json[cursor++] != ',') {
            error = L"Separador de elementos inválido.";
            return false;
        }
        while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) ++cursor;
        if (cursor >= json.size() || json[cursor] != '{') {
            error = L"La lista de overlays está incompleta.";
            return false;
        }
    }
    if (cursor >= json.size() || json[cursor] != ']') {
        error = L"La lista de overlays está incompleta.";
        return false;
    }
    parsed.revision = config.revision + 1;
    config = std::move(parsed);
    return true;
}

#ifdef _WIN32
inline bool Save(const std::wstring& path, const OverlayConfig& config, std::wstring& error) {
    // Guardado seguro: primero .tmp, FlushFileBuffers y luego reemplazo atomico.
    // Si se va la luz a media escritura, el perfil anterior sigue enterito.
    const std::string json = Serialize(config);
    const std::wstring temporary = path + L".tmp." + std::to_wstring(GetCurrentProcessId());
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"No se pudo crear el perfil (error " + std::to_wstring(GetLastError()) + L").";
        return false;
    }
    DWORD written = 0;
    bool ok = WriteFile(file, json.data(), static_cast<DWORD>(json.size()), &written, nullptr) != FALSE;
    DWORD code = ok ? ERROR_SUCCESS : GetLastError();
    if (ok && written != json.size()) { ok = false; code = ERROR_WRITE_FAULT; }
    if (ok && !FlushFileBuffers(file)) { ok = false; code = GetLastError(); }
    if (!CloseHandle(file) && ok) { ok = false; code = GetLastError(); }
    if (ok && !MoveFileExW(temporary.c_str(), path.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ok = false;
        code = GetLastError();
    }
    if (!ok) DeleteFileW(temporary.c_str());
    if (!ok) error = L"No se pudo guardar el perfil (error " + std::to_wstring(code) + L").";
    return ok;
}

inline bool Load(const std::wstring& path, OverlayConfig& config, std::wstring& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"No se pudo abrir el perfil (error " + std::to_wstring(GetLastError()) + L").";
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 2 || size.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(file);
        error = L"El perfil está vacío o es demasiado grande.";
        return false;
    }
    std::string json(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const bool ok = ReadFile(file, json.data(), static_cast<DWORD>(json.size()), &read, nullptr) &&
                    read == json.size();
    CloseHandle(file);
    if (!ok) {
        error = L"No se pudo leer el perfil completo.";
        return false;
    }
    return Parse(json, config, error);
}
#endif

}
