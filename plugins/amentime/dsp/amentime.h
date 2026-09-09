#pragma once

/*
 * File: amentime.h
 *
 * AmenTime: synthesized 1-bar amen-style break slicer for NTS-3.
 */

#include "amentime_pcm.h"

#define kSlicePcm8 kAmenPcm8
#define kSlicePcmLength kAmenPcmLength

#include "wav_slicer.h"

using AmenTime = WavSlicer;
