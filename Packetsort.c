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
#define our20ms 200000000
#define our1p5ms 1200000000
#define our600us 600000000
#define AUSWZEIT 400 * 1000 * 1000          // 400ms warten bis wieder zuklappen
#define ZEITVORAUSW 350 * 1000 * 1000       // 350ms warten bevor hochklappen
#define ZEITVORBAND1START 400 * 1000 * 1000 // warten bis band1 anläuft
#define SCANNER_RUHEZEIT 13 * 1000 * 10000

// devices: Zum Aktivieren eines Device muss das aktuelle Bit-Muster
//	   mit seiner Adresse verORt werden.
//	   Zum Deaktivieren eines Device muss das aktuelle Bit-Muster
//	   mit seiner Adresse verXORt werden.
#define PCI_VENDOR_ID_KOLTER 0x1001
#define PCI_DEVICE_ID_KOLTER_DIO 0x11

#define band1 0x01    // 1
#define band2 0x02    // 2
#define schieber 0x04 // 4
#define ausw1 0x08    // 8
#define ausw2 0x10    // 16
#define ausw3 0x20    // 32
#define scanner 0x80  // 128
#define keinLS e0
#define LS0 e1
#define LS1 e2
#define LS2 e4
#define LS3 e8
#define LENGTH_EAN 20
#define PCI_DEVICE_ID_KOLTER_DIO 0x11
#define CARD 0xc000 // absolute PCI-Kartenadresse

// globalvars:
RT_TASK ausw1task, ausw2task, ausw3task, ausw4task, scantask, rs232task;
int bitmuster = 0x00; // state der ganzen Maschine
int stateLS = 0;      // state der Lichtschranken
int lost = 0;         // Pakete ohne Adresse
unsigned char port_a;
unsigned char port_b;
char ch, eancode[LENGTH_EAN];
int charcounter = 0;
SEM sem_bit, s1;                                        // Semaphore for Bitmuster

Queue qs[4];

void activate(int val) { // schreibe 32 Bit auf Karte
    rt_sem_wait(&sem_bit);
    // rt_printk("val: %x\n",val);
    // rt_printk("bitmusteralt: %x\n",bitmuster);
    bitmuster = bitmuster | val;
    outb(bitmuster, CARD + 0);
    // rt_printk("bitmusterneu: %x\n",bitmuster);
    rt_sem_signal(&sem_bit);
}

void deactivate(int val) { // schreibe 32 Bit auf Karte
    rt_sem_wait(&sem_bit);
    // rt_printk("deactiv val: %x\n",val);
    bitmuster = bitmuster ^ val;
    // rt_printk("bitmuster deac: %x\n",bitmuster);
    outb(bitmuster, CARD + 0);
    rt_sem_signal(&sem_bit);
}

void trigger_scanner(void){
    deactivate(scanner);
    rt_sleep(nano2count(SCANNER_RUHEZEIT)); //muss sein, sonst wird nicht reaktiviert
    activate(scanner);
}

int computePosition(int eanLastDigit) {
    if (eanLastDigit >= 0 && eanLastDigit <= 2) {
        return 1;
    } else if (eanLastDigit >= 3 && eanLastDigit <= 5) {
        return 2;
    } else if (eanLastDigit >= 6 && eanLastDigit <= 9) {
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
    if(++scanCount % 2 == 0) return 0;

    command[r] = 0;
    char temp[14];
    sscanf(command,"%s",temp);

    rt_printk("string: %s\n", temp);

    int i;
    for(i=0; i<14; i++){
	rt_printk("%d: %c\n", i, temp[i]);
    }

    eanLastDigit = temp[13] - '0';
    rt_printk("num: %d\n", eanLastDigit);

    int pos = computePosition(eanLastDigit);
    rt_printk("pos: %d\n", pos);
    
    enqueue(&qs[0], pos);
    //trigger_scanner();
  }
  return 0;
}

int SchrankeUnterbrochen(int schranke){
    int stat = inb(CARD + 4);
    return ((~stat) & powx(2, schranke)) == powx(2, schranke);
}

void auswLoop(long schranke) {
    int stat;
    while (1) {
        if (SchrankeUnterbrochen(schranke)) {
            int qVal = dequeue(&qs[schranke-1]);
            while (1) {
                if (!SchrankeUnterbrochen(schranke)) {
                    if (qVal == schranke && schranke != 4) {
                        rt_sleep(nano2count(600000000));
                        activate(powx(2, schranke+2));
                        rt_printk("Ausgeworfen!\n");
                        rt_sleep(nano2count(AUSWZEIT));
                        deactivate(powx(2, schranke+2));
                    } else {
                        if (schranke != 4) enqueue(&qs[schranke], qVal);
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

}

void initMachine(void) {
    int i;
    for(i = 0; i<4; i++){
        qs[i].in = 0;
        qs[i].out = 0;
    }
    activate(band1);
    activate(band2);
    //enqueue(&qs[0], 1);
    //enqueue(&qs[0], 2);
    //enqueue(&qs[0], 3);
    //enqueue(&qs[0], 4);
    //enqueue(&qs[0], 10);
    //enqueue(&qs[0], 3);
    //enqueue(&qs[0], 2);
    //enqueue(&qs[0], 1);
    //enqueue(&qs[0], 3);
}

void exitMachine(void) {
    deactivate(band1);
    deactivate(band2);
}


static __init int parallel_init(void) {
    initMachine();

    deactivate(schieber);

    RTIME tperiod, tstart1, now;
    rt_mount();

    rtf_create(FIFO_NR, FIFO_SIZE);
    rtf_create_handler(FIFO_NR, &fifo_handler);

    rt_task_init(&ausw1task, auswLoop, 1, 3000, 4, 0, 0);
    rt_task_init(&ausw2task, auswLoop, 2, 3000, 5, 0, 0);
    rt_task_init(&ausw3task, auswLoop, 3, 3000, 6, 0, 0);
    rt_task_init(&ausw4task, auswLoop, 4, 3000, 7, 0, 0);
    rt_task_init(&scantask, scanLoop, 0, 3000, 8, 0, 0);

    rt_typed_sem_init(&sem_bit, 1, RES_SEM);
    rt_typed_sem_init(&s1, 1, RES_SEM);
    rt_set_periodic_mode();
    start_rt_timer(nano2count(500000));

    now = rt_get_time();
    tstart1 = now + nano2count(100000000);

    rt_task_make_periodic(&ausw1task, tstart1, nano2count(500000000));
    rt_task_make_periodic(&ausw2task, tstart1, nano2count(500000000));
    rt_task_make_periodic(&ausw3task, tstart1, nano2count(500000000));
    rt_task_make_periodic(&ausw4task, tstart1, nano2count(500000000));
    rt_task_make_periodic(&scantask, tstart1, nano2count(500000000));
    rt_printk("Module loaded\n");
    return 0;
}

static __exit void parallel_exit(void) {

    exitMachine();

    stop_rt_timer();
    rt_sem_delete(&s1);
    rt_sem_delete(&sem_bit);

    
    rtf_destroy(FIFO_NR);

    rt_task_delete(&ausw1task);
    rt_task_delete(&ausw2task);
    rt_task_delete(&ausw3task);
    rt_task_delete(&ausw4task);

    outb(0, CARD + 0); // 00000000
    outb(0, CARD + 1); // 00000000
    activate(scanner);
    rt_umount();
    rt_printk("Unloading modulski\n------------------\n");
}

module_init(parallel_init);
module_exit(parallel_exit);

MODULE_LICENSE("GPL");

