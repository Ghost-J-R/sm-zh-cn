@echo off
rem Build sm.exe with the bundled Tiny C Compiler (smre-cn project).
rem Requires: third_party\tcc\tcc.exe and third_party\SDL2-2.24.1\lib\x64 (both git-ignored, local only).
rem ROM expectation: sm.smc in the repository root (original or patched).

setlocal
cd /d "%~dp0"

set SDL2=third_party\SDL2-2.24.1

if not exist "third_party\tcc\tcc.exe" (
  echo ERROR: third_party\tcc\tcc.exe not found. Put the TCC distribution there first.
  exit /b 1
)
if not exist "%SDL2%\lib\x64\SDL2.dll" (
  echo ERROR: %SDL2%\lib\x64\SDL2.dll not found. Put the SDL2 VC development package there first.
  exit /b 1
)

echo Building with TCC...
third_party\tcc\tcc.exe -osm.exe -DCOMPILER_TCC=1 -DSTBI_NO_SIMD=1 -DHAVE_STDINT_H=1 -D_HAVE_STDINT_H=1 -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 -I%SDL2%/include -L%SDL2%/lib/x64 -lSDL2 -I. src/*.c src/snes/*.c third_party/gl_core/gl_core_3_1.c
if errorlevel 1 (
  echo Build FAILED
  exit /b 1
)

copy /y "%SDL2%\lib\x64\SDL2.dll" . >nul
echo Build OK: sm.exe + SDL2.dll
exit /b 0
