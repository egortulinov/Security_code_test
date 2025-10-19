
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>


#define FIFO_SIZE               8                   // размер fifo
#define HDLC_INFO_SIZE          16                  // размер информационного поля HDLC
#define HDLC_FD_FLAG            0x7E                // флаг протокола HDLC
#define HDLC_ESCAPE             0x7D                // эскейп последовательность байтстаффинга HDLC
#define HDLC_MASTER_ADDR        0x01                // адресс ведущего HDLC
#define HDLC_SLAVE_ADDR         0x02                // адрес ведомого HDLC


typedef struct                          // структура fifo
{
    uint8_t buffer[FIFO_SIZE];          // буффер fifo
    uint8_t byte_counter;               // счетчик байт в буфере
    uint8_t write_index;                // индекс для записи в fifo
    uint8_t read_index;                 // индекс для чтения из fifo
} fifo_typedef;

typedef enum
{
    MASTER_TX_STATE,
    MASTER_WAITING_REPLY_STATE,
    MASTER_RX_STATE,
    MASTER_PROCESSING_STATE
} fsm_state_master_typedef;

typedef enum
{
    SLAVE_WAITING_CMD_STATE,
    SLAVE_RX_STATE,
    SLAVE_PROCESSING_STATE,
    SLAVE_TX_STATE
} fsm_state_slave_typedef;

typedef enum                            // перечисление команд HDLC
{
    CMD_INVERSING_BYTES = 0x01,         // команда инверсии байт
    CMD_MIRRORING_BYTES = 0x02          // команда отражения байт (байт 1 на место n, байт n на место байта 1 и т.д.)
} hdlc_command_typedef;

typedef struct                              // структура данных пакета HDLC (нет флагов FD и FCS)
{
    uint8_t address;                        // адрес HDLC
    uint8_t control;                        // управляющее поле HDLC
    uint8_t information[HDLC_INFO_SIZE];    // информационное поле HDLC
} hdlc_packet_typedef;

typedef struct                          // структура для промежуточных данных передачи кадра
{
    uint8_t tx_stage;                   // текущая стадия передачи данных (0 - передача флааг FD, 1 - адрес, 2 - управление, 3 - информация, 4 - fcs, 5 - FD)
    uint8_t current_byte;               // номер байта, который мы отправляем
    hdlc_packet_typedef tx_data;        // сами данные (кроме флагов FD и FCS)
    uint8_t info_index;                 // индекс для передачи данных информационного поля (поскольку не влазит в fifo)
    uint8_t fcs_msb;                    // контрольная сумма старший байт
    uint8_t fcs_lsb;                    // контрольная сумма младший байт
    bool escape_next_byte;              // флаг байтстаффинга 
} hdlc_tx_context_typedef;

typedef struct                          // структура для промежуточных данных приёма кадра 
{
    bool fd_received;                   // флаг принятого флага fd
    uint8_t buffer[32];                 // буффер для приёма данных (сюда складывается всё подряд из fifo)
    uint8_t buf_index;                  // индекс для записи в буффер приёмника
    hdlc_packet_typedef rx_data;        // полезная часть данных (кроме флагов FD и FCS) (к ним будет применена обработка)
    uint8_t fcs_msb;                    // контрольная сумма старший байт
    uint8_t fcs_lsb;                    // контрольная сумма младший байт
    bool escape_next_byte;              // флаг байтстаффинга
} hdlc_rx_context_typedef;


fifo_typedef fifo_mts;      // fifo Master To Slave
fifo_typedef fifo_stm;      // fifo Slave To Master

hdlc_tx_context_typedef master_tx_context   = {0};      // инициализация структуры для отправки ведущим
hdlc_rx_context_typedef slave_rx_context    = {0};      // инициализация структуры для приема ведомым
hdlc_tx_context_typedef slave_tx_context    = {0};      // инициализация структуры для отправки ведомым (отправка ответ)
hdlc_rx_context_typedef master_rx_context   = {0};     // инициализация структуры для приёма ведущим (получение ответа)

fsm_state_master_typedef master_state = MASTER_TX_STATE;          // инициализация мастера в отправку
fsm_state_slave_typedef slave_state = SLAVE_WAITING_CMD_STATE;    // инициализация слейва в ожидание флага

