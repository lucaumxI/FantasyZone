@echo off
REM Build do Fantasy Zone para PC (simulacao SDL2)
REM Uso: clique duplo no build.bat  OU  execute no cmd

set OCTAVE_BIN=C:\Users\alana\AppData\Local\Programs\GNU Octave\Octave-11.1.0\mingw64\bin
set SDL_INC=%OCTAVE_BIN%\..\include
set SDL_LIB=%OCTAVE_BIN%\..\lib
set PATH=%OCTAVE_BIN%;%PATH%

echo Compilando...
"%OCTAVE_BIN%\gcc.exe" ^
    -DHOST_TEST ^
    -I"%SDL_INC%" ^
    -L"%SDL_LIB%" ^
    -Wall -Wextra -Wno-unused-variable -Wno-unused-function ^
    -o fantasyzone.exe ^
    main.c video_pc.c mapa.c ^
    -lmingw32 -lSDL2main -lSDL2 -mwindows

if %ERRORLEVEL% == 0 (
    echo OK - fantasyzone.exe gerado!
) else (
    echo ERRO na compilacao
    pause
)
