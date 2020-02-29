#include "pch.h"
#include "Library.h"
#include <stdexcept>
#include <dshow.h>
#include <iostream>

DIVO_MEDIA_BEGIN_DECLS
#include <libavutil/imgutils.h>
DIVO_MEDIA_END_DECLS

#ifdef DIVO_MEDIA_DEVICE_STREAMING
#include <divomedia/system/deviceenumerator.h>
#include <divomedia/system/inputdevice.h>
#endif

using namespace divomedia::utils;

static Library gSlibrary;

#ifndef MEDIASUBTYPE_I420
static constexpr GUID MEDIASUBTYPE_I420 = {
	0x30323449,
	0x0000,
	0x0010,
	{0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71} };
#endif

#ifndef MEDIASUBTYPE_422P
static constexpr GUID MEDIASUBTYPE_422P = {
	0x50323234,
	0x0000,
	0x0010,
	{0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71} };
#endif

#ifndef MEDIASUBTYPE_VYUY
static constexpr GUID MEDIASUBTYPE_VYUY = {
	0x59555956,
	0x0000,
	0x0010,
	{0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71} };
#endif

struct CameraFormatMapping {
	GUID mfSubtype;
	AVPixelFormat ffmpegFormat;
	const char* humanReadableName;
};

static const CameraFormatMapping kSupportedPixelFormats[] = {
	{ MEDIASUBTYPE_NV12,   AV_PIX_FMT_NV12,    "NV12"  },
	{ MEDIASUBTYPE_IYUV,   AV_PIX_FMT_YUV420P, "I420"  },
	{ MEDIASUBTYPE_I420,   AV_PIX_FMT_YUV420P, "I420"  },
	{ MEDIASUBTYPE_RGB32,  AV_PIX_FMT_BGR32,   "RGB32" },
	{ MEDIASUBTYPE_ARGB32, AV_PIX_FMT_ABGR,    "ARGB32"},
	{ MEDIASUBTYPE_RGB24,  AV_PIX_FMT_BGR24,   "RGB24" },
	{ MEDIASUBTYPE_422P,   AV_PIX_FMT_YUV422P, "422P"  },
	{ MEDIASUBTYPE_VYUY,   AV_PIX_FMT_YUYV422, "YUY2"  },
	{ MEDIASUBTYPE_UYVY,   AV_PIX_FMT_UYVY422, "UYVY"  },
	{ MEDIASUBTYPE_YV12,   AV_PIX_FMT_UYVY422, "UV12"  },
	{ MEDIASUBTYPE_YUY2,   AV_PIX_FMT_YUYV422, "YUY2"  } 
};

/// Map a AVPixelFormat to a MediaFoundation subtype.
/// If a format is not supported, 0 is returned.
static AVPixelFormat subtypeToPixelFormat(REFGUID subtype) {
	for (const auto& supportedFormat : kSupportedPixelFormats) {
		if (supportedFormat.mfSubtype == subtype) {
			return supportedFormat.ffmpegFormat;
		}
	}
	return AV_PIX_FMT_NONE;
}

// exported functions
int getDevicesNumber()
{
	return gSlibrary.devicesNumber();
}

const char* getDeviceName(int index)
{
	//av_log(nullptr, AV_LOG_INFO, "%s: %s\n", __FUNCTION__, gSlibrary.deviceName(index));
	//return gSlibrary.deviceName(index);
	return getDeviceFriendlyName(index);
}

const char* getDeviceFriendlyName(int index)
{
	return gSlibrary.deviceFriendlyName(index);
}

int supportsMediaType(GUID* guid, int index)
{
	return 1;
}

int deviceOpen(const char* name)
{
	return gSlibrary.openDevice(name);
}

void deviceClose(int index)
{
	gSlibrary.closeDevice(index);
}

int startCapture(int index, GUID* mediaType, int width, int height)
{
	return gSlibrary.startCapture(index, mediaType, width, height);
}

int stopCapture(int index)
{
	gSlibrary.closeDevice(index);
	return 0;
}

std::uint8_t* readFrame(int* len)
{
	std::uint8_t* data = gSlibrary.readFrame(len);
	return data;
}

// The library

Library::Library()
{
	divomedia::init();
}

int Library::devicesNumber()
{
#ifdef DIVO_MEDIA_MEDIA_FILE_STREAMING
	updateFileList();
	return mFileList.size();
#endif

#ifdef DIVO_MEDIA_DEVICE_STREAMING
	updateDeviceList();
	return mDevices.size();
#endif
	return 0;
}

