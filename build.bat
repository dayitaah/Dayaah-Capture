@echo off
setlocal

where cl.exe >nul 2>&1 || goto no_msvc
where rc.exe >nul 2>&1 || goto no_msvc

if not exist "%~dp0build" mkdir "%~dp0build"
pushd "%~dp0src"

rc.exe /nologo /fo DayaahCapture.res DayaahCapture.rc || goto failed
cl.exe /nologo /std:c++17 /O2 /EHsc /DUNICODE /D_UNICODE ^
  main.cpp DayaahCapture.res /Fe:"..\build\DayaahCapture.exe" ^
  /link /SUBSYSTEM:WINDOWS ^
  ole32.lib oleaut32.lib uuid.lib d3d11.lib dxgi.lib dxguid.lib ^
  mf.lib mfplat.lib mfreadwrite.lib mfuuid.lib avrt.lib propsys.lib ^
  shlwapi.lib dwmapi.lib uxtheme.lib comctl32.lib comdlg32.lib ^
  d2d1.lib dwrite.lib windowscodecs.lib gdi32.lib user32.lib
if errorlevel 1 goto failed

del /q main.obj DayaahCapture.res >nul 2>&1
popd
echo Dayaah Capture compilado en build\DayaahCapture.exe
exit /b 0

:no_msvc
echo Abre este archivo desde una consola x64 Native Tools de Visual Studio.
exit /b 1

:failed
echo Fallo la compilacion.
popd
exit /b 1
