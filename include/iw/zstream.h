#pragma once

#include <zlib.h>

namespace IW
{

	class zstream : public z_stream
	{
	public:
		zstream()
		{
			zalloc = (alloc_func)0;
			zfree = (free_func)0;
			opaque = (voidpf)0;
			avail_in = 0;
		};
	};

	class ZStreamBase
	{
	protected:
		zstream _stream;
		enum { buffer_size = IW::LoadBufferSize };
		BYTE _buffer[buffer_size + 1];
	};

	template<class TStreamNext>
	class zistream : ZStreamBase
	{
	public:		
		TStreamNext &_streamNext;

		zistream(TStreamNext &streamNext) : _streamNext(streamNext)
		{
			int err = inflateInit(&_stream);
			if (err != Z_OK) throw IW::invalid_file();
		}

		~zistream()
		{
			const int err = inflateEnd(&_stream);
			// A destructor is noexcept, so throwing here would terminate. There is
			// nothing to be done about a failed teardown anyway.
			assert(err == Z_OK || err == Z_DATA_ERROR);
			(void) err;
		}

		void Read(LPVOID lpBuf, DWORD nCount, LPDWORD pdwRead = 0)
		{
			assert(lpBuf != NULL);

			if (nCount != 0)
			{
				_stream.next_out = (LPBYTE)lpBuf;
				_stream.avail_out = nCount;			

				while(_stream.avail_out > 0)
				{
					if (_stream.avail_in == 0)
					{
						DWORD dwRead = 0;
						_streamNext.Read(_buffer, buffer_size, &dwRead);
						_stream.avail_in = dwRead;
						_stream.next_in = _buffer;

						if (dwRead == 0) break;
					}

					int err = inflate(&_stream, Z_NO_FLUSH);
					if (err == Z_STREAM_END) break;
					if (err != Z_OK) throw IW::invalid_file();
				}			
			}

			if (pdwRead) *pdwRead = nCount - _stream.avail_out;
		}
	};


	template<class TStreamNext>
	class zostream : ZStreamBase
	{
	public:
		TStreamNext &_streamNext;

		zostream(TStreamNext &streamNext) : _streamNext(streamNext)
		{
			int err = deflateInit(&_stream, Z_BEST_COMPRESSION);
			if (err != Z_OK) throw IW::invalid_file();

			_stream.next_out = _buffer;
			_stream.avail_out = buffer_size;
		}	

		~zostream()
		{
			const int err = deflateEnd(&_stream);
			// A destructor is noexcept, so throwing here would terminate. There is
			// nothing to be done about a failed teardown anyway.
			assert(err == Z_OK || err == Z_DATA_ERROR);
			(void) err;
		}

		void Write(LPCVOID lpBuf, DWORD nCount, LPDWORD pdwWritten = 0)
		{			
			DWORD nWrittenTotal = 0;

			assert(this);
			if (nCount == 0) return;

			assert(lpBuf != NULL);

			_stream.next_in  = (Bytef*)lpBuf;
			_stream.avail_in = nCount;

			while (_stream.avail_in != 0)
			{
				int err = deflate(&_stream, Z_NO_FLUSH);
				if (err != Z_OK) throw IW::invalid_file();

				if (_stream.avail_out == 0)
				{
					DWORD nWritten = 0;
					_streamNext.Write(&_buffer, buffer_size, &nWritten);
					if (nWritten != buffer_size) throw IW::invalid_file();
					nWrittenTotal += nWritten;

					_stream.next_out = _buffer;
					_stream.avail_out = buffer_size;
				}
			}

			if (pdwWritten) *pdwWritten = nWrittenTotal;
		}

		void Close()
		{
			int err = Z_OK;

			while(err == Z_OK) 
			{
				err = deflate(&_stream, Z_FINISH);

				DWORD nOut = buffer_size - _stream.avail_out;

				if (nOut > 0)
				{
					DWORD nWritten;
					_streamNext.Write(&_buffer, nOut, &nWritten);
					if (nWritten != nOut) throw IW::invalid_file();

					_stream.next_out = _buffer;
					_stream.avail_out = buffer_size;
				}

				if (err == Z_STREAM_END) break;
				if (err != Z_OK) throw IW::invalid_file();
			}
		}
	};

}; // namespace IW


