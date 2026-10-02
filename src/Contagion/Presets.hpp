// The Virus C's 1024 sounds, named and selectable. Self-contained (no Rack) so tests/Contagion
// can check it against the firmware's own LCD.
//
// Eight banks of 128 singles, A to H. A and B are the unit's battery RAM (what STORE writes);
// C to H are flash. Each single is a 256-byte page with its ten-character name at byte 240. The OS
// image holds the factory pages: its first two single banks (flash $50000 and $58000) are what
// A and B start as and what C and D are, and $60000-$7FFFF are E to H. The module reads the names
// from the user's own image and battery RAM at run time; nothing derived from the image is kept.
//
// Selecting one is MIDI: bank select (CC 32) then a program change, on the channel the part
// listens to (the unit answers SysEx program changes too, but not the manual's form of them).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vc {

struct Presets {
	static constexpr int BANKS = 8, PER_BANK = 128, COUNT = BANKS * PER_BANK;
	static constexpr size_t PAGE = 256, NAME_AT = 240, NAME_LEN = 10;
	static constexpr size_t FLASH_BASE = 0x50000, FLASH_BANK = 0x8000;   // singles start here, 32 KB a bank
	static constexpr size_t RAM_A = 0x10000;                              // user bank A in the battery RAM
	                                                                      // ($50000 - the RAM's $40000)

	std::vector<std::string> flash[6];   // the image's singles, banks at $50000, $58000, ... $78000

	static char letter(int bank) { return char('A' + bank); }
	/** "A0", "C127": a sound as the unit's display writes it. */
	static std::string label(int bank, int prog) { return std::string(1, letter(bank)) + std::to_string(prog); }

	/** The ten-character name at the end of a 256-byte page, with anything unprintable made a "?"
	    and trailing spaces dropped. */
	static std::string pageName(const uint8_t* page) {
		std::string s;
		for (size_t i = 0; i < NAME_LEN; i++) {
			const uint8_t c = page[NAME_AT + i];
			s += (c >= 32 && c < 127) ? char(c) : '?';
		}
		while (!s.empty() && s.back() == ' ') s.pop_back();
		return s;
	}

	/** Read the factory names out of a 512 KB image. False if it is not one. */
	bool loadImage(const std::vector<uint8_t>& image) {
		if (image.size() < FLASH_BASE + 6 * FLASH_BANK) return false;
		for (int b = 0; b < 6; b++) {
			flash[b].clear();
			for (int p = 0; p < PER_BANK; p++) flash[b].push_back(pageName(&image[FLASH_BASE + b * FLASH_BANK + p * PAGE]));
		}
		return true;
	}
	bool loaded() const { return !flash[0].empty(); }

	/** A sound's name: from the battery RAM for the user banks when it is to hand, else the image's. */
	std::string name(int bank, int prog, const std::vector<uint8_t>& bankRam) const {
		if (bank < 0 || bank >= BANKS || prog < 0 || prog >= PER_BANK) return "";
		if (bank < 2 && bankRam.size() >= RAM_A + 2 * FLASH_BANK)
			return pageName(&bankRam[RAM_A + bank * FLASH_BANK + prog * PAGE]);
		if (!loaded()) return "";
		const int f = bank < 2 ? bank : bank - 2;     // C and D are the image's first two banks again
		return flash[f][prog];
	}

	/** The MIDI that makes a part play a sound: bank select, then program change. */
	static std::vector<uint8_t> select(int channel, int bank, int prog) {
		const uint8_t ch = uint8_t(channel & 15);
		return { uint8_t(0xB0 | ch), 32, uint8_t(bank & 7), uint8_t(0xC0 | ch), uint8_t(prog & 127) };
	}

	/** Which sound an LCD's first line is showing ("A0  AutoBendBC", "..C127 ..."): bank * 128 + program,
	    or -1 when it is not showing one. */
	static int fromScreen(const uint8_t* line) {
		for (int i = 0; i < 4; i++) {
			if (line[i] < 'A' || line[i] > 'H') continue;
			int n = 0, d = 0, j = i + 1;
			while (j < 16 && line[j] >= '0' && line[j] <= '9' && d < 3) { n = n * 10 + (line[j] - '0'); j++; d++; }
			if (d > 0 && n < PER_BANK) return (line[i] - 'A') * PER_BANK + n;
		}
		return -1;
	}
};

} // namespace vc
