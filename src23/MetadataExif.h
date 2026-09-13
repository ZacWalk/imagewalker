// ImageWalker by Zac Walker
//
// Purpose: EXIF: the tag table, the template parser the loader uses for
//          camera settings and orientation, and the libexif-backed reader
//          the property page uses.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

extern "C"
{
#include <libexif/exif-data.h>
}

namespace IW
{
	struct MetadataProperties;
}

class Exif
{
public:

	typedef enum {
		EXIF_TAG_INTEROPERABILITY_INDEX		= 0x0001,
		EXIF_TAG_INTEROPERABILITY_VERSION	= 0x0002,
		EXIF_TAG_IMAGE_WIDTH 			= 0x0100,
		EXIF_TAG_IMAGE_LENGTH 			= 0x0101,
		EXIF_TAG_BITS_PER_SAMPLE 		= 0x0102,
		EXIF_TAG_COMPRESSION 			= 0x0103,
		EXIF_TAG_PHOTOMETRIC_INTERPRETATION 	= 0x0106,
		EXIF_TAG_FILL_ORDER 			= 0x010a,
		EXIF_TAG_DOCUMENT_NAME 			= 0x010d,
		EXIF_TAG_IMAGE_DESCRIPTION 		= 0x010e,
		EXIF_TAG_MAKE 				= 0x010f,
		EXIF_TAG_MODEL 				= 0x0110,
		EXIF_TAG_STRIP_OFFSETS 			= 0x0111,
		EXIF_TAG_ORIENTATION 			= 0x0112,
		EXIF_TAG_SAMPLES_PER_PIXEL 		= 0x0115,
		EXIF_TAG_ROWS_PER_STRIP 		= 0x0116,
		EXIF_TAG_STRIP_BYTE_COUNTS		= 0x0117,
		EXIF_TAG_X_RESOLUTION 			= 0x011a,
		EXIF_TAG_Y_RESOLUTION 			= 0x011b,
		EXIF_TAG_PLANAR_CONFIGURATION 		= 0x011c,
		EXIF_TAG_RESOLUTION_UNIT 		= 0x0128,
		EXIF_TAG_TRANSFER_FUNCTION 		= 0x012d,
		EXIF_TAG_SOFTWARE 			= 0x0131,
		EXIF_TAG_DATE_TIME			= 0x0132,
		EXIF_TAG_ARTIST				= 0x013b,
		EXIF_TAG_WHITE_POINT			= 0x013e,
		EXIF_TAG_PRIMARY_CHROMATICITIES		= 0x013f,
		EXIF_TAG_TRANSFER_RANGE			= 0x0156,
		EXIF_TAG_JPEG_PROC			= 0x0200,
		EXIF_TAG_JPEG_INTERCHANGE_FORMAT	= 0x0201,
		EXIF_TAG_JPEG_INTERCHANGE_FORMAT_LENGTH	= 0x0202,
		EXIF_TAG_YCBCR_COEFFICIENTS		= 0x0211,
		EXIF_TAG_YCBCR_SUB_SAMPLING		= 0x0212,
		EXIF_TAG_YCBCR_POSITIONING		= 0x0213,
		EXIF_TAG_REFERENCE_BLACK_WHITE		= 0x0214,
		EXIF_TAG_RELATED_IMAGE_FILE_FORMAT	= 0x1000,
		EXIF_TAG_RELATED_IMAGE_WIDTH		= 0x1001,
		EXIF_TAG_RELATED_IMAGE_LENGTH		= 0x1002,
		EXIF_TAG_CFA_REPEAT_PATTERN_DIM		= 0x828d,
		EXIF_TAG_CFA_PATTERN			= 0x828e,
		EXIF_TAG_BATTERY_LEVEL			= 0x828f,
		EXIF_TAG_COPYRIGHT			= 0x8298,
		EXIF_TAG_EXPOSURE_TIME			= 0x829a,
		EXIF_TAG_FNUMBER			= 0x829d,
		EXIF_TAG_IPTC_NAA			= 0x83bb,
		EXIF_TAG_EXIF_IFD_POINTER		= 0x8769,
		EXIF_TAG_INTER_COLOR_PROFILE		= 0x8773,
		EXIF_TAG_EXPOSURE_PROGRAM		= 0x8822,
		EXIF_TAG_SPECTRAL_SENSITIVITY		= 0x8824,
		EXIF_TAG_GPS_INFO_IFD_POINTER		= 0x8825,
		EXIF_TAG_ISO_SPEED_RATINGS		= 0x8827,
		EXIF_TAG_OECF				= 0x8828,
		EXIF_TAG_EXIF_VERSION			= 0x9000,
		EXIF_TAG_DATE_TIME_ORIGINAL		= 0x9003,
		EXIF_TAG_DATE_TIME_DIGITIZED		= 0x9004,
		EXIF_TAG_COMPONENTS_CONFIGURATION	= 0x9101,
		EXIF_TAG_COMPRESSED_BITS_PER_PIXEL	= 0x9102,
		EXIF_TAG_SHUTTER_SPEED_VALUE		= 0x9201,
		EXIF_TAG_APERTURE_VALUE			= 0x9202,
		EXIF_TAG_BRIGHTNESS_VALUE		= 0x9203,
		EXIF_TAG_EXPOSURE_BIAS_VALUE		= 0x9204,
		EXIF_TAG_MAX_APERTURE_VALUE		= 0x9205,
		EXIF_TAG_SUBJECT_DISTANCE		= 0x9206,
		EXIF_TAG_METERING_MODE			= 0x9207,
		EXIF_TAG_LIGHT_SOURCE			= 0x9208,
		EXIF_TAG_FLASH				= 0x9209,
		EXIF_TAG_FOCAL_LENGTH			= 0x920a,
		EXIF_TAG_SUBJECT_AREA			= 0x9214,
		EXIF_TAG_MAKER_NOTE			= 0x927c,
		EXIF_TAG_USER_COMMENT			= 0x9286,
		EXIF_TAG_SUB_SEC_TIME			= 0x9290,
		EXIF_TAG_SUB_SEC_TIME_ORIGINAL		= 0x9291,
		EXIF_TAG_SUB_SEC_TIME_DIGITIZED		= 0x9292,
		EXIF_TAG_FLASH_PIX_VERSION		= 0xa000,
		EXIF_TAG_COLOR_SPACE			= 0xa001,
		EXIF_TAG_PIXEL_X_DIMENSION		= 0xa002,
		EXIF_TAG_PIXEL_Y_DIMENSION		= 0xa003,
		EXIF_TAG_RELATED_SOUND_FILE		= 0xa004,
		EXIF_TAG_INTEROPERABILITY_IFD_POINTER	= 0xa005,
		EXIF_TAG_FLASH_ENERGY			= 0xa20b,
		EXIF_TAG_SPATIAL_FREQUENCY_RESPONSE	= 0xa20c,
		EXIF_TAG_FOCAL_PLANE_X_RESOLUTION	= 0xa20e,
		EXIF_TAG_FOCAL_PLANE_Y_RESOLUTION	= 0xa20f,
		EXIF_TAG_FOCAL_PLANE_RESOLUTION_UNIT	= 0xa210,
		EXIF_TAG_SUBJECT_LOCATION		= 0xa214,
		EXIF_TAG_EXPOSURE_INDEX			= 0xa215,
		EXIF_TAG_SENSING_METHOD			= 0xa217,
		EXIF_TAG_FILE_SOURCE			= 0xa300,
		EXIF_TAG_SCENE_TYPE			= 0xa301,
		EXIF_TAG_NEW_CFA_PATTERN		= 0xa302,
		EXIF_TAG_CUSTO_renderED		= 0xa401,
		EXIF_TAG_EXPOSURE_MODE			= 0xa402,
		EXIF_TAG_WHITE_BALANCE			= 0xa403,
		EXIF_TAG_DIGITAL_ZOOM_RATIO		= 0xa404,
		EXIF_TAG_FOCAL_LENGTH_IN_35MM_FILM	= 0xa405,
		EXIF_TAG_SCENE_CAPTURE_TYPE		= 0xa406,
		EXIF_TAG_GAIN_CONTROL			= 0xa407,
		EXIF_TAG_CONTRAST			= 0xa408,
		EXIF_TAG_SATURATION			= 0xa409,
		EXIF_TAG_SHARPNESS			= 0xa40a,
		EXIF_TAG_DEVICE_SETTING_DESCRIPTION	= 0xa40b,
		EXIF_TAG_SUBJECT_DISTANCE_RANGE		= 0xa40c,
		EXIF_TAG_IMAGE_UNIQUE_ID		= 0xa420
	} ExifTag;


