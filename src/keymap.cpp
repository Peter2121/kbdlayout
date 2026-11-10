/*
 * keymap.cpp - FreeBSD console keymap parsing and translation.
 *
 * The parser follows usr.sbin/kbdcontrol (lex.l + kbdcontrol.c) and the
 * translator follows genkbd_keyaction() in sys/dev/kbd/kbd.c, so the test
 * field behaves like the real console would with the selected keymap.
 */
#include "keymap.h"
#include "kernel_keymap.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>

namespace kbd {

/* ---- Tokenizer (mirrors kbdcontrol's lex.l) ---------------------------- */

namespace {

enum TokType { T_END, T_ERR, T_NUM, T_LET, T_SPEC, T_ACC, T_FLAG, T_LP, T_RP };

struct Token {
	TokType type;
	uint32_t value;
	int line;
};

struct NameDef {
	const char *name;
	TokType type;
	uint32_t value;
};

/* Words recognised by lex.l. Numbers are plain characters, the rest special. */
const NameDef kNames[] = {
	{"nop", T_SPEC, NOP}, {"lshift", T_SPEC, LSH}, {"rshift", T_SPEC, RSH},
	{"clock", T_SPEC, CLK}, {"nlock", T_SPEC, NLK}, {"slock", T_SPEC, SLK},
	{"lalt", T_SPEC, LALT}, {"alt", T_SPEC, LALT}, {"btab", T_SPEC, BTAB},
	{"lctrl", T_SPEC, LCTR}, {"ctrl", T_SPEC, LCTR}, {"nscr", T_SPEC, NEXT},
	{"pscr", T_SPEC, PREV}, {"rctrl", T_SPEC, RCTR}, {"ralt", T_SPEC, RALT},
	{"alock", T_SPEC, ALK}, {"ashift", T_SPEC, ASH}, {"meta", T_SPEC, META},
	{"boot", T_SPEC, RBT}, {"debug", T_SPEC, DBG}, {"susp", T_SPEC, SUSP},
	{"saver", T_SPEC, SPSC}, {"panic", T_SPEC, PNC},
	{"lshifta", T_SPEC, LSHA}, {"shifta", T_SPEC, LSHA},
	{"rshifta", T_SPEC, RSHA}, {"lctrla", T_SPEC, LCTRA},
	{"ctrla", T_SPEC, LCTRA}, {"rctrla", T_SPEC, RCTRA},
	{"lalta", T_SPEC, LALTA}, {"alta", T_SPEC, LALTA},
	{"ralta", T_SPEC, RALTA}, {"halt", T_SPEC, HALT}, {"pdwn", T_SPEC, PDWN},
	{"paste", T_SPEC, PASTE},

	{"nul", T_NUM, 0}, {"soh", T_NUM, 1}, {"stx", T_NUM, 2}, {"etx", T_NUM, 3},
	{"eot", T_NUM, 4}, {"enq", T_NUM, 5}, {"ack", T_NUM, 6}, {"bel", T_NUM, 7},
	{"bs", T_NUM, 8}, {"ht", T_NUM, 9}, {"lf", T_NUM, 10}, {"nl", T_NUM, 10},
	{"vt", T_NUM, 11}, {"ff", T_NUM, 12}, {"np", T_NUM, 12}, {"cr", T_NUM, 13},
	{"so", T_NUM, 14}, {"si", T_NUM, 15}, {"dle", T_NUM, 16}, {"dc1", T_NUM, 17},
	{"dc2", T_NUM, 18}, {"dc3", T_NUM, 19}, {"dc4", T_NUM, 20}, {"nak", T_NUM, 21},
	{"syn", T_NUM, 22}, {"etb", T_NUM, 23}, {"can", T_NUM, 24}, {"em", T_NUM, 25},
	{"sub", T_NUM, 26}, {"esc", T_NUM, 27}, {"fs", T_NUM, 28}, {"gs", T_NUM, 29},
	{"rs", T_NUM, 30}, {"ns", T_NUM, 31}, {"us", T_NUM, 31}, {"sp", T_NUM, 32},
	{"del", T_NUM, 127},

	{"dgra", T_ACC, 0}, {"dacu", T_ACC, 1}, {"dcir", T_ACC, 2}, {"dtil", T_ACC, 3},
	{"dmac", T_ACC, 4}, {"dbre", T_ACC, 5}, {"ddot", T_ACC, 6}, {"duml", T_ACC, 7},
	{"ddia", T_ACC, 7}, {"dsla", T_ACC, 8}, {"drin", T_ACC, 9}, {"dced", T_ACC, 10},
	{"dapo", T_ACC, 11}, {"ddac", T_ACC, 12}, {"dogo", T_ACC, 13},
	{"dcar", T_ACC, 14},
};

/* Control and accent names exist in upper case too (NUL, DGRA...). */
bool lookup_name(const std::string &w, Token &t)
{
	for (const auto &n : kNames) {
		if (w == n.name) {
			t.type = n.type;
			t.value = n.value;
			return true;
		}
		if (n.type != T_SPEC) {
			std::string up = n.name;
			for (auto &c : up)
				c = (char)toupper((unsigned char)c);
			if (w == up) {
				t.type = n.type;
				t.value = n.value;
				return true;
			}
		}
	}
	return false;
}

class Lexer {
public:
	explicit Lexer(const std::string &s) : s_(s) {}

