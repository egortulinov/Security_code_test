#include "hdlc.h"
#include <stdio.h>
#include <stdbool.h>


hdlc_tx_context_typedef master_tx_context   = {.internal_tx_buffer=USER_INFO_PACK};     // инициализация структуры для отправки ведущим
hdlc_rx_context_typedef slave_rx_context    = {0};                                      // инициализация структуры для приема ведомым
hdlc_tx_context_typedef slave_tx_context    = {0};                                      // инициализация структуры для отправки ведомым (отправка ответ)
hdlc_rx_context_typedef master_rx_context   = {0};                                      // инициализация структуры для приёма ведущим (получение ответа)

// функция для настройки контекста отправляемого сообщения
void HDLC_TxContextInit(hdlc_tx_context_typedef* tx_context, uint8_t destination_addr, uint8_t cmd) 
{
    uint8_t fcs_data[HDLC_INFO_SIZE+2];                 // данные для который считается FCS

    // Настройка контекста
    tx_context->tx_stage=TX_STAGE_FD_START;
    tx_context->info_index=0;
    tx_context->escape_next_byte=false;
    tx_context->tx_data.address=destination_addr;
    tx_context->tx_data.control=cmd;
    memcpy(tx_context->tx_data.information, tx_context->internal_tx_buffer, HDLC_INFO_SIZE);

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
    rx_context->frame_correct=false;                
    rx_context->buf_index=0;
    rx_context->escape_next_byte=false;
    rx_context->current_byte=0;
    rx_context->rx_data.address=0;
    rx_context->rx_data.control=0;
    rx_context->fcs_lsb=0;
    rx_context->fcs_msb=0;
    memset(rx_context->rx_data.information, 0, HDLC_INFO_SIZE);
    memset(rx_context->internal_rx_buffer, 0, HDLC_INFO_SIZE+1);
}

// расчет FCS для HDLC (доработанный) с https://github.com/jmswu/crc16
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

    crc = crc ^ 0xFFFF;                                                                 // финальное инвертирование для HDLC

    // возвращаем байты в правильном порядке для HDLC
    *fcs_msb = crc & 0xFF;
    *fcs_lsb = (crc >> 8) & 0xFF;
}

// функция проверки кадра на корректность
bool HDLC_FrameCorrect(hdlc_rx_context_typedef* rx_context, uint8_t expected_addr, const char* sender_name)
{
    // проверки на корректность формата сообщения
    if(rx_context->buf_index != (HDLC_INFO_SIZE+4))
    {
        printf("%s:\tWrong frame size: (%d bytes, expected %d)\n", sender_name, rx_context->buf_index, HDLC_INFO_SIZE+4);
        rx_context->frame_correct = false;
        return false;
    }
    if(!rx_context->frame_assembled)
    {
        printf("%s:\tFrame not assembled\n", sender_name);
        rx_context->frame_correct = false;
        return false;
    }
    if (rx_context->rx_data.address != expected_addr) 
    {
        printf("%s:\tInvalid destination address (received: 0x%02X, expected: 0x%02X)\n", sender_name, rx_context->rx_data.address, expected_addr);
        rx_context->frame_correct = false;
        return false;
    }
    if (rx_context->rx_data.control != CMD_INVERSING_BYTES && rx_context->rx_data.control != CMD_MIRRORING_BYTES) 
    {
        printf("%s:\tUnknown command: 0x%02X\n", sender_name, rx_context->rx_data.control);
        rx_context->frame_correct = false;
        return false;
    }

    // Сравнение полученной FCS с расчитанной
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
        rx_context->frame_correct = false;
        return false;
    }

    rx_context->frame_correct=true;
    return true;
}

// функция сохранения принятого сообщения во внутренний буффер
void HDLC_StoreRxData(hdlc_rx_context_typedef* rx_context)
{
    rx_context->internal_rx_buffer[0]=rx_context->rx_data.control;
    memcpy(&rx_context->internal_rx_buffer[1], rx_context->rx_data.information, HDLC_INFO_SIZE);
}

