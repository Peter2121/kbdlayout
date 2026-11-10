/*
 * kbdlayout - choose the FreeBSD console keyboard layout, without X.
 *
 * Draws directly on the console through SDL3 (KMSDRM) and Dear ImGui:
 * a centered window with the list of keymaps (the active one selected),
 * a field to try the selected layout before applying it, and OK / Cancel.
 *
 * OK loads the keymap with kbdcontrol(1), optionally saves it in
 * /etc/rc.conf (keymap="xx.kbd") like kbdmap(1) and bsdinstall do.
 *
 * Run from a vt text console:   kbdlayout
 * Keys: Up/Down/PgUp/PgDn select, Tab moves between fields,
 *       Enter = OK, Escape = Cancel.
 */
#include "keymap.h"
#include "kernel_keymap.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <spawn.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char **environ;

/* ---- SDL scancode -> FreeBSD keyboard keycode -------------------------- */

static int sdl_to_fbsd(SDL_Scancode sc)
{
	static const struct { SDL_Scancode sc; int kc; } table[] = {
		{SDL_SCANCODE_ESCAPE, 1}, {SDL_SCANCODE_1, 2}, {SDL_SCANCODE_2, 3},
		{SDL_SCANCODE_3, 4}, {SDL_SCANCODE_4, 5}, {SDL_SCANCODE_5, 6},
		{SDL_SCANCODE_6, 7}, {SDL_SCANCODE_7, 8}, {SDL_SCANCODE_8, 9},
		{SDL_SCANCODE_9, 10}, {SDL_SCANCODE_0, 11}, {SDL_SCANCODE_MINUS, 12},
		{SDL_SCANCODE_EQUALS, 13}, {SDL_SCANCODE_BACKSPACE, 14},
		{SDL_SCANCODE_TAB, 15}, {SDL_SCANCODE_Q, 16}, {SDL_SCANCODE_W, 17},
		{SDL_SCANCODE_E, 18}, {SDL_SCANCODE_R, 19}, {SDL_SCANCODE_T, 20},
		{SDL_SCANCODE_Y, 21}, {SDL_SCANCODE_U, 22}, {SDL_SCANCODE_I, 23},
		{SDL_SCANCODE_O, 24}, {SDL_SCANCODE_P, 25},
		{SDL_SCANCODE_LEFTBRACKET, 26}, {SDL_SCANCODE_RIGHTBRACKET, 27},
		{SDL_SCANCODE_RETURN, 28}, {SDL_SCANCODE_LCTRL, 29},
		{SDL_SCANCODE_A, 30}, {SDL_SCANCODE_S, 31}, {SDL_SCANCODE_D, 32},
		{SDL_SCANCODE_F, 33}, {SDL_SCANCODE_G, 34}, {SDL_SCANCODE_H, 35},
		{SDL_SCANCODE_J, 36}, {SDL_SCANCODE_K, 37}, {SDL_SCANCODE_L, 38},
		{SDL_SCANCODE_SEMICOLON, 39}, {SDL_SCANCODE_APOSTROPHE, 40},
		{SDL_SCANCODE_GRAVE, 41}, {SDL_SCANCODE_LSHIFT, 42},
		{SDL_SCANCODE_BACKSLASH, 43}, {SDL_SCANCODE_NONUSHASH, 43},
		{SDL_SCANCODE_Z, 44}, {SDL_SCANCODE_X, 45}, {SDL_SCANCODE_C, 46},
		{SDL_SCANCODE_V, 47}, {SDL_SCANCODE_B, 48}, {SDL_SCANCODE_N, 49},
		{SDL_SCANCODE_M, 50}, {SDL_SCANCODE_COMMA, 51},
		{SDL_SCANCODE_PERIOD, 52}, {SDL_SCANCODE_SLASH, 53},
		{SDL_SCANCODE_RSHIFT, 54}, {SDL_SCANCODE_KP_MULTIPLY, 55},
		{SDL_SCANCODE_LALT, 56}, {SDL_SCANCODE_SPACE, 57},
		{SDL_SCANCODE_CAPSLOCK, 58}, {SDL_SCANCODE_F1, 59}, {SDL_SCANCODE_F2, 60},
		{SDL_SCANCODE_F3, 61}, {SDL_SCANCODE_F4, 62}, {SDL_SCANCODE_F5, 63},
		{SDL_SCANCODE_F6, 64}, {SDL_SCANCODE_F7, 65}, {SDL_SCANCODE_F8, 66},
		{SDL_SCANCODE_F9, 67}, {SDL_SCANCODE_F10, 68},
		{SDL_SCANCODE_NUMLOCKCLEAR, 69}, {SDL_SCANCODE_SCROLLLOCK, 70},
		{SDL_SCANCODE_KP_7, 71}, {SDL_SCANCODE_KP_8, 72}, {SDL_SCANCODE_KP_9, 73},
		{SDL_SCANCODE_KP_MINUS, 74}, {SDL_SCANCODE_KP_4, 75},
		{SDL_SCANCODE_KP_5, 76}, {SDL_SCANCODE_KP_6, 77},
		{SDL_SCANCODE_KP_PLUS, 78}, {SDL_SCANCODE_KP_1, 79},
		{SDL_SCANCODE_KP_2, 80}, {SDL_SCANCODE_KP_3, 81}, {SDL_SCANCODE_KP_0, 82},
		{SDL_SCANCODE_KP_PERIOD, 83}, {SDL_SCANCODE_NONUSBACKSLASH, 86},
		{SDL_SCANCODE_F11, 87}, {SDL_SCANCODE_F12, 88},
		{SDL_SCANCODE_KP_ENTER, 89}, {SDL_SCANCODE_RCTRL, 90},
		{SDL_SCANCODE_KP_DIVIDE, 91}, {SDL_SCANCODE_PRINTSCREEN, 92},
		{SDL_SCANCODE_RALT, 93}, {SDL_SCANCODE_HOME, 94}, {SDL_SCANCODE_UP, 95},
		{SDL_SCANCODE_PAGEUP, 96}, {SDL_SCANCODE_LEFT, 97},
		{SDL_SCANCODE_RIGHT, 98}, {SDL_SCANCODE_END, 99}, {SDL_SCANCODE_DOWN, 100},
		{SDL_SCANCODE_PAGEDOWN, 101}, {SDL_SCANCODE_INSERT, 102},
		{SDL_SCANCODE_DELETE, 103}, {SDL_SCANCODE_PAUSE, 104},
		{SDL_SCANCODE_LGUI, 105}, {SDL_SCANCODE_RGUI, 106},
		{SDL_SCANCODE_APPLICATION, 107},
		{SDL_SCANCODE_INTERNATIONAL1, 115}, /* JIS "ro", ABNT2 "/?" */
		{SDL_SCANCODE_INTERNATIONAL3, 125}, /* JIS yen */
	};
	for (const auto &e : table)
		if (e.sc == sc)
			return e.kc;
	return -1;
}

/* ---- Running kbdcontrol / sysrc ---------------------------------------- */

