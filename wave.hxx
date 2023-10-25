#pragma once

#include <cstdio>

#include "capture.hxx"

namespace wave
{

typedef struct writer writer;

writer *newwriter(const char *dest, int nchannels, int samplerate, capture::pcm_type outtype);
bool write(writer *writer, const void *buf, size_t framecount, capture::pcm_type intype = capture::pcm_type::unknown);
bool close(writer *writer);

}
