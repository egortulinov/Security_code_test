#include "hdlc.h"
#include <stdio.h>

uint8_t internal_master_tx_buffer[HDLC_INFO_SIZE]=USER_INFO_PACK;       // внутренняя память ведущего на отправку (содержит информационное поле)
uint8_t internal_slave_rx_buffer[HDLC_INFO_SIZE+1];                     // внутренняя память ведомого на приём (содержит команду и информационное поле)
uint8_t internal_slave_tx_buffer[HDLC_INFO_SIZE];                       // внутренняя память ведомого на отправку (содержит информационное поле)
uint8_t internal_master_rx_buffer[HDLC_INFO_SIZE+1];                    // внутренняя память ведущего на приём (содержит команду и информационное поле)

hdlc_tx_context_typedef master_tx_context   = {0};      // инициализация структуры для отправки ведущим
hdlc_rx_context_typedef slave_rx_context    = {0};      // инициализация структуры для приема ведомым
hdlc_tx_context_typedef slave_tx_context    = {0};      // инициализация структуры для отправки ведомым (отправка ответ)
hdlc_rx_context_typedef master_rx_context   = {0};      // инициализация структуры для приёма ведущим (получение ответа)

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
    #ifdef TX_MORE_INFO 
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
    else 
    {
        // проверяем на FD
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
            return;
        }
    }

    // обработка данных (для всех кроме FD и ESC)
    if(rx_context->fd_received)
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

    #ifdef RX_MORE_INFO
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