#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include <time.h>

typedef struct
{
    uint32_t start_time;            // время на момент запуска таймаута
    uint32_t timeout_duration;      // длительность таймаута
}timeout_typedef;

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

// функция установки таймаута
static inline void SetTimeout(timeout_typedef* timeout, uint32_t duration)
{
    timeout->start_time=GetCurrentTimeMs();
    timeout->timeout_duration=duration;
}

// функция проверки таймаута (возвращает 1 - таймаут истек, 0 - не истек)
static inline bool CheckTimeoutPassed(timeout_typedef* timeout)
{
    uint32_t time_passed;                           // времени прощло
    uint32_t current_time=GetCurrentTimeMs();       // текущее время
    
    if(current_time<timeout->start_time)    // если было переполнение
        time_passed = UINT32_MAX - timeout->start_time + current_time + 1;
    else                                    // если переполнения не было
        time_passed=current_time-timeout->start_time;

    return(time_passed>=timeout->timeout_duration);
}
#endif