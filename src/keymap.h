/*
 * keymap.h - FreeBSD console keymap (.kbd) parsing, key translation and
 * detection of the keymap currently loaded in the kernel.
 *
 * This part has no SDL or ImGui dependency so it can be unit-tested.
 */
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace kbd {

/* Same sizes and values as <sys/kbio.h>; checked against it on FreeBSD. */
constexpr int NUM_KEYS = 256;
constexpr int NUM_STATES = 8;
constexpr int NUM_DEADKEYS = 15;
constexpr int ALTGR_OFFSET = 128;

enum Special : uint32_t {
	NOP = 0x00, LSH = 0x02, RSH = 0x03, CLK = 0x04, NLK = 0x05, SLK = 0x06,
	LALT = 0x07, BTAB = 0x08, LCTR = 0x09, NEXT = 0x0a,
	F_SCR = 0x0b, L_SCR = 0x1a, F_FN = 0x1b, L_FN = 0x7a,
	RCTR = 0x80, RALT = 0x81, ALK = 0x82, ASH = 0x83, META = 0x84,
	RBT = 0x85, DBG = 0x86, SUSP = 0x87, SPSC = 0x88,
	F_ACC = 0x89, L_ACC = 0x97, /* dead keys dgra .. dcar */
	STBY = 0x98, PREV = 0x99, PNC = 0x9a,
	LSHA = 0x9b, RSHA = 0x9c, LCTRA = 0x9d, RCTRA = 0x9e,
	LALTA = 0x9f, RALTA = 0xa0, HALT = 0xa1, PDWN = 0xa2, PASTE = 0xa3,
};

enum LockFlag : uint8_t { LOCK_O = 0, LOCK_C = 1, LOCK_N = 2 };

struct KeyEntry {
	uint32_t map[NUM_STATES] = {};
	uint8_t spcl = 0;  /* bit (0x80 >> state) set: map[state] is a Special */
	uint8_t flgs = 0;  /* LockFlag bits */

	bool is_special(int state) const { return spcl & (0x80 >> state); }
};

struct Accent {
	uint32_t accchar = 0; /* 0 = dead key not defined */
	std::vector<std::pair<uint32_t, uint32_t>> map;
};

struct Keymap {
	KeyEntry keys[NUM_KEYS];
	int n_keys = 0;
	Accent acc[NUM_DEADKEYS];

	bool has_second_layer() const;          /* keys >= 128 defined */
	std::string second_layer_hint() const;  /* "CapsLock", "hold Right Alt"... */
};

/* Parse a kbdcontrol(1) keymap file. Returns false and sets err on error. */
bool parse_keymap_file(const std::string &path, Keymap &out, std::string &err);
bool parse_keymap_text(const std::string &text, Keymap &out, std::string &err);

/* One entry of the layout list. */
struct Layout {
	std::string file;  /* "fr.acc.kbd" */
	std::string path;  /* "/usr/share/vt/keymaps/fr.acc.kbd" */
	std::string desc;  /* "French (accent keys)" */
	Keymap km;
	bool ok = false;
	std::string err;
};

/* Default keymap directory: vt or syscons, whichever the console uses. */
std::string default_keymap_dir();

/*
 * Load every *.kbd in dir, with descriptions from INDEX.keymaps in the
 * given language ("fr", "de"...; falls back to "en", then the file name).
 * The result is sorted by description.
 */
std::vector<Layout> load_layouts(const std::string &dir, const std::string &lang);

/* Two-letter language from LC_ALL / LC_MESSAGES / LANG, default "en". */
std::string user_language();

/*
 * Read the keymap currently loaded in the kernel (GIO_KEYMAP on the
 * console tty). Returns false where unsupported (not FreeBSD, not a vt).
 */
bool read_kernel_keymap(Keymap &out, std::string &err);

/* Fraction (0..1) of identical entries; 1.0 = same keymap. */
double compare_keymaps(const Keymap &a, const Keymap &b);

/* keymap= value from /etc/rc.conf (via sysrc), "" if unset. */
std::string rcconf_keymap();

/*
 * Mirrors the kernel's genkbd_keyaction() so that typing can be previewed
 * with any keymap without loading it. Feed it FreeBSD keycodes.
 */
class Translator {
public:
	void set_keymap(const Keymap *km);
	void set_locks(bool caps, bool num) { clked_ = caps; nlked_ = num; }

	/*
	 * Process a key press (down=true) or release. Returns a Unicode code
	 * point to insert, or 0 if the key produced no printable character.
	 */
	uint32_t key(int keycode, bool down, bool repeat);

	bool caps_lock() const { return clked_; }
	bool num_lock() const { return nlked_; }
	bool alt_lock() const { return alked_; }
	bool second_layer_active() const;
	uint32_t pending_accent() const; /* accent char waiting, or 0 */
	void reset_modifiers();

private:
	const Keymap *km_ = nullptr;
	/* held modifiers */
	bool lsh_ = false, rsh_ = false, lctr_ = false, rctr_ = false;
	bool lalt_ = false, ralt_ = false, ash_ = false, meta_ = false;
	/* locks and their "key is down" guards */
	bool clked_ = false, nlked_ = false, alked_ = false;
	bool clk_down_ = false, nlk_down_ = false, alk_down_ = false;
	bool shiftaon_ = false; /* lshifta & co pressed alone so far */
	int accent_ = 0;        /* 1-based dead key index, 0 = none */
	uint32_t lastact_[NUM_KEYS] = {};
};

/* FreeBSD keycode names used in hints ("CapsLock", "Right Alt"...). */
const char *keycode_name(int keycode);

/* Encode a code point as UTF-8 into buf (needs 5 bytes). */
void utf8_encode(uint32_t cp, char *buf);

} // namespace kbd
