// Includes of Linux libraries
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

// Includes of RTAI modules
#include <rtai.h>
#include <rtai_sched.h>
#include <rtai_sem.h>
#include <rtai_fifos.h>
#include <rtai_math.h>

// Includes of custom helper classes
#include "Queue.h"
#include "Util.h"

// Times
#define TICK_PERIOD 500000000               // Overall timer period
#define EJECTIONTIME 400 * 1000 * 1000      // Time before the ejection cylinder should retract again
#define TIMEBEFOREEJECTION 600000000        // Time between leaving the light barrier and the ejection
#define SCANNERRESTTIME 13 * 1000 * 10000   // Time of the high signal to the scanner to activate it

// Device-Bits
//  Activate: Bit-Pattern OR Address
//  Deactivate: Bit-Pattern XOR Address
#define conveyor1 0x01   // 1
#define conveyor2 0x02   // 2
#define slider 0x04      // 4
#define ejector1 0x08    // 8
#define ejector2 0x10    // 16
#define ejector3 0x20    // 32
#define scanner 0x80     // 128
#define CARD 0xc000      // Absolute PCI-Address

// Global variables
RT_TASK ejector1task, ejector2task, ejector3task, scantask;
int bitPattern = 0x00; // State of the machine
Queue qs[3];           // Ejection Queues
SEM sem_bit;           // Semaphore for the state bit pattern 
SEM qsSem[3];          // Semaphores for the queues

// Can be used to activate a device. Device is specified with the corresponding bit-pattern as parameter
void activate(int val) {
    rt_sem_wait(&sem_bit);
    bitPattern = bitPattern | val;
    outb(bitPattern, CARD + 0);
    rt_sem_signal(&sem_bit);
}

// Can be used to deactivate a device. Device is specified with the corresponding bit-pattern as parameter
void deactivate(int val) {
    rt_sem_wait(&sem_bit);
    bitPattern = bitPattern ^ val;
    outb(bitPattern, CARD + 0);
    rt_sem_signal(&sem_bit);
}

// Activates the scanner by setting the according bit to 0 for a certain time.
void trigger_scanner(void){
    deactivate(scanner);
    rt_sleep(nano2count(SCANNERRESTTIME));
    activate(scanner);
}

// Computes the correct ejection position for a givin digit.
int computePosition(int eanLastDigit) {
    if (eanLastDigit >= 0 && eanLastDigit <= 2) {
        return 1;
    } else if (eanLastDigit >= 3 && eanLastDigit <= 7) {
        return 2;
    } else if (eanLastDigit >= 8 && eanLastDigit <= 9) {
        return 3;
    } else {
        return 4;
    }
}

// Constants and variables for the FIFO handler.
#define FIFO_SIZE 1024
#define FIFO_NR 3
int scanCount = 0;

// Is called when there is a new entry to the FIFO. It extracts the EAN number and puts the according position into the first queue.
int fifo_handler(unsigned int fifo)
{
  char command[FIFO_SIZE];
  int r;
  int eanLastDigit;

  // Reading ean from queue.
  r = rtf_get(FIFO_NR, command, sizeof(command)-1);

  if (r > 0) {
    // Sort out non EAN elements.
    scanCount++;
    if(scanCount % 2 == 0) return 0;

    // Format the number so it can be processed.
    command[r] = 0;
    char temp[14];
    sscanf(command,"%s",temp);

    if(temp[1] < '0' || temp[1] > '9') return 0;

    eanLastDigit = temp[13] - '0';

    // Compute position and enqueue it.
    int pos = computePosition(eanLastDigit);
    rt_printk("pos: %d\n", pos);
    
    rt_sem_wait(&qsSem[0]);
    enqueue(&qs[0], pos);
    rt_sem_signal(&qsSem[0]);
  }
  return 0;
}

// Checks if the specified barrier is interrupted.
int BarrierInterrupted(int barrier){
    rt_sem_wait(&sem_bit);
    int stat = inb(CARD + 4);
    rt_sem_signal(&sem_bit);
    return ((~stat) & powx(2, barrier)) == powx(2, barrier);
}

