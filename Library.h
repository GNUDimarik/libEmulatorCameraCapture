#pragma once

#ifdef LIBCAMERACAPTURE_EXPORTS
#define LIBCAMERACAPTURE_API __declspec(dllexport)
#else
#define LIBCAMERACAPTURE_API __declspec(dllimport)
#endif

#include <divomedia/divomedia.h>
#include <divomedia/system/inputdevice.h>
#include <Windows.h>
#include <cstdint>
#include <memory>

using namespace divomedia;
using namespace divomedia::system;

#ifdef DIVO_MEDIA_MEDIA_FILE_STREAMING
#include <divomedia/utils/fs/path.h>
using namespace divomedia::utils::fs;
#endif

DIVO_MEDIA_BEGIN_DECLS

LIBCAMERACAPTURE_API int getDevicesNumber();
LIBCAMERACAPTURE_API const char* getDeviceName(int index);
LIBCAMERACAPTURE_API const char* getDeviceFriendlyName(int index);
LIBCAMERACAPTURE_API int supportsMediaType(GUID* guid, int index);
LIBCAMERACAPTURE_API int deviceOpen(const char* name);
LIBCAMERACAPTURE_API void deviceClose(int index);
LIBCAMERACAPTURE_API int startCapture(int index, GUID* mediaType, int width, int height);
LIBCAMERACAPTURE_API int stopCapture(int index);
LIBCAMERACAPTURE_API std::uint8_t* readFrame(int* len);

DIVO_MEDIA_END_DECLS

class Library
{
public:
	Library();
	int devicesNumber();
	const char* deviceName(int index);
	const char* deviceFriendlyName(int index);
	int openDevice(const char* name);
	void closeDevice(int index);
	int startCapture(int index, GUID* mediaType, int width, int height);
	std::uint8_t* readFrame(int* len);
#ifdef DIVO_MEDIA_MEDIA_FILE_STREAMING
	void updateFileList();
	void makeFriendlyName(const Path& path);
	Path findDeviceByName(const std::string& name);
#elif DIVO_MEDIA_DEVICE_STREAMING
	VideoDeviceDescription findDeviceByName(const std::string& name);
#endif
private:
#ifdef DIVO_MEDIA_MEDIA_FILE_STREAMING
	std::unique_ptr<InputFile> mSpMediaFile;
	std::vector<Path> mFileList;
	std::string mCurrentFileName;
#elif DIVO_MEDIA_DEVICE_STREAMING
	void updateDeviceList();
	std::shared_ptr<InputDevice> mSpDevice;
	std::vector<VideoDeviceDescription> mDevices;
	std::string mCurrentDeviceFriendlyName;
#endif
	std::unique_ptr<Scaler> mSpScaler;
	//std::unique_ptr<FilterGraph> mSpFilterGraph;
	std::shared_ptr<Decoder> mSpDecoder;
	std::shared_ptr<AVFrame> mSpCurrentFrame;
	Image mCurrentImage;
};