	class ExifMotorola
	{
	public:

		inline void SetUInt16 (unsigned char *b, IW::UInt16 value)
		{
			b[0] = (unsigned char) (value >> 8);
			b[1] = (unsigned char) value;
		}

		inline void SetInt32 (unsigned char *b, IW::Int32 value)
		{
			b[0] = (unsigned char) (value >> 24);
			b[1] = (unsigned char) (value >> 16);
			b[2] = (unsigned char) (value >> 8);
			b[3] = (unsigned char) value;
		}

		inline IW::Int16 GetInt16 (const unsigned char *buf)
		{		
			return ((buf[0] << 8) | buf[1]);
		}

		inline IW::Int32 GetInt32 (const unsigned char *b)
		{		
			return ((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]);
		}
	};

	class ExifIntel
	{
	public:
		inline void SetUInt16 (unsigned char *b, IW::UInt16 value)
		{		
			b[0] = (unsigned char) value;
			b[1] = (unsigned char) (value >> 8);
		}

		inline void SetInt32 (unsigned char *b, IW::Int32 value)
		{		
			b[3] = (unsigned char) (value >> 24);
			b[2] = (unsigned char) (value >> 16);
			b[1] = (unsigned char) (value >> 8);
			b[0] = (unsigned char) value;
		}