static int run_command(const std::vector<std::string> &args, int stdin_fd)
{
	std::vector<char *> argv;
	for (const auto &a : args)
		argv.push_back(const_cast<char *>(a.c_str()));
	argv.push_back(nullptr);

	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	if (stdin_fd > 0)
		posix_spawn_file_actions_adddup2(&fa, stdin_fd, STDIN_FILENO);
	pid_t pid;
	int rc = posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ);
	posix_spawn_file_actions_destroy(&fa);
	if (rc != 0) {
		fprintf(stderr, "kbdlayout: cannot run %s: %s\n", argv[0], strerror(rc));
		return -1;
	}
	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* ---- Touchpad: raw evdev relative tracking ----------------------------- *
 *
 * SDL's udev classifier never checks ID_INPUT_TOUCHPAD (only ID_INPUT_MOUSE
 * / ID_INPUT_TOUCHSCREEN), so a touchpad ends up classified as a plain
 * absolute-position mouse: SDL maps its raw pad coordinate onto the whole
 * screen on every report, and that full-pad-to-full-screen scaling is
 * baked into both the absolute position SDL hands the app and any delta
 * derived from it -- there's no usable relative-motion signal left in
 * anything SDL exposes for this device, no matter how it's post-processed.
 *
 * So this reads the touchpad's own /dev/input/eventN node directly and
 * takes exclusive ownership of it with EVIOCGRAB: once grabbed, the kernel
 * stops delivering that device's events to any other reader, including
 * SDL's own fd for the same device, so SDL's broken handling of it simply
 * never fires again. Every other device (real mouse, TrackPoint, keyboard)
 * is untouched -- SDL keeps reading those normally. */

struct TouchpadReader {
	int fd = -1;
	bool has_mt = false;            /* ABS_MT_SLOT present: Type B multitouch */
	int range_x = 0, range_y = 0;   /* physical pad range, raw hardware units */
	int deadzone_x = 1, deadzone_y = 1;
	int tap_move_x = 1, tap_move_y = 1; /* how far counts as "moved", not "tapped" */
	bool down = false, have_baseline = false;
	int raw_x = 0, raw_y = 0, last_x = 0, last_y = 0;
	float cursor_x = 0, cursor_y = 0;

	/* Only the first finger to touch down is tracked; any other slot that
	 * touches while it's still down (e.g. a second finger rested on the
	 * pad) is ignored entirely, so its position reports can't get mixed
	 * into the tracked finger's motion. */
	int current_slot = 0, primary_slot = -1;

	/* Tap-to-click: not a hardware button, just a short low-movement touch.
	 * Real clicks (BTN_LEFT/RIGHT/MIDDLE) are forwarded as themselves and
	 * set click_happened, which suppresses a redundant synthesized tap
	 * click for the same gesture (a firm press is itself a brief,
	 * low-movement touch). */
	int down_x = 0, down_y = 0;
	uint64_t down_time_us = 0;
	bool moved_too_much = false, click_happened = false;

	~TouchpadReader()
	{
		if (fd >= 0) {
			ioctl(fd, EVIOCGRAB, 0); /* release before closing: leave the device usable */
			close(fd);
		}
	}
};

static bool touchpad_find_path(std::string &path)
{
	DIR *d = opendir("/dev/input");
	if (!d)
		return false;
	bool found = false;
	struct dirent *de;
	while (!found && (de = readdir(d)) != nullptr) {
		if (strncmp(de->d_name, "event", 5) != 0)
			continue;
		std::string p = std::string("/dev/input/") + de->d_name;
		int fd = open(p.c_str(), O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		char name[128] = "";
		if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
			std::string ln = name;
			for (auto &c : ln)
				c = (char)tolower((unsigned char)c);
			if (ln.find("touchpad") != std::string::npos || ln.find("synaptics") != std::string::npos ||
			    ln.find("glidepoint") != std::string::npos || ln.find("alps") != std::string::npos) {
				path = p;
				found = true;
			}
		}
		close(fd);
	}
	closedir(d);
	return found;
}

/* Returns false if no touchpad-looking device is found, or it can't be
 * opened/grabbed -- the app then just keeps using SDL's own (broken, for
 * this device) handling, same as if this feature didn't exist. */
static bool touchpad_open(TouchpadReader &tp)
{
	std::string path;
	if (!touchpad_find_path(path))
		return false;

	tp.fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
	if (tp.fd < 0) {
		fprintf(stderr, "kbdlayout: cannot open %s: %s (touchpad fix disabled)\n",
		    path.c_str(), strerror(errno));
		return false;
	}

	struct input_absinfo abs_info;
	tp.has_mt = ioctl(tp.fd, EVIOCGABS(ABS_MT_SLOT), &abs_info) >= 0;
	if (tp.has_mt)
		tp.current_slot = abs_info.value;
	int xcode = tp.has_mt ? ABS_MT_POSITION_X : ABS_X;
	int ycode = tp.has_mt ? ABS_MT_POSITION_Y : ABS_Y;

	if (ioctl(tp.fd, EVIOCGABS(xcode), &abs_info) < 0) {
		close(tp.fd);
		tp.fd = -1;
		return false;
	}
	tp.range_x = abs_info.maximum - abs_info.minimum;
	/* A tenth of a millimeter of real sensor jitter shouldn't move the
	 * cursor; fall back to a small fraction of the range if the kernel
	 * doesn't report a resolution (units per mm) for this axis. */
	tp.deadzone_x = abs_info.resolution > 0 ? std::max(1, abs_info.resolution / 10) : std::max(1, tp.range_x / 3000);
	tp.tap_move_x = std::max(tp.deadzone_x * 3, tp.range_x / 50); /* ~2% of the pad: a tap, not a drag */

	if (ioctl(tp.fd, EVIOCGABS(ycode), &abs_info) < 0) {
		close(tp.fd);
		tp.fd = -1;
		return false;
	}
	tp.range_y = abs_info.maximum - abs_info.minimum;
	tp.deadzone_y = abs_info.resolution > 0 ? std::max(1, abs_info.resolution / 10) : std::max(1, tp.range_y / 3000);
	tp.tap_move_y = std::max(tp.deadzone_y * 3, tp.range_y / 50);

	if (tp.range_x <= 0 || tp.range_y <= 0) {
		close(tp.fd);
		tp.fd = -1;
		return false;
	}
	if (ioctl(tp.fd, EVIOCGRAB, 1) < 0) {
		fprintf(stderr, "kbdlayout: cannot grab %s: %s (touchpad fix disabled)\n", path.c_str(), strerror(errno));
		close(tp.fd);
		tp.fd = -1;
		return false;
	}
	char name[128] = "";
	ioctl(tp.fd, EVIOCGNAME(sizeof(name)), name);
	fprintf(stderr, "kbdlayout: touchpad fix active: %s (\"%s\", %s)\n",
	    path.c_str(), name, tp.has_mt ? "multitouch" : "single-touch");
	return true;
}

