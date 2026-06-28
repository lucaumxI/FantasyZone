#!/usr/bin/env bash
# Build do Fantasy Zone para PC (simulacao SDL2)
# Uso (Git Bash): bash build.sh

OCTAVE_BIN="/c/Users/alana/AppData/Local/Programs/GNU Octave/Octave-11.1.0/mingw64/bin"
SDL_INC="$OCTAVE_BIN/../include"
SDL_LIB="$OCTAVE_BIN/../lib"
export PATH="$OCTAVE_BIN:$PATH"

echo "Compilando..."
gcc -DHOST_TEST \
    -I"$SDL_INC" \
    -L"$SDL_LIB" \
    -Wall -Wextra -Wno-unused-variable -Wno-unused-function \
    -o fantasyzone.exe \
    main.c video_pc.c mapa.c \
    -lmingw32 -lSDL2main -lSDL2 -mwindows

if [ $? -eq 0 ]; then
    echo "OK - fantasyzone.exe gerado!"
else
    echo "ERRO na compilacao"
fi