		inline IW::Int16 GetInt16 (const unsigned char *buf)
		{
			return ((buf[1] << 8) | buf[0]);
		}

		inline IW::Int32 GetInt32 (const unsigned char *b)
		{
			return ((b[3] << 24) | (b[2] << 16) | (b[1] << 8) | b[0]);
		}
	};

	template<class T, class TStreamOut, class TData>
	class ExifParser : public T
	{
	public:
		inline IW::UInt16 GetUInt16 (const unsigned char *buf)
		{
			return (GetInt16 (buf) & 0xffff);
		}

		inline IW::UInt32 GetUInt32 (const unsigned char *buf)
		{
			return (GetInt32 (buf) & 0xffffffff);
		}

		inline void	SetUInt32 (unsigned char *b, IW::UInt32 value)
		{
			SetInt32 (b, value);
		}

		inline IW::SRational GetSRational (const unsigned char *buf)
		{
			IW::SRational r;

			r.numerator   = GetInt32 (buf);
			r.denominator = GetInt32 (buf + 4);

			return (r);
		}

		inline IW::Rational GetRational (const unsigned char *buf)
		{
			IW::Rational r;

			r.numerator   = GetUInt32 (buf);
			r.denominator = GetUInt32 (buf + 4);

			return (r);
		}

		inline void SetRational (unsigned char *buf, IW::Rational value)
		{
			SetUInt32 (buf, value.numerator);
			SetUInt32 (buf + 4, value.denominator);
		}

		inline void SetSRational (unsigned char *buf, IW::SRational value)
		{
			SetInt32 (buf, value.numerator);
			SetInt32 (buf + 4, value.denominator);
		}

