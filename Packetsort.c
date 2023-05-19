#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <rtai.h>
#include <rtai_sched.h>
#include <rtai_sem.h>

#include "Queue.h"

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
RT_TASK polltask, rs232task;
int bitmuster = 0x00; // state der ganzen Maschine
int stateLS = 0;      // state der Lichtschranken
int lost = 0;         // Pakete ohne Adresse
unsigned char port_a;
unsigned char port_b;
char ch, eancode[LENGTH_EAN];
int charcounter = 0;
SEM sem_bit, s1;                                        // Semaphore for Bitmuster

Queue q1, q2, q3, q4;

void initQueues(void){
    q1.in = 0;
    q1.out = 0;
    q2.in = 0;
    q2.out = 0;
    q3.in = 0;
    q3.out = 0;
    q4.in = 0;
    q4.out = 0;
}

void activate(int val)
{ // schreibe 32 Bit auf Karte
  rt_sem_wait(&sem_bit);
  // rt_printk("val: %x\n",val);
  // rt_printk("bitmusteralt: %x\n",bitmuster);
  bitmuster = bitmuster | val;
  outb(bitmuster, CARD + 0);
  // rt_printk("bitmusterneu: %x\n",bitmuster);
  rt_sem_signal(&sem_bit);
}

void deactivate(int val)
{ // schreibe 32 Bit auf Karte
  rt_sem_wait(&sem_bit);
  // rt_printk("deactiv val: %x\n",val);
  bitmuster = bitmuster ^ val;
  // rt_printk("bitmuster deac: %x\n",bitmuster);
  outb(bitmuster, CARD + 0);
  rt_sem_signal(&sem_bit);
}

//void auswLoop(Queue *q, int ausw){
void auswLoop(long l){
    Queue *q;
    int ausw;
    if(l == 2){
	q = &q2;
	ausw = 4;
    }
    activate(band2);
    enqueue(q, 4);
    int stat;
    while(1){
	stat = inb(CARD +4);
    	rt_printk("Stat = %3d \n ", ~stat);
	if(((~stat) & ausw) == ausw){
	    if(dequeue(q) == ausw){
		while(1){
		    stat = inb(CARD +4);
		    if(((~stat) & ausw) == 0){
			rt_sleep(nano2count(600000000));
  	    		activate(ausw2);
			rt_printk("Ausgeworfen!\n");
	    		rt_sleep(nano2count(AUSWZEIT));
	    		deactivate(ausw2);
			deactivate(band2);
			break;
		    }
		}
	    }
	}
	rt_task_wait_period();
    }
}

void test_machine(void)
{

  rt_printk(" -->Test Machine start\n");
  activate(band1);
  deactivate(scanner);
  rt_sleep(nano2count(300 * 1000 * 1000)); // 15 Sekunden bitte???
  deactivate(band1);
  activate(band2);
  rt_sleep(nano2count(300 * 1000 * 1000)); // 15 Sekunden bitte???
  deactivate(band2);
  activate(schieber);
  rt_sleep(nano2count(300 * 1000 * 1000)); // 15 Sekunden bitte???
  deactivate(schieber);
  activate(ausw1);
  rt_sleep(nano2count(300 * 1000 * 1000)); // 15 Sekunden bitte???
  deactivate(ausw1);
  activate(ausw2);
  rt_sleep(nano2count(300 * 1000 * 1000)); // 15 Sekunden bitte???
  deactivate(ausw2);
  activate(ausw3);
  rt_sleep(nano2count(300 * 1000 * 1000)); // 15 Sekunden bitte???
  deactivate(ausw3);
  rt_sleep(nano2count(300 * 1000 * 1000)); // 15 Sekunden bitte???
  activate(band1 + band2 + scanner);
  rt_sleep(nano2count(600 * 1000 * 1000)); // 30 Sekunden bitte???
}

void LSpoller(void)
{ // states pruefen
  int temp, oben;
  while (1){
    temp = inb(CARD + 4);
    oben = inb(CARD + 5);
    rt_printk("LSpoller in = %3d \n ", temp);
    rt_printk("LSoben in = %3d \n ", oben);
    test_machine();
    rt_task_wait_period();
  }
}

static __init int parallel_init(void)
{
  initQueues();

  deactivate(schieber);

  RTIME tperiod, tstart1, now;
  rt_mount();
  rt_task_init(&polltask, auswLoop, 2, 3000, 4, 0, 0);
  rt_typed_sem_init(&sem_bit, 1, RES_SEM);
  rt_typed_sem_init(&s1, 1, RES_SEM);
  rt_set_periodic_mode();
  start_rt_timer(nano2count(500000));

  now = rt_get_time();
  tstart1 = now + nano2count(100000000);
  rt_task_make_periodic(&polltask, tstart1, nano2count(500000000));
  rt_printk("Module loaded\n");
  return 0;
}

static __exit void parallel_exit(void)
{
  stop_rt_timer();
  rt_sem_delete(&s1);
  rt_sem_delete(&sem_bit);
  rt_task_delete(&polltask);
  outb(0, CARD + 0); // 00000000
  outb(0, CARD + 1); // 00000000
  activate(scanner);
  rt_umount();
  rt_printk("Unloading modulski\n------------------\n");
}

module_init(parallel_init);
module_exit(parallel_exit);

MODULE_LICENSE("GPL");
