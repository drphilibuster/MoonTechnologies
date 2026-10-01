// hsp_ref.hpp: REFERENCE model of the PCM 70's HSP (tick-accurate, driven by the two sequencer PROMs), used only by the tests.
// A straight transcription of the research tree's emu/hsp_tick.hpp (class HspTick) with its best-model configuration (emu/envcfg.hpp defaults) written in
// and every research probe removed (trace, logs, quantisers, op overrides, muted words). The compiled core in src/Pcm70Hsp.hpp must reproduce this
// bit for bit; this file is deliberately slow and obvious.
#pragma once
#include <cstdint>
#include <cmath>
#include <vector>

namespace pcm70 {
struct HspRef {
    // best model: offShift=1 (memory offset from word j+1), waShift=1, waShiftO=1; kRL=kRO=kROmw=5, kWR=6, kAdc=3, kDac=5; O latches on the falling edge of MIB26 delayed 2 ticks;
    // op(F=3,C=1) = (~A & B)/2 on 18-bit two's complement words (op 134); op(F=7) = -B; addr = pos + offset
    static const int kRL = 5, kWR = 6, kAdc = 3, kDac = 5, kRO = 5, kROmw = 5, xDelay = 2, OP31 = 134;
    struct W { uint8_t mwr, mcen, op, wa, ra, acc, dpd, n, m26; uint16_t off; };
    const uint8_t* U48 = nullptr; const uint8_t* U49 = nullptr;
    W wd[128]; std::vector<double> mem = std::vector<double>(65536, 0.0);
    double R[4] = {0, 0, 0, 0}, A = 0, P = 0, ACC = 0, O = 0; long t = 0; uint16_t pos = 0;
    int st = 0, nib = 0, dpdx = 1, ra_l = 0, prevX = 0, ctlPrev49 = 0, ctlPrev48 = 0; int scq[2];
    int xh[4], ndac = 0; double curIn = 0; double *L = nullptr, *Rr = nullptr;
    struct Ev { long at; int kind; int wa; uint16_t addr; };   // 0 mem read->R, 1 mem write, 2 adc->R, 3 dac, 4 R<-O
    std::vector<Ev> ev;

    void setProgram(const uint32_t* w) {
        for (int i = 0; i < 128; i++) {
            uint32_t x = w[i]; uint8_t h = x >> 24, c = (x >> 16) & 255;
            wd[i] = { uint8_t((h >> 7) & 1), uint8_t((h >> 6) & 1), uint8_t((h >> 5) & 1), uint8_t(h & 3), uint8_t((c >> 6) & 3), uint8_t((c >> 5) & 1), uint8_t((c >> 4) & 1), uint8_t(c & 15), uint8_t((h >> 2) & 1), uint16_t(x & 0xFFFF) };
        }
    }
    void reset() {
        mem.assign(65536, 0.0); for (auto& r : R) r = 0; A = P = ACC = O = 0; t = 0; pos = 0; st = 0; dpdx = 1; nib = 0; ra_l = 0; prevX = 0;
        for (int i = 0; i < 4; i++) xh[i] = 1; ctlPrev49 = 0xE9; ctlPrev48 = 0xF7; scq[0] = scq[1] = 0; ev.clear();
    }
    void process(double in, double& outL, double& outR) {
        outL = outR = 0; ndac = 0; curIn = in; L = &outL; Rr = &outR;
        for (int P_ = 0; P_ < 128; P_++) for (int s = 0; s < 3; s++) tick(P_, s);
        pos++;
    }
    static double logicop34(double a, double b) {                        // ~A & B on 18-bit two's complement
        auto q = [](double v) { long i = std::lround(v * 131072.0); if (i > 131071) i = 131071; if (i < -131072) i = -131072; return i; };
        long r = ~q(a) & q(b); r &= 0x3FFFF; if (r & 0x20000) r -= 0x40000; return r / 131072.0;
    }
    static double sat(double v) { return v < -1.0 ? -1.0 : (v > 0.99997 ? 0.99997 : v); }
    void runEvents(long now) {
        for (size_t i = 0; i < ev.size();) {
            if (ev[i].at > now) { i++; continue; }
            Ev e = ev[i]; ev.erase(ev.begin() + i);
            if (e.kind == 0) R[e.wa] = mem[e.addr] + 0.0;
            else if (e.kind == 1) mem[e.addr] = O;
            else if (e.kind == 2) R[e.wa] = curIn;
            else if (e.kind == 4) R[e.wa] = O;
            else { if (ndac == 0) *L = O; else if (ndac == 1) *Rr = O; ndac++; }
        }
    }
    void tick(int Pn, int s) {
        long now = t++;
        const W& f = wd[Pn]; const W& fp = wd[(Pn + 127) & 127]; const W& fpp = wd[(Pn + 126) & 127];
        if (s == 0) {
            uint16_t addr = uint16_t(pos + wd[(Pn + 1) & 127].off);
            long b = now;
            if (f.mcen == 0) { if (!f.mwr) ev.push_back({ b + kRL, 0, wd[(Pn + 1) & 127].wa, addr }); else ev.push_back({ b + kWR, 1, f.wa, addr }); }
            if (f.mwr) ev.push_back({ b + ((f.mcen == 0) ? kROmw : kRO), 4, wd[(Pn + 1) & 127].wa, addr });
            if (f.mcen != 0 && f.op == 0) { if (!f.mwr) ev.push_back({ b + kAdc, 2, f.wa, addr }); else ev.push_back({ b + kDac, 3, f.wa, addr }); }
            nib = fp.n; dpdx = fpp.dpd; ra_l = fp.ra;
        }
        const W& raw = (s == 2) ? f : fp; int dp = raw.dpd, acc_in = raw.acc, dpd_now = (s == 2) ? fp.dpd : fpp.dpd;
        int v49 = U49[(st & 3) | dp << 2 | dpd_now << 3 | acc_in << 4];
        int v48 = U48[(st & 3) | nib << 2 | dpdx << 6 | 0x180];
        int e49 = ctlPrev49, e48 = ctlPrev48;
        int b3 = e49 >> 7 & 1, b0 = e49 >> 6 & 1, an = e49 >> 5 & 1;
        int F = (e48 >> 3 & 1) << 2 | (e48 >> 4 & 1) << 1 | (e48 >> 5 & 1), Cc = e48 >> 2 & 1;
        double Rsel = R[ra_l];
        double bus = b0 ? ACC : Rsel; double B = b3 ? A / 2 : P;
        double op = 0;
        if (F == 1) op = Cc ? A : A + B;
        else if (F == 3) op = Cc ? 0.5 * logicop34(A, B) : B;
        else if (F == 4) op = A - B;
        else if (F == 7) op = -B;
        const W& rawx = (s == 2) ? f : fp;
        for (int i = 3; i > 0; i--) xh[i] = xh[i - 1];
        xh[0] = rawx.m26;
        int xl = xh[xDelay]; int xedge = (!xl && prevX); prevX = xl;
        runEvents(now);
        int commit = scq[0]; scq[0] = scq[1]; scq[1] = (v49 >> 4 & 1);
        if (xedge) O = sat(bus);
        if (commit) ACC = sat(op);
        double nA = an == 0 ? bus : A; P = op / 2; A = nA;
        st = v49 & 3; ctlPrev49 = v49; ctlPrev48 = v48;
    }
};
}
