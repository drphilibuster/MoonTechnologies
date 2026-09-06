// A stand-in for <rack.hpp>, just wide enough to compile Diversified's DSP
// headers off the audio thread and away from Rack.
//
// Primitives.hpp, MiawFx.hpp and Dsp99.hpp reach for exactly two things from
// Rack: `clamp`, and the `dsp::Upsampler` / `dsp::Decimator` pair that the
// PT2399 board oversamples through. (`lerp` looks like Rack's but is
// Diversified's own, in Primitives.hpp.)
//
// Both of those come from the real SDK headers rather than a hand-written
// stand-in: dsp/resampler.hpp and math.hpp are header-only and link without
// libRack, so what these tests exercise is the shipping signal path and not an
// approximation of it. Nothing else from Rack is pulled in -- a full <rack.hpp>
// drags in nanovg's global colour constants and will not link.
#pragma once

#include <dsp/resampler.hpp>
#include <math.hpp>

using namespace rack;
using namespace rack::math;
