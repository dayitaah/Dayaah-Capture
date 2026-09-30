#pragma once

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

#include "overlay_model.h"
#include "overlay_profile.h"

class OverlayEditor {
public:
    using ChangeCallback = std::function<void(const OverlayConfig&)>;
    using CloseCallback = std::function<void()>;

    ~OverlayEditor() { Close(); }

    void Open(HINSTANCE instance, HWND owner, const std::shared_ptr<const OverlayConfig>& current,
              ChangeCallback callback, CloseCallback closed = {}) {
        callback_ = std::move(callback);
        closed_ = std::move(closed);
        if (current) config_ = *current;
        if (window_) {
            SetEnabled(config_.enabled);
            RefreshList();
            ShowWindow(window_, SW_RESTORE);
            SetForegroundWindow(window_);
            return;
        }
        instance_ = instance;
        owner_ = owner;
        closing_ = false;
        WNDCLASSEXW klass{};
        klass.cbSize = sizeof(klass);
        klass.lpfnWndProc = WindowProc;
        klass.hInstance = instance_;
        klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        klass.lpszClassName = L"DayaahOverlayEditorWindow";
        RegisterClassExW(&klass);
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, klass.lpszClassName,
                                  L"Editor de overlays - Dayaah Capture",
                                  (WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX) | WS_CLIPCHILDREN,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 1120, 730,
                                  owner_, nullptr, instance_, this);
        if (!window_) return;
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
    }

    void Close(bool returnToCapture = false) {
        if (!window_ || closing_) return;
        ApplyProperties();
        closing_ = true;
        if (GetCapture() == window_) ReleaseCapture();
        DestroyWindow(window_);
        if (returnToCapture) {
            if (closed_) closed_();
            if (IsWindow(owner_) && !IsIconic(owner_)) {
                SetForegroundWindow(owner_);
                SetActiveWindow(owner_);
                SetFocus(owner_);
            }
        }
    }

    void SetEnabled(bool enabled) {
        config_.enabled = enabled;
        if (window_ && !closing_) Button_SetCheck(enabled_, enabled ? BST_CHECKED : BST_UNCHECKED);
    }

    static std::wstring PrepareProfileImages(OverlayConfig& config, const std::wstring& profilePath) {
        wchar_t directory[MAX_PATH]{};
        lstrcpynW(directory, profilePath.c_str(), MAX_PATH);
        PathRemoveFileSpecW(directory);
        std::wstring warnings;
        for (OverlayItem& item : config.items) {
            if (item.type != OverlayItemType::Image || item.imagePath.empty()) continue;
            if (PathIsRelativeW(item.imagePath.c_str())) {
                wchar_t absolute[MAX_PATH]{};
                if (PathCombineW(absolute, directory, item.imagePath.c_str())) item.imagePath = absolute;
            }
            std::wstring error;
            item.imagePixels = DecodePng(item.imagePath, error);
            if (!item.imagePixels) warnings += L"\n• " + item.imagePath;
        }
        return warnings;
    }

