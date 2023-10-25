#include <csignal>
#include <cstdio>

#include <fcntl.h>
#include <io.h>

#include "CLI11.hpp"
#include "capture.hxx"
#include "wave.hxx"

bool quitit = false;

void catchint(int i)
{
	quitit = true;
}

const char *mappcmtype(capture::pcm_type t)
{
	switch (t)
	{
	case capture::pcm_type::pcm_u8:
		return "pcm_8u";
	case capture::pcm_type::pcm_s16:
		return "pcm_s16";
	case capture::pcm_type::pcm_f32:
		return "pcm_f32";
	default:
		return "unknown";
	}
}

void printdevinfo(FILE *dest, const capture::device_info &info)
{
	fprintf(
		dest,
		"DEVICE INFORMATION\nName: %s\nSample Rate: %d\nChannels: %d\nBPS: %d\nData Type: %s\n",
		info.name.c_str(),
		info.sampleRate,
		info.channels,
		info.bitsPerSample,
		mappcmtype(info.dataType)
	);
}

int main(int argc, char *argv[])
{
	CLI::App parser("Simple loopback capture", argv[0]);

	std::string outputPath;
	std::string devName;
	std::vector<int> includeProcesses;
	std::vector<int> excludeProcesses;
	bool infoOnly = false;
	bool listOnly = false;
	bool findMode = false;

	parser.add_flag("--info", infoOnly, "Print output device information.");
	parser.add_flag("--list", listOnly, "Print device list.");
	parser.add_flag("--find", findMode, "Find device name instead of exact match.");
	parser.add_option("--name", devName, "Exact device name to use.");
	parser.add_option<std::vector<int>, int>("--include", includeProcesses, "List of PID to include audio.");
	parser.add_option<std::vector<int>, int>("--exclude", excludeProcesses, "List of PID to exclude audio.");
	parser.add_option("output", outputPath, "File output path.");

	try
	{
		parser.parse(argc, argv);
	}
	catch (const CLI::ParseError &e)
	{
		return parser.exit(e);
	}

	if (listOnly)
	{
		try
		{
			for (const capture::device_info &devinfo: capture::listdevices())
				printdevinfo(stdout, devinfo);

			return 0;
		}
		catch (const std::runtime_error &e)
		{
			fprintf(stderr, "Error: cannot enumerate devices: %s\n", e.what());
			return 1;
		}
	}

	capture::context *ctx = nullptr;

	try
	{
		ctx = capture::open(devName, findMode ? capture::name_match::partial : capture::name_match::exact);
	}
	catch (const std::runtime_error &e)
	{
		fprintf(stderr, "Error: cannot open capture device: %s\n", e.what());
		return 1;
	}

	capture::device_info devinfo = capture::getinfo(ctx);
	printdevinfo((outputPath.empty() && !infoOnly) ? stderr : stdout, devinfo);
	if (infoOnly)
	{
		capture::close(ctx);
		return 0;
	}

	wave::writer *writer = nullptr;

	if (outputPath.empty())
	{
		fflush(stdout);
		_setmode(fileno(stdout), _O_BINARY);
	}
	else
	{
		try
		{
			writer = wave::newwriter(outputPath.c_str(), devinfo.channels, devinfo.sampleRate, capture::pcm_type::pcm_s16);
			if (writer == nullptr)
				throw std::runtime_error("Cannot create WAV writer");
		}
		catch (const std::runtime_error &e)
		{
			// TODO
			capture::close(ctx);
			fprintf(stderr, "Error when making WAV writer: %s\n", e.what());
			return 1;
		}
	}

	if (!capture::start(ctx, 16384))
	{
		capture::close(ctx);
		fprintf(stderr, "Error: cannot start capture\n");
		return 1;
	}

	signal(SIGINT, catchint);

	while (!quitit)
	{
		std::vector<unsigned char> buffers = capture::getbuf(ctx);
		if (buffers.size() > 0)
		{
			if (writer)
				wave::write(writer, buffers.data(), buffers.size() / devinfo.channels / (devinfo.bitsPerSample / 8), devinfo.dataType);
			else
			{
				fwrite(buffers.data(), 1, buffers.size(), stdout);
				fflush(stdout);
			}
		}
	}

	capture::stop(ctx);
	capture::close(ctx);

	if (writer)
		wave::close(writer);
	
	return 0;
}
