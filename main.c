
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "fifo.h"

/*-----------------------------------------------------USER DATA-----------------------------------------------------------------------------------------*/
#define USER_INFO_PACK          {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F}    // информационное поле
#define HDLC_INFO_SIZE          16                      // размер информационного поля HDLC
#define USER_COMMAND            0x02                    // выбор команды 0x01 (INVERSING_BYTES) or 0x02 (CMD_MIRRORING_BYTES)
//#define MORE_INFO                                       // позволяет получить больше отладочной информации (побайтово отображает отправку и приём каждого байта в fifo)
/*---------------------------------------------------USER DATA END----------------------------------------------------------------------------------------*/

#define HDLC_MASTER_ADDR        0x01                    // адресс ведущего HDLC
#define HDLC_SLAVE_ADDR         0x02                    // адрес ведомого HDLC

#define HDLC_FD_FLAG            0x7E                    // флаг протокола HDLC
#define HDLC_ESCAPE             0x7D                    // эскейп последовательность байтстаффинга HDLC


void HDLC_CalculateFCS(uint8_t *data, int length, uint8_t *fcs_msb, uint8_t *fcs_lsb);

typedef enum                        // перечисление состояний ведущего устройства
{
    MASTER_PREPARE_STATE,           // подготовка данных для передачи
    MASTER_TX_STATE,                // состояние отправки данных в fifo
    MASTER_WAITING_REPLY_STATE,     // ожидание флага FD ответа
    MASTER_RX_STATE,                // состояние приёма из fifo
    MASTER_PROCESSING_STATE         // проверка и обработка принятого ответа
} fsm_state_master_typedef;

typedef enum                        // перечисление состояний ведомого устройства
{
    SLAVE_WAITING_CMD_STATE,        // состояние ожидания флага для приёма
    SLAVE_RX_STATE,                 // состояние приёма из fifo 
    SLAVE_PROCESSING_STATE,         // проверка, обработка принятых данных и выполнение команды
    SLAVE_TX_STATE                  // состояние отправки ответа в fifo
} fsm_state_slave_typedef;

typedef enum                            // перечисление команд HDLC
{
    CMD_INVERSING_BYTES = 0x01,         // команда инверсии байт
    CMD_MIRRORING_BYTES = 0x02          // команда отражения байт (байт 1 на место n, байт n на место байта 1 и т.д.)
} hdlc_command_typedef;

typedef struct                              // структура полезных данных пакета HDLC (нет флагов FD и FCS)
{
    uint8_t address;                        // адрес HDLC
    uint8_t control;                        // управляющее поле HDLC
    uint8_t information[HDLC_INFO_SIZE];    // информационное поле HDLC
} hdlc_packet_typedef;