/* A tap is a touch shorter than this, that moved less than tap_move_x/y,
 * with no real button fired during it -- the usual libinput/X "synaptics
 * driver" default is close to this; there's no hardware signal for a tap,
 * so this is the best the application can judge on its own. */
static const uint64_t TOUCHPAD_TAP_MAX_US = 200000;

static uint64_t touchpad_event_time_us(const struct input_event &ev)
{
	return (uint64_t)ev.time.tv_sec * 1000000ULL + (uint64_t)ev.time.tv_usec;
}

/* Non-blocking: drains whatever the touchpad has reported since the last
 * call. Real motion past the deadzone moves the ImGui cursor by the
 * finger's delta, scaled from the pad's own physical range to the window
 * (the same ratio SDL used, just applied as relative motion instead of an
 * absolute position) times `sensitivity`. The baseline resets on every new
 * finger-down, synced from the current cursor position -- that read only
 * ever happens right at finger-down, never mid-gesture, so a burst of
 * several samples between two display frames still accumulates correctly
 * instead of each one recomputing from a stale position.
 *
 * Physical buttons (a clickpad's mechanical press, or separate buttons on
 * a non-clickpad touchpad) are real BTN_LEFT/RIGHT/MIDDLE key events,
 * forwarded as themselves. A soft tap isn't a button at all -- it's
 * synthesized here from short duration + little movement, like libinput
 * would, since nothing below the application does that for a raw-grabbed
 * device. */
