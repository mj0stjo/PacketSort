#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <rtai.h>
#include <rtai_sched.h>
#include <rtai_sem.h>
#include <rtai_fifos.h>
#include <rtai_math.h>

#include "Queue.h"
#include "Util.h"

// times:
#define TICK_PERIOD 500000000
#define AUSWZEIT 400 * 1000 * 1000          // 400ms warten bis wieder zuklappen
#define ZEITVORAUSW 600000000
#define SCANNER_RUHEZEIT 13 * 1000 * 10000

// devices: Zum Aktivieren eines Device muss das aktuelle Bit-Muster
//	   mit seiner Adresse verORt werden.
//	   Zum Deaktivieren eines Device muss das aktuelle Bit-Muster
//	   mit seiner Adresse verXORt werden.
#define band1 0x01    // 1
#define band2 0x02    // 2
#define schieber 0x04 // 4
#define ausw1 0x08    // 8
#define ausw2 0x10    // 16
#define ausw3 0x20    // 32
#define scanner 0x80  // 128
#define CARD 0xc000 // absolute PCI-Kartenadresse

// globalvars:
RT_TASK ausw1task, ausw2task, ausw3task, scantask, rs232task;
int bitmuster = 0x00; // state der ganzen Maschine
Queue qs[3];          // Auswurf Warteschlangen
SEM sem_bit;          // Semaphore for Bitmuster 
SEM qsSem[3];         // Semaphoren für Auswurf Warteschlangen

void activate(int val) { // schreibe 32 Bit auf Karte
    rt_sem_wait(&sem_bit);
    bitmuster = bitmuster | val;
    outb(bitmuster, CARD + 0);
    rt_sem_signal(&sem_bit);
}

void deactivate(int val) { // schreibe 32 Bit auf Karte
    rt_sem_wait(&sem_bit);
    bitmuster = bitmuster ^ val;
    outb(bitmuster, CARD + 0);
    rt_sem_signal(&sem_bit);
}

void trigger_scanner(void){
    deactivate(scanner);
    rt_sleep(nano2count(SCANNER_RUHEZEIT)); //muss sein, sonst wird nicht reaktiviert
    activate(scanner);
}

int computePosition(int eanLastDigit) {
    rt_printk("%d\n", eanLastDigit);
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

#define FIFO_SIZE 1024
#define FIFO_NR 3

int scanCount = 0;

int fifo_handler(unsigned int fifo)
{

  char command[FIFO_SIZE];
  int r;
  int eanLastDigit;

  r = rtf_get(FIFO_NR, command, sizeof(command)-1);


  if (r > 0) {
    scanCount++;
    if(scanCount % 2 == 0) return 0;

    command[r] = 0;
    char temp[14];
    sscanf(command,"%s",temp);

    if(temp[1] < '0' || temp[1] > '9') return 0;

    rt_printk("string: %s\n", temp);

    eanLastDigit = temp[13] - '0';

    int pos = computePosition(eanLastDigit);
    rt_printk("pos: %d\n", pos);
    
    rt_sem_wait(&qsSem[0]);
    enqueue(&qs[0], pos);
    rt_sem_signal(&qsSem[0]);
  }
  return 0;
}

int SchrankeUnterbrochen(int schranke){
    rt_sem_wait(&sem_bit);
    int stat = inb(CARD + 4);
    rt_sem_signal(&sem_bit);
    //rt_printk("%d\n", stat);
    return ((~stat) & powx(2, schranke)) == powx(2, schranke);
}

void auswLoop(long schranke) {
    int stat;
    while (1) {
        if (SchrankeUnterbrochen(schranke)) {
            rt_sem_wait(&qsSem[schranke-1]);
            int qVal = dequeue(&qs[schranke-1]);
            rt_sem_signal(&qsSem[schranke-1]);
            while (1) {
                if (!SchrankeUnterbrochen(schranke)) {
                    if (qVal == schranke) {
                        rt_sleep(nano2count(ZEITVORAUSW));
                        activate(powx(2, schranke+2));
                        rt_printk("Ausgeworfen!\n");
                        rt_sleep(nano2count(AUSWZEIT));
                        deactivate(powx(2, schranke+2));
                    } else {
                        if (schranke < 3){
                            rt_sem_wait(&qsSem[schranke]);
                            enqueue(&qs[schranke], qVal);
                            rt_sem_signal(&qsSem[schranke]);
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

void scanLoop(long l){
    trigger_scanner();
    while (1) {
        if (SchrankeUnterbrochen(0)) {
	    deactivate(band1);
            while (1) {
                if (!SchrankeUnterbrochen(0)) {
		            //rt_printk("Band 1 An!\n");
                    trigger_scanner();
	 	            activate(band1);
                    break;
                }
                rt_task_wait_period();
            }
        }
        rt_task_wait_period();
    }

}

void initMachine(void) {
    deactivate(schieber);

    int i;
    for(i = 0; i<4; i++){
        qs[i].in = 0;
        qs[i].out = 0;
    }

    activate(band1);
    activate(band2);
}

void exitMachine(void) {
    deactivate(band1);
    deactivate(band2);
    deactivate(scanner);
}


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

    rt_task_init(&ausw1task, auswLoop, 1, 3000, 4, 0, 0);
    rt_task_init(&ausw2task, auswLoop, 2, 3000, 5, 0, 0);
    rt_task_init(&ausw3task, auswLoop, 3, 3000, 6, 0, 0);
    rt_task_init(&scantask, scanLoop, 0, 3000, 8, 0, 0);

    rt_typed_sem_init(&sem_bit, 1, RES_SEM);
    rt_set_periodic_mode();
    start_rt_timer(nano2count(500000));

    now = rt_get_time();
    tstart1 = now + nano2count(100000000);

    rt_task_make_periodic(&ausw1task, tstart1, nano2count(500000000));
    rt_task_make_periodic(&ausw2task, tstart1, nano2count(500000000));
    rt_task_make_periodic(&ausw3task, tstart1, nano2count(500000000));
    rt_task_make_periodic(&scantask, tstart1, nano2count(500000000));
    rt_printk("Module loaded\n");
    return 0;
}

static __exit void parallel_exit(void) {

    exitMachine();

    stop_rt_timer();
    rt_sem_delete(&sem_bit);
    int i;
    for(i = 0; i<4; i++){
        rt_sem_delete(&qsSem[i]);
    }

    
    rtf_destroy(FIFO_NR);

    rt_task_delete(&ausw1task);
    rt_task_delete(&ausw2task);
    rt_task_delete(&ausw3task);
    rt_task_delete(&scantask);

    outb(0, CARD + 0); // 00000000
    outb(0, CARD + 1); // 00000000
    activate(scanner);
    rt_umount();
    rt_printk("Unloading module\n------------------\n");
}

module_init(parallel_init);
module_exit(parallel_exit);

MODULE_LICENSE("GPL");