typedef struct                          // структура для промежуточных данных передачи кадра
{
    uint8_t tx_stage;                   // текущая стадия передачи данных (0 - передача флааг FD, 1 - адрес, 2 - управление, 3 - информация, 4,5 - fcs, 6 - FD)
    uint8_t current_byte;               // номер байта, который мы отправляем
    hdlc_packet_typedef tx_data;        // сами данные (кроме флагов FD и FCS)
    uint8_t info_index;                 // индекс для передачи данных информационного поля
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


uint8_t internal_master_tx_buffer[HDLC_INFO_SIZE]=USER_INFO_PACK;       // внутренняя память ведущего на отправку (содержит информационное поле)
uint8_t internal_slave_rx_buffer[HDLC_INFO_SIZE+1];                     // внутренняя память ведомого на приём (содержит команду и информационное поле)
uint8_t internal_slave_tx_buffer[HDLC_INFO_SIZE];                       // внутренняя память ведомого на отправку (содержит информационное поле)
uint8_t internal_master_rx_buffer[HDLC_INFO_SIZE+1];                    // внутренняя память ведущего на приём (содержит команду и информационное поле)

fifo_typedef fifo_mts = {0};      // fifo Master To Slave
fifo_typedef fifo_stm = {0};      // fifo Slave To Master

hdlc_tx_context_typedef master_tx_context   = {0};      // инициализация структуры для отправки ведущим
hdlc_rx_context_typedef slave_rx_context    = {0};      // инициализация структуры для приема ведомым
hdlc_tx_context_typedef slave_tx_context    = {0};      // инициализация структуры для отправки ведомым (отправка ответ)
hdlc_rx_context_typedef master_rx_context   = {0};      // инициализация структуры для приёма ведущим (получение ответа)

fsm_state_master_typedef master_state = MASTER_PREPARE_STATE;     // инициализация мастера в отправку
fsm_state_slave_typedef slave_state = SLAVE_WAITING_CMD_STATE;    // инициализация слейва в ожидание флага


// функция выполнения принятой команды
void ProcessCommand(uint8_t command, const uint8_t* input_data, uint8_t* output_data)   
{
    switch (command)
    {
        case CMD_INVERSING_BYTES:           // инверсия байтов
            printf("Slave:\tProcessing command 0x%02X: Inversing bytes\n", command);
            for(int i=0; i<HDLC_INFO_SIZE; i++)
            {
                output_data[i]=~input_data[i];
            }
            break;
        
        case CMD_MIRRORING_BYTES:           // отражение байтов
            printf("Slave:\tProcessing command 0x%02X: Mirroring bytes\n", command);
            for(int i=0; i<HDLC_INFO_SIZE; i++)
            {
                output_data[i]=input_data[HDLC_INFO_SIZE-1-i];
            }
            break;

        default:
            printf("Slave:\tUnknown command 0x%02X\n", command);
            memcpy(output_data, input_data, HDLC_INFO_SIZE);
            break;
    }
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
    rx_context->current_byte=0;
    rx_context->rx_data.address=0;
    rx_context->rx_data.control=0;
    rx_context->fcs_lsb=0;
    rx_context->fcs_msb=0;
    memset(rx_context->rx_data.information, 0, HDLC_INFO_SIZE);
}

// расчет fcs для HDLC (доработанный) с https://github.com/jmswu/crc16
void HDLC_CalculateFCS(uint8_t *data, int length, uint8_t *fcs_msb, uint8_t *fcs_lsb)  
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

// функция отправки одно байта в fifo
void HDLC_SendByte(hdlc_tx_context_typedef* tx_context, fifo_typedef* fifo)
{
    // обработка эскейп последовательности
    if (tx_context->escape_next_byte) 
    {
        if(FifoIsFull(fifo))    return;
        FifoWriteByte(fifo, tx_context->current_byte ^ 0x20);
        tx_context->escape_next_byte = false;
    
        // переход к следующему полю
        if(tx_context->tx_stage != 3) 
        {
            tx_context->tx_stage++;
        } 
        else if (tx_context->info_index >= HDLC_INFO_SIZE) 
        {
            tx_context->tx_stage=4;
        }
        return;
    }

    // определяем текущий байт
    switch(tx_context->tx_stage) 
    {
        case 0:     // флаг FD
            tx_context->current_byte = HDLC_FD_FLAG;
            break;

        case 1:     // адрес
            tx_context->current_byte = tx_context->tx_data.address;
            break;

        case 2:     // управляющее поле
            tx_context->current_byte = tx_context->tx_data.control;
            break;

        case 3:     // информационное поле
            if(tx_context->info_index < HDLC_INFO_SIZE) 
            {
                tx_context->current_byte = tx_context->tx_data.information[tx_context->info_index];
                tx_context->info_index++;
            } 
            else 
            {
                tx_context->tx_stage=4;
                return;
            }
            break;

        case 4:     // старший байт fcs
            tx_context->current_byte = tx_context->fcs_msb;
            break;

        case 5:     // младший байт fcs 
            tx_context->current_byte = tx_context->fcs_lsb;
            break;

        case 6:     // флаг FD окончания пакета
            tx_context->current_byte = HDLC_FD_FLAG;
            break;

        default:
            return;
    }

    // проверка на необходимость байтстаффинга
    if((tx_context->current_byte == HDLC_FD_FLAG) || (tx_context->current_byte == HDLC_ESCAPE)) 
    {
        if(tx_context->tx_stage == 0 || tx_context->tx_stage == 6)      // флаги начала и конца кадра не байтстаффятся
        {
            if(FifoIsFull(fifo)) return;
            FifoWriteByte(fifo, tx_context->current_byte);             
            
            tx_context->tx_stage++;
        } 
        else 
        {
            if(FifoIsFull(fifo)) return;
            // применяем байтстаффинг
            FifoWriteByte(fifo, HDLC_ESCAPE);
            tx_context->escape_next_byte = true;
        }
    }
    else 
    {
        // Обычный байт без байтстаффинга
        if(FifoIsFull(fifo)) return;
        FifoWriteByte(fifo, tx_context->current_byte);
        
        // Переход к следующему stage
        if(tx_context->tx_stage == 3) 
        {
            if(tx_context->info_index >= HDLC_INFO_SIZE) 
            {
                tx_context->tx_stage = 4;
            }
        } 
        else 
        {
            tx_context->tx_stage++;
        }
    }
    #ifdef MORE_INFO
    printf("Transmitted:\t%02X\n", tx_context->current_byte);
    #endif
}

// функция приёма одно байта из fifo
void HDLC_ReceiveByte(hdlc_rx_context_typedef* rx_context, fifo_typedef* fifo, char* sender_name)
{
    // проверки корректности
    if(FifoIsEmpty(fifo))                               return;
    if(rx_context->frame_assembled)                     return;
    if(rx_context->buf_index >= HDLC_INFO_SIZE + 5)     return;

    FifoReadByte(fifo, &rx_context->current_byte);

    // обработка эскейп последовательности
    if(rx_context->escape_next_byte) 
    {
        rx_context->current_byte ^= 0x20;
        rx_context->escape_next_byte = false;
    } 
    else if(rx_context->current_byte == HDLC_ESCAPE)
    {
        rx_context->escape_next_byte = true;
        return;
    }

    // обработка флага FD и основных данных
    if (rx_context->current_byte == HDLC_FD_FLAG) 
    {
        if(rx_context->fd_received) 
        {
            rx_context->frame_assembled = true;
            printf("%s:\tFD received - end of frame\n", sender_name);
        } 
        else
        {
            rx_context->fd_received = true;
            rx_context->buf_index = 0;
            printf("%s:\tFD received - start of frame\n", sender_name);
        }
    }
    else if(rx_context->fd_received)
    {
        if(rx_context->buf_index == 0)
        {                          
            rx_context->rx_data.address = rx_context->current_byte;
            printf("%s:\tAddress received\n", sender_name);
        }
        else if(rx_context->buf_index == 1)                     
        {
            rx_context->rx_data.control = rx_context->current_byte;
            printf("%s:\tCommand received\n", sender_name);
        }
        else if(rx_context->buf_index < HDLC_INFO_SIZE + 2)
        {     
            rx_context->rx_data.information[rx_context->buf_index - 2] = rx_context->current_byte;

            if (rx_context->buf_index == HDLC_INFO_SIZE + 1)
                printf("%s:\tInformation received\n", sender_name);

        }
        else if(rx_context->buf_index == HDLC_INFO_SIZE + 2)
        {
            rx_context->fcs_msb = rx_context->current_byte;
            printf("%s:\tFCS MSB received\n", sender_name);
        }
        else if(rx_context->buf_index == HDLC_INFO_SIZE + 3) 
        {
            rx_context->fcs_lsb = rx_context->current_byte;
            printf("%s:\tFCS LSB received\n", sender_name); 
        }
        rx_context->buf_index++;
    }

    #ifdef MORE_INFO
    printf("Received:\t%02X\n", rx_context->current_byte);
    #endif
}

// функция проверки корректности кадра
void HDLC_VerifyFrame(hdlc_rx_context_typedef* rx_context, uint8_t expected_addr, uint8_t* internal_buffer, char* sender_name)
{
    printf("%s:\tVerifying frame...\n", sender_name);

    // проверки на корректность формата сообщения
    if(rx_context->buf_index != (HDLC_INFO_SIZE+4))
    {
        printf("%s:\tWrong frame size: (%d bytes, expected %d)\n", sender_name, rx_context->buf_index, HDLC_INFO_SIZE+4);
        return;
    }
    if(!rx_context->frame_assembled)
    {
        printf("%s:\tFrame not assembled\n", sender_name);
        return;
    }
    if (rx_context->rx_data.address != expected_addr) {
        printf("%s:\tInvalid destination address (received: 0x%02X, expected: 0x%02X)\n", sender_name, rx_context->rx_data.address, expected_addr);
        return;
    }
    if (rx_context->rx_data.control != CMD_INVERSING_BYTES && rx_context->rx_data.control != CMD_MIRRORING_BYTES) {
        printf("%s:\tUnknown command: 0x%02X\n", sender_name, rx_context->rx_data.control);
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
        printf("%s:\tInvalid FCS (received: 0x%04X, calculated: 0x%04X)\n", sender_name, received_fcs, calculated_fcs);
        return;
    }

    // сохранение во внутренний буффер
    internal_buffer[0] = rx_context->rx_data.control;
    memcpy(&internal_buffer[1], rx_context->rx_data.information, HDLC_INFO_SIZE);
    rx_context->frame_verified=true;
}

// конечный автомат ведущего
void FSM_Master(void)
{
    static bool frame_sent=false;                   // флаг отправленного сообщения
    static uint32_t timeout=0;                      // таймаут в случае отстутствия ответа

    switch(master_state)
    {
        case MASTER_PREPARE_STATE:

            // Подготовка к началу общения
            printf("----------------------------------------------------------\n");
            printf("Master:\tPreparing message with command: 0x%02X to unit: 0x%02X \n", USER_COMMAND, HDLC_SLAVE_ADDR);

            HDLC_TxContextInit(&master_tx_context, HDLC_SLAVE_ADDR, USER_COMMAND, internal_master_tx_buffer);
            HDLC_RxContextInit(&master_rx_context);

            frame_sent=false;
            printf("Master:\tStart transmitting...\n");
            master_state=MASTER_TX_STATE;
            break;

        case MASTER_TX_STATE:

            // Отправка всего сообщения
            if(!frame_sent)
            {
                if(!FifoIsFull(&fifo_mts))
                {
                    HDLC_SendByte(&master_tx_context, &fifo_mts);
                    if(master_tx_context.tx_stage==7)
                    {
                        frame_sent=true;
                        printf("Master:\tFrame sent completely!\n");
                    }
                }
                else
                {
                    printf("Master:\tFIFO is full, waiting...\n");
                }
            }
            else
            {
                // Отладочная информация
                printf("Master:\tTransmitted information:\t");
                for(int i=0; i<HDLC_INFO_SIZE; i++)
                {
                    printf("%02X ", internal_master_tx_buffer[i]);
                }
                printf("\n");

                master_state=MASTER_WAITING_REPLY_STATE;
                printf("Master:\tWaiting for reply from unit 0x%02X...\n", master_tx_context.tx_data.address);
                timeout=0;
            }
            break;

        case MASTER_WAITING_REPLY_STATE:

            // Ожидаем флаг начала передачи от ведомого
            if (!FifoIsEmpty(&fifo_stm))
                HDLC_ReceiveByte(&master_rx_context, &fifo_stm, "Master");
            else
                printf("Master:\tFIFO is empty, waiting...\n");

            if(master_rx_context.fd_received && !master_rx_context.frame_assembled)
            {
                printf("Master:\tStart receiving frame...\n");
                master_state=MASTER_RX_STATE;
            }

            // Проверка на преувеличение времени ответа
            timeout++;
            if(timeout>1000000)
            {
                printf("Master:\tNo response received! Sending again...\n");
                HDLC_RxContextInit(&master_rx_context);
                master_state=MASTER_PREPARE_STATE;
            }
            break;

        case MASTER_RX_STATE:
            
            // Приём ответа от ведомого 
            if (!FifoIsEmpty(&fifo_stm)) 
                HDLC_ReceiveByte(&master_rx_context, &fifo_stm, "Master");
            else
                printf("Master:\tFIFO is empty, waiting...\n");

            if(master_rx_context.frame_assembled)
                master_state=MASTER_PROCESSING_STATE;

            break;

        case MASTER_PROCESSING_STATE:

            // Проверка и обработка принятого ответа
            if(!master_rx_context.frame_verified)
            {
                HDLC_VerifyFrame(&master_rx_context, HDLC_MASTER_ADDR, internal_master_rx_buffer, "Master");

                if(!master_rx_context.frame_verified)
                {
                    printf("Master:\tReply verification failed! Sending again...\n");
                    HDLC_RxContextInit(&master_rx_context);
                    master_state=MASTER_PREPARE_STATE;
                    break;
                }
                else
                {
                    printf("Master:\tReply verified successfully!\n");
                }
            }

            // Отладочный вывод
            printf("Master:\tReceived infromation:\t\t");
            for(int i=1; i<HDLC_INFO_SIZE+1; i++)
            {
                printf("%02X ", internal_master_rx_buffer[i]);
            }
            printf("\n");

            master_state=MASTER_PREPARE_STATE;
            break;

        default:
            master_state=MASTER_PREPARE_STATE;
            break;
    }
}

// конечный автомат ведомого
void FSM_Slave(void)
{
    static bool processing_complete=false;      // флаг завершения обработки принятого сообщения
    static bool reply_sent=false;               // флаг отправленного ответа

    switch(slave_state)
    {
        case SLAVE_WAITING_CMD_STATE:

            // Ожидаем флаг начала передачи от ведущего
            if (!FifoIsEmpty(&fifo_mts))
                HDLC_ReceiveByte(&slave_rx_context, &fifo_mts, "Slave");
            else
                printf("Slave:\tFIFO is empty, waiting...\n");

            if(slave_rx_context.fd_received && !slave_rx_context.frame_assembled)
            {
                printf("Slave:\tStart receiving frame...\n");
                slave_state=SLAVE_RX_STATE;
            }
            break;

        case SLAVE_RX_STATE:

            // Приём сообщения от ведущего
            if (!FifoIsEmpty(&fifo_mts))
                HDLC_ReceiveByte(&slave_rx_context, &fifo_mts, "Slave");
            else
                printf("Slave:\tFIFO is empty, waiting...\n");

            if(slave_rx_context.frame_assembled) 
            {
                printf("Slave:\tFrame received completely!\n");
                slave_state = SLAVE_PROCESSING_STATE;
            }
            break;

        case SLAVE_PROCESSING_STATE:

            // Проверка и обработка принятого сообщения
            if(!slave_rx_context.frame_verified)
            {
                HDLC_VerifyFrame(&slave_rx_context, HDLC_SLAVE_ADDR, internal_slave_rx_buffer, "Slave");

                if(!slave_rx_context.frame_verified)
                {
                    printf("Slave:\tFrame verification failed!!!\n");
                    printf("Slave:\tWaiting for message again\n");
                    HDLC_RxContextInit(&slave_rx_context);
                    slave_state = SLAVE_WAITING_CMD_STATE;
                    break;
                }
                else
                {
                    printf("Slave:\tFrame verified successfully!\n");
                }
            }

                // Отладочная информация
                printf("Slave:\tReceived information:\t\t");
                for(int i=1; i<HDLC_INFO_SIZE+1; i++)
                {
                    printf("%02X ", internal_slave_rx_buffer[i]);
                }
            printf("\n");

            ProcessCommand(internal_slave_rx_buffer[0], &internal_slave_rx_buffer[1], internal_slave_tx_buffer);
    
            processing_complete = true;
            reply_sent = false;
            slave_state = SLAVE_TX_STATE;
            break;

        case SLAVE_TX_STATE:

            // Отправка ответа ведущему
            if(processing_complete && !reply_sent)
            {
                HDLC_TxContextInit(&slave_tx_context, HDLC_MASTER_ADDR, internal_slave_rx_buffer[0], internal_slave_tx_buffer);
                printf("Slave:\tPreparing reply to master...\n");
                reply_sent=true;
            }

            if(!FifoIsFull(&fifo_stm))
            {
                HDLC_SendByte(&slave_tx_context, &fifo_stm);

                if(slave_tx_context.tx_stage==7)
                {
                    printf("Slave:\tReply sent completely!\n");

                    HDLC_RxContextInit(&slave_rx_context);
                    processing_complete=false;
                    reply_sent=false;

                    slave_state=SLAVE_WAITING_CMD_STATE;

                    // Отладочная информация
                    printf("Slave:\tTransmitted information:\t");
                    for(int i=0; i<HDLC_INFO_SIZE; i++)
                    {
                        printf("%02X ", internal_slave_tx_buffer[i]);
                    }
                    printf("\n");
                    printf("Slave:\tWaiting for next message...\n");
                }
            }
            else
            {
                printf("Slave:\tFIFO is full, waiting...\n");
            }
            break;

        default:
            HDLC_RxContextInit(&slave_rx_context);
            slave_state=SLAVE_WAITING_CMD_STATE;
            break;
    }
}

int main()
{
    FifoInit(&fifo_mts);
    FifoInit(&fifo_stm);

    printf("Master<-->Slave simulation starting...\n");

    while(1)
    {
        FSM_Master();
        FSM_Slave();
    }
    
    return 0; 
}