static void touchpad_poll(TouchpadReader &tp, ImGuiIO &io, int ww, int wh, float sensitivity)
{
	if (tp.fd < 0)
		return;

	struct input_event ev;
	ssize_t n;
	while ((n = read(tp.fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
		if (ev.type == EV_KEY && (ev.code == BTN_LEFT || ev.code == BTN_RIGHT || ev.code == BTN_MIDDLE)) {
			int button = ev.code == BTN_LEFT ? 0 : ev.code == BTN_RIGHT ? 1 : 2;
			io.AddMouseButtonEvent(button, ev.value != 0);
			if (ev.value)
				tp.click_happened = true; /* a real press: don't also synthesize a tap click */
		} else if (!tp.has_mt && ev.type == EV_KEY && ev.code == BTN_TOUCH) {
			if (ev.value && !tp.down) {
				tp.down = true;
				tp.have_baseline = false;
				tp.moved_too_much = false;
				tp.click_happened = false;
				tp.down_time_us = touchpad_event_time_us(ev);
			} else if (!ev.value && tp.down) {
				tp.down = false;
				if (!tp.click_happened && !tp.moved_too_much &&
				    touchpad_event_time_us(ev) - tp.down_time_us <= TOUCHPAD_TAP_MAX_US) {
					io.AddMouseButtonEvent(0, true);
					io.AddMouseButtonEvent(0, false);
				}
			}
		} else if (ev.type == EV_ABS) {
			if (!tp.has_mt && ev.code == ABS_X) {
				tp.raw_x = ev.value;
			} else if (!tp.has_mt && ev.code == ABS_Y) {
				tp.raw_y = ev.value;
			} else if (tp.has_mt && ev.code == ABS_MT_SLOT) {
				tp.current_slot = ev.value;
			} else if (tp.has_mt && ev.code == ABS_MT_POSITION_X &&
			           (tp.primary_slot < 0 || tp.current_slot == tp.primary_slot)) {
				/* No ordering guarantee between ABS_MT_TRACKING_ID and
				 * ABS_MT_POSITION_X/Y within a report: a fresh touch's very
				 * first position sample can arrive before its tracking id
				 * does, while primary_slot is still unset. Trust it anyway
				 * when no primary is established yet -- there's only one
				 * candidate finger at that point by definition -- so it
				 * isn't silently dropped, leaving raw_x/y stuck at the
				 * previous gesture's last position as the new baseline. */
				tp.raw_x = ev.value;
			} else if (tp.has_mt && ev.code == ABS_MT_POSITION_Y &&
			           (tp.primary_slot < 0 || tp.current_slot == tp.primary_slot)) {
				tp.raw_y = ev.value;
			} else if (tp.has_mt && ev.code == ABS_MT_TRACKING_ID) {
				if (ev.value >= 0 && tp.primary_slot < 0) {
					tp.primary_slot = tp.current_slot;
					tp.down = true;
					tp.have_baseline = false;
					tp.moved_too_much = false;
					tp.click_happened = false;
					tp.down_time_us = touchpad_event_time_us(ev);
				} else if (ev.value < 0 && tp.current_slot == tp.primary_slot) {
					tp.primary_slot = -1;
					tp.down = false;
					if (!tp.click_happened && !tp.moved_too_much &&
					    touchpad_event_time_us(ev) - tp.down_time_us <= TOUCHPAD_TAP_MAX_US) {
						io.AddMouseButtonEvent(0, true);
						io.AddMouseButtonEvent(0, false);
					}
				}
				/* any other slot touching down or lifting while the primary
				 * finger is active: not tracked, deliberately ignored */
			}
		} else if (ev.type == EV_SYN && ev.code == SYN_REPORT && tp.down) {
			if (!tp.have_baseline) {
				tp.have_baseline = true;
				tp.down_x = tp.raw_x;
				tp.down_y = tp.raw_y;
				tp.cursor_x = io.MousePos.x;
				tp.cursor_y = io.MousePos.y;
				if (tp.cursor_x == -FLT_MAX || tp.cursor_y == -FLT_MAX) {
					tp.cursor_x = ww / 2.0f; /* no prior mouse position to move from */
					tp.cursor_y = wh / 2.0f;
				}
				io.AddMousePosEvent(tp.cursor_x, tp.cursor_y);
			} else {
				int dx = tp.raw_x - tp.last_x, dy = tp.raw_y - tp.last_y;
				int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
				float move_x = adx >= tp.deadzone_x ? dx * sensitivity * ww / (float)tp.range_x : 0.0f;
				float move_y = ady >= tp.deadzone_y ? dy * sensitivity * wh / (float)tp.range_y : 0.0f;
				if (move_x != 0.0f || move_y != 0.0f) {
					tp.cursor_x = std::clamp(tp.cursor_x + move_x, 0.0f, (float)ww - 1);
					tp.cursor_y = std::clamp(tp.cursor_y + move_y, 0.0f, (float)wh - 1);
					io.AddMousePosEvent(tp.cursor_x, tp.cursor_y);
				}
				int ddx = tp.raw_x - tp.down_x, ddy = tp.raw_y - tp.down_y;
				if ((ddx < 0 ? -ddx : ddx) >= tp.tap_move_x || (ddy < 0 ? -ddy : ddy) >= tp.tap_move_y)
					tp.moved_too_much = true;
			}
			tp.last_x = tp.raw_x;
			tp.last_y = tp.raw_y;
		}
	}
}

/* ---- Fonts ------------------------------------------------------------- */

static bool file_exists(const char *p)
{
	struct stat st;
	return p && stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

struct FontPair {
	const char *regular, *bold;
};

/* x11-fonts/noto-basic, x11-fonts/dejavu (FreeBSD), then Linux paths for testing. */
static const FontPair kFonts[] = {
	{"/usr/local/share/fonts/noto/NotoSans-Regular.ttf", "/usr/local/share/fonts/noto/NotoSans-Bold.ttf"},
	{"/usr/local/share/fonts/dejavu/DejaVuSans.ttf", "/usr/local/share/fonts/dejavu/DejaVuSans-Bold.ttf"},
	{"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf"},
	{"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"},
};
/* Wide Unicode coverage (Armenian, Georgian, box drawing...) merged as fallback. */
static const char *kFallback[] = {
	"/usr/local/share/fonts/dejavu/DejaVuSans.ttf",
	"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
};

struct Fonts {
	ImFont *regular = nullptr, *bold = nullptr;
	bool unicode = false;
};

static Fonts load_fonts(const char *user_font, float size)
{
	ImGuiIO &io = ImGui::GetIO();
	Fonts f;
	const char *reg = nullptr, *bold = nullptr;
	if (user_font && file_exists(user_font)) {
		reg = user_font;
	} else {
		for (const auto &p : kFonts)
			if (file_exists(p.regular)) {
				reg = p.regular;
				bold = file_exists(p.bold) ? p.bold : nullptr;
				break;
			}
	}
	const char *fallback = nullptr;
	for (const char *p : kFallback)
		if (file_exists(p) && (!reg || strcmp(p, reg) != 0)) {
			fallback = p;
			break;
		}

	auto add_with_fallback = [&](const char *path) -> ImFont * {
		ImFontConfig cfg;
		cfg.OversampleH = 2;
		ImFont *font = io.Fonts->AddFontFromFileTTF(path, size, &cfg);
		if (font && fallback) {
			ImFontConfig m;
			m.MergeMode = true;
			io.Fonts->AddFontFromFileTTF(fallback, size, &m);
		}
		return font;
	};

	if (reg) {
		f.regular = add_with_fallback(reg);
		f.unicode = f.regular != nullptr;
	}
	if (!f.regular) {
		fprintf(stderr, "kbdlayout: no TrueType font found, using the built-in ASCII font.\n"
		    "           Install one with: pkg install dejavu   (or noto-basic)\n");
		ImFontConfig cfg;
		cfg.SizePixels = size;
		f.regular = io.Fonts->AddFontDefault(&cfg);
	}
	f.bold = bold ? add_with_fallback(bold) : f.regular;
	if (!f.bold)
		f.bold = f.regular;
	return f;
}

/* ---- Theme ------------------------------------------------------------- */

namespace col {
const ImVec4 bg      = ImVec4(0.106f, 0.118f, 0.149f, 1.0f); /* #1B1E26 */
const ImVec4 panel   = ImVec4(0.078f, 0.090f, 0.114f, 1.0f); /* #14171D */
const ImVec4 frame   = ImVec4(0.149f, 0.165f, 0.204f, 1.0f); /* #262A34 */
const ImVec4 hover   = ImVec4(0.192f, 0.212f, 0.263f, 1.0f);
const ImVec4 border  = ImVec4(0.204f, 0.227f, 0.282f, 1.0f);
const ImVec4 text    = ImVec4(0.906f, 0.918f, 0.941f, 1.0f);
const ImVec4 dim     = ImVec4(0.553f, 0.584f, 0.651f, 1.0f);
const ImVec4 accent  = ImVec4(0.388f, 0.553f, 1.000f, 1.0f); /* #638DFF */
const ImVec4 accent2 = ImVec4(0.302f, 0.471f, 0.949f, 1.0f);
const ImVec4 good    = ImVec4(0.361f, 0.808f, 0.588f, 1.0f);
const ImVec4 warn    = ImVec4(0.965f, 0.690f, 0.329f, 1.0f);
}

static ImVec4 alpha(ImVec4 c, float a) { c.w = a; return c; }

static void apply_theme(float scale)
{
	ImGuiStyle &s = ImGui::GetStyle();
	s = ImGuiStyle();
	s.WindowPadding = ImVec2(22, 20);
	s.FramePadding = ImVec2(12, 8);
	s.ItemSpacing = ImVec2(10, 10);
	s.ItemInnerSpacing = ImVec2(8, 6);
	s.WindowRounding = 14;
	s.ChildRounding = 10;
	s.FrameRounding = 8;
	s.PopupRounding = 8;
	s.GrabRounding = 8;
	s.ScrollbarRounding = 8;
	s.ScrollbarSize = 10;
	s.WindowBorderSize = 1;
	s.ChildBorderSize = 1;
	s.FrameBorderSize = 0;
	s.SelectableTextAlign = ImVec2(0, 0.5f);
	s.ScaleAllSizes(scale);

	ImVec4 *c = s.Colors;
	c[ImGuiCol_Text] = col::text;
	c[ImGuiCol_TextDisabled] = col::dim;
	c[ImGuiCol_WindowBg] = col::bg;
	c[ImGuiCol_ChildBg] = col::panel;
	c[ImGuiCol_PopupBg] = col::bg;
	c[ImGuiCol_Border] = col::border;
	c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_FrameBg] = col::frame;
	c[ImGuiCol_FrameBgHovered] = col::hover;
	c[ImGuiCol_FrameBgActive] = col::hover;
	c[ImGuiCol_CheckMark] = ImVec4(1, 1, 1, 1);
	c[ImGuiCol_Button] = col::frame;
	c[ImGuiCol_ButtonHovered] = col::hover;
	c[ImGuiCol_ButtonActive] = col::border;
	c[ImGuiCol_Header] = alpha(col::accent, 0.30f);
	c[ImGuiCol_HeaderHovered] = alpha(col::text, 0.06f);
	c[ImGuiCol_HeaderActive] = alpha(col::accent, 0.40f);
	c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_ScrollbarGrab] = col::border;
	c[ImGuiCol_ScrollbarGrabHovered] = col::hover;
	c[ImGuiCol_ScrollbarGrabActive] = col::dim;
	c[ImGuiCol_Separator] = col::border;
	c[ImGuiCol_TextSelectedBg] = alpha(col::accent, 0.35f);
	c[ImGuiCol_NavCursor] = col::accent;
	c[ImGuiCol_InputTextCursor] = col::accent;
}

/* ---- Application ------------------------------------------------------- */

enum Result { RUNNING, ACCEPTED, CANCELLED };

struct App {
	std::vector<kbd::Layout> layouts;
	std::string dir;
	int current = -1;            /* active layout, -1 = unknown */
	std::string current_source;  /* how it was detected */
	int selected = -1;
	std::vector<int> visible;    /* indices matching the filter */
	char filter[128] = "";
	char test[1024] = "";
	bool save_default = false;
	bool can_save = false;
	kbd::Translator tr;
	bool test_active = false;
	Result result = RUNNING;
	bool scroll_to_selected = true;
	float scroll_align = 0.5f;
	int focus_request = 1;       /* 1 = search field, 2 = test field */
	Fonts fonts;
	float s = 1.0f;              /* UI scale */

	void select(int idx)
	{
		if (idx == selected || idx < 0)
			return;
		selected = idx;
		tr.set_keymap(&layouts[idx].km);
	}

	void refilter()
	{
		visible.clear();
		std::string f = filter;
		for (auto &c : f)
			c = (char)tolower((unsigned char)c);
		for (int i = 0; i < (int)layouts.size(); i++) {
			std::string hay = layouts[i].desc + " " + layouts[i].file;
			for (auto &c : hay)
				c = (char)tolower((unsigned char)c);
			if (f.empty() || hay.find(f) != std::string::npos)
				visible.push_back(i);
		}
		if (!visible.empty() &&
		    std::find(visible.begin(), visible.end(), selected) == visible.end()) {
			select(visible[0]);
			scroll_to_selected = true;
		}
	}
};

static void detect_current(App &app)
{
	kbd::Keymap kernel;
	std::string err;
	if (kbd::read_kernel_keymap(kernel, err)) {
		double best = 0;
		int best_i = -1;
		for (int i = 0; i < (int)app.layouts.size(); i++) {
			if (!app.layouts[i].ok)
				continue;
			double d = kbd::compare_keymaps(kernel, app.layouts[i].km);
			if (d > best) {
				best = d;
				best_i = i;
			}
		}
		/* Several files can be identical (fi = se): prefer the rc.conf one. */
		std::string rc = kbd::rcconf_keymap();
		if (best == 1.0 && !rc.empty())
			for (int i = 0; i < (int)app.layouts.size(); i++)
				if ((app.layouts[i].file == rc || app.layouts[i].file == rc + ".kbd") &&
				    kbd::compare_keymaps(kernel, app.layouts[i].km) == 1.0)
					best_i = i;
		if (best_i >= 0 && best >= 0.97) {
			app.current = best_i;
			app.current_source = best == 1.0 ? "loaded" : "closest match";
			return;
		}
		fprintf(stderr, "kbdlayout: the loaded keymap matches no file in %s\n",
		    app.dir.c_str());
	} else {
		fprintf(stderr, "kbdlayout: %s\n", err.c_str());
	}
	std::string rc = kbd::rcconf_keymap();
	const char *source = "from rc.conf";
	if (rc.empty()) {
		rc = "us.kbd";
		source = "assumed";
	}
	for (int i = 0; i < (int)app.layouts.size(); i++)
		if (app.layouts[i].file == rc || app.layouts[i].file == rc + ".kbd") {
			app.current = i;
			app.current_source = source;
		}
}

/* A tiny keyboard pictogram for the header. */
static void draw_keyboard_icon(ImDrawList *dl, ImVec2 p, float h)
{
	float w = h * 1.5f, r = h * 0.16f, k = h * 0.17f, gap = h * 0.07f;
	ImU32 c = ImGui::GetColorU32(col::accent);
	dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(alpha(col::accent, 0.16f)), r);
	dl->AddRect(p, ImVec2(p.x + w, p.y + h), c, r, 0, h * 0.06f);
	float x0 = p.x + h * 0.18f, y = p.y + h * 0.2f;
	for (int row = 0; row < 2; row++, y += k + gap)
		for (float x = x0 + row * k * 0.5f; x + k <= p.x + w - h * 0.15f; x += k + gap)
			dl->AddRectFilled(ImVec2(x, y), ImVec2(x + k, y + k), c, k * 0.25f);
	dl->AddRectFilled(ImVec2(p.x + w * 0.28f, y), ImVec2(p.x + w * 0.72f, y + k), c, k * 0.25f);
}

