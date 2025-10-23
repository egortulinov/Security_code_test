#ifndef HDLC_H
#define HDLC_H

#include "fifo.h"
#include "user.h"

#define HDLC_MASTER_ADDR        0x01                    // адресс ведущего HDLC
#define HDLC_SLAVE_ADDR         0x02                    // адрес ведомого HDLC
#define HDLC_FD_FLAG            0x7E                    // флаг протокола HDLC
#define HDLC_ESCAPE             0x7D                    // эскейп последовательность байтстаффинга HDLC


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

extern uint8_t internal_master_tx_buffer[];             // внутренняя память ведущего на отправку (содержит информационное поле)
extern uint8_t internal_slave_rx_buffer[];              // внутренняя память ведомого на приём (содержит команду и информационное поле)
extern uint8_t internal_slave_tx_buffer[];              // внутренняя память ведомого на отправку (содержит информационное поле)
extern uint8_t internal_master_rx_buffer[];             // внутренняя память ведущего на приём (содержит команду и информационное поле)

extern hdlc_tx_context_typedef master_tx_context;      // инициализация структуры для отправки ведущим
extern hdlc_rx_context_typedef slave_rx_context;      // инициализация структуры для приема ведомым
extern hdlc_tx_context_typedef slave_tx_context;      // инициализация структуры для отправки ведомым (отправка ответ)
extern hdlc_rx_context_typedef master_rx_context;      // инициализация структуры для приёма ведущим (получение ответа)

// функция для настройки контекста отправляемого сообщения
void HDLC_TxContextInit(hdlc_tx_context_typedef* tx_context, uint8_t destination_addr, uint8_t cmd, const uint8_t* internal_info_buffer);

// функция настройки контекста для принимаемого сообщения
void HDLC_RxContextInit(hdlc_rx_context_typedef* rx_context);    

// расчет fcs для HDLC (доработанный) с https://github.com/jmswu/crc16
void HDLC_CalculateFCS(uint8_t *data, int length, uint8_t *fcs_msb, uint8_t *fcs_lsb);  

// функция отправки одно байта в fifo
void HDLC_SendByte(hdlc_tx_context_typedef* tx_context, fifo_typedef* fifo);

// функция приёма одно байта из fifo
void HDLC_ReceiveByte(hdlc_rx_context_typedef* rx_context, fifo_typedef* fifo, char* sender_name);

// функция проверки корректности кадра
void HDLC_VerifyFrame(hdlc_rx_context_typedef* rx_context, uint8_t expected_addr, uint8_t* internal_buffer, char* sender_name);

// функция выполнения принятой команды
void ProcessCommand(uint8_t command, const uint8_t* input_data, uint8_t* output_data);

#endif
