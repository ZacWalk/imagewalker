#pragma once

class CDib;
class CStatus;

class CLoadAny
{
public:
	BOOL Load(CDib* dib, int resource, CStatus* status, BOOL thumbnail);
	BOOL Load(CDib* dib, LPCSTR path, CStatus* status, BOOL thumbnail);
	BOOL Load(CDib* dib, LPBYTE bytes, DWORD size, CStatus* status, BOOL thumbnail);
};
