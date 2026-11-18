#!/bin/sh
# Builds xinput9_1_0.dll (jebridge) with clang + lld-link; no Windows SDK or CRT needed.
set -e
cd "$(dirname "$0")"
mkdir -p out
for l in k32 ws2 u32; do llvm-dlltool -m i386 -k -d $l.def -l out/$l.lib; done
clang --target=i686-pc-windows-msvc -O2 -ffreestanding -fno-builtin -fno-stack-protector -mno-sse \
      -mstack-probe-size=65536 -Wall -c jebridge.c -o out/jebridge.obj
lld-link /dll /nodefaultlib /entry:DllMain@12 /machine:x86 /subsystem:windows /def:jebridge.def \
         /out:out/xinput9_1_0.dll out/jebridge.obj out/k32.lib out/ws2.lib out/u32.lib /safeseh:no /Brepro
sha256sum out/xinput9_1_0.dll
