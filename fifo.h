#ifndef FIFO_H
#define FIFO_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define FIFO_SIZE       8           // размер fifo


typedef struct                          // структура fifo
{
    uint8_t buffer[FIFO_SIZE];          // буффер fifo
    uint8_t byte_counter;               // счетчик байт в буфере
    uint8_t write_index;                // индекс для записи в fifo
    uint8_t read_index;                 // индекс для чтения из fifo
} fifo_typedef;

// функция инициализации fifo
static inline void FifoInit(fifo_typedef* fifo)   
{
    fifo->byte_counter=0;
    fifo->write_index=0;
    fifo->read_index=0;
    memset(fifo->buffer, 0, FIFO_SIZE);
}

// функция проверки fifo на полноту
static inline bool FifoIsFull(fifo_typedef* fifo)      
{
    return (fifo->byte_counter==FIFO_SIZE);  // если полон, то возвращается 1
}

// проверка fifo на отсутствие данных
static inline bool FifoIsEmpty(fifo_typedef* fifo)      
{
    return (fifo->byte_counter==0);          // если пуст, то возвращается 1
}

// функция записи байта в fifo
static inline void FifoWriteByte(fifo_typedef* fifo, uint8_t data)    
{
    fifo->buffer[fifo->write_index]=data;               
    fifo->write_index=(fifo->write_index+1)%FIFO_SIZE;
    fifo->byte_counter++;                               
}

// функция чтения байта из fifo в буффер приёмника
static inline void FifoReadByte(fifo_typedef* fifo, uint8_t* rx_data)     
{
    *rx_data=fifo->buffer[fifo->read_index];                    
    fifo->read_index=(fifo->read_index+1)%FIFO_SIZE;
    fifo->byte_counter--;                                       
}

static inline void DebugFifoState(fifo_typedef* fifo, const char* fifo_name)
{
    printf("%s FIFO: [", fifo_name);
    for(int i = 0; i < FIFO_SIZE; i++) {
        if(i == fifo->read_index && i == fifo->write_index) {
            printf(" RW:%02X", fifo->buffer[i]);  // Чтение и запись на одной позиции
        } else if(i == fifo->read_index) {
            printf(" R:%02X", fifo->buffer[i]);   // Позиция чтения
        } else if(i == fifo->write_index) {
            printf(" W:%02X", fifo->buffer[i]);   // Позиция записи
        } else {
            printf(" %02X", fifo->buffer[i]);     // Обычный байт
        }
    }
    printf(" ]\n");
}

extern fifo_typedef fifo_mts;
extern fifo_typedef fifo_stm;

#endif