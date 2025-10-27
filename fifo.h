#ifndef FIFO_H
#define FIFO_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define FIFO_SIZE       8           // размер FIFO


typedef struct                          // структура FIFO
{
    uint8_t buffer[FIFO_SIZE];          // буффер FIFO
    uint16_t write_index;                // индекс для записи в FIFO
    uint16_t read_index;                 // индекс для чтения из FIFO
} fifo_typedef;

// функция инициализации FIFO
static inline void FifoInit(fifo_typedef* fifo)   
{
    fifo->write_index=0;
    fifo->read_index=0;
    memset(fifo->buffer, 0, FIFO_SIZE);
}

// функция проверки FIFO на полноту
static inline bool FifoIsFull(fifo_typedef* fifo)      
{
    return (((fifo->write_index%FIFO_SIZE) == (fifo->read_index%FIFO_SIZE)) &&
            (fifo->write_index!=fifo->read_index));           // если полон, то возвращается 1
}

// проверка FIFO на отсутствие данных
static inline bool FifoIsEmpty(fifo_typedef* fifo)      
{
    return (fifo->write_index==fifo->read_index);               // если пуст, то возвращается 1
}

// функция записи байта в FIFO
static inline void FifoWriteByte(fifo_typedef* fifo, uint8_t data)    
{
    fifo->buffer[fifo->write_index%FIFO_SIZE]=data;               
    fifo->write_index++;
}

// функция чтения байта из FIFO в буффер приёмника
static inline void FifoReadByte(fifo_typedef* fifo, uint8_t* rx_data)     
{
    *rx_data=fifo->buffer[fifo->read_index%FIFO_SIZE];                    
    fifo->read_index++;                                      
}

// функция сброса индексов FIFO
static inline void FifoIndexReset(fifo_typedef* fifo)
{
    fifo->write_index = fifo->read_index;
}

// отладочная функция
static inline void DebugFifoState(fifo_typedef* fifo, const char* fifo_name)
{
    printf("%s FIFO: [", fifo_name);
    for(int i = 0; i < FIFO_SIZE; i++) {

        uint8_t read_pos=fifo->read_index%FIFO_SIZE;
        uint8_t write_pos=fifo->write_index%FIFO_SIZE;

        if(i == read_pos && i == write_pos) {
            printf(" RW:%02X", fifo->buffer[i]);  
        } else if(i == read_pos) {
            printf(" R:%02X", fifo->buffer[i]);   
        } else if(i == write_pos) {
            printf(" W:%02X", fifo->buffer[i]);   
        } else {
            printf(" %02X", fifo->buffer[i]);
        }
    }
    printf(" ]\n");
}

#endif