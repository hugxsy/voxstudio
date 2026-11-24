#!/bin/sh
# Compile Vox Studio pour Windows 64 bits depuis Linux (MinGW-w64)
set -e
cd "$(dirname "$0")"
mkdir -p build/obj
[ -d third_party/imgui ] || git clone -q --depth 1 --branch v1.91.5 https://github.com/ocornut/imgui.git third_party/imgui
[ -d third_party/rnnoise ] || { git clone -q https://github.com/xiph/rnnoise.git third_party/rnnoise && git -C third_party/rnnoise checkout -q v0.1.1; }
IMGUI=third_party/imgui
RN=third_party/rnnoise
CC=x86_64-w64-mingw32-gcc
CXX=x86_64-w64-mingw32-g++
for f in denoise celt_lpc kiss_fft pitch rnn rnn_data; do
  [ build/obj/rn_$f.o -nt $RN/src/$f.c ] || $CC -O2 -c -I$RN/include -I$RN/src $RN/src/$f.c -o build/obj/rn_$f.o
done
for f in imgui imgui_draw imgui_tables imgui_widgets backends/imgui_impl_win32 backends/imgui_impl_dx11; do
  o=build/obj/$(basename $f).o
  [ $o -nt $IMGUI/$f.cpp ] || $CXX -std=c++17 -O2 -I$IMGUI -c $IMGUI/$f.cpp -o $o
done
FLAGS="-std=c++17 -O2 -DNDEBUG -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -I$IMGUI -I$IMGUI/backends -I$RN/include"
$CXX $FLAGS -Wall -Wno-unused-function src/main.cpp src/win_audio.cpp src/updater.cpp build/obj/*.o -o build/VoxStudio.exe \
  -municode -mwindows -static -static-libgcc -static-libstdc++ \
  -ld3d11 -ld3dcompiler -ldxgi -lole32 -loleaut32 -luuid -lavrt -ldwmapi -lgdi32 -luser32 -lksuser -lwinhttp
x86_64-w64-mingw32-strip build/VoxStudio.exe
ls -lh build/VoxStudio.exe