private:
    enum : int {
        ID_LIST = 4100, ID_ENABLED, ID_ADD_TEXT, ID_ADD_CLOCK, ID_ADD_IMAGE,
        ID_ADD_STATS, ID_DELETE, ID_SAVE, ID_LOAD, ID_VISIBLE, ID_CONTENT,
        ID_FONT, ID_FONT_BUTTON, ID_SIZE, ID_OPACITY, ID_COLOR, ID_ANCHOR,
        ID_ALIGNMENT, ID_CLOCK24
    };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        OverlayEditor* editor = reinterpret_cast<OverlayEditor*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            editor = static_cast<OverlayEditor*>(create->lpCreateParams);
            editor->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(editor));
        }
        return editor ? editor->HandleMessage(window, message, wParam, lParam)
                      : DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            syncing_ = true;
            CreateControls();
            syncing_ = false;
            RefreshList();
            return 0;
        case WM_SIZE:
            Layout(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_COMMAND:
            OnCommand(LOWORD(wParam), HIWORD(wParam));
            return 0;
        case WM_LBUTTONDOWN:
            BeginDrag(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSEMOVE:
            ContinueDrag(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONUP:
            if (dragging_) {
                dragging_ = false;
                ReleaseCapture();
            }
            return 0;
        case WM_CAPTURECHANGED:
            dragging_ = false;
            return 0;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_CLOSE:
            Close(true);
            return 0;
        case WM_DESTROY:
            closing_ = true;
            dragging_ = false;
            return 0;
        case WM_NCDESTROY: {
            const LRESULT result = DefWindowProcW(window, message, wParam, lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            if (font_) {
                DeleteObject(font_);
                font_ = nullptr;
            }
            window_ = nullptr;
            list_ = nullptr;
            return result;
        }
        default:
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }

    HWND Control(const wchar_t* klass, const wchar_t* text, DWORD style, int id) {
        HWND result = CreateWindowExW(0, klass, text, WS_CHILD | WS_VISIBLE | style,
                                      0, 0, 10, 10, window_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                      instance_, nullptr);
        if (font_) SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        return result;
    }

    void CreateControls() {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
        font_ = CreateFontIndirectW(&metrics.lfMessageFont);

        enabled_ = Control(L"BUTTON", L"Mostrar overlays (F10)", BS_AUTOCHECKBOX, ID_ENABLED);
        list_ = Control(L"LISTBOX", L"", LBS_NOTIFY | WS_BORDER | WS_VSCROLL, ID_LIST);
        addText_ = Control(L"BUTTON", L"+ Texto", BS_PUSHBUTTON, ID_ADD_TEXT);
        addClock_ = Control(L"BUTTON", L"+ Reloj", BS_PUSHBUTTON, ID_ADD_CLOCK);
        addImage_ = Control(L"BUTTON", L"+ PNG", BS_PUSHBUTTON, ID_ADD_IMAGE);
        addStats_ = Control(L"BUTTON", L"+ Estadísticas", BS_PUSHBUTTON, ID_ADD_STATS);
        delete_ = Control(L"BUTTON", L"Eliminar", BS_PUSHBUTTON, ID_DELETE);
        save_ = Control(L"BUTTON", L"Guardar perfil...", BS_PUSHBUTTON, ID_SAVE);
        load_ = Control(L"BUTTON", L"Cargar perfil...", BS_PUSHBUTTON, ID_LOAD);

        propertyTitle_ = Control(L"STATIC", L"PROPIEDADES", SS_LEFT, 0);
        visible_ = Control(L"BUTTON", L"Visible", BS_AUTOCHECKBOX, ID_VISIBLE);
        contentLabel_ = Control(L"STATIC", L"Texto", SS_LEFT, 0);
        content_ = Control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, ID_CONTENT);
        fontLabel_ = Control(L"STATIC", L"Fuente", SS_LEFT, 0);
        fontEdit_ = Control(L"EDIT", L"Segoe UI", WS_BORDER | ES_AUTOHSCROLL, ID_FONT);
        fontButton_ = Control(L"BUTTON", L"Elegir...", BS_PUSHBUTTON, ID_FONT_BUTTON);
        sizeLabel_ = Control(L"STATIC", L"Tamaño (px a 1080p)", SS_LEFT, 0);
        sizeEdit_ = Control(L"EDIT", L"36", WS_BORDER | ES_NUMBER, ID_SIZE);
        opacityLabel_ = Control(L"STATIC", L"Opacidad (0-100)", SS_LEFT, 0);
        opacityEdit_ = Control(L"EDIT", L"100", WS_BORDER | ES_NUMBER, ID_OPACITY);
        colorButton_ = Control(L"BUTTON", L"Color...", BS_PUSHBUTTON, ID_COLOR);
        anchorLabel_ = Control(L"STATIC", L"Anclaje", SS_LEFT, 0);
        anchorCombo_ = Control(L"COMBOBOX", L"", CBS_DROPDOWNLIST, ID_ANCHOR);
        alignLabel_ = Control(L"STATIC", L"Alineación", SS_LEFT, 0);
        alignCombo_ = Control(L"COMBOBOX", L"", CBS_DROPDOWNLIST, ID_ALIGNMENT);
        clock24_ = Control(L"BUTTON", L"Reloj de 24 horas", BS_AUTOCHECKBOX, ID_CLOCK24);
        const wchar_t* anchors[] = {L"Superior izquierda", L"Superior derecha",
                                    L"Inferior izquierda", L"Inferior derecha", L"Centro"};
        for (const wchar_t* value : anchors) ComboBox_AddString(anchorCombo_, value);
        const wchar_t* alignments[] = {L"Izquierda", L"Centro", L"Derecha"};
        for (const wchar_t* value : alignments) ComboBox_AddString(alignCombo_, value);
        Button_SetCheck(enabled_, config_.enabled ? BST_CHECKED : BST_UNCHECKED);
    }

    void Layout(int width, int height) {
        const int margin = 14;
        const int leftWidth = 220;
        const int rightWidth = 260;
        const int availableLeft = margin + leftWidth + 14;
        const int availableRight = std::max(availableLeft + 240,
                                            width - rightWidth - margin - 14);
        const int availableTop = 56;
        const int availableBottom = std::max(availableTop + 160, height - 28);
        int viewerWidth = availableRight - availableLeft;
        int viewerHeight = static_cast<int>(viewerWidth * 9.0 / 16.0);
        const int maximumHeight = availableBottom - availableTop;
        if (viewerHeight > maximumHeight) {
            viewerHeight = maximumHeight;
            viewerWidth = static_cast<int>(viewerHeight * 16.0 / 9.0);
        }
        const int viewerX = availableLeft + (availableRight - availableLeft - viewerWidth) / 2;
        const int viewerY = availableTop + (availableBottom - availableTop - viewerHeight) / 2;
        viewer_ = {viewerX, viewerY, viewerX + viewerWidth, viewerY + viewerHeight};

        MoveWindow(enabled_, margin, 14, leftWidth, 26, TRUE);
        MoveWindow(list_, margin, 50, leftWidth, std::max(140, height - 250), TRUE);
        int y = std::max(200, height - 190);
        MoveWindow(addText_, margin, y, 105, 28, TRUE);
        MoveWindow(addClock_, margin + 115, y, 105, 28, TRUE);
        MoveWindow(addImage_, margin, y + 34, 105, 28, TRUE);
        MoveWindow(addStats_, margin + 115, y + 34, 105, 28, TRUE);
        MoveWindow(delete_, margin, y + 68, leftWidth, 28, TRUE);
        MoveWindow(save_, margin, y + 105, leftWidth, 28, TRUE);
        MoveWindow(load_, margin, y + 139, leftWidth, 28, TRUE);

        const int x = width - rightWidth - margin;
        int py = 16;
        MoveWindow(propertyTitle_, x, py, rightWidth, 24, TRUE); py += 30;
        MoveWindow(visible_, x, py, rightWidth, 24, TRUE); py += 32;
        MoveWindow(contentLabel_, x, py, rightWidth, 20, TRUE); py += 20;
        MoveWindow(content_, x, py, rightWidth, 25, TRUE); py += 34;
        MoveWindow(fontLabel_, x, py, rightWidth, 20, TRUE); py += 20;
        MoveWindow(fontEdit_, x, py, rightWidth - 82, 25, TRUE);
        MoveWindow(fontButton_, x + rightWidth - 76, py, 76, 25, TRUE); py += 34;
        MoveWindow(sizeLabel_, x, py, rightWidth, 20, TRUE); py += 20;
        MoveWindow(sizeEdit_, x, py, rightWidth, 25, TRUE); py += 34;
        MoveWindow(opacityLabel_, x, py, rightWidth, 20, TRUE); py += 20;
        MoveWindow(opacityEdit_, x, py, rightWidth, 25, TRUE); py += 34;
        MoveWindow(colorButton_, x, py, rightWidth, 28, TRUE); py += 38;
        MoveWindow(anchorLabel_, x, py, rightWidth, 20, TRUE); py += 20;
        MoveWindow(anchorCombo_, x, py, rightWidth, 180, TRUE); py += 34;
        MoveWindow(alignLabel_, x, py, rightWidth, 20, TRUE); py += 20;
        MoveWindow(alignCombo_, x, py, rightWidth, 120, TRUE); py += 38;
        MoveWindow(clock24_, x, py, rightWidth, 24, TRUE);
        InvalidateRect(window_, nullptr, TRUE);
    }

    void RefreshList() {
        if (!list_) return;
        syncing_ = true;
        const int previous = selected_;
        ListBox_ResetContent(list_);
        for (const OverlayItem& item : config_.items) ListBox_AddString(list_, item.name.c_str());
        if (!config_.items.empty()) {
            selected_ = std::clamp(previous, 0, static_cast<int>(config_.items.size()) - 1);
            ListBox_SetCurSel(list_, selected_);
        } else selected_ = -1;
        syncing_ = false;
        LoadProperties();
        InvalidateRect(window_, &viewer_, TRUE);
    }

    OverlayItem* Selected() {
        return selected_ >= 0 && selected_ < static_cast<int>(config_.items.size())
                   ? &config_.items[static_cast<size_t>(selected_)] : nullptr;
    }

    void LoadProperties() {
        syncing_ = true;
        OverlayItem* item = Selected();
        const bool available = item != nullptr;
        HWND fields[] = {visible_, content_, fontEdit_, fontButton_, sizeEdit_, opacityEdit_,
                         colorButton_, anchorCombo_, alignCombo_, clock24_};
        for (HWND field : fields) EnableWindow(field, available);
        if (item) {
            Button_SetCheck(visible_, item->visible ? BST_CHECKED : BST_UNCHECKED);
            SetWindowTextW(content_, item->text.c_str());
            SetWindowTextW(fontEdit_, item->fontFamily.c_str());
            SetWindowTextW(sizeEdit_, std::to_wstring(static_cast<int>(item->fontSize)).c_str());
            SetWindowTextW(opacityEdit_, std::to_wstring(std::lround(item->opacity * 100)).c_str());
            ComboBox_SetCurSel(anchorCombo_, static_cast<int>(item->anchor));
            ComboBox_SetCurSel(alignCombo_, static_cast<int>(item->alignment));
            Button_SetCheck(clock24_, item->clock24h ? BST_CHECKED : BST_UNCHECKED);
            ShowWindow(clock24_, item->type == OverlayItemType::Clock ? SW_SHOW : SW_HIDE);
            ShowWindow(content_, item->type == OverlayItemType::Text ? SW_SHOW : SW_HIDE);
            ShowWindow(contentLabel_, item->type == OverlayItemType::Text ? SW_SHOW : SW_HIDE);
        }
        syncing_ = false;
    }

    void OnCommand(int id, int notification) {
        if (syncing_ || closing_) return;
        if (id == ID_ENABLED && notification == BN_CLICKED) {
            config_.enabled = Button_GetCheck(enabled_) == BST_CHECKED;
            Publish();
            return;
        }
        if (id == ID_LIST && notification == LBN_SELCHANGE) {
            selected_ = ListBox_GetCurSel(list_);
            LoadProperties();
            InvalidateRect(window_, &viewer_, TRUE);
            return;
        }
        if (id == ID_ADD_TEXT) Add(OverlayItemType::Text);
        else if (id == ID_ADD_CLOCK) Add(OverlayItemType::Clock);
        else if (id == ID_ADD_STATS) Add(OverlayItemType::Stats);
        else if (id == ID_ADD_IMAGE) AddImage();
        else if (id == ID_DELETE) DeleteSelected();
        else if (id == ID_SAVE) SaveProfile();
        else if (id == ID_LOAD) LoadProfile();
        else if (id == ID_COLOR && notification == BN_CLICKED) ChooseItemColor();
        else if (id == ID_FONT_BUTTON && notification == BN_CLICKED) ChooseItemFont();
        else if ((id == ID_VISIBLE || id == ID_CLOCK24) && notification == BN_CLICKED) ApplyProperties();
        else if ((id == ID_ANCHOR || id == ID_ALIGNMENT) && notification == CBN_SELCHANGE) ApplyProperties();
        else if ((id == ID_CONTENT || id == ID_FONT || id == ID_SIZE || id == ID_OPACITY) &&
                 notification == EN_CHANGE) ApplyProperties();
        else if ((id == ID_SIZE || id == ID_OPACITY) && notification == EN_KILLFOCUS) {
            ApplyProperties();
            OverlayItem* item = Selected();
            if (item) {
                syncing_ = true;
                const HWND field = id == ID_SIZE ? sizeEdit_ : opacityEdit_;
                const long number = std::lround(id == ID_SIZE ? item->fontSize : item->opacity * 100);
                SetWindowTextW(field, std::to_wstring(number).c_str());
                syncing_ = false;
            }
        }
    }

    void Add(OverlayItemType type) {
        config_.items.push_back(MakeDefaultOverlayItem(type, NextId()));
        selected_ = static_cast<int>(config_.items.size()) - 1;
        RefreshList();
        Publish();
    }

    void AddImage() {
        wchar_t path[MAX_PATH]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window_;
        dialog.lpstrFilter = L"Imágenes PNG (*.png)\0*.png\0Todos los archivos\0*.*\0";
        dialog.lpstrFile = path;
        dialog.nMaxFile = _countof(path);
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&dialog)) return;
        OverlayItem item = MakeDefaultOverlayItem(OverlayItemType::Image, NextId());
        item.imagePath = path;
        item.name = PathFindFileNameW(path);
        std::wstring error;
        item.imagePixels = DecodePng(path, error);
        if (!item.imagePixels) {
            MessageBoxW(window_, error.c_str(), L"Cargar PNG", MB_ICONERROR);
            return;
        }
        config_.items.push_back(std::move(item));
        selected_ = static_cast<int>(config_.items.size()) - 1;
        RefreshList();
        Publish();
    }

    void DeleteSelected() {
        if (!Selected()) return;
        config_.items.erase(config_.items.begin() + selected_);
        if (selected_ >= static_cast<int>(config_.items.size())) --selected_;
        RefreshList();
        Publish();
    }

    void ApplyProperties() {
        if (syncing_ || closing_ || !window_) return;
        OverlayItem* item = Selected();
        if (!item) return;
        wchar_t value[512]{};
        item->visible = Button_GetCheck(visible_) == BST_CHECKED;
        GetWindowTextW(content_, value, _countof(value)); item->text = value;
        GetWindowTextW(fontEdit_, value, _countof(value)); item->fontFamily = value;
        GetWindowTextW(sizeEdit_, value, _countof(value));
        if (*value) item->fontSize = static_cast<float>(_wtoi(value));
        GetWindowTextW(opacityEdit_, value, _countof(value));
        if (*value) item->opacity = static_cast<float>(_wtoi(value)) / 100.0f;
        const int anchor = ComboBox_GetCurSel(anchorCombo_);
        const int alignment = ComboBox_GetCurSel(alignCombo_);
        if (anchor >= 0) item->anchor = static_cast<OverlayAnchor>(anchor);
        if (alignment >= 0) item->alignment = static_cast<OverlayAlignment>(alignment);
        item->clock24h = Button_GetCheck(clock24_) == BST_CHECKED;
        ClampOverlayItem(*item);
        Publish();
        InvalidateRect(window_, &viewer_, TRUE);
    }

    void ChooseItemColor() {
        OverlayItem* item = Selected();
        if (!item) return;
        static COLORREF custom[16]{};
        CHOOSECOLORW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window_;
        dialog.rgbResult = RGB((item->argb >> 16) & 0xff, (item->argb >> 8) & 0xff,
                               item->argb & 0xff);
        dialog.lpCustColors = custom;
        dialog.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (!ChooseColorW(&dialog)) return;
        item->argb = 0xff000000u | GetRValue(dialog.rgbResult) << 16 |
                     GetGValue(dialog.rgbResult) << 8 | GetBValue(dialog.rgbResult);
        Publish();
        InvalidateRect(window_, &viewer_, TRUE);
    }

    void ChooseItemFont() {
        OverlayItem* item = Selected();
        if (!item) return;
        LOGFONTW value{};
        lstrcpynW(value.lfFaceName, item->fontFamily.c_str(), LF_FACESIZE);
        value.lfHeight = -static_cast<LONG>(item->fontSize);
        CHOOSEFONTW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window_;
        dialog.lpLogFont = &value;
        dialog.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;
        if (!ChooseFontW(&dialog)) return;
        item->fontFamily = value.lfFaceName;
        item->fontSize = static_cast<float>(dialog.iPointSize) / 10.0f * 96.0f / 72.0f;
        ClampOverlayItem(*item);
        LoadProperties();
        Publish();
        InvalidateRect(window_, &viewer_, TRUE);
    }

    void SaveProfile() {
        wchar_t path[MAX_PATH] = L"overlays.json";
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window_;
        dialog.lpstrFilter = L"Perfil JSON (*.json)\0*.json\0";
        dialog.lpstrFile = path;
        dialog.nMaxFile = _countof(path);
        dialog.lpstrDefExt = L"json";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&dialog)) return;
        std::wstring error;
        if (!overlay_profile::Save(path, config_, error)) {
            MessageBoxW(window_, error.c_str(), L"Guardar perfil", MB_ICONERROR);
        }
    }

    void LoadProfile() {
        wchar_t path[MAX_PATH]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window_;
        dialog.lpstrFilter = L"Perfil JSON (*.json)\0*.json\0Todos los archivos\0*.*\0";
        dialog.lpstrFile = path;
        dialog.nMaxFile = _countof(path);
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&dialog)) return;
        std::wstring error;
        if (!overlay_profile::Load(path, config_, error)) {
            MessageBoxW(window_, error.c_str(), L"Cargar perfil", MB_ICONERROR);
            return;
        }
        const std::wstring imageWarnings = PrepareProfileImages(config_, path);
        if (!imageWarnings.empty()) {
            MessageBoxW(window_, (L"El perfil se cargó, pero no pude abrir estas imágenes:" +
                        imageWarnings).c_str(), L"Cargar perfil", MB_ICONWARNING);
        }
        Button_SetCheck(enabled_, config_.enabled ? BST_CHECKED : BST_UNCHECKED);
        selected_ = config_.items.empty() ? -1 : 0;
        RefreshList();
        Publish();
    }

    RECT ItemPixels(const OverlayItem& item) const {
        const OverlayRectF rect = ResolveOverlayRect(item);
        const int width = viewer_.right - viewer_.left;
        const int height = viewer_.bottom - viewer_.top;
        return {viewer_.left + static_cast<LONG>(rect.left * width),
                viewer_.top + static_cast<LONG>(rect.top * height),
                viewer_.left + static_cast<LONG>(rect.right * width),
                viewer_.top + static_cast<LONG>(rect.bottom * height)};
    }

    void BeginDrag(int x, int y) {
        POINT point{x, y};
        if (!PtInRect(&viewer_, point)) return;
        for (int index = static_cast<int>(config_.items.size()) - 1; index >= 0; --index) {
            RECT rect = ItemPixels(config_.items[static_cast<size_t>(index)]);
            if (!PtInRect(&rect, point)) continue;
            selected_ = index;
            ListBox_SetCurSel(list_, selected_);
            LoadProperties();
            dragRect_ = rect;
            dragStart_ = point;
            resizing_ = x >= rect.right - 14 && y >= rect.bottom - 14;
            dragging_ = true;
            SetCapture(window_);
            InvalidateRect(window_, &viewer_, TRUE);
            return;
        }
    }

    void ContinueDrag(int x, int y) {
        if (!dragging_) return;
        OverlayItem* item = Selected();
        if (!item) return;
        const float width = static_cast<float>(viewer_.right - viewer_.left);
        const float height = static_cast<float>(viewer_.bottom - viewer_.top);
        const float dx = (x - dragStart_.x) / width;
        const float dy = (y - dragStart_.y) / height;
        if (resizing_) {
            item->width = (dragRect_.right - dragRect_.left) / width + dx;
            item->height = (dragRect_.bottom - dragRect_.top) / height + dy;
        } else {
            const float left = (dragRect_.left - viewer_.left) / width + dx;
            const float top = (dragRect_.top - viewer_.top) / height + dy;
            SetOverlayTopLeft(*item, left, top);
        }
        ClampOverlayItem(*item);
        Publish();
        InvalidateRect(window_, &viewer_, TRUE);
    }

    void Paint() {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window_, &paint);
        HBRUSH background = CreateSolidBrush(RGB(20, 22, 27));
        FillRect(dc, &viewer_, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(145, 150, 160));
        RECT hint{viewer_.left + 12, viewer_.top + 10, viewer_.right - 12, viewer_.top + 34};
        DrawTextW(dc, L"VISOR  ·  arrastra para mover  ·  esquina inferior derecha para redimensionar",
                  -1, &hint, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        for (size_t index = 0; index < config_.items.size(); ++index) {
            const OverlayItem& item = config_.items[index];
            if (!item.visible) continue;
            RECT rect = ItemPixels(item);
            const float alpha = item.opacity * ((item.argb >> 24) & 0xff) / 255.0f;
            const COLORREF color = RGB(
                std::lround(20 + (static_cast<int>((item.argb >> 16) & 0xff) - 20) * alpha),
                std::lround(22 + (static_cast<int>((item.argb >> 8) & 0xff) - 22) * alpha),
                std::lround(27 + (static_cast<int>(item.argb & 0xff) - 27) * alpha));
            HPEN pen = CreatePen(PS_SOLID, index == static_cast<size_t>(selected_) ? 2 : 1,
                                 index == static_cast<size_t>(selected_) ? RGB(0, 180, 255) : RGB(90, 95, 105));
            HGDIOBJ oldPen = SelectObject(dc, pen);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(pen);
            SetTextColor(dc, color);
            std::wstring label = item.text;
            if (item.type == OverlayItemType::Clock) label = item.clock24h ? L"23:59:59" : L"11:59:59 PM";
            else if (item.type == OverlayItemType::Image) label = L"PNG · " + item.name;
            else if (item.type == OverlayItemType::Stats) label = L"Entrada: 60 fps | Presentado: 60 fps\nFrames descartados: 0\n1920x1080 NV12 · 1 ms";
            RECT textRect = rect;
            InflateRect(&textRect, -5, -4);
            UINT flags = DT_WORDBREAK | DT_TOP | DT_END_ELLIPSIS;
            if (item.alignment == OverlayAlignment::Center) flags |= DT_CENTER;
            else if (item.alignment == OverlayAlignment::Right) flags |= DT_RIGHT;
            DrawTextW(dc, label.c_str(), -1, &textRect, flags);
            if (index == static_cast<size_t>(selected_)) {
                RECT handle{rect.right - 10, rect.bottom - 10, rect.right, rect.bottom};
                HBRUSH handleBrush = CreateSolidBrush(RGB(0, 180, 255));
                FillRect(dc, &handle, handleBrush);
                DeleteObject(handleBrush);
            }
        }
        EndPaint(window_, &paint);
    }

    std::uint64_t NextId() const {
        std::uint64_t result = 1;
        for (const OverlayItem& item : config_.items) result = std::max(result, item.id + 1);
        return result;
    }

    template <typename T> static void Release(T*& value) {
        if (value) {
            value->Release();
            value = nullptr;
        }
    }

    static std::shared_ptr<const OverlayImagePixels> DecodePng(
            const std::wstring& path, std::wstring& error) {
        IWICImagingFactory* factory = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&factory));
        if (SUCCEEDED(hr)) {
            hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                    WICDecodeMetadataCacheOnLoad, &decoder);
        }
        if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
        if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
        if (SUCCEEDED(hr)) {
            hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                       WICBitmapDitherTypeNone, nullptr, 0.0,
                                       WICBitmapPaletteTypeMedianCut);
        }
        UINT width = 0;
        UINT height = 0;
        if (SUCCEEDED(hr)) hr = converter->GetSize(&width, &height);
        if (SUCCEEDED(hr) && (!width || !height || width > 16384 || height > 16384)) {
            hr = E_INVALIDARG;
        }
        std::shared_ptr<OverlayImagePixels> pixels;
        if (SUCCEEDED(hr)) {
            pixels = std::make_shared<OverlayImagePixels>();
            pixels->width = width;
            pixels->height = height;
            pixels->stride = width * 4;
            pixels->bgraPremultiplied.resize(static_cast<size_t>(pixels->stride) * height);
            hr = converter->CopyPixels(nullptr, pixels->stride,
                                       static_cast<UINT>(pixels->bgraPremultiplied.size()),
                                       pixels->bgraPremultiplied.data());
        }
        Release(converter);
        Release(frame);
        Release(decoder);
        Release(factory);
        if (FAILED(hr)) {
            wchar_t code[32]{};
            _snwprintf_s(code, _countof(code), _TRUNCATE, L"0x%08lX",
                         static_cast<unsigned long>(hr));
            error = L"No pude decodificar el PNG (" + std::wstring(code) + L").";
            return nullptr;
        }
        return pixels;
    }

    void Publish() {
        ++config_.revision;
        if (callback_) callback_(config_);
    }

    HINSTANCE instance_ = nullptr;
    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    HFONT font_ = nullptr;
    HWND enabled_ = nullptr;
    HWND list_ = nullptr;
    HWND addText_ = nullptr;
    HWND addClock_ = nullptr;
    HWND addImage_ = nullptr;
    HWND addStats_ = nullptr;
    HWND delete_ = nullptr;
    HWND save_ = nullptr;
    HWND load_ = nullptr;
    HWND propertyTitle_ = nullptr;
    HWND visible_ = nullptr;
    HWND contentLabel_ = nullptr;
    HWND content_ = nullptr;
    HWND fontLabel_ = nullptr;
    HWND fontEdit_ = nullptr;
    HWND fontButton_ = nullptr;
    HWND sizeLabel_ = nullptr;
    HWND sizeEdit_ = nullptr;
    HWND opacityLabel_ = nullptr;
    HWND opacityEdit_ = nullptr;
    HWND colorButton_ = nullptr;
    HWND anchorLabel_ = nullptr;
    HWND anchorCombo_ = nullptr;
    HWND alignLabel_ = nullptr;
    HWND alignCombo_ = nullptr;
    HWND clock24_ = nullptr;
    RECT viewer_{250, 56, 800, 650};
    RECT dragRect_{};
    POINT dragStart_{};
    OverlayConfig config_{};
    ChangeCallback callback_;
    CloseCallback closed_;
    int selected_ = -1;
    bool syncing_ = false;
    bool closing_ = false;
    bool dragging_ = false;
    bool resizing_ = false;
};
