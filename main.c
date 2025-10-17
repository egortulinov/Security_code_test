
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>


#define FIFO_SIZE   8

typedef struct                          // структура fifo
{
    uint8_t buffer[FIFO_SIZE];    // буффер fifo
    uint8_t byte_counter;                   // счетчик байт в буфере
    uint8_t write_index;                    // индекс для записи в fifo
    uint8_t read_index;                     // индекс для чтения из fifo
} fifo_typedef;

void FifoInit(fifo_typedef* fifo)           // функция инициализации fifo
{
    fifo->byte_counter=0;
    fifo->write_index=0;
    fifo->read_index=0;
}

bool FifoCheckEmpty(fifo_typedef* fifo)      // проверка fifo на отсутствие данных
{
    return (fifo->byte_counter==0);          // если пуст, то возвращается 1
}

bool FifoCheckFull(fifo_typedef* fifo)      // функция проверки fifo на полноту
{
    return (fifo->byte_counter==FIFO_SIZE);  // если полон, то возвращается 1
}

bool FifoWriteByte(fifo_typedef* fifo, uint8_t data)    // функция записи байта в fifo
{
    if(FifoCheckFull(fifo))   return 0;                 // если буфер полон, вернется 0 с выходом

    fifo->buffer[fifo->write_index]=data;               // запись данных по индексу для записи
    fifo->write_index=(fifo->write_index+1)%FIFO_SIZE;  // увеличиваем индекс записи на 1 (остаткой от деления избегаем превышения размера fifo)
    fifo->byte_counter++;                               // увеличиваем число байт в fifo (отстаток от деления не нужен, так как есть проверка переполнения)
    return 1;
}

bool FifoReadByte(fifo_typedef* fifo, uint8_t* rx_data)     // функция чтения байта из fifo в буффер приёмника
{
    if(FifoCheckEmpty(fifo))    return 0;                       // если fifo пуст, вернется 0 с выходом

    *rx_data=fifo->buffer[fifo->read_index];                    // далее аналогия с FifoWriteByte()
    fifo->read_index=(fifo->read_index+1)%FIFO_SIZE;
    fifo->byte_counter--;                                       // после прочтения "освобождаем" байт
    return 1;
}


int main()
{


    return 0; 
}