		inline void Parse(TStreamOut &propertiesOut, TData *d, unsigned int size)
		{
			// Fixed value
			if (GetUInt16 (d + 8) != 0x002a)
			{
				return;
			}

			// IFD 0 offset
			IW::UInt32 nOffset = GetUInt32 (d + 10);

			// Parse the actual exif data (offset 14)
			propertiesOut.StartSection(_T("IFD 0"));
			ParseContent (propertiesOut, d + 6, size - 6, nOffset);
			propertiesOut.EndSection();

			// IFD 1 nOffset 
			propertiesOut.StartSection(_T("IFD 1"));

			if (size >= 10 && nOffset <= size - 8)
			{
				IW::UInt16 n = GetUInt16 (d + 6 + nOffset);
				const IW::UInt32 nNext = nOffset + 2 + 12 * static_cast<IW::UInt32>(n);

				if (nNext <= size - 10)
				{
					nOffset = GetUInt32 (d + 6 + nNext);

					if (nOffset) 
					{		
						ParseContent (propertiesOut, d + 6, size - 6, nOffset);
					}
				}
			}

			propertiesOut.EndSection();
		}

		

		inline void ParseContent (TStreamOut &propertiesOut, TData *d, unsigned int ds, unsigned int offset, int nDepth = 0)
		{
			// An IFD pointer back into an enclosing IFD would recurse forever
			if (nDepth > 6)
			{
				return;
			}

			if (ds <= offset)
			{
				return; // Check Overflow 
			}

			IW::UInt32 o, thumbnail_offset = 0, thumbnail_length = 0;

			if (offset >= ds || ds - offset < 2)
			{
				return; // Check Overflow 
			}

			IW::UInt16 nNumberOfEntries = GetUInt16 (d + offset);
			offset += 2;

			for (IW::UInt16 i = 0; i < nNumberOfEntries; i++) 
			{
				IW::UInt32 nCurPos = offset + (12 * i);
				if (nCurPos + 12 > ds) return; // Check Overflow 
				TData *pEntry = d + nCurPos;

				ExifTag tag = (ExifTag)GetUInt16(pEntry);
				int format = GetUInt16(pEntry + 2);
				int Components = GetUInt32(pEntry + 4);

				switch (tag) 
				{
				case EXIF_TAG_EXIF_IFD_POINTER:
					o = GetUInt32 (pEntry + 8);
					if (o <= offset) return;
					propertiesOut.StartSection(_T("IFD EXIF"));
					ParseContent (propertiesOut, d, ds, o, nDepth + 1);
					propertiesOut.EndSection();
					break;

				case EXIF_TAG_GPS_INFO_IFD_POINTER:
					o = GetUInt32 (pEntry + 8);
					if (o <= offset) return;
					propertiesOut.StartSection(_T("IFD GPS"));					
					ParseContent (propertiesOut, d, ds, o, nDepth + 1);
					propertiesOut.EndSection();
					break;

				case EXIF_TAG_INTEROPERABILITY_IFD_POINTER:					
					o = GetUInt32 (pEntry + 8);
					if (o <= offset) return;
					propertiesOut.StartSection(_T("IFD INTEROPERABILITY"));					
					ParseContent (propertiesOut, d, ds, o, nDepth + 1);
					propertiesOut.EndSection();
					break;

				case EXIF_TAG_JPEG_INTERCHANGE_FORMAT:
					thumbnail_offset = GetUInt32 (pEntry + 8);
					if (thumbnail_offset && thumbnail_length && thumbnail_offset < ds && thumbnail_length <= ds - thumbnail_offset)
					{
						propertiesOut.Thumbnail(d + thumbnail_offset, thumbnail_length);
					}
					break;

				case EXIF_TAG_JPEG_INTERCHANGE_FORMAT_LENGTH:

					thumbnail_length = GetUInt32 (pEntry + 8);
					if (thumbnail_offset && thumbnail_length && thumbnail_offset < ds && thumbnail_length <= ds - thumbnail_offset)
					{
						propertiesOut.Thumbnail(d + thumbnail_offset, thumbnail_length);
					}
					break;				

				default:
					{
						static const int BytesPerFormat[] = {0,1,1,2,4,8,1,1,2,4,8,4,8};

						enum
						{
							NUM_FORMATS = 12,
							FMT_BYTE       = 1,
							FMT_STRING     = 2,
							FMT_USHORT     = 3,
							FMT_ULONG      = 4,
							FMT_URATIONAL  = 5,
							FMT_SBYTE      = 6,
							FMT_UNDEFINED  = 7,
							FMT_SSHORT     = 8,
							FMT_SLONG      = 9,
							FMT_SRATIONAL = 10,
							FMT_SINGLE    = 11,
							FMT_DOUBLE    = 12
						};

						if (format > 0 && format < NUM_FORMATS && Components >= 0 &&
							static_cast<IW::UInt32>(Components) <= 0xFFFFFFFFu / BytesPerFormat[format]) 
						{
							IW::UInt32 BytesCount = static_cast<IW::UInt32>(Components) * BytesPerFormat[format];

							if (BytesCount > 4)
							{
								IW::UInt32 valueOffset = GetUInt32(pEntry + 8);
								// If its bigger than 4 unsigned chars, the dir entry contains an offset.
								if (valueOffset < ds && BytesCount <= ds - valueOffset)
								{					
									propertiesOut.Tag(this, tag, d + valueOffset, format);
								}
							}
							else
							{
								// 4 unsigned chars or less and value is in the dir entry itself
								propertiesOut.Tag(this, tag, pEntry + 8, format);
							}

							
						}
					}
					break;
				}
			}
		}
	};


