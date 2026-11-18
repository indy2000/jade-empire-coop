#!/bin/sh
# Builds the test-only steam_api.dll mock (32-bit). Not for players: real Steam uses the real steam_api.dll.
set -e
cd "$(dirname "$0")"
mkdir -p out
llvm-dlltool -m i386 -k -d k32m.def -l out/k32m.lib
llvm-dlltool -m i386 -k -d ws2m.def -l out/ws2m.lib
clang --target=i686-pc-windows-msvc -O2 -ffreestanding -fno-builtin -fno-stack-protector -mno-sse \
      -fno-omit-frame-pointer -Wall -c steam_api_mock.c -o out/steam_api_mock.obj
lld-link /dll /nodefaultlib /entry:DllMain@12 /machine:x86 /subsystem:windows /out:out/steam_api.dll \
         out/steam_api_mock.obj out/k32m.lib out/ws2m.lib /safeseh:no /Brepro
sha256sum out/steam_api.dll
