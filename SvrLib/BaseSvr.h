#pragma once

#include "windows.h"
#include <string>

using namespace std;

class CBaseSrv
{
public:
    explicit CBaseSrv(const wchar_t* szSrvName);
	~CBaseSrv(void);
	int Run(void);

	static void WINAPI ServiceStartCB(DWORD argc, LPTSTR *argv);
	static void WINAPI ServiceCtrlHandler(DWORD Opcode);

	virtual int  Init(void)  { return 1;}
	virtual void Start(void) = 0;
	virtual void Stop(void)  = 0;
	virtual void Pause(void) {;}
	virtual void Continue(void) {;}

private:
	static basic_string<wchar_t> s_strSrvName;
	static SERVICE_STATUS        s_stSrvStatus;
	static SERVICE_STATUS_HANDLE s_hSrvStatus;
	static CBaseSrv*             s_This;
};
