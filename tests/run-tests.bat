@echo off
setlocal
where cl.exe >nul 2>&1 || exit /b 1
pushd "%~dp0"
if not exist out mkdir out
for %%T in (test_disco_effect test_disco_secret test_latency_estimator test_mode_selection test_overlay_model test_overlay_profile test_window_behavior test_video_effects_model) do (
  cl.exe /nologo /std:c++17 /utf-8 /O2 /EHsc /W4 "%%T.cpp" /Fe:"out\%%T.exe" /Fo:"out\%%T.obj"
  if errorlevel 1 goto failed
  "out\%%T.exe"
  if errorlevel 1 goto failed
)
cl.exe /nologo /std:c++17 /utf-8 /O2 /EHsc /W4 test_video_effects_windows.cpp /Fe:out\test_video_effects_windows.exe /Fo:out\test_video_effects_windows.obj /link d3dcompiler.lib
if errorlevel 1 goto failed
out\test_video_effects_windows.exe
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
