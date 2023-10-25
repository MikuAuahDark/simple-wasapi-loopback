#include <algorithm>
#include <stdexcept>
#include <vector>
#include <typeinfo>
#include <type_traits>

#include <combaseapi.h>
#include <initguid.h>
#include <propkeydef.h>

#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>

#include "capture.hxx"
#include "conv.hxx"

namespace capture
{

class COMException: public std::runtime_error
{
	static constexpr HRESULT WCODE_HRESULT_FIRST = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x200);
	static constexpr HRESULT WCODE_HRESULT_LAST = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF+1, 0) - 1;

public:
	COMException(HRESULT code)
	: std::runtime_error(fromHRESULT(code))
	{
	}

	static std::string fromHRESULT(HRESULT code)
	{
		wchar_t *errorMessage = nullptr;

		FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			code,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPWSTR)&errorMessage,
			0,
			nullptr
		);

		if (errorMessage != nullptr)
		{
			size_t nLen = wcslen(errorMessage);

			if (nLen > 1 && errorMessage[nLen - 1] == '\n') {
				errorMessage[nLen - 1] = 0;
				if (errorMessage[nLen - 2] == '\r')
					errorMessage[nLen - 2] = 0;
			}
		}
		else
		{
			errorMessage = (LPWSTR) LocalAlloc(0, 32 * sizeof(wchar_t));

			if (errorMessage != nullptr) {
				int wCode = (code >= WCODE_HRESULT_FIRST && code <= WCODE_HRESULT_LAST) ? (code - WCODE_HRESULT_FIRST) : 0;

				if (wCode != 0)
					swprintf(errorMessage, 32, L"IDispatch error #%d", wCode);
				else
					swprintf(errorMessage, 32, L"Unknown error 0x%08X", code);
			}
		}

		if (errorMessage == nullptr)
			return "Unknown Error";

		std::string result = conv::fromwstring(errorMessage);
		LocalFree(errorMessage);

		return result;
	}
};

inline void checkHRESULT(HRESULT hr)
{
	if (FAILED(hr))
		throw COMException(hr);
}

inline PROPVARIANT PropVariantInit()
{
	PROPVARIANT propv;
	::PropVariantInit(&propv);
	return propv;
}

inline char tolowerchar(char val)
{
	return (char) ::tolower((unsigned char) val);
}

inline wchar_t tolowerwchar(wchar_t val)
{
	if (sizeof(wchar_t) == 1)
		return (wchar_t) ::tolower((unsigned char) val);
	else if (sizeof(wchar_t) == 2)
		return (wchar_t) ::tolower((unsigned short) val);
	else
		return (wchar_t) ::tolower((unsigned int) val);
}

