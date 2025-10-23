#ifndef FSM_H
#define FSM_H

#include "hdlc.h"
#include "fifo.h"


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

extern fsm_state_master_typedef master_state;
extern fsm_state_slave_typedef slave_state; 

// конечный автомат ведущего
void FSM_Master(void);

// конечный автомат ведомого
void FSM_Slave(void);

#endif