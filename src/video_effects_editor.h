#pragma once
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <functional>
#include <string>
#include "localization.h"
#include "video_effects_model.h"

class VideoEffectsEditor {
public:
    using Changed = std::function<void(const VideoEffects&)>;
    ~VideoEffectsEditor() { Close(); }
    void Open(HINSTANCE instance, HWND owner, const VideoEffects& settings,
              AppLanguage language, Changed changed) {
        instance_ = instance;
        owner_ = owner;
        settings_ = settings;
        language_ = language;
        changed_ = std::move(changed);
        if (window_) {
            Sync(settings);
            ShowWindow(window_, SW_RESTORE);
            SetForegroundWindow(window_);
            return;
        }
        WNDCLASSEXW c{};
        c.cbSize = sizeof(c);
        c.hInstance = instance;
        c.lpfnWndProc = Proc;
        c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        c.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        c.lpszClassName = L"DayaahVideoEffectsEditor";
        RegisterClassExW(&c);
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
        RECT bounds{0, 0, 620, 570};
        AdjustWindowRectEx(&bounds, style, FALSE, WS_EX_TOOLWINDOW);
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, c.lpszClassName,
            Tr(language_, L"Efectos de video - Dayaah Capture", L"Video effects - Dayaah Capture"),
            style, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
            owner, nullptr, instance, this);
        if (window_) { ShowWindow(window_, SW_SHOW); UpdateWindow(window_); }
    }
    void Close() { if (window_) DestroyWindow(window_); }
    void Sync(const VideoEffects& s) {
        settings_ = s;
        if (!window_ || !checks_[0]) return;
        const bool flags[] = {s.enabled, s.sharpenEnabled, s.colorEnabled, s.crtEnabled};
        for (int i = 0; i < 4; ++i) Button_SetCheck(checks_[i], flags[i] ? BST_CHECKED : BST_UNCHECKED);
        for (int i = 0; i < 8; ++i) {
            SendMessageW(sliders_[i], TBM_SETPOS, TRUE, s.*fields_[i]);
            UpdateValue(i);
        }
        SetWindowTextW(status_, !s.enabled
            ? Tr(language_, L"Original: efectos apagados", L"Original: effects disabled")
            : s.HasWork()
                ? Tr(language_, L"Efectos activos - cambios en vivo", L"Effects active - live changes")
                : Tr(language_, L"Valores neutros: imagen original", L"Neutral values: original image"));
    }
    bool HandleDialog(MSG& message) {
        if (!window_ || (message.hwnd != window_ && !IsChild(window_, message.hwnd))) return false;
        if (message.message == WM_KEYDOWN && !(message.lParam & (1LL << 30))) {
            if (message.wParam == VK_F6) {
                settings_.enabled = !settings_.enabled;
                Publish();
                return true;
            }
            if (message.wParam == VK_ESCAPE) { ReturnToViewer(); return true; }
        }
        return IsDialogMessageW(window_, &message) != FALSE;
    }
