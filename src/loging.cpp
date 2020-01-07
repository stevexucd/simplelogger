#ifdef _WIN32
#include <Windows.h>
#include <direct.h>
#include <io.h>
#include <share.h>
#pragma comment(lib, "Advapi32.lib")
#else // Linux
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/time.h>
#include <dirent.h>
#include <stdarg.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#endif // _WIN32

#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <map>

#include "loging.h"

namespace xy
{

static const char *LOGNAME = "XLoger";
static const char *LogLevels[] = {
	"FATAL",
	"ERROR",
	"UERR",
	"WARN",
	"INFO",
	"DEBUG",
	"TRACE",
	"ALL",
};

CLoger::CLoger()
	: m_strLogFodler("log"), m_nMaxLogFileSize(DEFAULT_LOG_FILE_SIZE), m_nMaxFolderSize(DEFAULT_LOG_FOLDER_SIZE), m_fd(-1), m_tag(""), m_strCurFileName(""), m_changeStamp(0), m_bSwitching(false)
#ifdef _WIN32
	  ,
	  m_evQuit(INVALID_HANDLE_VALUE), m_evCheck(INVALID_HANDLE_VALUE)
#else
	  ,
	  m_thdIdFolder(0), m_thdIdMonitor(0), m_evQuit(-1), m_evCheck(-1)
#endif
	  ,
	  m_bQuit(false)
{
}
static int index = 0;
std::string CLoger::getFileName()
{
	std::string strFileName;
	char szTimeFileExt[30];

	memset(szTimeFileExt, 0, sizeof(szTimeFileExt));
	time_t t = time(NULL);

#ifdef _WIN32
	struct tm pt;
	errno_t err = localtime_s(&pt, &t);
	if (0 != err)
	{
		_snprintf_s(szTimeFileExt, sizeof(szTimeFileExt), ".%d%02d%02d%02d%02d%02d", 1970, 1, 1, 0, 0, 0);
	}
	else
	{
		strftime(szTimeFileExt, sizeof(szTimeFileExt), ".%Y%m%d%H%M%I", &pt);
	}
	strFileName = m_strLogFodler + "\\";
#else
	struct tm *pt = localtime(&t);
	strftime(szTimeFileExt, sizeof(szTimeFileExt), ".%Y%m%d%H%M%I", pt);
	strFileName = m_strLogFodler + "/";
#endif // _WIN32

	if (m_tag.size() > 0)
	{
		// 使用tag来作为日志文件名
		strFileName += m_tag;
	}
	else
	{
		uint32_t processId;
#ifdef _WIN32
		processId = GetCurrentProcessId();
		strFileName += std::to_string(processId);
#else
		processId = getpid();
		char szprocess[30];
		snprintf(szprocess, sizeof(szprocess), "%u", processId);
		strFileName += szprocess;
#endif // _WIN32
	}
	strFileName += szTimeFileExt;
#ifdef _WIN32
	strFileName += std::to_string(index++);
#else
	char szIdx[30];
	snprintf(szIdx, sizeof(szIdx), "%d", index++);
	strFileName += szIdx;
#endif
	return strFileName;
}

void CLoger::writeLogerEvent(const char *pMessage)
{

#ifdef _WIN32
	HANDLE evLog = RegisterEventSourceA(NULL, LOGNAME);
	ReportEventA(evLog, EVENTLOG_SUCCESS, 0, 0, NULL, 1, 0, &pMessage, NULL);
#else
	syslog(LOG_ERR, pMessage);
#endif // _WIN32
}

#ifdef _WIN32
void CLoger::monitorThread(int arg)
#else
void *CLoger::monitorThread(void *arg)
#endif
{
	CLoger *pThis = reinterpret_cast<CLoger *>(arg);
	if (NULL == pThis)
	{
#ifdef _WIN32
		return;
#else
		return 0;
#endif // _WIN32
	}
	pThis->threadChecking();
#ifdef _WIN32
	return;
#else
	return 0;
#endif // _WIN32
}

#ifdef _WIN32
void CLoger::folderCheckThread(int arg)
#else
void *CLoger::folderCheckThread(void *arg)
#endif
{
	CLoger *pThis = reinterpret_cast<CLoger *>(arg);
	if (NULL == pThis)
	{
#ifdef _WIN32
		return;
#else
		return 0;
#endif // _WIN32
	}
	pThis->folderChecking();
#ifdef _WIN32
	return;
#else
	return 0;
#endif // _WIN32
}

void CLoger::threadChecking()
{
	while (!m_bQuit)
	{
#ifdef _WIN32
		if (!m_thdFolder.joinable())
		{
			// 线程没运行
			m_thdFolder = std::thread(folderCheckThread, (int)this);
		}

		DWORD ret = WaitForSingleObject(m_evQuit, 10000); // 10 seconds
		if (WAIT_OBJECT_0 == ret)
		{
			break;
		}
#else
		if (0 == m_thdIdFolder)
		{
			// 线程没运行
			if (0 != pthread_create(&m_thdIdFolder, NULL, folderCheckThread, this))
			{
				writeLogerEvent("Failed to create folder check thread");
			}
		}
		else
		{
			int ret = pthread_kill(m_thdIdFolder, 0);
			if (ESRCH == ret || EINVAL == ret)
			{
				// 线程没运行
				if (0 != pthread_create(&m_thdIdFolder, NULL, folderCheckThread, this))
				{
					writeLogerEvent("Failed to create folder check thread");
				}
			}
		}

		fd_set rfds;
		struct timeval tv;

		tv.tv_sec = 1;
		tv.tv_usec = 0;

		FD_ZERO(&rfds);
		FD_SET(m_evQuit, &rfds);
		int ret = select(m_evQuit + 1, &rfds, NULL, NULL, &tv);
		if (-1 == ret)
		{
			// error occured
			break;
		}
		else if (0 == ret)
		{
			// timeout
			continue;
		}
		else
		{
			if (FD_ISSET(m_evQuit, &rfds))
			{
				break;
			}
		}

#endif // _WIN32
	}
}

void CLoger::folderChecking()
{
	while (!m_bQuit)
	{
		// remove the oldest file
		removeOldestFile();

#ifdef _WIN32
		HANDLE szEvs[2];
		szEvs[0] = m_evQuit;
		szEvs[1] = m_evCheck;
		DWORD ret = WaitForMultipleObjects(2, szEvs, FALSE, 3600000); // one hour
		if (WAIT_OBJECT_0 == ret)
		{
			// quit
			break;
		}
#else
		fd_set rfds;
		struct timeval tv;

		tv.tv_sec = 1;
		tv.tv_usec = 0;

		FD_ZERO(&rfds);
		FD_SET(m_evQuit, &rfds);
		int ret = select(m_evQuit + 1, &rfds, NULL, NULL, &tv);
		if (-1 == ret)
		{
			// error occured
			break;
		}
		else if (0 == ret)
		{
			// timeout
			continue;
		}
		else
		{
			if (FD_ISSET(m_evQuit, &rfds))
			{
				break;
			}
		}

#endif // _WIN32
	}
}

CLoger::~CLoger()
{
#ifdef _WIN32
	SetEvent(m_evQuit);
	m_bQuit = true;
	m_thdFolder.join();
	m_thdMonitor.join();
	CloseHandle(m_evQuit);
	CloseHandle(m_evCheck);
#else
	eventfd_write(m_evQuit, 0); // Notify thread, quit
	m_bQuit = true;
	pthread_join(m_thdIdMonitor, NULL);
	pthread_join(m_thdIdFolder, NULL);
	close(m_evQuit);
	close(m_evCheck);
	closelog();
#endif

	if (m_fd > 0)
	{
#ifdef _WIN32
		_close(m_fd);
#else
		close(m_fd);
#endif // _WIN32
	}
}

int CLoger::start()
{
#ifdef _WIN32
	m_evQuit = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (INVALID_HANDLE_VALUE == m_evQuit)
	{
		return -1;
	}
	m_evCheck = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (INVALID_HANDLE_VALUE == m_evCheck)
	{
		CloseHandle(m_evQuit);
		return -1;
	}
	m_thdMonitor = std::thread(monitorThread, (int)this);
#else
	openlog(LOGNAME, LOG_NDELAY | LOG_PID, LOG_USER);
	m_evCheck = eventfd(0, EFD_NONBLOCK);
	if (-1 == m_evCheck)
	{
		return -1;
	}

	m_evQuit = eventfd(0, EFD_NONBLOCK);
	if (-1 == m_evQuit)
	{
		close(m_evCheck);
		return -1;
	}

	if (0 != pthread_create(&m_thdIdMonitor, NULL, monitorThread, this))
	{
		return -1;
	}
#endif // _WIN32

	return openLogFile();
}

void CLoger::setLogPath(const char *path)
{
#ifdef _WIN32
	if (0 != _access(path, 0))
	{
		if (0 == _mkdir(path))
		{
			// 创建目录成功
		}
	}
#else
	if (0 != access(path, 0))
	{
		if (0 == mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH))
		{
			// 创建目录成功
		}
		else{
			perror("create log folder failed");
		}
	}
#endif // _WIN32