	Token next()
	{
		for (;;) {
			if (p_ >= s_.size())
				return {T_END, 0, line_};
			char c = s_[p_];
			if (c == '\n') {
				line_++;
				p_++;
			} else if (c == ' ' || c == '\t' || c == '\r') {
				p_++;
			} else if (c == '#') {
				while (p_ < s_.size() && s_[p_] != '\n')
					p_++;
			} else {
				break;
			}
		}
		char c = s_[p_];
		if (c == '\'' && p_ + 2 < s_.size() && s_[p_ + 2] == '\'') {
			uint32_t v = (unsigned char)s_[p_ + 1];
			p_ += 3;
			return {T_LET, v, line_};
		}
		if (c == '(') {
			p_++;
			return {T_LP, 0, line_};
		}
		if (c == ')') {
			p_++;
			return {T_RP, 0, line_};
		}
		if (isdigit((unsigned char)c)) {
			size_t start = p_;
			if (c == '0' && p_ + 1 < s_.size() && (s_[p_ + 1] == 'x' || s_[p_ + 1] == 'X')) {
				p_ += 2;
				while (p_ < s_.size() && isxdigit((unsigned char)s_[p_]))
					p_++;
				return {T_NUM, (uint32_t)strtoul(s_.substr(start, p_ - start).c_str(), nullptr, 16), line_};
			}
			while (p_ < s_.size() && isdigit((unsigned char)s_[p_]))
				p_++;
			return {T_NUM, (uint32_t)strtoul(s_.substr(start, p_ - start).c_str(), nullptr, 10), line_};
		}
		if (isalpha((unsigned char)c)) {
			size_t start = p_;
			while (p_ < s_.size() && isalnum((unsigned char)s_[p_]))
				p_++;
			std::string w = s_.substr(start, p_ - start);
			Token t{T_ERR, 0, line_};
			if (w.size() == 1 && strchr("OCNB", w[0])) {
				t.type = T_FLAG;
				t.value = (uint32_t)(strchr("OCNB", w[0]) - "OCNB");
				return t;
			}
			if (lookup_name(w, t))
				return t;
			if (w.rfind("fkey", 0) == 0 && w.size() > 4 && isdigit((unsigned char)w[4])) {
				uint32_t n = (uint32_t)atoi(w.c_str() + 4);
				t.type = T_SPEC;
				t.value = n + F_FN - 1;
				if (t.value > L_FN)
					t.type = T_ERR;
				return t;
			}
			if (w.rfind("scr", 0) == 0 && w.size() > 3 && isdigit((unsigned char)w[3])) {
				uint32_t n = (uint32_t)atoi(w.c_str() + 3);
				t.type = T_SPEC;
				t.value = n + F_SCR - 1;
				if (t.value > L_SCR)
					t.type = T_ERR;
				return t;
			}
			return t; /* unknown word */
		}
		p_++;
		return {T_ERR, (uint32_t)(unsigned char)c, line_};
	}

private:
	const std::string &s_;
	size_t p_ = 0;
	int line_ = 1;
};

} // namespace

/* ---- Parser (mirrors kbdcontrol.c get_*_definition_line) --------------- */

bool parse_keymap_text(const std::string &text, Keymap &out, std::string &err)
{
	out = Keymap();
	Lexer lx(text);
	Token t = lx.next();
	char buf[128];

	auto fail = [&](const char *what, int line) {
		snprintf(buf, sizeof(buf), "line %d: %s", line, what);
		err = buf;
		return false;
	};

	while (t.type != T_END) {
		if (t.type == T_NUM) {
			/* key definition: keycode, 8 actions, lock flag */
			if (t.value >= (uint32_t)NUM_KEYS)
				return fail("key number out of range", t.line);
			KeyEntry &k = out.keys[t.value];
			int keycode = (int)t.value;
			k.spcl = 0;
			for (int i = 0; i < NUM_STATES; i++) {
				Token e = lx.next();
				switch (e.type) {
				case T_NUM:
					if (e.value > 0x10FFFF)
						return fail("character out of range", e.line);
					k.map[i] = e.value;
					break;
				case T_LET:
					k.map[i] = e.value;
					break;
				case T_SPEC:
					k.map[i] = e.value;
					k.spcl |= (uint8_t)(0x80 >> i);
					break;
				case T_ACC:
					k.map[i] = F_ACC + e.value;
					k.spcl |= (uint8_t)(0x80 >> i);
					break;
				default:
					return fail("invalid key definition", e.line);
				}
			}
			Token f = lx.next();
			if (f.type != T_FLAG)
				return fail("missing lock flag (O, C, N or B)", f.line);
			k.flgs = (uint8_t)f.value;
			out.n_keys = std::max(out.n_keys, keycode + 1);
			t = lx.next();
		} else if (t.type == T_ACC) {
			/* accent definition: dead key, its char, (base accented)... */
			Accent &a = out.acc[t.value];
			if (a.accchar != 0)
				return fail("duplicated accent key definition", t.line);
			Token c = lx.next();
			if (c.type != T_NUM && c.type != T_LET)
				return fail("invalid accent character", c.line);
			a.accchar = c.value;
			for (t = lx.next(); t.type == T_LP; t = lx.next()) {
				Token c1 = lx.next(), c2 = lx.next(), rp = lx.next();
				if ((c1.type != T_NUM && c1.type != T_LET) ||
				    (c2.type != T_NUM && c2.type != T_LET) || rp.type != T_RP)
					return fail("invalid accent pair", c1.line);
				if (a.map.size() < 52) /* NUM_ACCENTCHARS */
					a.map.emplace_back(c1.value, c2.value);
			}
		} else {
			return fail("illegal definition line", t.line);
		}
	}
	if (out.n_keys == 0) {
		err = "no key definitions";
		return false;
	}
	return true;
}

bool parse_keymap_file(const std::string &path, Keymap &out, std::string &err)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		err = "cannot open " + path;
		return false;
	}
	std::stringstream ss;
	ss << f.rdbuf();
	return parse_keymap_text(ss.str(), out, err);
}