private:
    inline static constexpr int VideoEffects::*fields_[8] = {
        &VideoEffects::sharpness, &VideoEffects::contrast, &VideoEffects::saturation,
        &VideoEffects::gamma, &VideoEffects::temperature, &VideoEffects::scanlines,
        &VideoEffects::mask, &VideoEffects::curvature};
    static LRESULT CALLBACK Proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<VideoEffectsEditor*>(GetWindowLongPtrW(w, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<VideoEffectsEditor*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->window_ = w;
            SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(w, m, wp, lp);
        switch (m) {
        case WM_CREATE:
            self->CreateControls();
            self->Sync(self->settings_);
            return 0;
        case WM_HSCROLL:
            for (int i = 0; i < 8; ++i) if (reinterpret_cast<HWND>(lp) == self->sliders_[i]) {
                self->settings_.*fields_[i] = int(SendMessageW(self->sliders_[i], TBM_GETPOS, 0, 0));
                self->Publish();
                break;
            }
            return 0;
        case WM_COMMAND:
            if (HIWORD(wp) == BN_CLICKED) self->Command(LOWORD(wp));
            return 0;
        case WM_CLOSE:
            self->ReturnToViewer();
            return 0;
        case WM_NCDESTROY:
            self->window_ = nullptr;
            for (auto& check : self->checks_) check = nullptr;
            return DefWindowProcW(w, m, wp, lp);
        default:
            return DefWindowProcW(w, m, wp, lp);
        }
    }
    HWND Control(const wchar_t* type, const wchar_t* text, DWORD style,
                 int x, int y, int width, int height, int id) {
        HWND c = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
            x, y, width, height, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return c;
    }
    void CreateControls() {
        checks_[0] = Control(L"BUTTON", Tr(language_, L"Activar efectos (F6)", L"Enable effects (F6)"), BS_AUTOCHECKBOX | WS_TABSTOP, 20, 14, 230, 25, 5100);
        status_ = Control(L"STATIC", L"", 0, 280, 18, 325, 24, 0);
        checks_[1] = Control(L"BUTTON", Tr(language_, L"Nitidez adaptativa", L"Adaptive sharpening"), BS_AUTOCHECKBOX | WS_TABSTOP, 20, 53, 560, 24, 5101);
        checks_[2] = Control(L"BUTTON", Tr(language_, L"Color", L"Color"), BS_AUTOCHECKBOX | WS_TABSTOP, 20, 130, 560, 24, 5102);
        checks_[3] = Control(L"BUTTON", L"CRT", BS_AUTOCHECKBOX | WS_TABSTOP, 20, 325, 560, 24, 5103);
        const wchar_t* labelsEs[] = {L"Intensidad", L"Contraste", L"Saturación", L"Gamma", L"Temperatura", L"Scanlines", L"Máscara RGB", L"Curvatura"};
        const wchar_t* labelsEn[] = {L"Intensity", L"Contrast", L"Saturation", L"Gamma", L"Temperature", L"Scanlines", L"RGB mask", L"Curvature"};
        const int ys[] = {87, 163, 201, 239, 277, 359, 397, 435};
        const int lows[] = {0, 50, 0, 50, -100, 0, 0, 0};
        const int highs[] = {100, 150, 200, 200, 100, 100, 100, 100};
        for (int i = 0; i < 8; ++i) {
            Control(L"STATIC", language_ == AppLanguage::English ? labelsEn[i] : labelsEs[i], 0, 27, ys[i] + 5, 112, 24, 0);
            sliders_[i] = Control(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, 140, ys[i], 360, 30, 5200 + i);
            SendMessageW(sliders_[i], TBM_SETRANGE, TRUE, MAKELPARAM(lows[i], highs[i]));
            SendMessageW(sliders_[i], TBM_SETPAGESIZE, 0, 5);
            values_[i] = Control(L"STATIC", L"", SS_RIGHT, 505, ys[i] + 5, 86, 24, 0);
        }
        Control(L"BUTTON", Tr(language_, L"Nitidez suave", L"Soft sharpening"), WS_TABSTOP, 20, 486, 130, 30, 5300);
        Control(L"BUTTON", Tr(language_, L"CRT suave", L"Soft CRT"), WS_TABSTOP, 160, 486, 120, 30, 5301);
        Control(L"BUTTON", Tr(language_, L"Restablecer todo", L"Reset all"), WS_TABSTOP, 290, 486, 145, 30, 5302);
        Control(L"BUTTON", Tr(language_, L"Cerrar", L"Close"), WS_TABSTOP, 455, 486, 140, 30, 5303);
        Control(L"STATIC", Tr(language_,
            L"F6 compara con el original. Los cambios se guardan automáticamente.",
            L"F6 compares with the original. Changes are saved automatically."), 0, 20, 535, 580, 24, 0);
    }
    void UpdateValue(int i) {
        wchar_t text[48]{};
        const int value = settings_.*fields_[i];
        if (i == 3) _snwprintf_s(text, 48, _TRUNCATE, L"%.2f", value / 100.0);
        else if (i == 4) _snwprintf_s(text, 48, _TRUNCATE, L"%+d", value);
        else _snwprintf_s(text, 48, _TRUNCATE, L"%d%%", value);
        SetWindowTextW(values_[i], text);
    }
    void Command(int id) {
        if (id >= 5100 && id <= 5103) {
            settings_.enabled = Button_GetCheck(checks_[0]) == BST_CHECKED;
            settings_.sharpenEnabled = Button_GetCheck(checks_[1]) == BST_CHECKED;
            settings_.colorEnabled = Button_GetCheck(checks_[2]) == BST_CHECKED;
            settings_.crtEnabled = Button_GetCheck(checks_[3]) == BST_CHECKED;
        } else if (id == 5300) {
            settings_ = VideoEffects{};
            settings_.enabled = true;
            settings_.sharpness = 35;
        } else if (id == 5301) {
            settings_ = VideoEffects{};
            settings_.enabled = true;
            settings_.crtEnabled = true;
        } else if (id == 5302) settings_ = VideoEffects{};
        else if (id == 5303 || id == IDCANCEL) { ReturnToViewer(); return; }
        else return;
        Publish();
    }
    void Publish() {
        settings_.Sanitize();
        Sync(settings_);
        if (changed_) changed_(settings_);
    }
    void ReturnToViewer() {
        Close();
        if (IsWindow(owner_) && !IsIconic(owner_)) {
            SetForegroundWindow(owner_);
            SetActiveWindow(owner_);
            SetFocus(owner_);
        }
    }
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr, owner_ = nullptr, status_ = nullptr;
    HWND checks_[4]{}, sliders_[8]{}, values_[8]{};
    VideoEffects settings_;
    Changed changed_;
    AppLanguage language_ = AppLanguage::Spanish;
};