	m_strLogFodler = path;
}

void CLoger::setLogTag(const char *tag)
{
	if (tag)
	{
		m_tag = tag;
	}
}

uint32_t CLoger::folderSize()
{
	std::string logSearch = m_strLogFodler + "\\*.*";
	uint32_t fileSize = 0;
#ifdef _WIN32
	int k;
	long HANDLE;

	_finddata_t file;
	k = HANDLE = _findfirst(logSearch.c_str(), &file);
	while (k != -1)
	{
		k = _findnext(HANDLE, &file);
		if (0 == strcmp(file.name, ".."))
		{
			continue;
		}
		fileSize += file.size;
	}
	_findclose(HANDLE);
#else
#endif
	return (fileSize / 1024);
}

void CLoger::removeOldestFile()
{
	std::string logSearch = m_strLogFodler + "\\*.*";
	std::map<__time64_t, FileInfo> files;
	uint64_t filesSize = 0;

#ifdef _WIN32
	int k;
	long HANDLE;

	_finddata_t file;
	k = HANDLE = _findfirst(logSearch.c_str(), &file);

	while (k != -1)
	{
		k = _findnext(HANDLE, &file);
		if (0 == strcmp(file.name, ".."))
		{
			continue;
		}
		files[file.time_create] = FileInfo(file.size, file.name, file.time_create);
		filesSize += file.size;
	}
	_findclose(HANDLE);
#else
	DIR *dir;
	struct dirent *ptr;
	std::string folderPath = m_strLogFodler + "/";
	dir = opendir(m_strLogFodler.c_str());
	while ((ptr = readdir(dir)) != NULL)
	{
		struct stat s;
		std::string path = folderPath + ptr->d_name;
		lstat(path.c_str(), &s);
		if (S_ISDIR(s.st_mode))
		{
			continue;
		}
		files[s.st_ctim.tv_sec] = FileInfo(s.st_size, ptr->d_name, s.st_ctim.tv_sec);
		filesSize += s.st_size;
	}
	closedir(dir);
#endif
	uint64_t limitSize = (uint64_t)m_nMaxFolderSize * 1024;

	while (filesSize > limitSize)
	{
		filesSize = 0;
#ifdef _WIN32
		std::string filePath = m_strLogFodler + "\\";
#else
		std::string filePath = m_strLogFodler + "/";
#endif
		filePath += files.begin()->second.name;
		int ret = remove(filePath.c_str());
		if (0 != ret)
		{
			writeLogerEvent(filePath.c_str());
			writeLogerEvent("remove file failed");
			break;
		}

		files.erase(files.begin());
		for (auto i = files.begin(); i != files.end(); i++)
		{
			filesSize += i->second.size;
		}
	}
}

