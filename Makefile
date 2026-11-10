# kbdlayout - FreeBSD console keyboard layout chooser (SDL3 KMSDRM + Dear ImGui)
#
#   make            static build: downloads SDL3 once and links it statically
#   make shared     quick build against the system SDL3 package (pkg install sdl3)
#   make test       parser/translator unit tests against the system keymaps
#   make install    installs to $(PREFIX)/sbin (default /usr/local)
#
# Works with FreeBSD make and GNU make.

PROG=		kbdlayout
PREFIX?=	/usr/local
CXX?=		c++
CXXFLAGS?=	-O2
JOBS?=		4
FETCH?=		fetch -o
PKG_CONFIG?=	pkg-config
KEYMAPDIR?=	/usr/share/vt/keymaps

SDL_VERSION=	3.4.20
SDL_URL=	https://github.com/libsdl-org/SDL/archive/refs/tags/release-$(SDL_VERSION).tar.gz
SDL_SRC=	deps/SDL-release-$(SDL_VERSION)
SDL_BUILD=	deps/sdl-build
SDL_PREFIX=	deps/sdl3
SDL_LIB=	$(SDL_PREFIX)/lib/libSDL3.a
SDL_PC=		PKG_CONFIG_PATH=$(SDL_PREFIX)/lib/pkgconfig $(PKG_CONFIG)

IMGUI=		third_party/imgui
INCS=		-Isrc -I$(IMGUI) -I$(IMGUI)/backends
WARN=		-Wall -Wextra -Wno-unused-parameter
SRCS=		src/main.cpp src/keymap.cpp src/kernel_keymap.cpp \
		$(IMGUI)/imgui.cpp $(IMGUI)/imgui_draw.cpp \
		$(IMGUI)/imgui_tables.cpp $(IMGUI)/imgui_widgets.cpp \
		$(IMGUI)/backends/imgui_impl_sdl3.cpp \
		$(IMGUI)/backends/imgui_impl_sdlrenderer3.cpp

# SDL3 reduced to what a console program needs: KMSDRM video with
# OpenGL ES / OpenGL renderers through Mesa (loaded at run time),
# evdev input enumerated through libudev-devd, no audio, no X11/Wayland.
SDL_CMAKE_FLAGS= \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_LIBDIR=lib \
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
	-DSDL_SHARED=OFF -DSDL_STATIC=ON \
	-DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF \
	-DSDL_INSTALL_DOCS=OFF \
	-DSDL_UNIX_CONSOLE_BUILD=ON \
	-DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_VULKAN=OFF \
	-DSDL_KMSDRM=ON -DSDL_LIBUDEV=ON \
	-DSDL_OPENGL=ON -DSDL_OPENGLES=ON \
	-DSDL_GPU=OFF -DSDL_RENDER_GPU=OFF -DSDL_RENDER_VULKAN=OFF \
	-DSDL_AUDIO=OFF -DSDL_CAMERA=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF \
	-DSDL_HIDAPI=OFF -DSDL_DIALOG=OFF -DSDL_TRAY=OFF \
	-DSDL_DBUS=OFF -DSDL_IBUS=OFF

all: static

static: $(SDL_LIB)
	$(CXX) $(CXXFLAGS) -std=c++17 $(WARN) $(INCS) `$(SDL_PC) --cflags sdl3` \
	    -o $(PROG) $(SRCS) $(SDL_LIB) \
	    `$(SDL_PC) --static --libs sdl3 | sed -e 's/-lSDL3//g'`
	@echo
	@echo "Built $(PROG). Shared libraries it needs at run time:"
	@ldd $(PROG) | sed -e 1d

shared:
	$(CXX) $(CXXFLAGS) -std=c++17 $(WARN) $(INCS) `$(PKG_CONFIG) --cflags sdl3` \
	    -o $(PROG) $(SRCS) `$(PKG_CONFIG) --libs sdl3`

$(SDL_LIB):
	@command -v cmake >/dev/null || { echo "cmake is required: pkg install cmake ninja pkgconf"; exit 1; }
	mkdir -p deps
	if [ ! -f $(SDL_SRC)/CMakeLists.txt ]; then \
		$(FETCH) deps/sdl3-$(SDL_VERSION).tar.gz $(SDL_URL) && \
		tar -xzf deps/sdl3-$(SDL_VERSION).tar.gz -C deps; \
	fi
	cmake -S $(SDL_SRC) -B $(SDL_BUILD) \
	    `command -v ninja >/dev/null && echo -G Ninja` \
	    -DCMAKE_INSTALL_PREFIX="`pwd`/$(SDL_PREFIX)" $(SDL_CMAKE_FLAGS)
	@cfg=`find $(SDL_BUILD) -name SDL_build_config.h | head -1`; \
	for f in SDL_VIDEO_DRIVER_KMSDRM SDL_INPUT_LINUXEV SDL_UDEV_DYNAMIC; do \
		grep -q "define $$f" $$cfg || { \
		echo "SDL3 was configured without $$f: the console keyboard/display will not work."; \
		echo "Install the build dependencies:"; \
		echo "  pkg install cmake ninja pkgconf libdrm mesa-libs libudev-devd evdev-proto"; \
		echo "then run: rm -rf $(SDL_BUILD) && make"; exit 1; }; \
	done
	cmake --build $(SDL_BUILD) -j $(JOBS)
	cmake --install $(SDL_BUILD)

test:
	$(CXX) $(CXXFLAGS) -std=c++17 $(WARN) $(INCS) -o test_keymap \
	    tests/test_keymap.cpp src/keymap.cpp src/kernel_keymap.cpp
	./test_keymap $(KEYMAPDIR)

install: $(PROG)
	install -d $(DESTDIR)$(PREFIX)/sbin
	install -s -m 755 $(PROG) $(DESTDIR)$(PREFIX)/sbin/$(PROG)

clean:
	rm -f $(PROG) test_keymap

distclean: clean
	rm -rf deps

.PHONY: all static shared test install clean distclean