static pcm_type pcmtype_from_waveformat(const WAVEFORMATEXTENSIBLE &wfx)
{
	if (wfx.Format.cbSize >= 22)
	{
		// WAVEFORMATEXTENSIBLE
		if (wfx.Format.wFormatTag != WAVE_FORMAT_EXTENSIBLE)
			return pcm_type::unknown;

		if (wfx.SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
		{
			switch (wfx.Format.wBitsPerSample)
			{
			case 8:
				return pcm_type::pcm_u8;
			case 16:
				return pcm_type::pcm_s16;
			}
		}
		else if (wfx.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT && wfx.Format.wBitsPerSample == 32)
			return pcm_type::pcm_f32;
		else
			return pcm_type::unknown;
	}
	else
	{
		if (wfx.Format.wFormatTag != WAVE_FORMAT_PCM)
			return pcm_type::unknown;

		switch (wfx.Format.wBitsPerSample)
		{
		case 8:
			return pcm_type::pcm_u8;
		case 16:
			return pcm_type::pcm_s16;
		}
	}

	return pcm_type::unknown;
}

static size_t pcmtype_size(pcm_type t)
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

template<typename T = IUnknown>
class COMWrapper
{
public:
	COMWrapper()
	: original(nullptr)
	{
	}

	// WARNING: No AddRef()
	COMWrapper(T *ptr)
	: original(ptr)
	{
	}

	COMWrapper(const COMWrapper<T> &other)
	: original(other->original)
	{
		original->AddRef();
	}

	COMWrapper(COMWrapper<T> &&other)
	: original(other->original)
	{
		if (original)
			original->AddRef();

		if (other.original)
			other.original->Release();

		other.original = nullptr;
	}

	COMWrapper(REFCLSID clsid, DWORD context, LPUNKNOWN aggregate = nullptr)
	: original(nullptr)
	{
		checkHRESULT(CoCreateInstance(clsid, aggregate, context, __uuidof(T), (void **) &original));
	}

	COMWrapper(REFCLSID clsid, DWORD context, REFIID iid, LPUNKNOWN aggregate = nullptr)
	: original(nullptr)
	{
		checkHRESULT(CoCreateInstance(clsid, aggregate, context, iid, (void **) &original));
	}

	~COMWrapper()
	{
		if (original)
			original->Release();
	}

	// WARNING: No AddRef()
	COMWrapper<T> &operator=(T *ptr)
	{
		original = ptr;
	}

	COMWrapper<T> &operator=(const COMWrapper<T> &other)
	{
		original = other.original;
		original->AddRef();
		return *this;
	}

	bool operator==(std::nullptr_t &other) const noexcept
	{
		return original == nullptr;
	}

	bool operator!=(std::nullptr_t &other) const noexcept
	{
		return original != nullptr;
	}

	operator T*()
	{
		return original;
	}

	operator bool() const noexcept
	{
		return original;
	}

	// WARNING: No AddRef()
	T** operator&()
	{
		return &original;
	}

	template<typename U = IUnknown>
	explicit operator COMWrapper<U>()
	{
		U* result = nullptr;
		HRESULT code = original->QueryInterface(__uuidof(U), (void **) &result);

		if (FAILED(code))
		{
			std::string err = COMException::fromHRESULT(code);
			throw std::bad_cast(err.c_str(), err.length());
		}

		return COMWrapper<U>(result);
	}

	T *operator->() const noexcept
	{
		return original;
	}

	void release()
	{
		original->Release();
		original = nullptr;
	}

private:
	T *original;
};

struct context
{
	context(const std::string &device, name_match match);
	~context();

	device_info getInfo() const noexcept;
	bool startCapture(size_t ringbufsize);
	bool stopCapture();
	std::vector<unsigned char> getBuffers();

private:
	WAVEFORMATEXTENSIBLE format;
	std::string name;
	COMWrapper<IMMDevice> device;
	COMWrapper<IAudioClient> audioClient;
	COMWrapper<IAudioCaptureClient> audioCaptureClient;
	size_t bufcount;
	bool capture;
};

context::context(const std::string &device, name_match match)
: format()
, name()
, device(nullptr)
, audioClient(nullptr)
, audioCaptureClient(nullptr)
, bufcount(0)
, capture(false)
{
	COMWrapper<IMMDeviceEnumerator> enumerator(__uuidof(MMDeviceEnumerator), CLSCTX_ALL);
	COMWrapper<IMMDevice> targetDevice;

	if (!device.empty())
	{
		std::wstring deviceName = conv::fromstring(device);
		if (match == name_match::partial)
			std::transform(deviceName.begin(), deviceName.end(), deviceName.begin(), tolowerwchar);

		COMWrapper<IMMDeviceCollection> devices;
		checkHRESULT(enumerator->EnumAudioEndpoints(EDataFlow::eRender, DEVICE_STATE_ACTIVE, &devices));

		UINT deviceCount = 0;
		checkHRESULT(devices->GetCount(&deviceCount));

		for (UINT i = 0; i < deviceCount; i++)
		{
			COMWrapper<IMMDevice> immDevice;
			COMWrapper<IPropertyStore> immProp;

			checkHRESULT(devices->Item(i, &immDevice));
			checkHRESULT(immDevice->OpenPropertyStore(STGM_READ, &immProp));

			PROPVARIANT realDeviceName = PropVariantInit();
			checkHRESULT(immProp->GetValue(PKEY_Device_FriendlyName, &realDeviceName));

			std::wstring friendlyName = std::wstring(realDeviceName.pwszVal);

			if (match == name_match::partial)
			{
				std::transform(friendlyName.begin(), friendlyName.end(), friendlyName.begin(), tolowerwchar);

				if (friendlyName.find(deviceName) != std::wstring::npos)
				{
					targetDevice = immDevice;
					break;
				}
			}
			else if (match == name_match::exact && friendlyName == deviceName)
			{
				targetDevice = immDevice;
				break;
			}
		}
	}
	else
		checkHRESULT(enumerator->GetDefaultAudioEndpoint(EDataFlow::eRender, ERole::eConsole, &targetDevice));

	if (targetDevice == nullptr)
		throw std::runtime_error("No device found");


	COMWrapper<IPropertyStore> immProp;
	checkHRESULT(targetDevice->OpenPropertyStore(STGM_READ, &immProp));

	PROPVARIANT realDeviceName = PropVariantInit();
	checkHRESULT(immProp->GetValue(PKEY_Device_FriendlyName, &realDeviceName));

	COMWrapper<IAudioClient> targetAudioClient;
	WAVEFORMATEXTENSIBLE *formatTemp = nullptr;
	checkHRESULT(targetDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **) &targetAudioClient));
	checkHRESULT(targetAudioClient->GetMixFormat((WAVEFORMATEX **) &formatTemp));

	this->device = targetDevice;
	audioClient = targetAudioClient;
	format = *formatTemp;
	name = conv::fromwstring(realDeviceName.pwszVal);

	CoTaskMemFree(formatTemp);
}