/* Small rounded label ("Caps", "Dead key ^"...). */
static void chip(const char *text, ImVec4 fg, bool filled)
{
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImVec2 ts = ImGui::CalcTextSize(text);
	ImVec2 pad(ImGui::GetFontSize() * 0.45f, ImGui::GetFontSize() * 0.12f);
	ImVec2 p = ImGui::GetCursorScreenPos();
	ImVec2 q(p.x + ts.x + pad.x * 2, p.y + ts.y + pad.y * 2);
	dl->AddRectFilled(p, q, ImGui::GetColorU32(alpha(fg, filled ? 0.22f : 0.10f)), ts.y);
	dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), ImGui::GetColorU32(fg), text);
	ImGui::Dummy(ImVec2(q.x - p.x, q.y - p.y));
}

static void draw_ui(App &app)
{
	ImGuiIO &io = ImGui::GetIO();
	ImGuiStyle &st = ImGui::GetStyle();
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	const float s = app.s;
	const float fs = ImGui::GetFontSize();

	/* Backdrop: soft vertical gradient. */
	ImDrawList *bg = ImGui::GetBackgroundDrawList();
	ImVec2 v0 = vp->Pos, v1(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
	bg->AddRectFilledMultiColor(v0, v1, IM_COL32(24, 30, 46, 255), IM_COL32(24, 30, 46, 255),
	    IM_COL32(9, 11, 16, 255), IM_COL32(9, 11, 16, 255));

	/* Window geometry: centered, sized from the font. */
	ImVec2 size(std::min(vp->Size.x * 0.92f, fs * 34.0f), std::min(vp->Size.y * 0.92f, fs * 32.0f));
	ImVec2 pos(vp->Pos.x + (vp->Size.x - size.x) * 0.5f, vp->Pos.y + (vp->Size.y - size.y) * 0.5f);

	/* Drop shadow. */
	for (int i = 1; i <= 16; i++) {
		float d = i * s * 1.5f;
		bg->AddRectFilled(ImVec2(pos.x - d, pos.y - d + 8 * s), ImVec2(pos.x + size.x + d, pos.y + size.y + d + 8 * s),
		    IM_COL32(0, 0, 0, 9), st.WindowRounding + d);
	}

	ImGui::SetNextWindowPos(pos);
	ImGui::SetNextWindowSize(size);
	ImGui::Begin("##kbdlayout", nullptr,
	    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	ImDrawList *dl = ImGui::GetWindowDrawList();

	/* Header */
	{
		float ih = fs * 1.9f;
		ImVec2 p = ImGui::GetCursorScreenPos();
		draw_keyboard_icon(dl, ImVec2(p.x, p.y + fs * 0.1f), ih);
		ImGui::SetCursorScreenPos(ImVec2(p.x + ih * 1.5f + fs * 0.8f, p.y - fs * 0.15f));
		ImGui::BeginGroup();
		ImGui::PushFont(app.fonts.bold, st.FontSizeBase * 1.3f);
		ImGui::TextUnformatted("Keyboard layout");
		ImGui::PopFont();
		ImGui::PushStyleColor(ImGuiCol_Text, col::dim);
		if (app.current >= 0)
			ImGui::Text("Current: %s  \xc2\xb7  %s%s%s%s", app.layouts[app.current].desc.c_str(),
			    app.layouts[app.current].file.c_str(),
			    app.current_source == "loaded" ? "" : "  (",
			    app.current_source == "loaded" ? "" : app.current_source.c_str(),
			    app.current_source == "loaded" ? "" : ")");
		else
			ImGui::TextUnformatted("Current layout: unknown");
		ImGui::PopStyleColor();
		ImGui::EndGroup();
		ImGui::Dummy(ImVec2(0, fs * 0.2f));
	}

	/* Keyboard navigation of the list works from any field. */
	if (!app.visible.empty()) {
		int n = (int)app.visible.size();
		int at = (int)(std::find(app.visible.begin(), app.visible.end(), app.selected) - app.visible.begin());
		if (at >= n)
			at = 0;
		int page = 8, to = at;
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) to = at - 1;
		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) to = at + 1;
		if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) to = at - page;
		if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) to = at + page;
		if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Home, false)) to = 0;
		if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_End, false)) to = n - 1;
		to = std::clamp(to, 0, n - 1);
		if (to != at || app.selected < 0) {
			app.scroll_align = to < at ? 0.0f : 1.0f;
			app.select(app.visible[to]);
			app.scroll_to_selected = true;
		}
	}

	/* Search */
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (app.focus_request == 1) {
		ImGui::SetKeyboardFocusHere();
		app.focus_request = 0;
	}
	if (ImGui::InputTextWithHint("##filter", "Search layouts...", app.filter, sizeof(app.filter)))
		app.refilter();

	/* The list takes whatever height is left above the test block and the
	 * bottom bar (which is pinned to the bottom edge of the window). */
	const float fh = ImGui::GetFrameHeight(), sp = st.ItemSpacing.y, lh = ImGui::GetTextLineHeight();
	const float chip_h = lh * 0.82f + fs * 0.24f;
	const float y_buttons = ImGui::GetWindowHeight() - st.WindowPadding.y - fh;
	const float y_rule = y_buttons - sp * 1.2f;
	float test_block = (fs * 0.1f + sp) + (lh + sp) + (fh + sp) + chip_h;
	float list_h = std::max(fh * 3, y_rule - sp * 1.2f - ImGui::GetCursorPosY() - sp - test_block);

	/* Layout list */
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6 * s, 6 * s));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2 * s));
	ImGui::BeginChild("list", ImVec2(0, list_h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
	{
		const float row_h = fs + 14 * s, padx = 12 * s;
		const float w = ImGui::GetContentRegionAvail().x;
		ImDrawList *ldl = ImGui::GetWindowDrawList();
		ImGuiListClipper clip;
		int sel_pos = (int)(std::find(app.visible.begin(), app.visible.end(), app.selected) - app.visible.begin());
		clip.Begin((int)app.visible.size(), row_h + 2 * s);
		if (app.scroll_to_selected && sel_pos < (int)app.visible.size())
			clip.IncludeItemByIndex(sel_pos);
		while (clip.Step()) {
			for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
				int idx = app.visible[r];
				const kbd::Layout &L = app.layouts[idx];
				bool sel = idx == app.selected;
				ImGui::PushID(idx);
				ImVec2 p = ImGui::GetCursorScreenPos();
				if (!L.ok)
					ImGui::BeginDisabled();
				ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7 * s);
				if (ImGui::Selectable("##row", sel, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(w, row_h))) {
					app.select(idx);
					app.focus_request = 2; /* go type in the test field */
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
						app.result = ACCEPTED;
				}
				ImGui::PopStyleVar();
				if (!L.ok)
					ImGui::EndDisabled();
				if (sel)
					ldl->AddRectFilled(ImVec2(p.x, p.y + row_h * 0.22f), ImVec2(p.x + 3 * s, p.y + row_h * 0.78f),
					    ImGui::GetColorU32(col::accent), 2 * s);

				float ty = p.y + (row_h - fs) * 0.5f;
				ImVec2 fsz = ImGui::CalcTextSize(L.file.c_str());
				float fx = p.x + w - padx - fsz.x;
				float x = p.x + padx;
				if (idx == app.current) {
					float r2 = fs * 0.2f;
					ldl->AddCircleFilled(ImVec2(x + r2, p.y + row_h * 0.5f), r2, ImGui::GetColorU32(col::good));
				}
				x += fs * 0.9f;
				ldl->PushClipRect(ImVec2(x, p.y), ImVec2(fx - fs * 0.8f, p.y + row_h), true);
				ldl->AddText(ImVec2(x, ty), ImGui::GetColorU32(L.ok ? col::text : col::dim), L.desc.c_str());
				ldl->PopClipRect();
				ldl->AddText(ImVec2(fx, ty), ImGui::GetColorU32(L.ok ? col::dim : col::warn),
				    L.ok ? L.file.c_str() : "parse error");
				if (!L.ok && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("%s: %s", L.file.c_str(), L.err.c_str());

				if (sel && app.scroll_to_selected) {
					float top = ImGui::GetWindowPos().y, bot = top + ImGui::GetWindowHeight();
					if (app.scroll_align == 0.5f || p.y < top || p.y + row_h > bot)
						ImGui::SetScrollHereY(app.scroll_align);
					app.scroll_to_selected = false;
				}
				ImGui::PopID();
			}
		}
		if (app.visible.empty()) {
			ImGui::Dummy(ImVec2(0, fs * 0.5f));
			ImGui::PushStyleColor(ImGuiCol_Text, col::dim);
			ImGui::SetCursorPosX(padx);
			ImGui::TextUnformatted("No layout matches your search.");
			ImGui::PopStyleColor();
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);

	/* Test field */
	ImGui::Dummy(ImVec2(0, fs * 0.1f));
	ImGui::PushStyleColor(ImGuiCol_Text, col::dim);
	if (app.selected >= 0)
		ImGui::Text("Try \"%s\"", app.layouts[app.selected].desc.c_str());
	else
		ImGui::TextUnformatted("Try it");
	ImGui::PopStyleColor();

	float clear_w = ImGui::CalcTextSize("Clear").x + st.FramePadding.x * 2;
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clear_w - st.ItemSpacing.x);
	if (app.focus_request == 2) {
		ImGui::SetKeyboardFocusHere();
		app.focus_request = 0;
	}
	ImGui::PushStyleColor(ImGuiCol_FrameBg, col::panel);
	ImGui::InputTextWithHint("##test", "Type here to test the selected layout", app.test, sizeof(app.test));
	ImGui::PopStyleColor();
	app.test_active = ImGui::IsItemActive();
	ImGui::SameLine();
	if (ImGui::Button("Clear")) {
		app.test[0] = 0;
		app.focus_request = 2;
	}

	/* Status chips: locks, pending dead key, second layer. */
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6 * s, 0));
		bool any = false;
		auto sep = [&]() { if (any) ImGui::SameLine(); any = true; };
		ImGui::PushFont(nullptr, st.FontSizeBase * 0.82f);
		if (app.tr.caps_lock()) { sep(); chip("Caps Lock", col::warn, true); }
		if (uint32_t a = app.tr.pending_accent()) {
			char b[8], t[48];
			kbd::utf8_encode(a, b);
			snprintf(t, sizeof(t), "Dead key %s \xe2\x80\x93 type a letter", b);
			sep();
			chip(t, col::accent, true);
		}
		if (app.selected >= 0) {
			const kbd::Keymap &km = app.layouts[app.selected].km;
			std::string hint = km.second_layer_hint();
			if (!hint.empty()) {
				std::string t = (app.tr.second_layer_active() ? "Layer 2 on \xc2\xb7 switch: " : "Layer 2: ") + hint;
				sep();
				chip(t.c_str(), app.tr.second_layer_active() ? col::good : col::dim, app.tr.second_layer_active());
			}
		}
		if (!any)
			ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight() + fs * 0.24f));
		ImGui::PopFont();
		ImGui::PopStyleVar();
	}

	/* Bottom: save option + buttons, pinned to the window bottom. */
	float bw = std::max(fs * 6.5f, ImGui::CalcTextSize("Cancel").x + st.FramePadding.x * 4);
	{
		ImVec2 wp = ImGui::GetWindowPos();
		float ly = wp.y + y_rule;
		dl->AddLine(ImVec2(wp.x + st.WindowPadding.x, ly),
		    ImVec2(wp.x + ImGui::GetWindowWidth() - st.WindowPadding.x, ly), ImGui::GetColorU32(col::border));
	}
	ImGui::SetCursorPosY(y_buttons);

	ImGui::AlignTextToFramePadding();
	if (!app.can_save)
		ImGui::BeginDisabled();
	ImGui::Checkbox("Make default at boot", &app.save_default);
	if (!app.can_save) {
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Run as root to save keymap= in /etc/rc.conf");
	} else if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Also set keymap= in /etc/rc.conf");
	}

	ImGui::SameLine(ImGui::GetWindowWidth() - st.WindowPadding.x - bw * 2 - st.ItemSpacing.x);
	if (ImGui::Button("Cancel", ImVec2(bw, 0)))
		app.result = CANCELLED;
	ImGui::SameLine();
	bool can_ok = app.selected >= 0 && app.layouts[app.selected].ok;
	if (!can_ok)
		ImGui::BeginDisabled();
	ImGui::PushStyleColor(ImGuiCol_Button, col::accent2);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col::accent);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, col::accent2);
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
	if (ImGui::Button("OK", ImVec2(bw, 0)))
		app.result = ACCEPTED;
	ImGui::PopStyleColor(4);
	if (!can_ok)
		ImGui::EndDisabled();

	/* Global shortcuts */
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		app.result = CANCELLED;
	if ((ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) && can_ok)
		app.result = ACCEPTED;

	ImGui::End();
}

