#pragma once

#include <windows.h>

class CSvrCtrl
{
public:
	CSvrCtrl(void);
	~CSvrCtrl(void);

	int Install(const wchar_t* szSvrName, const wchar_t* szDisplayName, wchar_t* szDescription = nullptr);
	int Remove(const wchar_t* szSvrName);
	int Start(const wchar_t* szSvrName);
	int Stop(const wchar_t* szSvrName);
	int Pause(const wchar_t* szSvrName);
	int Continue(const wchar_t* szSvrName);

private:
    bool SelfElevat();
    bool SetServiceDescription(const wchar_t* szSvrName, wchar_t* szDescription);

private:
	SC_HANDLE m_hSCManager;
};
