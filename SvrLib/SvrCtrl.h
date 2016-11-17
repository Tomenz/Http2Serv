#pragma once

#include <windows.h>

class CSvrCtrl
{
public:
	CSvrCtrl(void);
	~CSvrCtrl(void);

	int Install(wchar_t* szSvrName, wchar_t* szDisplayName);
	int Remove(wchar_t* szSvrName);
	int Start(wchar_t* szSvrName);
	int Stop(wchar_t* szSvrName);
	int Pause(wchar_t* szSvrName);
	int Continue(wchar_t* szSvrName);
	bool SetServiceDescription(wchar_t* szSvrName, wchar_t* szDescription);

private:
	SC_HANDLE m_hSCManager;
};