/* ---- Keymap helpers ---------------------------------------------------- */

bool Keymap::has_second_layer() const
{
	for (int i = ALTGR_OFFSET; i < NUM_KEYS; i++)
		for (int s = 0; s < NUM_STATES; s++)
			if (keys[i].map[s] != 0 && !(keys[i].is_special(s) && keys[i].map[s] == NOP))
				return true;
	return false;
}

const char *keycode_name(int kc)
{
	switch (kc) {
	case 28: return "Enter";
	case 29: return "Left Ctrl";
	case 42: return "Left Shift";
	case 54: return "Right Shift";
	case 56: return "Left Alt";
	case 57: return "Space";
	case 58: return "CapsLock";
	case 69: return "NumLock";
	case 70: return "ScrollLock";
	case 90: return "Right Ctrl";
	case 93: return "Right Alt";
	case 104: return "Pause";
	case 105: return "Left Win";
	case 106: return "Right Win";
	case 107: return "Menu";
	default: return nullptr;
	}
}

std::string Keymap::second_layer_hint() const
{
	if (!has_second_layer())
		return "";
	std::vector<std::string> hints;
	for (int kc = 0; kc < ALTGR_OFFSET; kc++) {
		const KeyEntry &k = keys[kc];
		for (int s = 0; s < NUM_STATES; s++) {
			if (!k.is_special(s))
				continue;
			uint32_t a = k.map[s];
			bool toggle = (a == ALK);
			bool tap = (a >= LSHA && a <= RALTA);
			bool hold = (a == ASH);
			if (!toggle && !tap && !hold)
				continue;
			const char *name = keycode_name(kc);
			if (!name)
				break; /* obscure key (e.g. 84 = Alt+SysRq): not worth a hint */
			std::string h;
			if (s & 2) h += "Ctrl+";
			if (s & 4) h += "Alt+";
			if (s & 1) h += "Shift+";
			h += name;
			if (tap)
				h += " (tap)";
			if (hold)
				h = "hold " + h;
			if (std::find(hints.begin(), hints.end(), h) == hints.end())
				hints.push_back(h);
			break; /* first matching state of this key is enough */
		}
	}
	/* simplest shortcuts first */
	std::stable_sort(hints.begin(), hints.end(), [](const std::string &a, const std::string &b) {
		return std::count(a.begin(), a.end(), '+') < std::count(b.begin(), b.end(), '+');
	});
	std::string r;
	for (size_t i = 0; i < hints.size() && i < 2; i++) {
		if (i)
			r += " or ";
		r += hints[i];
	}
	return r;
}

