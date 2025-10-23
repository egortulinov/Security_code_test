#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include <time.h>

// реализация данных функций была взята с просторов интернета
#ifdef WINDOWS
#include <windows.h>
static inline uint32_t GetCurrentTimeMs(void)
{
    return GetTickCount();
}
#endif

#ifdef LINUX
static inline uint32_t GetCurrentTimeMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}
#endif

// Макрос для удобства создания дедлайнов
#define SET_TIMEOUT(ms) (GetCurrentTimeMs() + (ms))

// Макрос для проверки таймаута
#define CHECK_TIMEOUT(deadline) (GetCurrentTimeMs() > (deadline))

#endif