int CLoger::openLogFile()
{
	int fd = -1;
	char szErr[200];

	time_t now = time(NULL);

	uint64_t old = m_changeStamp.exchange(now);
	if (now == old)
	{
		// 已经有其他线程在执行这个函数了
		//writeLogerEvent("try to create file, acquire control failed");
		return 0;
	}
	//printf("get control:%u, id:%u\n", old, std::this_thread::get_id());

	std::string newFileName = getFileName();
	if (newFileName == m_strCurFileName)
	{
		return 0;
	}
#ifdef _WIN32
	errno_t err = _sopen_s(&fd, newFileName.c_str(), _O_APPEND | _O_CREAT | _O_WRONLY, _SH_DENYRD, _S_IWRITE);
	if (0 != err)
	{
		// 打开文件出错
		_snprintf_s(szErr, sizeof(szErr), "open file:%s failed, error:%d", getFileName().c_str(), err);
		writeLogerEvent(szErr);
		return -1;
	}
#else
	fd = open(newFileName.c_str(), O_APPEND | O_CREAT | O_WRONLY | O_CLOEXEC, DEFFILEMODE);
	if (-1 == fd)
	{
		// 打开文件出错
		snprintf(szErr, sizeof(szErr), "open file:%s failed, error:%d", newFileName.c_str(), errno);
		writeLogerEvent(szErr);
		perror(szErr);
		return -1;
	}
#endif // _WIN32

	m_strCurFileName = newFileName;
	if (-1 == m_fd)
	{
		m_fd = fd;
	}
	else
	{
		m_bSwitching.store(true);
#ifdef _WIN32
		int ret = _dup2(fd, m_fd);
#else
		int ret = dup2(fd, m_fd);
#endif
		m_bSwitching.store(false);
		if (0 != ret)
		{
#ifdef _WIN32
			_close(fd);
			_snprintf_s(szErr, sizeof(szErr), "_dup2 error:%d", ret);
#else
			close(fd);
			snprintf(szErr, sizeof(szErr), "_dup2 error:%d", ret);
#endif // _WIN32
			fd = -1;
			writeLogerEvent(szErr);
			return -1;
		}
#ifdef _WIN32
		_close(fd);
		SetEvent(m_evCheck);
#else
		close(fd);
#endif // _WIN32
	}

	return 0;
}