double compare_keymaps(const Keymap &a, const Keymap &b)
{
	int same = 0, total = 0;
	for (int i = 0; i < NUM_KEYS; i++) {
		const KeyEntry &x = a.keys[i], &y = b.keys[i];
		for (int s = 0; s < NUM_STATES; s++) {
			total++;
			if (x.map[s] == y.map[s] && x.is_special(s) == y.is_special(s))
				same++;
		}
		total++;
		if (x.flgs == y.flgs)
			same++;
	}
	return (double)same / total;
}

/* ---- Layout list ------------------------------------------------------- */

std::string user_language()
{
	const char *vars[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
	for (const char *v : vars) {
		const char *s = getenv(v);
		if (s && strlen(s) >= 2 && strcmp(s, "C") != 0 && strcmp(s, "POSIX") != 0)
			return std::string(s, 2);
	}
	return "en";
}

static std::string lower(std::string s)
{
	for (auto &c : s)
		c = (char)tolower((unsigned char)c);
	return s;
}

std::vector<Layout> load_layouts(const std::string &dir, const std::string &lang)
{
	/* file -> (lang -> description) from INDEX.keymaps */
	std::map<std::string, std::map<std::string, std::string>> index;
	std::ifstream idx(dir + "/INDEX.keymaps");
	std::string line;
	while (std::getline(idx, line)) {
		if (line.empty() || line[0] == '#')
			continue;
		size_t c1 = line.find(':');
		size_t c2 = c1 == std::string::npos ? c1 : line.find(':', c1 + 1);
		if (c2 == std::string::npos)
			continue;
		std::string file = line.substr(0, c1);
		std::string langs = line.substr(c1 + 1, c2 - c1 - 1);
		std::string desc = line.substr(c2 + 1);
		if (file == "MENU" || file == "FONT" || file == "TITLE")
			continue;
		if (langs.empty())
			langs = "en";
		std::stringstream ls(langs);
		std::string l;
		while (std::getline(ls, l, ','))
			if (!index[file].count(l))
				index[file][l] = desc;
	}

	std::vector<Layout> out;
	DIR *d = opendir(dir.c_str());
	if (!d)
		return out;
	while (struct dirent *e = readdir(d)) {
		std::string name = e->d_name;
		if (name.size() <= 4 || name.compare(name.size() - 4, 4, ".kbd") != 0)
			continue;
		Layout L;
		L.file = name;
		L.path = dir + "/" + name;
		auto it = index.find(name);
		if (it != index.end()) {
			auto &m = it->second;
			if (m.count(lang))
				L.desc = m[lang];
			else if (m.count("en"))
				L.desc = m["en"];
			else
				L.desc = m.begin()->second;
		}
		if (L.desc.empty())
			L.desc = name.substr(0, name.size() - 4);
		L.ok = parse_keymap_file(L.path, L.km, L.err);
		out.push_back(std::move(L));
	}
	closedir(d);
	std::sort(out.begin(), out.end(), [](const Layout &a, const Layout &b) {
		std::string la = lower(a.desc), lb = lower(b.desc);
		return la != lb ? la < lb : a.file < b.file;
	});
	return out;
}

std::string default_keymap_dir()
{
	return kernel_console_is_vt() ? "/usr/share/vt/keymaps"
	                              : "/usr/share/syscons/keymaps";
}

std::string rcconf_keymap()
{
	FILE *p = popen("/usr/sbin/sysrc -n keymap 2>/dev/null", "r");
	if (!p)
		return "";
	char buf[256] = {0};
	if (!fgets(buf, sizeof(buf), p))
		buf[0] = 0;
	pclose(p);
	std::string s = buf;
	while (!s.empty() && isspace((unsigned char)s.back()))
		s.pop_back();
	if (lower(s) == "no")
		s.clear();
	return s;
}

bool read_kernel_keymap(Keymap &out, std::string &err)
{
	static uint32_t map[NUM_KEYS][NUM_STATES];
	static uint8_t spcl[NUM_KEYS], flgs[NUM_KEYS];
	unsigned n_keys = 0;
	char ebuf[256] = {0};
	if (!kernel_keymap_read(&n_keys, map, spcl, flgs, ebuf, sizeof(ebuf))) {
		err = ebuf;
		return false;
	}
	out = Keymap();
	out.n_keys = (int)n_keys;
	for (int i = 0; i < NUM_KEYS; i++) {
		memcpy(out.keys[i].map, map[i], sizeof(map[i]));
		out.keys[i].spcl = spcl[i];
		out.keys[i].flgs = flgs[i];
	}
	return true;
}

/* ---- Translator (mirrors genkbd_keyaction) ----------------------------- */

void Translator::set_keymap(const Keymap *km)
{
	km_ = km;
	accent_ = 0;
	alked_ = false;
	shiftaon_ = false;
}

void Translator::reset_modifiers()
{
	lsh_ = rsh_ = lctr_ = rctr_ = lalt_ = ralt_ = ash_ = meta_ = false;
	clk_down_ = nlk_down_ = alk_down_ = shiftaon_ = false;
	accent_ = 0;
	memset(lastact_, 0, sizeof(lastact_));
}

bool Translator::second_layer_active() const
{
	return km_ && km_->has_second_layer() && (ash_ != alked_);
}

uint32_t Translator::pending_accent() const
{
	if (!km_ || accent_ <= 0)
		return 0;
	return km_->acc[accent_ - 1].accchar;
}

static bool printable(uint32_t c)
{
	return c >= 0x20 && c != 0x7f && !(c >= 0x80 && c < 0xa0) && c <= 0x10FFFF;
}

uint32_t Translator::key(int keycode, bool down, bool repeat)
{
	if (!km_ || keycode < 0 || keycode >= NUM_KEYS)
		return 0;

	if (!down) {
		uint32_t action = lastact_[keycode];
		lastact_[keycode] = NOP;
		bool toggle = shiftaon_ && action >= LSHA && action <= RALTA;
		switch (action) {
		case LSHA: case LSH: lsh_ = false; break;
		case RSHA: case RSH: rsh_ = false; break;
		case LCTRA: case LCTR: lctr_ = false; break;
		case RCTRA: case RCTR: rctr_ = false; break;
		case LALTA: case LALT: lalt_ = false; break;
		case RALTA: case RALT: ralt_ = false; break;
		case ASH: ash_ = false; break;
		case META: meta_ = false; break;
		case NLK: nlk_down_ = false; break;
		case CLK: clk_down_ = false; break;
		case ALK: alk_down_ = false; break;
		default: break;
		}
		if (toggle)
			alked_ = !alked_; /* "...a" modifier tapped alone */
		shiftaon_ = false;
		return 0;
	}

	int i = keycode;
	if (ash_ != alked_ && i + ALTGR_OFFSET < NUM_KEYS)
		i += ALTGR_OFFSET;
	const KeyEntry &k = km_->keys[i];
	int st = ((lsh_ || rsh_) ? 1 : 0) | ((lctr_ || rctr_) ? 2 : 0) |
	    ((lalt_ || ralt_) ? 4 : 0);
	if (((k.flgs & LOCK_C) && clked_) || ((k.flgs & LOCK_N) && nlked_))
		st ^= 1;
	uint32_t action = k.map[st];

	if (k.is_special(st)) {
		if (repeat)
			return 0; /* auto-repeat of modifiers/function keys */
		shiftaon_ = false;
		if (lastact_[keycode] == NOP)
			lastact_[keycode] = action;
		if (lastact_[keycode] != action)
			action = NOP;
		switch (action) {
		case NLK: if (!nlk_down_) { nlk_down_ = true; nlked_ = !nlked_; } return 0;
		case CLK: if (!clk_down_) { clk_down_ = true; clked_ = !clked_; } return 0;
		case ALK: if (!alk_down_) { alk_down_ = true; alked_ = !alked_; } return 0;
		case LSHA: shiftaon_ = true; lsh_ = true; return 0;
		case LSH: lsh_ = true; return 0;
		case RSHA: shiftaon_ = true; rsh_ = true; return 0;
		case RSH: rsh_ = true; return 0;
		case LCTRA: shiftaon_ = true; lctr_ = true; return 0;
		case LCTR: lctr_ = true; return 0;
		case RCTRA: shiftaon_ = true; rctr_ = true; return 0;
		case RCTR: rctr_ = true; return 0;
		case LALTA: shiftaon_ = true; lalt_ = true; return 0;
		case LALT: lalt_ = true; return 0;
		case RALTA: shiftaon_ = true; ralt_ = true; return 0;
		case RALT: ralt_ = true; return 0;
		case ASH: ash_ = true; return 0;
		case META: meta_ = true; return 0;
		case NOP: case SLK: return 0;
		default:
			break;
		}
		if (action >= F_ACC && action <= L_ACC) {
			/* dead key */
			int idx = (int)(action - F_ACC) + 1;
			const Accent &a = km_->acc[idx - 1];
			if (a.accchar == 0) {
				accent_ = 0;
				return 0;
			}
			if (idx == accent_) { /* same dead key twice */
				accent_ = 0;
				return printable(a.accchar) ? a.accchar : 0;
			}
			accent_ = idx;
			return 0;
		}
		accent_ = 0; /* function keys etc. cancel a pending accent */
		return 0;
	}

	/* regular key */
	shiftaon_ = false;
	lastact_[keycode] = NOP;
	if (accent_ > 0) {
		const Accent &a = km_->acc[accent_ - 1];
		accent_ = 0;
		if (action == ' ')
			return printable(a.accchar) ? a.accchar : 0;
		for (const auto &p : a.map)
			if (p.first == action)
				return printable(p.second) ? p.second : 0;
		return 0; /* cannot be accented: the console beeps */
	}
	return printable(action) ? action : 0;
}

void utf8_encode(uint32_t cp, char *b)
{
	if (cp < 0x80) {
		b[0] = (char)cp; b[1] = 0;
	} else if (cp < 0x800) {
		b[0] = (char)(0xC0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 0x3F)); b[2] = 0;
	} else if (cp < 0x10000) {
		b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		b[2] = (char)(0x80 | (cp & 0x3F)); b[3] = 0;
	} else {
		b[0] = (char)(0xF0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
		b[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (char)(0x80 | (cp & 0x3F)); b[4] = 0;
	}
}

} // namespace kbd
