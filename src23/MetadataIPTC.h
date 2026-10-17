// ImageWalker by Zac Walker
//
// Purpose: IPTC IIM reader. One bounds-checked walk over the record stream;
//          read-only.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

namespace IW
{
	struct MetadataProperties;
}

typedef struct tagIptcSpec
{
	short nId;
	DWORD nStringID;
} IptcSpec;

extern IptcSpec g_iptc_tags[];

// Read-only. The record editor this used to carry rewrote the whole IIM block
// in place -- through a refcount-shared buffer, at ANSI byte lengths taken from
// wide character counts -- and its callers reassembled a Photoshop resource
// block from just the one resource they understood. All of it is gone.
class MetadataIPTC
{
private:
	IW::MetaData _data;

public:
	explicit MetadataIPTC(const IW::MetaData &data) : _data(data) {}

	bool Read(int nDataset, int nRecord, CString &strOut) const;
	void Load(IW::MetadataProperties &propertiesOut) const;

	static CString GetKey(int nDataset, int nRecord);

	// The blob is either a bare IIM stream, from a format that stores one, or the
	// whole Photoshop APP13 segment a JPEG carries: "Photoshop 3.0\0" followed by
	// a chain of 8BIM resources, of which 0x0404 holds the IIM records. The JPEG
	// loader keeps the segment intact so a save can put it back byte for byte,
	// which is why finding the records is this class's job and not the loader's.
	static bool FindIimStream(LPCBYTE p, int nSize, int &nOffsetOut, int &nLengthOut)
	{
		if (p == nullptr || nSize <= 0)
			return false;

		if (p[0] == 0x1c)
		{
			nOffsetOut = 0;
			nLengthOut = nSize;
			return true;
		}

		int i = 0;

		if (nSize >= 14 && memcmp(p, "Photoshop 3.0", 13) == 0 && p[13] == 0)
			i = 14;

		while (i + 12 <= nSize)
		{
			if (memcmp(p + i, "8BIM", 4) != 0)
				return false;

			const int nId = (p[i + 4] << 8) | p[i + 5];

			// A Pascal string name, padded so the size that follows it is aligned.
			int j = i + 6;
			j += 1 + p[j];
			if ((j - (i + 6)) & 1) j += 1;

			if (j + 4 > nSize)
				return false;

			const unsigned nResource = (static_cast<unsigned>(p[j]) << 24) |
				(static_cast<unsigned>(p[j + 1]) << 16) |
				(static_cast<unsigned>(p[j + 2]) << 8) |
				static_cast<unsigned>(p[j + 3]);

			j += 4;

			if (static_cast<__int64>(j) + nResource > nSize)
				return false;

			if (nId == 0x0404)
			{
				nOffsetOut = j;
				nLengthOut = static_cast<int>(nResource);
				return true;
			}

			j += static_cast<int>(nResource + (nResource & 1));
			i = j;
		}

		return false;
	}

	// What a format with no Photoshop container of its own has to be given.
	static bool GetIimStream(const IW::MetaData &data, LPCBYTE &pOut, unsigned &nSizeOut)
	{
		int nOffset = 0;
		int nLength = 0;

		if (data.IsEmpty() || !FindIimStream(data.GetData(), data.GetDataSize(), nOffset, nLength))
			return false;

		pOut = data.GetData() + nOffset;
		nSizeOut = static_cast<unsigned>(nLength);
		return true;
	}

	// One walk over the IIM stream; both readers above are a filter on it. The
	// four hand-rolled copies this replaces disagreed about bounds, and the one
	// the property page used read up to eight bytes off the end of a truncated
	// block.
	template <class TFunc>
	void ForEachRecord(TFunc fn) const
	{
		if (_data.IsEmpty())
			return;

		int nStart = 0;
		int nDataLength = 0;

		if (!FindIimStream(_data.GetData(), _data.GetDataSize(), nStart, nDataLength))
			return;

		LPCBYTE pBuffer = _data.GetData() + nStart;

		for (int i = 0; i + 5 <= nDataLength;)
		{
			if (pBuffer[i] != 0x1c)
				break;

			int nLength = 0;
			int nHeaderLength = 0;

			if (pBuffer[i + 3] & static_cast<unsigned char>(0x80))
			{
				if (i + 8 > nDataLength)
					break;

				nLength = (static_cast<long>(pBuffer[i + 4]) << 24) |
					(static_cast<long>(pBuffer[i + 5]) << 16) |
					(static_cast<long>(pBuffer[i + 6]) << 8) |
					(static_cast<long>(pBuffer[i + 7]));

				nHeaderLength = 8;
			}
			else
			{
				nLength = (pBuffer[i + 3] << 8) | pBuffer[i + 4];
				nHeaderLength = 5;
			}

			if (nLength < 0 || static_cast<__int64>(i) + nLength + nHeaderLength > nDataLength)
				break;

			if (nLength > 0)
			{
				fn(static_cast<int>(pBuffer[i + 1]), static_cast<int>(pBuffer[i + 2]),
				   reinterpret_cast<LPCSTR>(pBuffer + i + nHeaderLength), nLength);
			}

			i += nLength + nHeaderLength;
		}
	}
};