const char* Library::deviceName(int index)
{
#ifdef DIVO_MEDIA_MEDIA_FILE_STREAMING
	updateFileList();

	try {
		mCurrentFileName = mFileList.at(index).toString() + Path::kPathSeparator + "zzz";
		return mCurrentFileName.c_str();
	}
	catch (std::out_of_range& e) {
		av_log(nullptr, AV_LOG_ERROR, "%s\n", e.what());
	}
#endif

	try {
		mCurrentDeviceFriendlyName = mDevices.at(index).name();
		return mCurrentDeviceFriendlyName.c_str();
	}
	catch (std::out_of_range & e) {
		av_log(nullptr, AV_LOG_ERROR, "%s\n", e.what());
	}

	return nullptr;
}

const char* Library::deviceFriendlyName(int index)
{
	updateDeviceList();
#ifdef DIVO_MEDIA_MEDIA_FILE_STREAMING
	try {
		Path path = mFileList.at(index);
		makeFriendlyName(path);
		return mCurrentDeviceFriendlyName.c_str();
	}
	catch (std::out_of_range& e) {
		av_log(nullptr, AV_LOG_ERROR, "%s\n", e.what());
	}
#endif

#ifdef DIVO_MEDIA_DEVICE_STREAMING
	try {
		VideoDeviceDescription descr = mDevices.at(index);
		mCurrentDeviceFriendlyName = descr.name();
		return mCurrentDeviceFriendlyName.c_str();
	}
	catch (std::out_of_range & e) {
		av_log(nullptr, AV_LOG_ERROR, "%s\n", e.what());
	}

	return nullptr;
#endif
}

