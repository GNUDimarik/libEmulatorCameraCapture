#include "pch.h"
#include "Library.h"
#include <stdexcept>
#include <dshow.h>

DIVO_MEDIA_BEGIN_DECLS
#include <libavutil/imgutils.h>
DIVO_MEDIA_END_DECLS

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
	{ MEDIASUBTYPE_NV12,   AV_PIX_FMT_NV12, "NV12"    },
	{ MEDIASUBTYPE_IYUV,   AV_PIX_FMT_YUV420P, "I420" },
	{ MEDIASUBTYPE_I420,   AV_PIX_FMT_YUV420P, "I420" },
	{ MEDIASUBTYPE_RGB32,  AV_PIX_FMT_BGR32, "RGB32"  },
	{ MEDIASUBTYPE_ARGB32, AV_PIX_FMT_ABGR, "ARGB32"  },
	{ MEDIASUBTYPE_RGB24,  AV_PIX_FMT_BGR24, "RGB24"  },
	{ MEDIASUBTYPE_422P,   AV_PIX_FMT_YUV422P, "422P" },
	{ MEDIASUBTYPE_VYUY,   AV_PIX_FMT_YUYV422, "YUY2" },
	{ MEDIASUBTYPE_UYVY,   AV_PIX_FMT_UYVY422, "UYVY" },
	{ MEDIASUBTYPE_YV12,   AV_PIX_FMT_UYVY422, "UV12" },
	{ MEDIASUBTYPE_YUY2,   AV_PIX_FMT_YUYV422, "YUY2" } 
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
    // TODO: enumerate real devices
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
	return nullptr;
}

const char* Library::deviceFriendlyName(int index)
{
	try {
		Path path = mFileList.at(index);
		makeFriendlyName(path);
		return mCurrentFileFriendlyName.c_str();
	}
	catch (std::out_of_range& e) {
		av_log(nullptr, AV_LOG_ERROR, "%s\n", e.what());
	}
}

int Library::openDevice(const char* name)
{
#ifdef DIVO_MEDIA_MEDIA_FILE_STREAMING
	Path path = findDeviceByName(name);
	mSpMediaFile.reset(new InputFile(path.toString()));

	if (mSpMediaFile->open(InputFile::kReadOnly)) {
		std::vector<std::shared_ptr<Decoder>> decoders = mSpMediaFile->decoders();
		std::vector<std::shared_ptr<Decoder>>::const_iterator iter;

		for (iter = decoders.begin(); iter != decoders.end(); ++iter) {
			std::shared_ptr<Decoder> decoder = *iter;

			if (decoder->mediaType() == Codec::kVideo) {
				mSpDecoder = decoder;
				break;
			}
		}
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
		mSpDecoder.reset();
		mSpMediaFile.reset();
	}
	else {
		av_log(nullptr, AV_LOG_ERROR, "device is not open\n");
	}
#endif

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

	if (mSpMediaFile) {
		if (mSpDecoder) {
			std::shared_ptr<AVPacket> packet;

			do {
				packet = mSpMediaFile->read();

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
		mCurrentFileFriendlyName = *stringList.rbegin();
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
#endif