	template<class TStreamOut, class TData>
	static inline void IterateExif(TStreamOut &propertiesOut, TData *d, unsigned int size)
	{
		assert (d && size); // Do we have everything?
		static const unsigned char ExifHeader[] = {0x45, 0x78, 0x69, 0x66, 0x00, 0x00};

		// Check the header signature
		if (size < 14 || memcmp (d, ExifHeader, 6) != 0) 
		{
			return;
		}

		IW::UInt16 ended = *((IW::UInt16*)(d + 6));

		if (ended == 'II')
		{
			ExifParser<ExifIntel, TStreamOut, TData> parser;
			parser.Parse(propertiesOut, d, size);
		}
		else if (ended == 'MM')
		{
			ExifParser<ExifMotorola, TStreamOut, TData> parser;
			parser.Parse(propertiesOut, d, size);
		}
	}

	

	class ExifCameraSettingsProcessor
	{
		IW::CameraSettings *_pSettings;

	public:
		ExifCameraSettingsProcessor(IW::CameraSettings *pSettings) : _pSettings(pSettings)
		{
		}

		void StartSection(LPCTSTR szValueKey) {}
		void EndSection() {}
		void Thumbnail(LPCBYTE pData, DWORD dwSize)	{}

		template<class TSender>
		void Tag(TSender *pSender, ExifTag tag, const unsigned char *d, int format)
		{
			switch(tag)
			{
			case EXIF_TAG_ORIENTATION:
				_pSettings->Orientation = pSender->GetUInt16(d);
				break;

			case EXIF_TAG_APERTURE_VALUE: 
				_pSettings->Aperture = pSender->GetRational(d);
				break;

			case EXIF_TAG_EXPOSURE_TIME: 
				_pSettings->ExposureTime = pSender->GetRational(d);
				break; 

			case EXIF_TAG_ISO_SPEED_RATINGS:
				_pSettings->IsoSpeed = pSender->GetUInt16(d);
				break;

			case EXIF_TAG_FOCAL_LENGTH: 
				_pSettings->FocalLength = pSender->GetRational(d);
				break;

			case EXIF_TAG_FOCAL_LENGTH_IN_35MM_FILM: 
				_pSettings->FocalLength35mmEquivalent = pSender->GetUInt16(d);
				break;

			case EXIF_TAG_DATE_TIME_ORIGINAL:
				_pSettings->DateTaken.ParseExifDate((const char*)d);
				break;
			}
		}
	};

