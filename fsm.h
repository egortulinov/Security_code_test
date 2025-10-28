#ifndef FSM_H
#define FSM_H

#include "hdlc.h"
#include "fifo.h"
#include "timer.h"


typedef enum                        // перечисление состояний ведущего устройства
{
    MASTER_PREPARE_STATE,           // подготовка данных для передачи
    MASTER_TX_STATE,                // состояние отправки данных в FIFO
    MASTER_WAITING_REPLY_STATE,     // ожидание флага FD ответа
    MASTER_RX_STATE,                // состояние приёма из FIFO
    MASTER_PROCESSING_STATE         // проверка и обработка принятого ответа
} fsm_state_master_typedef;

typedef enum                        // перечисление состояний ведомого устройства
{
    SLAVE_WAITING_CMD_STATE,        // состояние ожидания флага для приёма
    SLAVE_RX_STATE,                 // состояние приёма из FIFO 
    SLAVE_PROCESSING_STATE,         // проверка, обработка принятых данных и выполнение команды
    SLAVE_TX_STATE                  // состояние отправки ответа в FIFO
} fsm_state_slave_typedef;

extern fsm_state_master_typedef master_state;   // состояние ведущего в конечном автомате
extern fsm_state_slave_typedef slave_state;     // состояние ведомого в конечном автомате

extern fifo_typedef fifo_mts;                   // FIFO Master To Slave
extern fifo_typedef fifo_stm;                   // FIFO Slave To Master

extern timeout_typedef master_timeout;                // таймаут для получения ответа

// конечный автомат ведущего
void FSM_Master(void);

// конечный автомат ведомого
void FSM_Slave(void);

#endif