/* ---- Testing helper: type ASCII text as positional (US) key presses ---- */

static void push_key(SDL_Scancode sc, bool down, SDL_WindowID win)
{
	SDL_Event e;
	SDL_zero(e);
	e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	e.key.scancode = sc;
	e.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
	e.key.down = down;
	e.key.windowID = win;
	SDL_PushEvent(&e);
}

static bool ascii_to_scancode(char c, SDL_Scancode &sc, bool &shift)
{
	static const char *lower = "abcdefghijklmnopqrstuvwxyz";
	shift = false;
	if (c >= 'A' && c <= 'Z') { shift = true; c = (char)tolower(c); }
	if (const char *p = strchr(lower, c); p && *p) { sc = (SDL_Scancode)(SDL_SCANCODE_A + (p - lower)); return true; }
	if (c >= '1' && c <= '9') { sc = (SDL_Scancode)(SDL_SCANCODE_1 + (c - '1')); return true; }
	switch (c) {
	case '0': sc = SDL_SCANCODE_0; return true;
	case ' ': sc = SDL_SCANCODE_SPACE; return true;
	case '[': sc = SDL_SCANCODE_LEFTBRACKET; return true;
	case ']': sc = SDL_SCANCODE_RIGHTBRACKET; return true;
	case ';': sc = SDL_SCANCODE_SEMICOLON; return true;
	case '\'': sc = SDL_SCANCODE_APOSTROPHE; return true;
	case ',': sc = SDL_SCANCODE_COMMA; return true;
	case '.': sc = SDL_SCANCODE_PERIOD; return true;
	case '/': sc = SDL_SCANCODE_SLASH; return true;
	case '-': sc = SDL_SCANCODE_MINUS; return true;
	case '=': sc = SDL_SCANCODE_EQUALS; return true;
	case '~': sc = SDL_SCANCODE_CAPSLOCK; return true; /* '~' = tap CapsLock */
	case '\n': sc = SDL_SCANCODE_RETURN; return true;
	case '\x1b': sc = SDL_SCANCODE_ESCAPE; return true;
	default: return false;
	}
}