// This is the ejection logic.
void ejectionLoop(long barrier) {
    int stat;
    while (1) {
        // Waiting until the light barrier is interrupted.
        if (BarrierInterrupted(barrier)) {
            // Get the desired position of the incoming package.
            rt_sem_wait(&qsSem[barrier-1]);
            int qVal = dequeue(&qs[barrier-1]);
            rt_sem_signal(&qsSem[barrier-1]);
            while (1) {
                // Wait until package is fully inside the area.
                if (!BarrierInterrupted(barrier)) {
                    if (qVal == barrier) {
                        // Eject the parcel.
                        rt_sleep(nano2count(TIMEBEFOREEJECTION));
                        activate(powx(2, barrier+2));
                        rt_sleep(nano2count(EJECTIONTIME));
                        deactivate(powx(2, barrier+2));
                    } else {
                        // Enqueue the parcel into the next queue.
                        if (barrier < 3){
                            rt_sem_wait(&qsSem[barrier]);
                            enqueue(&qs[barrier], qVal);
                            rt_sem_signal(&qsSem[barrier]);
                        }
                    }
                    break;
                }
                rt_task_wait_period();
            }
        }
        rt_task_wait_period();
    }
}

// This is the scanning logic.
void scanLoop(long l){
    trigger_scanner();
    while (1) {
        // Wait until parcel arrives
        if (BarrierInterrupted(0)) {
	    deactivate(conveyor1);
            while (1) {
                // When parcel leaves the area the scanner and the conveyor belt are reactivated.
                if (!BarrierInterrupted(0)) {
                    trigger_scanner();
	 	            activate(conveyor1);
                    break;
                }
                rt_task_wait_period();
            }
        }
        rt_task_wait_period();
    }

}

// This starts the conveyor belts and initializes the semaphores.
void initMachine(void) {
    deactivate(slider);

    int i;
    for(i = 0; i<4; i++){
        qs[i].in = 0;
        qs[i].out = 0;
    }

    activate(conveyor1);
    activate(conveyor2);
}

// This deactivates the conveyor belts.
void exitMachine(void) {
    deactivate(conveyor1);
    deactivate(conveyor2);
    deactivate(scanner);
}

// Initialization of the tasks and semaphores.
static __init int parallel_init(void) {
    RTIME tperiod, tstart1, now;
    rt_mount();

    initMachine();

    int i;
    for(i = 0; i<4; i++){
        rt_sem_init(&qsSem[i],1);
    }

    rtf_create(FIFO_NR, FIFO_SIZE);
    rtf_create_handler(FIFO_NR, &fifo_handler);

    rt_task_init(&ejector1task, ejectionLoop, 1, 3000, 4, 0, 0);
    rt_task_init(&ejector2task, ejectionLoop, 2, 3000, 5, 0, 0);
    rt_task_init(&ejector3task, ejectionLoop, 3, 3000, 6, 0, 0);
    rt_task_init(&scantask, scanLoop, 0, 3000, 8, 0, 0);

    rt_typed_sem_init(&sem_bit, 1, RES_SEM);
    rt_set_periodic_mode();
    start_rt_timer(nano2count(500000));

    now = rt_get_time();
    tstart1 = now + nano2count(100000000);

    rt_task_make_periodic(&ejector1task, tstart1, nano2count(500000000));
    rt_task_make_periodic(&ejector2task, tstart1, nano2count(500000000));
    rt_task_make_periodic(&ejector3task, tstart1, nano2count(500000000));
    rt_task_make_periodic(&scantask, tstart1, nano2count(500000000));
    rt_printk("Module loaded\n");
    return 0;
}

// Cleanup of the tasks and semaphores.
static __exit void parallel_exit(void) {

    exitMachine();

    stop_rt_timer();
    rt_sem_delete(&sem_bit);
    int i;
    for(i = 0; i<4; i++){
        rt_sem_delete(&qsSem[i]);
    }

    rtf_destroy(FIFO_NR);

    rt_task_delete(&ejector1task);
    rt_task_delete(&ejector2task);
    rt_task_delete(&ejector3task);
    rt_task_delete(&scantask);

    outb(0, CARD + 0);
    outb(0, CARD + 1);
    activate(scanner);
    rt_umount();
    rt_printk("Unloading module\n------------------\n");
}

module_init(parallel_init);
module_exit(parallel_exit);

MODULE_LICENSE("GPL");