context::~context()
{
}

device_info context::getInfo() const noexcept
{
	return {name, (int) format.Format.nSamplesPerSec, format.Format.nChannels, format.Format.wBitsPerSample, pcmtype_from_waveformat(format)};
}

bool context::startCapture(size_t ringbufsize)
{
	REFERENCE_TIME bufdur = ringbufsize * 10000000LL / format.Format.nSamplesPerSec;
	try
	{
		checkHRESULT(audioClient->Initialize(
			AUDCLNT_SHAREMODE::AUDCLNT_SHAREMODE_SHARED,
			AUDCLNT_STREAMFLAGS_LOOPBACK,
			bufdur,
			0LL,
			(const WAVEFORMATEX *) &format,
			nullptr
		));

		UINT32 frameCount = 0;
		checkHRESULT(audioClient->GetBufferSize(&frameCount));
		bufcount = frameCount;

		checkHRESULT(audioClient->GetService(__uuidof(IAudioCaptureClient), (void **) &audioCaptureClient));
		checkHRESULT(audioClient->Start());

	} catch (const COMException &e)
	{
		fprintf(stderr, "DEBUG: %s\n", e.what());
		return false;
	}

	return capture = true;
}

std::vector<unsigned char> context::getBuffers()
{
	std::vector<unsigned char> result;
	size_t pos = 0;
	size_t cursize = 0;
	size_t framesize = pcmtype_size(pcmtype_from_waveformat(format)) * format.Format.nChannels;
	DWORD flags = 0;

	while (true)
	{
		UINT32 packetSize = 0;
		checkHRESULT(audioCaptureClient->GetNextPacketSize(&packetSize));

		if (packetSize == 0)
			break;

		BYTE *dataPtr = nullptr;
		checkHRESULT(audioCaptureClient->GetBuffer(&dataPtr, &packetSize, &flags, nullptr, nullptr));
		result.resize(cursize += packetSize * framesize, 0);

		if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT))
			std::copy(dataPtr, dataPtr + packetSize * framesize, result.begin() + pos);

		pos += packetSize * framesize;
		checkHRESULT(audioCaptureClient->ReleaseBuffer(packetSize));
	}

	return result;
}

bool context::stopCapture()
{
	if (!capture)
		return false;

	checkHRESULT(audioClient->Stop());
	audioCaptureClient.release();
	return capture = false;
}

context *open(const std::string &device, name_match devmatch)
{
	checkHRESULT(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
	return new context(device, devmatch);
}

void close(context *&ctx)
{
	delete ctx;
	ctx = nullptr;
	CoUninitialize();
}

std::vector<device_info> listdevices()
{
	checkHRESULT(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
	std::vector<device_info> result;

	try
	{
		COMWrapper<IMMDeviceEnumerator> enumerator(__uuidof(MMDeviceEnumerator), CLSCTX_ALL);

		COMWrapper<IMMDeviceCollection> devices;
		checkHRESULT(enumerator->EnumAudioEndpoints(EDataFlow::eRender, DEVICE_STATE_ACTIVE, &devices));

		UINT deviceCount = 0;
		checkHRESULT(devices->GetCount(&deviceCount));

		for (UINT i = 0; i < deviceCount; i++)
		{
			COMWrapper<IMMDevice> immDevice;
			COMWrapper<IPropertyStore> immProp;

			checkHRESULT(devices->Item(i, &immDevice));
			checkHRESULT(immDevice->OpenPropertyStore(STGM_READ, &immProp));

			PROPVARIANT realDeviceName = PropVariantInit();
			checkHRESULT(immProp->GetValue(PKEY_Device_FriendlyName, &realDeviceName));
			
			COMWrapper<IAudioClient> audioClient;
			WAVEFORMATEXTENSIBLE *format = nullptr;
			checkHRESULT(immDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **) &audioClient));
			checkHRESULT(audioClient->GetMixFormat((WAVEFORMATEX **) &format));

			result.push_back({
				conv::fromwstring(realDeviceName.pwszVal),
				(int) format->Format.nSamplesPerSec,
				format->Format.nChannels,
				format->Format.wBitsPerSample,
				pcmtype_from_waveformat(*format)
			});
			CoTaskMemFree(format);
		}
	}
	catch (const std::exception &e)
	{
		CoUninitialize();
		throw;
	}

	return result;
}

device_info getinfo(context *ctx) noexcept
{
	return ctx->getInfo();
}

bool start(context *ctx, size_t ringbufsize)
{
	return ctx->startCapture(ringbufsize);
}

bool stop(context *ctx)
{
	return ctx->stopCapture();
}

std::vector<unsigned char> getbuf(context *ctx)
{
	return ctx->getBuffers();
}

void sleep(double nsec)
{
	Sleep(DWORD(nsec * 1000.0));
}

}
