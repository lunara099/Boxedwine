#/bin/sh
set -e
emcc -std=c++20 -Wall -Wno-unused-private-field -Wno-unused-variable -Wno-parentheses-equality -Wno-unused-function -I../../lib/simde -I../../include ../../source/sdl/*.cpp ../../source/sdl/emscripten/*.cpp ../../platform/sdl/*.cpp ../../platform/linux/*.cpp ../../source/emulation/cpu/*.cpp ../../source/emulation/cpu/common/*.cpp ../../source/emulation/cpu/normal/*.cpp ../../source/emulation/softmmu/*.cpp ../../source/io/*.cpp ../../source/kernel/*.cpp ../../source/kernel/devs/*.cpp ../../source/kernel/proc/*.cpp ../../source/kernel/sys/*.cpp ../../source/kernel/loader/*.cpp ../../source/util/*.cpp ../../source/opengl/sdl/*.cpp ../../source/opengl/*.cpp ../../source/vulkan/*.cpp -o boxedwine.html -O3 -DUSE_MMU -s USE_SDL=2 -DSDL2 -DBOXEDWINE_DISABLE_UI -DBOXEDWINE_HAS_SETJMP -fwasm-exceptions -s TOTAL_MEMORY=536870912 -s FS_DEBUG=1 --shell-file shellfs.html -s DEFAULT_LIBRARY_FUNCS_TO_INCLUDE='["$ERRNO_CODES"]' -s EXPORTED_RUNTIME_METHODS='["addRunDependency", "removeRunDependency"]'