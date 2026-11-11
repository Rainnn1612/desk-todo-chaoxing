@echo off
setlocal
rem ===== DeskTodo build script (MinGW-w64 / gcc) =====
rem ASCII-only on purpose: .bat files are read with the OEM codepage.
rem Toolchain lookup order:
rem   1) MINGW_BIN env var, if you set it yourself
rem   2) gcc already on PATH
rem   3) winget-installed WinLibs package (BrechtSanders.WinLibs.*)
if defined MINGW_BIN set "PATH=%MINGW_BIN%;%PATH%"
where gcc >nul 2>nul
if errorlevel 1 for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.*") do if exist "%%D\mingw64\bin\gcc.exe" set "PATH=%%D\mingw64\bin;%PATH%"
where gcc >nul 2>nul
if errorlevel 1 (
  echo [X] gcc not found.
  echo     Put a MinGW-w64 toolchain on PATH, or set MINGW_BIN to its bin folder.
  echo     winget install BrechtSanders.WinLibs.POSIX.UCRT
  exit /b 1
)

cd /d "%~dp0"
if not exist out mkdir out
if not exist obj mkdir obj

echo [1/3] building icon ...
python tools\make_icon.py res\app.ico || goto :err

echo [2/3] compiling resources ...
windres res\app.rc -O coff -o obj\app_res.o || goto :err

echo [3/3] compiling and linking ...
gcc -Wall -Wextra -O2 -municode -x c src\json.c src\store.c src\theme.c src\ui.c src\input.c src\widget.c src\detail.c src\sync.c src\courses.c src\colorpick.c src\themdlg.c src\login.c src\main.c -x none obj\app_res.o -mwindows -s -static -luser32 -lgdi32 -lshell32 -ladvapi32 -lcomctl32 -lcrypt32 -o out\DeskTodo.exe || goto :err

echo.
for %%F in (out\DeskTodo.exe) do echo DONE: %%~fF  (%%~zF bytes)
exit /b 0

:err
echo.
echo *** BUILD FAILED ***
exit /b 1