	static inline void Parse(IW::CameraSettings *pSettings, const unsigned char *d, unsigned int size)
	{
		ExifCameraSettingsProcessor processor(pSettings);
		IterateExif(processor, d, size);
	}

	// A tag has to be rewritten at the width the file declares for it. A 16 bit
	// write into a LONG lands in the high half on a Motorola-order file and
	// multiplies the recorded value by 65536.
	template<class TSender>
	static void WriteTagValue(TSender *pSender, unsigned char *d, int format, int value)
	{
		switch(format)
		{
		case 3: // SHORT
		case 8: // SSHORT
			pSender->SetUInt16(d, static_cast<IW::UInt16>(value));
			break;

		case 4: // LONG
		case 9: // SLONG
			pSender->SetUInt32(d, static_cast<IW::UInt32>(value));
			break;
		}
	}

	// Dimensions only. Orientation is not this function's business: the tag
	// describes how the pixels are stored, and only code that has actually
	// reoriented them may touch it.
	class ExifFixProcessor
	{
		const CSize &_sizeImage;

	public:
		ExifFixProcessor(const CSize &sizeImage) : _sizeImage(sizeImage)
		{
		}

		void StartSection(LPCTSTR szValueKey) {}
		void EndSection() {}
		void Thumbnail(LPCBYTE pData, DWORD dwSize)	{}

		template<class TSender>
		void Tag(TSender *pSender, ExifTag tag, unsigned char *d, int format)
		{
			switch(tag)
			{
			case EXIF_TAG_PIXEL_X_DIMENSION:
				WriteTagValue(pSender, d, format, _sizeImage.cx);
				break;

			case EXIF_TAG_PIXEL_Y_DIMENSION:
				WriteTagValue(pSender, d, format, _sizeImage.cy);
				break;
			}
		}
	};

	class ExifSetOrientationProcessor
	{
		int _nOrientation;

	public:
		explicit ExifSetOrientationProcessor(int nOrientation) : _nOrientation(nOrientation)
		{
		}

		void StartSection(LPCTSTR szValueKey) {}
		void EndSection() {}
		void Thumbnail(LPCBYTE pData, DWORD dwSize)	{}

		template<class TSender>
		void Tag(TSender *pSender, ExifTag tag, unsigned char *d, int format)
		{
			if (tag == EXIF_TAG_ORIENTATION)
				WriteTagValue(pSender, d, format, _nOrientation);
		}
	};

	static inline void FixExif(unsigned char *d, unsigned int size, int cx, int cy)
	{
		const CSize sizeImage(cx, cy);
		ExifFixProcessor processor(sizeImage);
		IterateExif(processor, d, size);
	}

	static inline void SetOrientation(unsigned char *d, unsigned int size, int nOrientation)
	{
		if (nOrientation < IW::Orientation::TopLeft || nOrientation > IW::Orientation::LeftBottom)
			return;

		ExifSetOrientationProcessor processor(nOrientation);
		IterateExif(processor, d, size);
	}

	// TopLeft when the file does not say. A processor of its own would be a third
	// read-side instantiation of the parser, which is what the debug back end
	// falls over on (C1001 in p2), and this one is already built.
	static inline int ReadOrientation(const unsigned char *d, unsigned int size)
	{
		IW::CameraSettings settings;
		Parse(&settings, d, size);

		const int nOrientation = static_cast<int>(settings.Orientation);

		if (nOrientation < IW::Orientation::TopLeft || nOrientation > IW::Orientation::LeftBottom)
			return IW::Orientation::TopLeft;

		return nOrientation;
	}
};

// The property view's reader. This uses libexif rather than the parser above,
// because it wants every tag with its name, title and description rather than
// the handful the loader cares about.
class MetadataExif
{
private:
	ExifData *_pExif;

	MetadataExif(const MetadataExif &other);
	void operator=(const MetadataExif &other);

public:
	explicit MetadataExif(const IW::MetaData &data);
	~MetadataExif();

	void Load(IW::MetadataProperties &propertiesOut) const;
};



