/*
 * Unit tests for the keymap parser and translator.
 * Usage: test_keymap <keymap dir>   (e.g. /usr/share/vt/keymaps)
 */
#include "../src/keymap.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace kbd;

static int failures = 0;

#define CHECK(cond, ...)                                     \
	do {                                                     \
		if (!(cond)) {                                       \
			failures++;                                      \
			printf("FAIL %s:%d: ", __FILE__, __LINE__);      \
			printf(__VA_ARGS__);                             \
			printf("\n");                                    \
		}                                                    \
	} while (0)

/* Type a sequence: positive = press+release, negative = press only,
 * 1000+k = release only. Returns the UTF-8 text produced. */
static std::string type(Translator &t, std::vector<int> seq)
{
	std::string out;
	char b[8];
	for (int k : seq) {
		if (k >= 1000) {
			t.key(k - 1000, false, false);
			continue;
		}
		bool hold = k < 0;
		int kc = hold ? -k : k;
		uint32_t cp = t.key(kc, true, false);
		if (cp) {
			utf8_encode(cp, b);
			out += b;
		}
		if (!hold)
			t.key(kc, false, false);
	}
	return out;
}

static const Layout *find(const std::vector<Layout> &v, const char *file)
{
	for (auto &l : v)
		if (l.file == file)
			return &l;
	return nullptr;
}

int main(int argc, char **argv)
{
	std::string dir = argc > 1 ? argv[1] : "/usr/share/vt/keymaps";
	auto layouts = load_layouts(dir, "en");
	printf("%zu layouts in %s\n", layouts.size(), dir.c_str());
	CHECK(layouts.size() > 50, "expected many layouts");
	int bad = 0;
	for (auto &l : layouts)
		if (!l.ok) {
			bad++;
			printf("  parse error %s: %s\n", l.file.c_str(), l.err.c_str());
		}
	CHECK(bad == 0, "%d keymaps failed to parse", bad);

	/* Descriptions */
	const Layout *fr = find(layouts, "fr.kbd");
	CHECK(fr && fr->desc == "French", "fr.kbd desc = '%s'", fr ? fr->desc.c_str() : "?");
	auto fr_layouts = load_layouts(dir, "fr");
	const Layout *frfr = find(fr_layouts, "fr.acc.kbd");
	CHECK(frfr && frfr->desc == "Français (avec accents)", "fr desc = '%s'",
	    frfr ? frfr->desc.c_str() : "?");

	Translator t;
	/* us: plain, shift, caps lock */
	t.set_keymap(&find(layouts, "us.kbd")->km);
	CHECK(type(t, {35, 18, 38, 38, 24}) == "hello", "us hello");
	CHECK(type(t, {-42, 35, 1042, 18}) == "He", "us shift");
	CHECK(type(t, {58, 30, 2, 58, 30}) == "A1a", "us capslock: %s", type(t, {}).c_str());

	/* fr: AZERTY */
	t.set_keymap(&fr->km);
	CHECK(type(t, {16, 17, 44}) == "azw", "fr azerty: got '%s'", type(t, {16, 17, 44}).c_str());
	CHECK(type(t, {2, 3}) == "&\xc3\xa9", "fr digits row: '%s'", type(t, {2, 3}).c_str());
	CHECK(type(t, {-93, 3, 1093}) == "~", "fr AltGr+2: '%s'", type(t, {-93, 3, 1093}).c_str());

	/* fr.acc: dead circumflex then e = ê; twice = ^ */
	t.set_keymap(&find(layouts, "fr.acc.kbd")->km);
	CHECK(type(t, {26}) == "" && t.pending_accent() == '^', "fr.acc dead key pending");
	CHECK(type(t, {18}) == "\xc3\xaa", "fr.acc ^e");
	CHECK(type(t, {26, 26}) == "^", "fr.acc ^^");
	CHECK(type(t, {26, 57}) == "^", "fr.acc ^space");
	CHECK(type(t, {26, 48}) == "", "fr.acc ^b produces nothing (like the console)");
	CHECK(t.pending_accent() == 0, "accent cleared");

	/* de: QWERTZ */
	t.set_keymap(&find(layouts, "de.kbd")->km);
	CHECK(type(t, {21, 44}) == "zy", "de qwertz");

	/* ru: CapsLock toggles the Cyrillic layer (alock in column 0) */
	const Layout *ru = find(layouts, "ru.kbd");
	CHECK(ru->km.has_second_layer(), "ru has second layer");
	printf("ru hint: %s\n", ru->km.second_layer_hint().c_str());
	t.set_keymap(&ru->km);
	CHECK(type(t, {16}) == "q", "ru latin q");
	type(t, {58});
	CHECK(t.second_layer_active(), "ru layer on");
	std::string cyr = type(t, {16, 17});
	CHECK(cyr == "\xd0\xb9\xd1\x86", "ru cyrillic: '%s'", cyr.c_str());
	type(t, {58});
	CHECK(!t.second_layer_active() && type(t, {16}) == "q", "ru back to latin");

	/* Hints for every layout with a second layer */
	for (auto &l : layouts)
		if (l.ok && l.km.has_second_layer())
			printf("  %-24s second layer: %s\n", l.file.c_str(),
			    l.km.second_layer_hint().c_str());

	/* Comparison: identical = 1.0, fr vs fr.acc close but not equal */
	CHECK(compare_keymaps(fr->km, fr->km) == 1.0, "self compare");
	double d = compare_keymaps(fr->km, find(layouts, "fr.acc.kbd")->km);
	CHECK(d < 1.0 && d > 0.9, "fr vs fr.acc = %f", d);
	int dup = 0;
	for (size_t i = 0; i < layouts.size(); i++)
		for (size_t j = i + 1; j < layouts.size(); j++)
			if (compare_keymaps(layouts[i].km, layouts[j].km) == 1.0) {
				dup++;
				printf("  identical keymaps: %s = %s\n", layouts[i].file.c_str(),
				    layouts[j].file.c_str());
			}

	printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
	return failures ? 1 : 0;
}
