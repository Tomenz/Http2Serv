
#include ".\baseSvr.h"

basic_string<wchar_t>   CBaseSrv::s_strSrvName;
SERVICE_STATUS        CBaseSrv::s_stSrvStatus;
SERVICE_STATUS_HANDLE CBaseSrv::s_hSrvStatus;
CBaseSrv*             CBaseSrv::s_This;

CBaseSrv::CBaseSrv(const wchar_t* szSrvName)
{
	s_strSrvName = szSrvName;
	s_This       = this;
}

CBaseSrv::~CBaseSrv(void)
{
}

int CBaseSrv::Run(void)
{
	SERVICE_TABLE_ENTRY DispatchTable[2];
	DispatchTable[0].lpServiceName = (LPWSTR)s_strSrvName.c_str();
	DispatchTable[0].lpServiceProc = ServiceStartCB;
	DispatchTable[1].lpServiceName = 0;
	DispatchTable[1].lpServiceProc = 0;


	if (StartServiceCtrlDispatcher(DispatchTable) == FALSE)
	{
		return GetLastError();
	}

	return 0;
}

void WINAPI CBaseSrv::ServiceStartCB(DWORD argc, LPTSTR *argv)
{
	s_stSrvStatus.dwServiceType        = SERVICE_WIN32_OWN_PROCESS;
    s_stSrvStatus.dwCurrentState       = SERVICE_START_PENDING;
    s_stSrvStatus.dwControlsAccepted   = 0;
    s_stSrvStatus.dwWin32ExitCode      = NO_ERROR;
    s_stSrvStatus.dwServiceSpecificExitCode = 0;
    s_stSrvStatus.dwCheckPoint         = 0;
    s_stSrvStatus.dwWaitHint           = 0;

	s_hSrvStatus = RegisterServiceCtrlHandler(argv[0], ServiceCtrlHandler);
    if (s_hSrvStatus == static_cast<SERVICE_STATUS_HANDLE>(0))
        return;

	// The Installation of the User-Programm is called
	// Should only take a few seconds
	if (s_This->Init() != 0)
	{
		// Initialization complete - report running status.
		s_stSrvStatus.dwCurrentState       = SERVICE_RUNNING;
		s_stSrvStatus.dwControlsAccepted   = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_PAUSE_CONTINUE;

		if (!SetServiceStatus (s_hSrvStatus, &s_stSrvStatus))
		{
//			SvcDebugOut(" [MY_SERVICE] SetServiceStatus error %ld\n",GetLastError());
		}

		// Run the service, until it comes back, when it is finished
		s_This->Start();
	}

    s_stSrvStatus.dwCurrentState       = SERVICE_STOPPED;

    if (!SetServiceStatus (s_hSrvStatus, &s_stSrvStatus))
    {
	}
}

void WINAPI CBaseSrv::ServiceCtrlHandler(DWORD Opcode)
{
	switch(Opcode)
	{
	case SERVICE_CONTROL_PAUSE:
	// Do whatever it takes to pause here.
		s_This->Pause();
		s_stSrvStatus.dwCurrentState = SERVICE_PAUSED;
        break;

	case SERVICE_CONTROL_CONTINUE:
	// Do whatever it takes to continue here.
		s_This->Continue();
		s_stSrvStatus.dwCurrentState = SERVICE_RUNNING;
        break;

	case SERVICE_CONTROL_STOP:
	case SERVICE_CONTROL_SHUTDOWN:
	// Do whatever it takes to stop here.
		s_This->Stop();
		s_stSrvStatus.dwCurrentState  = SERVICE_STOP_PENDING;
        s_stSrvStatus.dwWaitHint = 5000;
        break;

	case SERVICE_CONTROL_INTERROGATE:
	// Fall through to send current status.
		break;

	default:
		;
//		SvcDebugOut(" [MY_SERVICE] Unrecognized opcode %ld\n", Opcode);
	}
    s_stSrvStatus.dwCheckPoint++;

	// Send current status.
	if (!SetServiceStatus (s_hSrvStatus,  &s_stSrvStatus))
	{
//		SvcDebugOut(" [MY_SERVICE] SetServiceStatus error %ld\n", GetLastError());
	}

	return;
}
