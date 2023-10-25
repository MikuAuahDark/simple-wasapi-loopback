#include <stdexcept>

#include "wave.hxx"

namespace wave
{

template<typename T>
inline bool writeint(FILE *f, T v)
{
	return fwrite(&v, 1, sizeof(v), f) == sizeof(v);
}

static size_t pcmtype_size(capture::pcm_type t)
{
	switch (t)
	{
	case capture::pcm_type::pcm_u8:
	default:
		return 1;
	case capture::pcm_type::pcm_s16:
		return 2;
	case capture::pcm_type::pcm_f32:
		return 4;
	}
}

struct writer
{
	writer(const char *dest, int nchannels, int samplerate, capture::pcm_type outtype);
	~writer();

	bool write(const void *buf, size_t framecount, capture::pcm_type intype);
	bool writeend();
private:
	static constexpr size_t FMT_HEADER_SIZE = 16;
	static constexpr size_t ALL_DATA_SIZE_OFF = 4;
	static constexpr size_t DATA_CHUNK_SIZE_OFF = 40;

	FILE *outfile;
	size_t bytesWritten;
	size_t channels;
	capture::pcm_type resampleTo;

	bool write_pass(const void *buf, size_t framecount);
	bool write_8_16(const unsigned char *buf, size_t framecount);
	bool write_16_8(const short *buf, size_t framecount);
	bool write_32_8(const float *buf, size_t framecount);
	bool write_32_16(const float *buf, size_t framecount);
	bool update();
};

writer::writer(const char *dest, int nchannels, int samplerate, capture::pcm_type outtype)
: outfile(nullptr)
, bytesWritten(0)
, channels(nchannels)
, resampleTo(outtype)
{
	if (outtype == capture::pcm_type::pcm_f32)
		throw std::runtime_error("Float is not supported");

	outfile = fopen(dest, "wb");
	if (outfile == nullptr)
		throw std::runtime_error("Cannot open output file");

	size_t bps = pcmtype_size(outtype);
	fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, outfile);
	writeint<unsigned int>(outfile, FMT_HEADER_SIZE);
	writeint<unsigned short>(outfile, 1); // PCM
	writeint<unsigned short>(outfile, nchannels);
	writeint<unsigned int>(outfile, samplerate);
	writeint<unsigned int>(outfile, 1ULL * samplerate * nchannels * bps);
	writeint<unsigned short>(outfile, nchannels * bps);
	writeint<unsigned short>(outfile, bps * 8);
	fwrite("data\0\0\0\0", 1, 8, outfile);
}


writer::~writer()
{
	fclose(outfile);
}

bool writer::write(const void *buf, size_t framecount, capture::pcm_type intype)
{
	if (intype == capture::pcm_type::unknown)
		intype = resampleTo;

	switch (intype)
	{
		case capture::pcm_type::pcm_u8:
		{
			switch (resampleTo)
			{
			case capture::pcm_type::pcm_u8:
				return write_pass(buf, framecount);
			case capture::pcm_type::pcm_s16:
				return write_8_16((const unsigned char *) buf, framecount);
			default:
				break;
			}
			break;
		}
		case capture::pcm_type::pcm_s16:
		{
			switch (resampleTo)
			{
			case capture::pcm_type::pcm_u8:
				return write_16_8((const short *) buf, framecount);
			case capture::pcm_type::pcm_s16:
				return write_pass(buf, framecount);
			default:
				break;
			}
			break;
		}
		case capture::pcm_type::pcm_f32:
		{
			switch (resampleTo)
			{
			case capture::pcm_type::pcm_u8:
				return write_32_8((const float *) buf, framecount);
			case capture::pcm_type::pcm_s16:
				return write_32_16((const float *) buf, framecount);
			default:
				break;
			}
			break;
		}
		default:
			break;
	}

	return false;
}

bool writer::update()
{
	fseek(outfile, (long) ALL_DATA_SIZE_OFF, SEEK_SET);
	writeint<unsigned int>(outfile, bytesWritten + 4 + 8 + FMT_HEADER_SIZE + 4 + 4);
	fseek(outfile, (long) DATA_CHUNK_SIZE_OFF, SEEK_SET);
	writeint<unsigned int>(outfile, bytesWritten);
	fseek(outfile, 0, SEEK_END);
	return true;
}

bool writer::write_pass(const void *buf, size_t framecount)
{
	size_t bps = pcmtype_size(resampleTo);
	size_t framesize = bps * channels;
	size_t writesz = framesize * framecount;

	if (fwrite(buf, 1, writesz, outfile) != writesz)
		return false;

	bytesWritten += writesz;
	return update();
}

bool writer::write_8_16(const unsigned char *buf, size_t framecount)
{
	for (size_t i = 0; i < framecount; i++)
	{
		for (size_t c = 0; c < channels; c++)
		{
			unsigned char data = buf[i * channels + c];
			// Range: 0...255, zero is 127
			// FIXME: Performance
			double data2 = (data - 127) / 127.0;
			short data3 = std::min(std::max(data2, -1.0), 1.0) * 32767;
			if (!writeint(outfile, data3))
				return false;

			bytesWritten += 2;
		}
	}

	return update();
}

bool writer::write_16_8(const short *buf, size_t framecount)
{
	for (size_t i = 0; i < framecount; i++)
	{
		for (size_t c = 0; c < channels; c++)
		{
			short data = buf[i * channels + c];
			// Range: -32767...32767, zero is 0
			// FIXME: Performance
			unsigned char data2 = std::min(std::max(data / 32767.0, -1.0), 1.0) * 127.0 + 127.0;
			if (!writeint(outfile, data2))
				return false;

			bytesWritten += 1;
		}
	}

	return update();
}

bool writer::write_32_8(const float *buf, size_t framecount)
{
	for (size_t i = 0; i < framecount; i++)
	{
		for (size_t c = 0; c < channels; c++)
		{
			double data = buf[i * channels + c];
			// Range: [-1, 1]
			unsigned char data2 = std::min(std::max(data, -1.0), 1.0) * 127.0 + 127.0;
			if (!writeint(outfile, data2))
				return false;

			bytesWritten += 1;
		}
	}

	return update();
}

bool writer::write_32_16(const float *buf, size_t framecount)
{
	for (size_t i = 0; i < framecount; i++)
	{
		for (size_t c = 0; c < channels; c++)
		{
			double data = buf[i * channels + c];
			// Range: [-1, 1]
			short data2 = std::min(std::max(data, -1.0), 1.0) * 32767.0;
			if (!writeint(outfile, data2))
				return false;

			bytesWritten += 2;
		}
	}

	return update();
}


bool writer::writeend()
{
	return update();
}

writer *newwriter(const char *dest, int nchannels, int samplerate, capture::pcm_type outtype)
{
	return new writer(dest, nchannels, samplerate, outtype);
}

bool write(writer *writer, const void *buf, size_t framecount, capture::pcm_type intype)
{
	return writer->write(buf, framecount, intype);
}

bool close(writer *writer)
{
	bool result = writer->writeend();
	delete writer;
	return result;
}

}
