#pragma once
#include <rack.hpp>

#include <algorithm>
#include <cmath>


namespace upol {


struct Deviation {
	float newValue = 0.f;
	bool changed = false;
};


/** Choose "up" with probability proportional to the headroom above the current
    position. At the top rail hu is 0, so travel is always downward; at the
    centre of the range it is an even coin flip. */
inline bool pickDirection(float headroomUp, float headroomDown) {
	float total = headroomUp + headroomDown;
	if (total <= 0.f)
		return false;
	return rack::random::uniform() * total < headroomUp;
}


/** Deviate a param by up to `amount` (0..1) of its full range, starting from X,
    its current knob position.

    The naive form -- clamp(X +/- U(0, d)) -- quietly biases against knobs that
    sit near a rail: half the rolls for a knob already at max clamp back onto max
    and change nothing, so it moves half as often as a centred knob and piles up
    on the endpoint. Three things avoid that:

      1. Direction is drawn in proportion to the headroom on each side, so a
         pinned knob spends all of its probability mass travelling inward.
      2. Magnitude is drawn from [floor, d] with a non-zero floor, so a roll
         never resolves to "no audible change".
      3. Magnitude is then capped to the chosen direction's headroom -- which
         rarely binds, because the direction was already chosen in proportion
         to it.

    Stepped params (snapEnabled) take a discrete path and always move at least
    one step, so a switch genuinely flips instead of rounding back onto itself.

    Params whose author set randomizeEnabled = false are left alone, matching
    Rack's own randomize -- unless the caller has established that the flag came
    from configButton() on a latching button rather than from a deliberate
    decision, which is the one case where honouring it means ignoring real patch
    content. The caller has to make that determination; this function will not
    guess it. */
inline Deviation deviate(rack::engine::ParamQuantity* pq, float amount,
                         bool allowDisabled = false) {
	Deviation out;
	if (!pq || !pq->isBounded())
		return out;
	if (!pq->randomizeEnabled && !allowDisabled)
		return out;

	const float lo = pq->getMinValue();
	const float hi = pq->getMaxValue();
	const float range = hi - lo;
	if (!(range > 0.f) || amount <= 0.f)
		return out;

	const float x = rack::math::clamp(pq->getValue(), lo, hi);
	const float headUp = hi - x;
	const float headDown = x - lo;
	if (headUp <= 0.f && headDown <= 0.f)
		return out;

	if (pq->snapEnabled) {
		// Discrete: count in steps and guarantee at least one.
		const int stepsUp = (int) std::lround(headUp);
		const int stepsDown = (int) std::lround(headDown);
		if (stepsUp < 1 && stepsDown < 1)
			return out;

		bool up = pickDirection((float) stepsUp, (float) stepsDown);
		int avail = up ? stepsUp : stepsDown;
		if (avail < 1) {
			up = !up;
			avail = up ? stepsUp : stepsDown;
		}
		if (avail < 1)
			return out;

		const int span = std::max(1, (int) std::lround(amount * range));
		const int maxStep = std::min(span, avail);
		const int k = 1 + (int) (rack::random::uniform() * (float) maxStep);

		out.newValue = x + (up ? (float) k : -(float) k);
		out.newValue = rack::math::clamp(out.newValue, lo, hi);
		out.changed = (out.newValue != x);
		return out;
	}

	const float d = amount * range;
	bool up = pickDirection(headUp, headDown);
	float avail = up ? headUp : headDown;
	if (avail <= 0.f) {
		up = !up;
		avail = up ? headUp : headDown;
	}
	if (avail <= 0.f)
		return out;

	const float magHi = std::min(d, avail);
	// A tenth of the requested excursion is the smallest move worth making.
	const float magLo = std::min(0.10f * d, magHi);
	const float mag = magLo + rack::random::uniform() * (magHi - magLo);
	if (!(mag > 0.f))
		return out;

	out.newValue = rack::math::clamp(up ? x + mag : x - mag, lo, hi);
	out.changed = (out.newValue != x);
	return out;
}


} // namespace upol
