#include "fifo.h"
#include "user.h"
#include "hdlc.h"
#include "fsm.h"


fifo_typedef fifo_mts = {0};      // fifo Master To Slave
fifo_typedef fifo_stm = {0};      // fifo Slave To Master


int main()
{
    FifoInit(&fifo_mts);
    FifoInit(&fifo_stm);

    printf("Master<-->Slave simulation starting...\n");

    while(1)
    {
        FSM_Master();
        FSM_Slave();

        #ifdef MORE_FIFO_INFO
        DebugFifoState(&fifo_mts, "MTS");
        DebugFifoState(&fifo_stm, "STM");
        #endif
    }
    
    return 0; 
}
