#include <iostream>
#include <time.h>
#include <string>
#include <thread>
#include <stdio.h>
#include "loging.h"

void testThread(int n) {
    for (size_t i = 0; i < 111120; i++)
    {
        xy::LOG("log test:%d, idx:%u", std::this_thread::get_id(), i);
        //std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

int main()
{
    xy::SetLogTag("logtest");
    xy::SetLogPath("/home/xy/temp/test");
    xy::SetMaxFileSize(20000); // 1k
    xy::SetMaxFolderSize(70000);
    xy::StartLoger();

    time_t tstart = time(NULL);
    std::thread threadpools[100];
    for (size_t i = 0; i < 100; i++)
    {
        threadpools[i] = std::thread(testThread, i);
    }

    for (auto& t : threadpools) {
        t.join();
    }
    printf("elapsed time:%I64u", time(NULL) - tstart);
}