#pragma once

#include <string>
#include <vector>

namespace capture
{

typedef struct context context;

typedef enum class pcm_type
{
	unknown,
	pcm_u8,
	pcm_s16,
	pcm_f32,
	max_enum
} pcm_type;

typedef struct device_info
{
	std::string name;
	int sampleRate;
	int channels;
	int bitsPerSample;
	pcm_type dataType;
} device_info;

typedef enum class name_match
{
	exact,
	partial,
	max_enum
} name_match;

context *open(const std::string &device = "", name_match devmatch = name_match::exact);
void close(context *&ctx);

std::vector<device_info> listdevices();

device_info getinfo(context *ctx) noexcept;
bool start(context *ctx, size_t ringbufsize);
bool stop(context *ctx);
std::vector<unsigned char> getbuf(context *ctx);

}