// функция отправки одно байта в FIFO
void HDLC_SendByte(hdlc_tx_context_typedef* tx_context, fifo_typedef* fifo)
{
    // обработка ESCAPE последовательности
    if (tx_context->escape_next_byte) 
    {
        if(FifoIsFull(fifo))    return;
        FifoWriteByte(fifo, tx_context->current_byte ^ 0x20);
        tx_context->escape_next_byte = false;
    
        // переход к следующему полю
        if(tx_context->tx_stage != TX_STAGE_INFORMATION) 
        {
            tx_context->tx_stage++;
        } 
        else if (tx_context->info_index >= HDLC_INFO_SIZE) 
        {
            tx_context->tx_stage=TX_STAGE_FCS_MSB;
        }
        return;
    }

    // определяем текущий байт
    switch(tx_context->tx_stage) 
    {
        case TX_STAGE_FD_START:     // флаг FD
            tx_context->current_byte = HDLC_FD_FLAG;
            break;

        case TX_STAGE_ADDRESS:     // адрес
            tx_context->current_byte = tx_context->tx_data.address;
            break;

        case TX_STAGE_CONTROL:     // управляющее поле
            tx_context->current_byte = tx_context->tx_data.control;
            break;

        case TX_STAGE_INFORMATION:     // информационное поле
            if(tx_context->info_index < HDLC_INFO_SIZE) 
            {
                tx_context->current_byte = tx_context->tx_data.information[tx_context->info_index];
                tx_context->info_index++;
            } 
            else 
            {
                tx_context->tx_stage=TX_STAGE_FCS_MSB;
                return;
            }
            break;

        case TX_STAGE_FCS_MSB:     // старший байт FCS
            tx_context->current_byte = tx_context->fcs_msb;
            break;

        case TX_STAGE_FCS_LSB:     // младший байт FCS 
            tx_context->current_byte = tx_context->fcs_lsb;
            break;

        case TX_STAGE_FD_END:     // флаг FD окончания пакета
            tx_context->current_byte = HDLC_FD_FLAG;
            break;

        default:
            return;
    }

    // проверка на необходимость байтстаффинга
    if((tx_context->current_byte == HDLC_FD_FLAG) || (tx_context->current_byte == HDLC_ESCAPE)) 
    {
        if(tx_context->tx_stage == TX_STAGE_FD_START || tx_context->tx_stage == TX_STAGE_FD_END)      // флаги начала и конца кадра не байтстаффятся
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
        
        // Переход к следующему полю
        if(tx_context->tx_stage == TX_STAGE_INFORMATION) 
        {
            if(tx_context->info_index >= HDLC_INFO_SIZE) 
            {
                tx_context->tx_stage = TX_STAGE_FCS_MSB;
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

// функция приёма одно байта из FIFO
void HDLC_ReceiveByte(hdlc_rx_context_typedef* rx_context, fifo_typedef* fifo, uint8_t expected_addr, const char* sender_name)
{
    // проверки корректности
    if(FifoIsEmpty(fifo))                               return;
    if(rx_context->frame_assembled)                     return;
    if(rx_context->buf_index >= HDLC_INFO_SIZE + 5)     return;

    FifoReadByte(fifo, &rx_context->current_byte);

    #ifdef RX_MORE_INFO
    printf("Received:\t%02X\n", rx_context->current_byte);
    #endif
    
    // обработка ESCAPE последовательности
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

                // проверка FCS
                if(HDLC_FrameCorrect(rx_context, expected_addr, sender_name))
                {
                    printf("%s:\tFrame validated successfully!\n", sender_name);
                }
                else
                {
                    printf("%s:\tFrame validation failed!\n", sender_name);
                    HDLC_RxContextInit(rx_context);
                }
            } 
            else
            {
                rx_context->fd_received = true;
                rx_context->buf_index = 0;
                rx_context->frame_correct=false;
                printf("%s:\tNew message detected! Start receiving...\n", sender_name);
                printf("%s:\tFD received - start of frame\n", sender_name);
            }
            return;
        }
    }

    // обработка данных (для всех кроме FD и ESC)
    if(rx_context->fd_received && !rx_context->escape_next_byte)
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
}

// функция выполнения принятой команды
void ProcessCommand(hdlc_rx_context_typedef* rx_context, hdlc_tx_context_typedef* tx_context)   
{
    switch (rx_context->internal_rx_buffer[0])
    {
        case CMD_INVERSING_BYTES:           // инверсия байтов
            printf("Slave:\tProcessing command 0x%02X: Inversing bytes\n", rx_context->internal_rx_buffer[0]);
            for(int i=0; i<HDLC_INFO_SIZE; i++)
            {
                tx_context->internal_tx_buffer[i]=~rx_context->internal_rx_buffer[i+1];
            }
            break;
        
        case CMD_MIRRORING_BYTES:           // отражение байтов
            printf("Slave:\tProcessing command 0x%02X: Mirroring bytes\n", rx_context->internal_rx_buffer[0]);
            for(int i=0; i<HDLC_INFO_SIZE; i++)
            {
                tx_context->internal_tx_buffer[i]=rx_context->internal_rx_buffer[HDLC_INFO_SIZE-i];
            }
            break;

        default:
            printf("Slave:\tUnknown command 0x%02X\n", rx_context->internal_rx_buffer[0]);
            memcpy(tx_context->internal_tx_buffer, &rx_context->internal_rx_buffer[1], HDLC_INFO_SIZE);
            break;
    }
}