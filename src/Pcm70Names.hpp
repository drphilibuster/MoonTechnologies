// Pcm70Names.hpp: the factory programs' names, read out of the user's own firmware at runtime (nothing here is derived from a ROM at build time and nothing is
// stored in the repository).
//
// The firmware shows the name of the program it would load ("1.3 MIDI MOD PA") as soon as a program digit is down in PGM mode, so a scratch machine is powered up
// beside the real one and asked, slot by slot, what it would load. That costs no audio and disturbs nothing; it finishes in a few seconds of CPU on a worker.
// The display is 16 digits, so a long name can arrive cut short; when it does, the rest is taken from the printable string in the images that starts the same way.
#pragma once
#include <atomic>
#include <cctype>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "Pcm70Voice.hpp"

namespace pcm70 {

struct ProgramNames {
    static const int ROWS = 7, COLS = 10, SLOTS = ROWS * COLS;       // rows 0..6: row 7 is the machine's own housekeeping programs, which the panel does not offer
    std::string name[SLOTS];                                          // empty: the firmware has no program there (or it was not read)
    bool done = false;

    /** "??.1.3 MIDI MOD PA" -> slot 13 and "MIDI MOD PA"; false if the display is not showing a program. `cut`: the name runs to the end of the display. */
    static bool parse(const std::string& display, int& slot, std::string& nm, bool& cut) {
        std::string t; for (char ch : display) if (ch != '?') t += ch;
        for (size_t i = 0; i + 2 < t.size(); i++)
            if (std::isdigit((unsigned char)t[i]) && t[i + 1] == '.' && std::isdigit((unsigned char)t[i + 2])) {
                slot = (t[i] - '0') * 10 + (t[i + 2] - '0');
                size_t a = t.find_first_not_of(' ', i + 3), b = t.find_last_not_of(' ');
                nm = a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
                // the digit the name's last letter sits in: from the last three of the sixteen, the display may have cut it off
                int digit = -1, last = -1; for (size_t k = 0; k < display.size(); k++) { if (display[k] == '.') continue; digit++; if (display[k] != ' ') last = digit; }
                cut = !nm.empty() && last >= 13;
                return true;
            }
        return false;
    }
    /** A cut name, finished from the printable string in the images that begins with it (the first, if several do). */
    static std::string finish(const std::string& stub, const std::vector<const std::vector<uint8_t>*>& images) {
        if (stub.size() < 6) return stub;
        for (const std::vector<uint8_t>* img : images)
            for (size_t i = 0; i + stub.size() < img->size(); i++) {
                if (memcmp(img->data() + i, stub.data(), stub.size()) != 0) continue;
                if (i > 0 && (*img)[i - 1] >= 32 && (*img)[i - 1] < 127) continue;               // a match must start a string
                std::string s = stub; size_t j = i + stub.size();
                while (j < img->size() && (*img)[j] >= 32 && (*img)[j] < 127 && s.size() < 16) s += (char)(*img)[j++];
                size_t b = s.find_last_not_of(' '); s = b == std::string::npos ? stub : s.substr(0, b + 1);
                return s;
            }
        return stub;
    }

    /** Power up a scratch machine on the images and read every slot. `cancel` is polled so a worker can be abandoned (a new ROM set, the module deleted). */
    static ProgramNames harvest(const std::vector<uint8_t>& u62, const std::vector<uint8_t>& u95, const std::vector<uint8_t>& u67, const std::vector<uint8_t>& u48,
                                const std::vector<uint8_t>& u49, bool v3, const std::atomic<bool>& cancel) {
        ProgramNames out;
        std::unique_ptr<Voice> V(new Voice());
        V->load(u62.data(), u62.size(), u95.data(), u95.size(), u67.data(), u48.data(), u49.data(), v3);
        const double rate = 32000.0; V->configure(rate);
        double l, r;
        auto run = [&](double s) { for (long long i = 0, n = (long long)(s * rate); i < n && !cancel.load(); i++) V->process(0.0, l, r); };
        auto settle = [&]() { run(0.3); for (int k = 0; k < 80 && !cancel.load() && (V->keys.busy() || V->control.busy()); k++) run(0.1); run(0.2); };
        run(9.6); settle();
        const std::vector<const std::vector<uint8_t>*> images = { &u62, &u95 };
        for (int s = 0; s < SLOTS && !cancel.load(); s++) {
            V->keys.browseProgram(s / COLS, s % COLS); settle();
            int got = -1; std::string nm; bool cut = false;
            if (!parse(V->control.displayText(), got, nm, cut) || got != s) { run(0.5); settle(); if (!parse(V->control.displayText(), got, nm, cut) || got != s) continue; }
            if (cut) nm = finish(nm, images);
            std::string one; for (char ch : nm) if (ch != ' ' || (!one.empty() && one.back() != ' ')) one += ch;        // the firmware pads with doubled spaces ("BONANZA  BPM")
            out.name[s] = one;
        }
        out.done = !cancel.load();
        return out;
    }
};

}
