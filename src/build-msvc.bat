@echo off
setlocal
where cl.exe >nul 2>&1 || goto no_msvc
where rc.exe >nul 2>&1 || goto no_msvc

pushd "%~dp0"
rc.exe /nologo /fo DayaahCapture.res DayaahCapture.rc || goto failed
cl.exe /nologo /std:c++17 /O2 /EHsc /DUNICODE /D_UNICODE main.cpp DayaahCapture.res /Fe:..\DayaahCapture.exe /link /SUBSYSTEM:WINDOWS ole32.lib oleaut32.lib uuid.lib d3d11.lib dxgi.lib dxguid.lib mf.lib mfplat.lib mfreadwrite.lib mfuuid.lib avrt.lib propsys.lib shlwapi.lib dwmapi.lib uxtheme.lib comctl32.lib gdi32.lib user32.lib || goto failed
del /q main.obj DayaahCapture.res >nul 2>&1
popd
echo DayaahCapture.exe compilado correctamente.
exit /b 0

:no_msvc
echo Abre este BAT desde "x64 Native Tools Command Prompt for VS 2022".
pause
exit /b 1

:failed
echo Fallo la compilacion.
popd
pause
exit /b 1