/* ---- main -------------------------------------------------------------- */

static void usage()
{
	fprintf(stderr,
	    "usage: kbdlayout [-n] [-w] [-d keymapdir] [-f font.ttf] [-L lang]\n"
	    "                 [-- COMMAND [ARG...]]\n"
	    "  -n        dry run: print the commands instead of running them\n"
	    "  -w        windowed (when running under X11/Wayland with a shared SDL)\n"
	    "  -d DIR    keymap directory (default /usr/share/vt/keymaps)\n"
	    "  -f FONT   TrueType font to use\n"
	    "  -L LANG   language of the descriptions (default from $LANG)\n"
	    "  -- COMMAND [ARG...]\n"
	    "            after a successful layout change, replace kbdlayout with\n"
	    "            COMMAND (e.g. an editor) on the same console -- not run on\n"
	    "            cancel, error, or a dry run (where it's only printed)\n"
	    "testing:\n"
	    "  -p FILE   preselect FILE (e.g. fr.acc.kbd)\n"
	    "  -t TEXT   type TEXT into the test field as US-positional keys\n"
	    "  -s SHOT   save a BMP screenshot and exit\n"
	    "  -g        disable the touchpad fix (raw evdev relative tracking)\n"
	    "  -T SENS   touchpad sensitivity, default 1.0\n"
	    "exit status: 0 = layout applied, 1 = cancelled, 2 = error\n");
}

