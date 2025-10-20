
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

void HDLC_CalculateFCS(uint8_t *data, int length, uint8_t *fcs_msb, uint8_t *fcs_lsb);

typedef struct                          // структура fifo
{
    uint8_t buffer[FIFO_SIZE];          // буффер fifo
    uint8_t byte_counter;               // счетчик байт в буфере
    uint8_t write_index;                // индекс для записи в fifo
    uint8_t read_index;                 // индекс для чтения из fifo
} fifo_typedef;

typedef enum
{
    MASTER_PREPARE_STATE,
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
    bool frame_assembled;               // флаг собранного сообщения
    bool frame_verified;                // флаг пройденной проверки
    hdlc_packet_typedef rx_data;        // полезная часть данных (без FD и FCS)
    uint8_t buf_index;                  // индекс для записи в буффер rx_data
    uint8_t current_byte;               // текущий прочитанный байт
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

fsm_state_master_typedef master_state = MASTER_PREPARE_STATE;     // инициализация мастера в отправку
fsm_state_slave_typedef slave_state = SLAVE_WAITING_CMD_STATE;    // инициализация слейва в ожидание флага

uint8_t internal_master_tx_buffer[HDLC_INFO_SIZE]={0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
uint8_t internal_slave_rx_buffer[HDLC_INFO_SIZE+1];
uint8_t internal_slave_tx_buffer[HDLC_INFO_SIZE];
uint8_t internal_master_rx_buffer[HDLC_INFO_SIZE+1];


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

// функция для настройки контекста отправляемого сообщения
void HDLC_TxContextInit(hdlc_tx_context_typedef* tx_context, uint8_t destination_addr, uint8_t cmd, const uint8_t* internal_info_buffer) 
{
    uint8_t fcs_data[HDLC_INFO_SIZE+2];                 // данные для который считается fcs

    // Настройка контекста
    tx_context->tx_stage=0;
    tx_context->info_index=0;
    tx_context->escape_next_byte=false;
    tx_context->tx_data.address=destination_addr;
    tx_context->tx_data.control=cmd;
    memcpy(tx_context->tx_data.information, internal_info_buffer, HDLC_INFO_SIZE);

    // вычисление FCS
    fcs_data[0]=tx_context->tx_data.address;
    fcs_data[1]=tx_context->tx_data.control;
    memcpy(&fcs_data[2], tx_context->tx_data.information, HDLC_INFO_SIZE);
    HDLC_CalculateFCS(fcs_data, HDLC_INFO_SIZE+2, &tx_context->fcs_msb, &tx_context->fcs_lsb);
}

// функция настройки контекста для принимаемого сообщения
void HDLC_RxContextInit(hdlc_rx_context_typedef* rx_context)    
{
    rx_context->fd_received=false;
    rx_context->frame_assembled=false;
    rx_context->frame_verified=false;                
    rx_context->buf_index=0;
    rx_context->escape_next_byte=false;
    rx_context->rx_data.address=0;
    rx_context->rx_data.control=0;
    memset(rx_context->rx_data.information, 0, HDLC_INFO_SIZE);
}

// функция отправки одно байта в fifo
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

// функция приёма одно байта из fifo
void HDLC_ReceiveByte(hdlc_rx_context_typedef* rx_context, fifo_typedef* fifo)
{
    if(FifoIsEmpty(fifo))                               return;
    if(rx_context->frame_assembled)                     return;
    if(rx_context->buf_index >= HDLC_INFO_SIZE + 4)     return;

    FifoReadByte(fifo, &rx_context->current_byte);

    if(rx_context->escape_next_byte)
    {
        rx_context->current_byte ^= 0x20;
        rx_context->escape_next_byte=false;

        if(rx_context->fd_received)         
            rx_context->rx_data.information[rx_context->buf_index++]=rx_context->current_byte;
    }
    else if (rx_context->current_byte==HDLC_ESCAPE)         // если эскейп последовательность
    {
        rx_context->escape_next_byte=true;
        return;
    }
    else if (rx_context->current_byte==HDLC_FD_FLAG)        // если флаг FD
    {
        if(rx_context->fd_received)
        {
            // конец кадра
            rx_context->frame_assembled=true;
        }
        else
        {
            // начало кадра
            rx_context->fd_received=true;
            rx_context->buf_index=0;
        }
        rx_context->escape_next_byte=false;
        return;
    }
    else
    {
        if(rx_context->fd_received)
        {
            if(rx_context->buf_index==0)                        rx_context->rx_data.address=rx_context->current_byte;
            else if(rx_context->buf_index==1)                   rx_context->rx_data.control=rx_context->current_byte;
            else if(rx_context->buf_index<HDLC_INFO_SIZE+2)     rx_context->rx_data.information[rx_context->buf_index-2]=rx_context->current_byte;
            else if(rx_context->buf_index==HDLC_INFO_SIZE+2)    rx_context->fcs_msb=rx_context->current_byte;
            else if(rx_context->buf_index==HDLC_INFO_SIZE+3)    rx_context->fcs_lsb=rx_context->current_byte;

            rx_context->buf_index++;
        }
    }
}

void HDLC_VerifyFrame(hdlc_rx_context_typedef* rx_context, uint8_t expected_addr, uint8_t* internal_buffer, char* sender_name)
{
    printf("%s: Verifying frame\n", sender_name);

    // проверки на корректность формата сообщения
    if(rx_context->buf_index != (HDLC_INFO_SIZE+4))
    {
        printf("%s: Wrong frame size: (%d bytes, expected %d)\n", sender_name, rx_context->buf_index, HDLC_INFO_SIZE+4);
        return;
    }
    if(!rx_context->frame_assembled)
    {
        printf("%s: Frame not assembled\n", sender_name);
        return;
    }
    if (rx_context->rx_data.address != expected_addr) {
        printf("%s: ERROR - Invalid destination address (received: 0x%02X, expected: 0x%02X)\n", sender_name, rx_context->rx_data.address, expected_addr);
        return;
    }
    if (rx_context->rx_data.control != CMD_INVERSING_BYTES && rx_context->rx_data.control != CMD_MIRRORING_BYTES) {
        printf("%s: ERROR - Unknown command: 0x%02X\n", sender_name, rx_context->rx_data.control);
        return;
    }

    // Сравнение полученной fcs с расчитанной
    uint16_t received_fcs=(rx_context->fcs_msb<<8)|(rx_context->fcs_lsb);
    uint8_t fcs_data[HDLC_INFO_SIZE+2];
    uint8_t calculated_fcs_msb, calculated_fcs_lsb;
    fcs_data[0]=rx_context->rx_data.address;
    fcs_data[1]=rx_context->rx_data.control;
    memcpy(&fcs_data[2], rx_context->rx_data.information, HDLC_INFO_SIZE);

    HDLC_CalculateFCS(fcs_data, 2 + HDLC_INFO_SIZE, &calculated_fcs_msb, &calculated_fcs_lsb);
    uint16_t calculated_fcs = (calculated_fcs_msb << 8) | calculated_fcs_lsb;

    if (received_fcs != calculated_fcs) 
    {
        printf("%s: Invalid FCS (received: 0x%04X, calculated: 0x%04X)\n", sender_name, received_fcs, calculated_fcs);
        return;
    }

    // сохранение во внутренний буффер
    internal_buffer[0] = rx_context->rx_data.control;
    memcpy(&internal_buffer[1], rx_context->rx_data.information, HDLC_INFO_SIZE);
    rx_context->frame_verified=true;
}

void FSM_MASTER(void)
{
    static bool frame_sent=false;
    static uint8_t command= CMD_INVERSING_BYTES;
    switch(master_state)
    {
        case MASTER_PREPARE_STATE:
            printf("Master: Preparing message with command: 0x%02X\n", command);
            HDLC_TxContextInit(&master_tx_context, HDLC_SLAVE_ADDR, command, internal_master_tx_buffer);
            frame_sent=false;
            master_state=MASTER_TX_STATE;
            break;

        case MASTER_TX_STATE:
            if(!frame_sent)
            {
                if(!FifoIsFull(&fifo_mts))
                {
                    HDLC_SendByte(&master_tx_context, &fifo_mts);

                    if(master_tx_context.tx_stage==7)
                    {
                        frame_sent=true;
                        printf("Master: Frame sent\n");
                    }
                }
            }
            else
            {
                master_state=MASTER_WAITING_REPLY_STATE;
                printf("Master: Waiting for reply\n");
            }
            break;

        case MASTER_WAITING_REPLY_STATE:
            master_state=MASTER_RX_STATE;
            break;

        case MASTER_RX_STATE:
            master_state=MASTER_PROCESSING_STATE;
            break;

        case MASTER_PROCESSING_STATE:
            master_state=MASTER_PREPARE_STATE;
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
    int cnt=20;
    FifoInit(&fifo_mts);
    FifoInit(&fifo_stm);
    while(cnt)
    {
        FSM_MASTER();
        cnt--;
    }
    
    return 0; 
}