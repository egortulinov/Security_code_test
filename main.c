#include "fifo.h"
#include "user.h"
#include "hdlc.h"
#include "fsm.h"


fifo_typedef fifo_mts = {0};      // FIFO Master To Slave
fifo_typedef fifo_stm = {0};      // FIFO Slave To Master


int main()
{
    FifoInit(&fifo_mts);        // инициализация FIFO master to slave
    FifoInit(&fifo_stm);        // инициализация FIFO slave to master

    printf("Master<-->Slave simulation starting...\n");

    while(1)
    {
        FSM_Master();   // конечный автомат ведущего
        FSM_Slave();    // конечный автомат ведомого

        #ifdef MORE_FIFO_INFO
        DebugFifoState(&fifo_mts, "MTS");
        DebugFifoState(&fifo_stm, "STM");
        #endif
    }
    
    return 0; 
}
