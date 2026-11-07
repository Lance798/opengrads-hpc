@echo off
setlocal
set "OPENGRADS_ROOT=%~dp0"
if /I "%~1"=="--version" (
  type "%OPENGRADS_ROOT%VERSION"
  exit /b
)
for /f "tokens=1,2" %%A in ('type "%OPENGRADS_ROOT%VERSION"') do if "%%A"=="opengrads-hpc" echo %%A %%B
set "PATH=%OPENGRADS_ROOT%lib;%OPENGRADS_ROOT%plugins;%PATH%"
set "GA_ROOT=%OPENGRADS_ROOT%plugins"
set "GAUDPT=%OPENGRADS_ROOT%etc\udpt"
set "GADDIR=%OPENGRADS_ROOT%cola\data"
set "GASCRP=%OPENGRADS_ROOT%lib\scripts"
rem This archive ships only the headless gxdummy driver, so the GrADS default
rem of "Cairo" would fail to load. The drivers are named first so that any
rem -d or -h the caller passes in %* still overrides them.
"%OPENGRADS_ROOT%bin\grads.exe" -d gxdummy -h gxdummy %*
endlocal