int main(int argc, char **argv)
{
	App app;
	app.dir = kbd::default_keymap_dir();
	const char *font_path = nullptr, *shot = nullptr, *preselect = nullptr, *typed = nullptr;
	std::string lang = kbd::user_language();
	bool dry_run = false, windowed = false, touchpad_fix = true;
	float touchpad_sensitivity = 1.0f;

	int opt;
	while ((opt = getopt(argc, argv, "nwd:f:L:p:t:s:gT:h")) != -1) {
		switch (opt) {
		case 'n': dry_run = true; break;
		case 'w': windowed = true; break;
		case 'd': app.dir = optarg; break;
		case 'f': font_path = optarg; break;
		case 'L': lang = optarg; break;
		case 'p': preselect = optarg; break;
		case 't': typed = optarg; break;
		case 's': shot = optarg; break;
		case 'g': touchpad_fix = false; break;
		case 'T': touchpad_sensitivity = (float)atof(optarg); break;
		default: usage(); return 2;
		}
	}
	std::vector<std::string> launch_cmd(argv + optind, argv + argc);

	app.layouts = kbd::load_layouts(app.dir, lang);
	if (app.layouts.empty()) {
		fprintf(stderr, "kbdlayout: no keymaps found in %s\n", app.dir.c_str());
		return 2;
	}
	detect_current(app);   /* before SDL takes over the keyboard */
	app.can_save = geteuid() == 0 || dry_run;

	int initial = app.current >= 0 ? app.current : 0;
	if (preselect)
		for (int i = 0; i < (int)app.layouts.size(); i++)
			if (app.layouts[i].file == preselect)
				initial = i;
	app.select(initial);
	app.refilter();

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
		fprintf(stderr, "kbdlayout: SDL_Init: %s\n", SDL_GetError());
		return 2;
	}

	TouchpadReader touchpad;
	if (touchpad_fix && !touchpad_open(touchpad))
		touchpad_fix = false; /* no touchpad found, or couldn't grab it: fall back to plain SDL */

	const char *driver = SDL_GetCurrentVideoDriver();
	bool console = driver && (strcmp(driver, "kmsdrm") == 0 || strcmp(driver, "offscreen") == 0);
	const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
	int sw = dm ? dm->w : 1280, sh = dm ? dm->h : 800;
	if (windowed || !console) {
		sw = 1100;
		sh = 820;
	}
	SDL_Window *win = SDL_CreateWindow("Keyboard layout", sw, sh,
	    (windowed || !console) ? 0 : SDL_WINDOW_FULLSCREEN);
	SDL_Renderer *ren = win ? SDL_CreateRenderer(win, nullptr) : nullptr;
	if (!ren) {
		fprintf(stderr, "kbdlayout: cannot open the display: %s\n"
		    "Run it from a text console (not under X), with access to /dev/dri.\n",
		    SDL_GetError());
		SDL_Quit();
		return 2;
	}
	SDL_SetRenderVSync(ren, 1);

	SDL_Keymod mods = SDL_GetModState();
	app.tr.set_locks(mods & SDL_KMOD_CAPS, mods & SDL_KMOD_NUM);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;
	if (console && strcmp(driver, "kmsdrm") == 0)
		io.MouseDrawCursor = true; /* no system cursor on the console */

	/* Scale everything from the screen height: ~22px text on 1080p. */
	float font_px = std::clamp(std::round(sh / 48.0f), 15.0f, 40.0f);
	app.s = font_px / 16.0f;
	apply_theme(app.s);
	app.fonts = load_fonts(font_path, font_px);
	ImGui::GetStyle().FontSizeBase = font_px;

	ImGui_ImplSDL3_InitForSDLRenderer(win, ren);
	ImGui_ImplSDLRenderer3_Init(ren);

	/* Scripted typing for -t: one key event per frame once the field is focused. */
	std::vector<std::pair<SDL_Scancode, bool>> script;
	if (typed) {
		app.focus_request = 2;
		for (const char *c = typed; *c; c++) {
			SDL_Scancode sc;
			bool shift;
			if (!ascii_to_scancode(*c, sc, shift))
				continue;
			if (shift) script.push_back({SDL_SCANCODE_LSHIFT, true});
			script.push_back({sc, true});
			script.push_back({sc, false});
			if (shift) script.push_back({SDL_SCANCODE_LSHIFT, false});
		}
	}
	size_t script_pos = 0;
	int frame = 0;

	while (app.result == RUNNING) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
				int kc = sdl_to_fbsd(e.key.scancode);
				if (kc >= 0) {
					uint32_t cp = app.tr.key(kc, e.type == SDL_EVENT_KEY_DOWN, e.key.repeat);
					if (cp && app.test_active)
						io.AddInputCharacter(cp);
				}
			}
			/* In the test field, text comes from our translator, not SDL. */
			if (e.type == SDL_EVENT_TEXT_INPUT && app.test_active)
				continue;
			if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST)
				app.tr.reset_modifiers(); /* VT switched away */
			ImGui_ImplSDL3_ProcessEvent(&e);
			if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
				app.result = CANCELLED;
		}
		if (touchpad_fix)
			touchpad_poll(touchpad, io, sw, sh, touchpad_sensitivity);
		if (frame >= 3 && script_pos < script.size()) {
			push_key(script[script_pos].first, script[script_pos].second, SDL_GetWindowID(win));
			script_pos++;
		}

		ImGui_ImplSDLRenderer3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();
		draw_ui(app);
		ImGui::Render();

		SDL_SetRenderDrawColor(ren, 9, 11, 16, 255);
		SDL_RenderClear(ren);
		ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);
		frame++;
		if (shot && frame > 6 && script_pos >= script.size()) {
			if (SDL_Surface *surf = SDL_RenderReadPixels(ren, nullptr)) {
				SDL_SaveBMP(surf, shot);
				SDL_DestroySurface(surf);
			}
			app.result = CANCELLED;
		}
		SDL_RenderPresent(ren);
		if (!console)
			SDL_Delay(1);
	}

	ImGui_ImplSDLRenderer3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit(); /* gives the keyboard back to the console */

	if (app.result != ACCEPTED || shot)
		return 1;

	const kbd::Layout &L = app.layouts[app.selected];
	std::vector<std::string> load = {"/usr/sbin/kbdcontrol", "-l", L.path};
	std::vector<std::string> save = {"/usr/sbin/sysrc", "keymap=" + L.file};
	if (dry_run) {
		printf("%s %s %s\n", load[0].c_str(), load[1].c_str(), load[2].c_str());
		if (app.save_default)
			printf("%s %s\n", save[0].c_str(), save[1].c_str());
		if (!launch_cmd.empty()) {
			for (size_t i = 0; i < launch_cmd.size(); i++)
				printf("%s%s", i ? " " : "", launch_cmd[i].c_str());
			printf("\n");
		}
		return 0;
	}
	int tty = console_tty_fd();
	if (run_command(load, tty == STDIN_FILENO ? -1 : tty) != 0) {
		fprintf(stderr, "kbdlayout: kbdcontrol failed to load %s\n", L.path.c_str());
		return 2;
	}
	printf("Keyboard layout set to %s (%s)\n", L.desc.c_str(), L.file.c_str());
	if (app.save_default) {
		if (run_command(save, -1) != 0) {
			fprintf(stderr, "kbdlayout: sysrc failed, /etc/rc.conf not changed\n");
			return 2;
		}
		printf("Saved in /etc/rc.conf: keymap=\"%s\"\n", L.file.c_str());
	}

	if (!launch_cmd.empty()) {
		std::vector<char *> largv;
		for (auto &a : launch_cmd)
			largv.push_back(const_cast<char *>(a.c_str()));
		largv.push_back(nullptr);
		execvp(largv[0], largv.data());
		fprintf(stderr, "kbdlayout: cannot run %s: %s (layout was already applied)\n",
		    launch_cmd[0].c_str(), strerror(errno));
		return 2;
	}
	return 0;
}
