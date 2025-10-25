#include "fsm.h"
#include <stdio.h>

fsm_state_master_typedef master_state = MASTER_PREPARE_STATE;     // инициализация мастера в отправку
fsm_state_slave_typedef slave_state = SLAVE_WAITING_CMD_STATE;    // инициализация слейва в ожидание флага

uint64_t master_timeout_deadline = 0;
uint64_t master_retry_deadline = 0;

// конечный автомат ведущего
void FSM_Master(void)
{
    static bool frame_sent=false;                   // флаг отправленного сообщения
    
    switch(master_state)
    {
        case MASTER_PREPARE_STATE:

            // подготовка к началу общения
            printf("----------------------------------------------------------\n");
            printf("Master:\tPreparing message with command: 0x%02X to unit: 0x%02X \n", USER_COMMAND, HDLC_SLAVE_ADDR);

            FifoIndexReset(&fifo_mts);
            HDLC_TxContextInit(&master_tx_context, HDLC_SLAVE_ADDR, USER_COMMAND, internal_master_tx_buffer);
            HDLC_RxContextInit(&master_rx_context);
            
            frame_sent=false;
            printf("Master:\tStart transmitting...\n");
            master_state=MASTER_TX_STATE;
            break;

        case MASTER_TX_STATE:

            // отправка всего сообщения
            if(!frame_sent)
            {
                if(!FifoIsFull(&fifo_mts))
                {
                    HDLC_SendByte(&master_tx_context, &fifo_mts);
                    if(master_tx_context.tx_stage==TX_STAGE_COMPLETED)
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
                // отладочная информация
                printf("Master:\tTransmitted information:\t");
                for(int i=0; i<HDLC_INFO_SIZE; i++)
                {
                    printf("%02X ", internal_master_tx_buffer[i]);
                }
                printf("\n");

                printf("Master:\tWaiting for reply from unit 0x%02X...\n", master_tx_context.tx_data.address);
                master_timeout_deadline = SET_TIMEOUT(MASTER_WAIT_REPLY_MS);
                master_state=MASTER_WAITING_REPLY_STATE;
            }
            break;

        case MASTER_WAITING_REPLY_STATE:

            // ожидаем флаг начала передачи от ведомого
            if (!FifoIsEmpty(&fifo_stm))
                HDLC_ReceiveByte(&master_rx_context, &fifo_stm, HDLC_MASTER_ADDR, "Master");
            else
                printf("Master:\tFIFO is empty, waiting...\n");

            if(master_rx_context.fd_received && !master_rx_context.frame_assembled)
            {
                master_state=MASTER_RX_STATE;
                master_timeout_deadline = 0;
            }

            // проверка на таймаута
            if (master_timeout_deadline != 0 && CHECK_TIMEOUT(master_timeout_deadline))
            {
                master_state = MASTER_PREPARE_STATE;
                printf("Master:\tNo reply received. Sending again...\n");
            }
            break;

        case MASTER_RX_STATE:
            
            // приём ответа от ведомого 
            if (!FifoIsEmpty(&fifo_stm)) 
                HDLC_ReceiveByte(&master_rx_context, &fifo_stm, HDLC_MASTER_ADDR, "Master");
            else
                printf("Master:\tFIFO is empty, waiting...\n");

            if(master_rx_context.frame_assembled && master_rx_context.frame_correct)
            {
                master_state=MASTER_PROCESSING_STATE;
                printf("Master:\tFrame received correctly!\n");
            }
            else if(master_rx_context.frame_assembled && !master_rx_context.frame_correct)
            {
                printf("Master:\tFrame is incorrect!\n");
                HDLC_RxContextInit(&master_rx_context);
                master_state = MASTER_PREPARE_STATE;
            }
            break;

        case MASTER_PROCESSING_STATE:

            // сохраняем данные во внутренний буффер
            internal_master_rx_buffer[0]=master_rx_context.rx_data.control;
            memcpy(&internal_master_rx_buffer[1], master_rx_context.rx_data.information, HDLC_INFO_SIZE);
            
            // отладочный вывод
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

            // ожидаем флаг начала передачи от ведущего
            if (!FifoIsEmpty(&fifo_mts))
                HDLC_ReceiveByte(&slave_rx_context, &fifo_mts, HDLC_SLAVE_ADDR, "Slave");
            else
                printf("Slave:\tFIFO is empty, waiting...\n");

            if(slave_rx_context.fd_received && !slave_rx_context.frame_assembled)
            {
                slave_state=SLAVE_RX_STATE;
            }
            break;

        case SLAVE_RX_STATE:

            // приём сообщения от ведущего
            if (!FifoIsEmpty(&fifo_mts))
                HDLC_ReceiveByte(&slave_rx_context, &fifo_mts, HDLC_SLAVE_ADDR, "Slave");
            else
                printf("Slave:\tFIFO is empty, waiting...\n");

            if(slave_rx_context.frame_assembled && slave_rx_context.frame_correct)
            {
                printf("Slave:\tFrame received completely!\n");
                slave_state = SLAVE_PROCESSING_STATE;
            }
            else if(slave_rx_context.frame_assembled && !slave_rx_context.frame_correct)
            {
                printf("Slave:\tFrame is incorrect!");
                HDLC_RxContextInit(&slave_rx_context);
                slave_state = SLAVE_WAITING_CMD_STATE;
            }
            break;

        case SLAVE_PROCESSING_STATE:

            // проверка и обработка принятого сообщения
            internal_slave_rx_buffer[0]=slave_rx_context.rx_data.control;
            memcpy(&internal_slave_rx_buffer[1], slave_rx_context.rx_data.information, HDLC_INFO_SIZE);

            // отладочная информация
            printf("Slave:\tReceived information:\t\t");
            for(int i=1; i<HDLC_INFO_SIZE+1; i++)
            {
                printf("%02X ", internal_slave_rx_buffer[i]);
            }
            printf("\n");

            ProcessCommand(internal_slave_rx_buffer[0], &internal_slave_rx_buffer[1], internal_slave_tx_buffer);
    
            processing_complete = true;
            reply_sent = false;
            HDLC_RxContextInit(&slave_rx_context);
            slave_state = SLAVE_TX_STATE;
            break;

        case SLAVE_TX_STATE:

            // При получении нового сообщения - прерываем отправку
            if (!FifoIsEmpty(&fifo_mts)) 
            {
                HDLC_ReceiveByte(&slave_rx_context, &fifo_mts, HDLC_SLAVE_ADDR, "Slave");
        
                if (slave_rx_context.fd_received && !slave_rx_context.frame_assembled) 
                {
                    FifoIndexReset(&fifo_stm);
                    slave_state = SLAVE_RX_STATE;
                    processing_complete = false;
                    reply_sent = false;
                    return;
                }
            }

            // отправка ответа ведущему
            if(processing_complete && !reply_sent)
            {
                HDLC_TxContextInit(&slave_tx_context, HDLC_MASTER_ADDR, internal_slave_rx_buffer[0], internal_slave_tx_buffer);
                printf("Slave:\tPreparing reply to master...\n");
                reply_sent=true;
            }

            if(!FifoIsFull(&fifo_stm))
            {
                HDLC_SendByte(&slave_tx_context, &fifo_stm);

                if(slave_tx_context.tx_stage==TX_STAGE_COMPLETED)
                {
                    printf("Slave:\tReply sent completely!\n");

                    HDLC_RxContextInit(&slave_rx_context);
                    processing_complete=false;
                    reply_sent=false;

                    slave_state=SLAVE_WAITING_CMD_STATE;

                    // отладочная информация
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