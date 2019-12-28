#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <thread>
#else
#include <pthread.h>
#endif // _WIN32

#include <stdint.h>
#include <string>
#include <atomic>


#define	DEFAULT_LOG_FILE_SIZE	1024*50						// 50MBytes
#define DEFAULT_LOG_FOLDER_SIZE DEFAULT_LOG_FILE_SIZE * 60	// 保存60个最大的文件

#ifndef __time64_t
#define __time64_t uint64_t
#endif  

namespace xy {

	class CLoger
	{
	public:
		class FileInfo {
		public:
			uint64_t size;
			std::string name;
			__time64_t create;

		public:
			FileInfo(uint64_t s, const char* pName, __time64_t c)
				: size(s), name(pName), create(c)
			{

			}

			FileInfo(const FileInfo& info)
				: size(info.size)
				, name(info.name)
				, create(info.create) {
			}

			FileInfo& operator = (const FileInfo& info) {
				this->name = info.name;
				this->size = info.size;
				this->create = info.create;
				return *this;
			}

			FileInfo() : size(0), name(""), create(0) {}
		};

	private:
		std::string m_strLogFodler;		// 日志路径
		uint32_t m_nMaxLogFileSize;		// 最大单个日志文件的长度，单位KB
		uint32_t m_nMaxFolderSize;		// 目录最大允许使用的大小，单位KB

		int m_fd;						// 当前日志文件的句柄
		std::string m_tag;				// 日志标志，标识日志对应的APP，
										// 如果没有设置APP标识，则采用时间作为文件名

		std::string m_strCurFileName;	// 当前的日志文件名
		std::atomic<uint64_t> m_changeStamp;		// 新文件切换时间戳
		std::atomic<bool> m_bSwitching;				// 当前文件句柄是否可切换

#ifdef _WIN32
		std::thread m_thdFolder;			// 文件夹大小检测线程
		std::thread m_thdMonitor;			// 监测线程
#else
		pthread_t m_thdIdFolder;
		pthread_t m_thdIdMonitor;
#endif

#ifdef _WIN32
		HANDLE m_evQuit;
		HANDLE m_evCheck;
#else
		int m_evQuit;
		int m_evCheck;
#endif // _WIN32

		bool m_bQuit;						// Quit flag

	protected:
		CLoger();
		CLoger(const CLoger& loger);
		CLoger& operator= (const CLoger& loger);

		uint32_t folderSize();
		void removeOldestFile();

		std::string getFileName();
		void writeLogerEvent(const char* pMessage);

#ifdef _WIN32
		static void monitorThread(int arg);
		static void folderCheckThread(int arg);
#else
		static void* monitorThread(void *);
		static void* folderCheckThread(void *);
#endif
		void threadChecking();
		void folderChecking();

	public:
		~CLoger();
		static CLoger& getLoger() {
			static CLoger loger;
			return loger;
		}

		/**
		* Start Loger
		*/
		int start();

		/**
		* 设置日志路径
		*
		* @param path 日志路径
		*/
		void setLogPath(const char* path);

		/**
		* 设置日志标识
		*
		* @param tag 日志标识
		*/
		void setLogTag(const char* tag);

		/**
		* 设置日志文件名，并打开文件
		*
		* @return 0: 成功，其他失败
		*/
		int openLogFile();

		void setFileMaxSize(uint32_t maxSize);

		void setFolderMaxSize(uint32_t maxSize);

		/**
		* 写日志
		*
		* @param level 日志级别
		* @param pFile 输出日志的文件
		* @param line 输出日志所在的行
		* @param func 输入出日志的函数
		* @param fmt 日志内容
		*/
		void log(uint32_t level, const char* pFile, uint32_t line, const char* func, const char* fmt...);
	};


#define SetMaxFolderSize(n) CLoger::getLoger().setFolderMaxSize(n)
#define SetMaxFileSize(n) CLoger::getLoger().setFileMaxSize(n)
#define StartLoger() CLoger::getLoger().start()
#define SetLogPath(path) CLoger::getLoger().setLogPath(path)
#define SetLogTag(tag) CLoger::getLoger().setLogTag(tag)
#define LOG(...) CLoger::getLoger().log(0, __FILE__, __LINE__, __func__, __VA_ARGS__)
}