uint8_t information[16]={0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

void FifoInit(fifo_typedef* fifo)   // функция инициализации fifo
{
    fifo->byte_counter=0;
    fifo->write_index=0;
    fifo->read_index=0;
}

bool FifoIsEmpty(fifo_typedef* fifo)      // проверка fifo на отсутствие данных
{
    return (fifo->byte_counter==0);          // если пуст, то возвращается 1
}

bool FifoIsFull(fifo_typedef* fifo)      // функция проверки fifo на полноту
{
    return (fifo->byte_counter==FIFO_SIZE);  // если полон, то возвращается 1
}

void FifoWriteByte(fifo_typedef* fifo, uint8_t data)    // функция записи байта в fifo
{
    fifo->buffer[fifo->write_index]=data;               // запись данных по индексу для записи
    fifo->write_index=(fifo->write_index+1)%FIFO_SIZE;  // увеличиваем индекс записи на 1 (остатком от деления избегаем превышения размера fifo)
    fifo->byte_counter++;                               // увеличиваем число байт в fifo
}

void FifoReadByte(fifo_typedef* fifo, uint8_t* rx_data)     // функция чтения байта из fifo в буффер приёмника
{
    *rx_data=fifo->buffer[fifo->read_index];                    // далее аналогия с FifoWriteByte()
    fifo->read_index=(fifo->read_index+1)%FIFO_SIZE;
    fifo->byte_counter--;                                       // после прочтения "освобождаем" байт
}

void HDLC_SendByte(hdlc_tx_context_typedef* tx_context, fifo_typedef* fifo)
{
    static bool escape_second_byte_sent=false;                  // флаг отправленной второй части эскейп последовательности

    if (tx_context->escape_next_byte)                           // если поднят флаг байтстаффинга
    {
        if(escape_second_byte_sent)                             // если оправлен второй байт эскейп последовательности
        {
            // снимаем флаги
            tx_context->escape_next_byte=false;                
            escape_second_byte_sent=false;                      

            if(tx_context->tx_stage !=3)                        // если сейчас записывается не информационное поле
            {
                tx_context->tx_stage++;                         // следующее поле
            }
            else if (tx_context->info_index >= HDLC_INFO_SIZE)  // или если индекс информационного поля больше всего размера поля
            {
                tx_context->tx_stage++;
            }

            return;
        }

        if(FifoIsFull(fifo))    return;                         // если буфер полон, выходим
        FifoWriteByte(fifo, tx_context->current_byte^0x20);     // записываем второй байт последовательности
        escape_second_byte_sent = true;                         // поднимаем флаг того, что отправили второй байт
        return;
    }

    // определяем текущий байт
    switch(tx_context->tx_stage)
    {
        case 0:     // флаг FD
            tx_context->current_byte=HDLC_FD_FLAG;
            break;

        case 1:     // адрес
            tx_context->current_byte=tx_context->tx_data.address;
            break;

        case 2:     // управляющее поле
            tx_context->current_byte=tx_context->tx_data.control;
            break;

        case 3:     // информационное поле
            if(tx_context->info_index < HDLC_INFO_SIZE)                                             // если не конец поля
            {
                tx_context->current_byte=tx_context->tx_data.information[tx_context->info_index];   // записываем в текущий байт
                tx_context->info_index++;                                                           // увеличиываем индекс
            }
            else
            {
                tx_context->tx_stage++;                                                             // следующее поле
                return;
            }
            break;

        case 4:     // старший байт fcs
            tx_context->current_byte=tx_context->fcs_msb;
            break;

        case 5:     // младший байт msb 
            tx_context->current_byte=tx_context->fcs_lsb;
            break;

        case 6:     // флаг FD окончания пакета
            tx_context->current_byte=HDLC_FD_FLAG;
            break;

        default:
            break;
    }

    if((tx_context->current_byte == HDLC_FD_FLAG) || (tx_context->current_byte == HDLC_ESCAPE))     // проверка на необходимость байтстаффинга
    {
        if(FifoIsFull(fifo))    return;     // если занят, то выходим
        FifoWriteByte(fifo, HDLC_ESCAPE);   // иначе записываем первый байт эскейп последовательности

        tx_context->escape_next_byte=true;  // подняли флаг
        escape_second_byte_sent=false;      // флаг отправленного второго байта опускаем
    }
    else
    {
        if(FifoIsFull(fifo))    return;
        FifoWriteByte(fifo, tx_context->current_byte);  // записываем текущий байт

        if(tx_context->tx_stage != 3)                   // если не информационное поле то
        {   
            tx_context->tx_stage++;                     // переходим к следующему полю
        }
    }
}

void FSM_MASTER(void)
{
    static bool frame_sent=false;
    switch(master_state)
    {
        case MASTER_TX_STATE:
            if(!frame_sent)
            {
                if(!FifoIsFull(&fifo_mts))
                {
                    
                }
            }
            else    master_state = MASTER_WAITING_REPLY_STATE;
        break;

        case MASTER_WAITING_REPLY_STATE:
        break;

        case MASTER_RX_STATE:
        break;

        case MASTER_PROCESSING_STATE:
        break;

        default:
        break;
    }
}


void HDLC_CalculateFCS(uint8_t *data, int length, uint8_t *fcs_msb, uint8_t *fcs_lsb)  // расчет fcs для HDLC (доработанный) с https://github.com/jmswu/crc16
{
    uint16_t crc = 0xFFFF;                                                              // начальное значение HDLC
    uint8_t i;
    int count = length;

    while (count-- > 0)
    {
        crc = crc ^ (((uint16_t)(*data)) << 8);
        data++;
        
        for (i = 0; i < 8; i++)
        {
            if (crc & 0x8000)
            {
                crc = (crc << 1) ^ 0x1021;                                              // полином CRC-CCITT
            }
            else
            {
                crc = crc << 1;
            }
        }
    }

    crc = crc ^ 0xFFFF;                                                                 // Финальное инвертирование для HDLC

    // Возвращаем байты в правильном порядке для HDLC
    *fcs_msb = crc & 0xFF;
    *fcs_lsb = (crc >> 8) & 0xFF;
}


int main()
{
    FifoInit(&fifo_mts);
    FifoInit(&fifo_stm);

    return 0; 
}