int Library::openDevice(const char* name)
{
	std::cout << "openDevice" << std::endl;
	std::vector<std::shared_ptr<Decoder>> decoders;
#ifdef DIVO_MEDIA_MEDIA_FILE_STREAMING
	Path path = findDeviceByName(name);
	mSpMediaFile.reset(new InputFile(path.toString()));

	if (mSpMediaFile->open(InputFile::kReadOnly)) {
#elif DIVO_MEDIA_DEVICE_STREAMING
	updateDeviceList();
	VideoDeviceDescription descr = findDeviceByName(name);
	mCurrentDeviceFriendlyName = descr.name().c_str();
	mSpDevice.reset(new InputDevice("dshow", "vMix Video YV12"));

	if (mSpDevice->open(IODevice::kReadOnly)) {
#ifdef DIVO_MEDIA_MEDIA_FILE_STREAMING
		std::vector<std::shared_ptr<Decoder>> decoders = mSpMediaFile->decoders();
#elif DIVO_MEDIA_DEVICE_STREAMING
		mSpDevice.reset(new InputDevice("dshow", name));
		decoders = mSpDevice->decoders();
#endif
		for (const std::shared_ptr<Decoder>& decoder : decoders) {
			std::shared_ptr<Decoder> decoder = decoder;

			if (decoder->mediaType() == Codec::kVideo) {
				mSpDecoder = decoder;
				break;
			}
		}
	}
	else {
		av_log(nullptr, AV_LOG_ERROR, "device %s is not open. Error %s\n", name, mSpDevice->lastError().c_str());
	}

#endif

	if (mSpDecoder) {
		av_log(nullptr, AV_LOG_INFO, "device %s is open. Using decoder %s\n", name, mSpDecoder->name().c_str());
		return 0;
	}

	return -1;
}

void Library::closeDevice(int /* index */)
{
#ifdef DIVO_MEDIA_MEDIA_FILE_STREAMING
	if (mSpMediaFile) {
		mSpMediaFile->close();
		mSpMediaFile.reset();
	}
	else {
		av_log(nullptr, AV_LOG_ERROR, "device is not open\n");
	}
#elif DIVO_MEDIA_DEVICE_STREAMING
	if (mSpDevice) {
		mSpDevice->close();
		mSpDevice.reset();
	}
#endif

	mSpDecoder.reset();
	mSpScaler.reset();
	//mSpFilterGraph.reset();
	mSpCurrentFrame.reset();
}

int Library::startCapture(int index, GUID* mediaType, int width, int height)
{
	AVPixelFormat pixFmt = subtypeToPixelFormat(*mediaType);

	if (pixFmt != AV_PIX_FMT_NONE) {
		if (mSpDecoder) {
			Size videoSize(width, height);

			if (mSpDecoder->videoSize() != videoSize) {
				mSpScaler.reset(new Scaler());
				mSpScaler->setInputFormat(mSpDecoder->fourcc());
				//mSpScaler->setOutputFormat(Fourcc::fromPixelFormat(pixFmt));
				mSpScaler->setOutputFormat(Fourcc::fromPixelFormat(AV_PIX_FMT_BGR32));
				mSpScaler->setInputSize(mSpDecoder->videoSize());
				mSpScaler->setOutputSize(videoSize);
				mSpScaler->initialize();
				mCurrentImage = mSpScaler->createOutputImage();
			}

			return 0;
		}
	}

	av_log(nullptr, AV_LOG_ERROR, "requested format not supported\n");
	return -1;
}

std::uint8_t* Library::readFrame(int* len)
{
	if (!len) {
		av_log(nullptr, AV_LOG_ERROR, "Output parameter for frame lenght is null\n");
		return nullptr;
	}
#ifdef DIVO_MEDIA_MEDIA_FILE_STREAMING
	if (mSpMediaFile) {
#elif DIVO_MEDIA_DEVICE_STREAMING
	if (mSpDevice) {
#endif
		if (mSpDecoder) {
			std::shared_ptr<AVPacket> packet;

			do {
#ifdef DIVO_MEDIA_MEDIA_FILE_STREAMING
				packet = mSpMediaFile->read();
#elif DIVO_MEDIA_DEVICE_STREAMING
				packet = mSpDevice->read();
#endif
				if (!packet || packet->size <= 0) {
					break;
				}

			} while (!mSpDecoder->isSameStream(packet));

			mSpCurrentFrame = utils::createEmptyFrame();

			if (mSpDecoder->processPacket(packet, mSpCurrentFrame) == 0) {
				Size sz;
				Fourcc fourcc;
				if (mSpScaler) {
					int scaledHeight = mSpScaler->scale(mSpCurrentFrame, mCurrentImage);

					if (scaledHeight > 0) {
						sz = mSpScaler->outputSize();
						fourcc = mSpScaler->outputFormat();
						* len = mCurrentImage.bufferSize();
						return mCurrentImage.data();
					}
					else {
						av_log(nullptr, AV_LOG_ERROR, "Could not scale frame\n");
					}
				}

				sz = mSpDecoder->videoSize();
				fourcc = mSpDecoder->fourcc();
				*len = av_image_get_buffer_size(fourcc.toPixelFormat(), sz.width(), sz.height(), 1);
				return mSpCurrentFrame->data[0];
			}
			else {
				av_log(nullptr, AV_LOG_ERROR, "Could not decode frame\n");
			}
		}
		else {
			av_log(nullptr, AV_LOG_ERROR, "decoder is not initialized. Something wrong ...\n");
		}
	}
	else {
		av_log(nullptr, AV_LOG_ERROR, "device is not open\n");
	}

	*len = 0;
	return nullptr;
}

#ifdef DIVO_MEDIA_MEDIA_FILE_STREAMING
void Library::updateFileList()
{
	Path home = Path::homePath();
	std::list<std::string> mimeTypes;
	mimeTypes.push_back(".mp4");
	std::list<Path> children = home.children(mimeTypes);
	std::copy(children.begin(), children.end(), std::back_inserter(mFileList));
}

void Library::makeFriendlyName(const Path& path)
{
	if (path.isFile() && path.exists()) {
		std::string str = path.toString();
		std::list<std::string> stringList = StringUtils::split(str, Path::kPathSeparator);
		mCurrentDeviceFriendlyName = *stringList.rbegin();
	}
}

Path Library::findDeviceByName(const std::string& name)
{
	updateFileList();
	Path ret;
	std::vector<Path>::const_iterator iter;

	for (iter = mFileList.begin(); iter != mFileList.end(); ++iter) {
		if (StringUtils::endsWidth(iter->toString(), name)) {
			return *iter;
		}
	}

	return ret;
}

#elif DIVO_MEDIA_DEVICE_STREAMING
VideoDeviceDescription Library::findDeviceByName(const std::string& name)
{
	VideoDeviceDescription descr;

	for (const VideoDeviceDescription &descr : mDevices) {
		if (descr.name() == name) {
			return descr;
		}
	}

	return descr;
}

void Library::updateDeviceList()
{
	DeviceEnumerator enumerator("dshow");
	mDevices = enumerator.availableVideoCaptureDevices();
}
#endif
