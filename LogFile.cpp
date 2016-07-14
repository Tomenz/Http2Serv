
#include <sstream>

#include "LogFile.h"

map<wstring, CLogFile>CLogFile::s_lstLogFiles;
thread_local stringstream CLogFile::m_ssMsg;
