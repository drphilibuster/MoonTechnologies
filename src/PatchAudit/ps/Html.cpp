#include "Html.hpp"

#include <stdlib.h>

#include <string>

namespace ps {

/** Appends `cp` as UTF-8. */
static void appendUtf8(std::string& out, unsigned int cp) {
	if (cp < 0x80) {
		out += (char) cp;
	}
	else if (cp < 0x800) {
		out += (char) (0xC0 | (cp >> 6));
		out += (char) (0x80 | (cp & 0x3F));
	}
	else if (cp < 0x10000) {
		out += (char) (0xE0 | (cp >> 12));
		out += (char) (0x80 | ((cp >> 6) & 0x3F));
		out += (char) (0x80 | (cp & 0x3F));
	}
	else if (cp <= 0x10FFFF) {
		out += (char) (0xF0 | (cp >> 18));
		out += (char) (0x80 | ((cp >> 12) & 0x3F));
		out += (char) (0x80 | ((cp >> 6) & 0x3F));
		out += (char) (0x80 | (cp & 0x3F));
	}
}

struct NamedEntity { const char* name; unsigned int cp; };

/** The entities that actually show up in Patchstorage titles. Anything else is
    left as literal text, which is the safe failure: an unrecognised "&foo;"
    reads as itself rather than vanishing. */
static const NamedEntity ENTITIES[] = {
	{"amp", 38}, {"lt", 60}, {"gt", 62}, {"quot", 34}, {"apos", 39},
	{"nbsp", 32}, {"hellip", 0x2026}, {"mdash", 0x2014}, {"ndash", 0x2013},
	{"rsquo", 0x2019}, {"lsquo", 0x2018}, {"ldquo", 0x201C}, {"rdquo", 0x201D},
	{"middot", 0x00B7}, {"deg", 0x00B0}, {"times", 0x00D7}, {"copy", 0x00A9},
	{"trade", 0x2122}, {"reg", 0x00AE}, {"eacute", 0x00E9}, {"egrave", 0x00E8},
	{"bull", 0x2022}, {"prime", 0x2032}, {"Prime", 0x2033},
};
static const int NUM_ENTITIES = (int) (sizeof(ENTITIES) / sizeof(ENTITIES[0]));

std::string decodeEntities(const std::string& s) {
	std::string out;
	out.reserve(s.size());
	size_t i = 0;
	while (i < s.size()) {
		if (s[i] != '&') {
			out += s[i++];
			continue;
		}
		size_t semi = s.find(';', i + 1);
		// A bare '&' or a runaway entity: emit it literally and move on.
		if (semi == std::string::npos || semi - i > 12) {
			out += s[i++];
			continue;
		}
		std::string body = s.substr(i + 1, semi - i - 1);
		bool handled = false;
		if (!body.empty() && body[0] == '#') {
			const char* p = body.c_str() + 1;
			int base = 10;
			if (*p == 'x' || *p == 'X') { base = 16; p++; }
			char* end = NULL;
			unsigned long cp = strtoul(p, &end, base);
			if (end && *end == '\0' && cp > 0 && cp <= 0x10FFFF) {
				appendUtf8(out, (unsigned int) cp);
				handled = true;
			}
		}
		else {
			for (int e = 0; e < NUM_ENTITIES && !handled; e++) {
				if (body == ENTITIES[e].name) {
					appendUtf8(out, ENTITIES[e].cp);
					handled = true;
				}
			}
		}
		if (handled)
			i = semi + 1;
		else
			out += s[i++];
	}
	return out;
}

std::string stripTags(const std::string& s) {
	std::string out;
	out.reserve(s.size());
	bool inTag = false;
	for (size_t i = 0; i < s.size(); i++) {
		char c = s[i];
		if (c == '<') { inTag = true; continue; }
		if (c == '>') { if (inTag) { inTag = false; out += ' '; } continue; }
		if (inTag) continue;
		out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
	}
	// collapse runs of spaces
	std::string collapsed;
	collapsed.reserve(out.size());
	bool space = false;
	for (size_t i = 0; i < out.size(); i++) {
		if (out[i] == ' ') {
			space = true;
			continue;
		}
		if (space && !collapsed.empty())
			collapsed += ' ';
		space = false;
		collapsed += out[i];
	}
	return collapsed;
}

std::string plainText(const std::string& s) {
	std::string t = decodeEntities(stripTags(s));
	size_t b = t.find_first_not_of(" \t\n\r");
	if (b == std::string::npos) return "";
	size_t e = t.find_last_not_of(" \t\n\r");
	return t.substr(b, e - b + 1);
}

} // namespace ps