void CLoger::setFileMaxSize(uint32_t maxSize)
{
	m_nMaxLogFileSize = maxSize;
}

void CLoger::setFolderMaxSize(uint32_t maxSize)
{
	m_nMaxFolderSize = maxSize;
}

void CLoger::log(uint32_t level, const char *pFile, uint32_t line, const char *func, const char *fmt...)
{
	char szLog[4096];

	if (-1 == m_fd)
	{
		if (0 != openLogFile())
		{
			writeLogerEvent("CLoger::log: Open log file failed");
			return;
		}
	}
	long fSize = 0;
#ifdef _WIN32
	fSize = _tell(m_fd);
#else
	fSize = lseek(m_fd, 0, SEEK_CUR);
#endif
	if (((uint32_t)fSize / 1024) > m_nMaxLogFileSize)
	{
		// 打开新的文件
		openLogFile();
	}

	int usedSize = 0;
	char szTimeStr[50];
#ifdef _WIN32
	SYSTEMTIME localSysTime;
	GetLocalTime(&localSysTime);
	_snprintf_s(szTimeStr, sizeof(szTimeStr), "%d-%02d-%02d %02d:%02d:%02d.%03d",
				localSysTime.wYear,
				localSysTime.wMonth,
				localSysTime.wDay,
				localSysTime.wHour,
				localSysTime.wMinute,
				localSysTime.wSecond,
				localSysTime.wMilliseconds);
	std::string strFileName = pFile;
	size_t pos = strFileName.rfind('\\', strFileName.size());
	if (std::string::npos != pos)
	{
		strFileName = strFileName.substr(pos + 1, strFileName.size());
	}

	usedSize = _snprintf_s(szLog, sizeof(szLog), "%s %s::%d::%s: ", szTimeStr, strFileName.c_str(), line, func);
#else
	std::string strFileName = pFile;
	size_t pos = strFileName.rfind('/', strFileName.size());
	if (std::string::npos != pos)
	{
		strFileName = strFileName.substr(pos + 1, strFileName.size());
	}

	struct timeval tv;
    struct timezone tz;
    gettimeofday(&tv,&tz);

    struct tm* ptm;
    char time_string[40];
    long milliseconds;
    ptm = localtime (&(tv.tv_sec));
    milliseconds = tv.tv_usec / 1000;
    strftime (time_string, sizeof(time_string), "%Y-%m-%d %H:%M:%S", ptm);
    snprintf(szTimeStr, sizeof(szTimeStr), "%s.%03ld", time_string, milliseconds);

	usedSize = snprintf(szLog, sizeof(szLog), "%s %s::%d::%s: ", szTimeStr, strFileName.c_str(), line, func);
#endif // _WIN32

	char *p = &szLog[usedSize];
	va_list args;
	va_start(args, fmt);
	usedSize += vsnprintf(p, sizeof(szLog) - usedSize, fmt, args);
	p = &szLog[usedSize];
	va_end(args);
	if (usedSize < sizeof(szLog))
	{
		szLog[usedSize++] = '\n';
	}

	while (m_bSwitching);
#ifdef _WIN32
	int err = _write(m_fd, szLog, usedSize);
#else
	ssize_t err = write(m_fd, szLog, usedSize);
#endif // _WIN32
	if (usedSize != err)
	{
		writeLogerEvent("write file failed");
	}
}
} // namespace xy