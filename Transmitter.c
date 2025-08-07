/*
ECE243 - Computer Organization
Project/IR_LED_control
By: John Cameron & Michael Feng
Last Modified: 2025/03/31
*/

// #include "address_map_niosv.h"  // comment out for CPUlator
#define TIMER_LOAD 1308     // half period (in seconds) * 100000000, 100MHz Clock (approx. 1310 for NEC)
#define BURST_LENGTH 43          //num cycles, 21*2 for NEC
#define START_CODE_LENGTH 684    //num cycles, 342*2 for NEC
#define DEVICE_ADDRESS 0x04
#define BLUE 0b001
#define GREEN 0b010
#define RED 0b100

// function prototypes - defined below the main program
static void interrupt_handler(void) __attribute__((interrupt("machine")));
void set_and_start_timer(void);
void timer_ISR(void);
void interrupt_setup(void);

// global variables - can be used or modified by any subroutine as needed
volatile int* EXPANSION_BASE = (int*)0xFF200070;
volatile int* LED_ADDRESS = (int*)0xFF200000;
volatile int* TIMER_BASE = (int*)0xFF202000;
volatile int* KEYS = (int*)0xFF200050;
volatile int* SW = (int*)0xFF200040;
volatile int carrier_toggle;
volatile int carrier_count = 0;

int main(void) {
  char command = 0x0;
  char address = (char)DEVICE_ADDRESS;
  unsigned int send_data = 0;

  carrier_toggle = 0;
  *LED_ADDRESS = 0;

  *(EXPANSION_BASE + 1) = 0b1; // set direction register of bit 0 to be an OUTPUT
  *EXPANSION_BASE = 0x0; // initialize expansion pins to off

  interrupt_setup();      // set up the interupts
  set_and_start_timer();  // load and start the timer, CONT is set

  while (1) {
    int KEY_edges;
    do {
      KEY_edges = (*(KEYS + 3) & 0xF);
    }
    while(!KEY_edges); // poll KEYs, wait for edge
    
    switch(KEY_edges){
      case 0b0001: command = 0x02; break; //VOL_UP
      case 0b0010: command = 0x03; break; //VOL_DOWN
      case 0b0100:                        //COLOUR_CHANGE
        command = 0xF0;
        command |= (*SW & 0xF);           //get bit3 of SW, UP=1/DOWN=0. bits2-0 indicate RGB
        break;
      case 0b1000:                        //PAUSE
        command = 0x04; break;
      default: break;
    }

    *(KEYS + 3) = KEY_edges; //reset

    //VOLUME UP:
    //address: 0x04 0b0000 0100
    //~address 0xFB 0b1111 1011
    //command: 0x02 0b0000 0010
    //~command 0xFD 0b1111 1101

    //package send_data appropriately based on NEC protocol
    send_data |= 0xFF & (~command);
    send_data = send_data << 8;
    send_data |= command;
    send_data = send_data << 8;
    send_data |= 0xFF & (~address);
    send_data = send_data << 8;
    send_data |= address;

    carrier_toggle = 1;
    for(carrier_count = 0; carrier_count < START_CODE_LENGTH;); //9ms start code
  
    carrier_toggle = 0;
    for(carrier_count = 0; carrier_count < START_CODE_LENGTH/2;); //4.5ms silence
  
    //carrier_toggle = 1;
    for(int i = 0; i < 32; i++){ //send 32 bits;
      if((send_data) & 0x01) {
        //send logical 1, 2.25ms Bit time
        for(int j = 0; j < 4; j++){
          if(j == 0) carrier_toggle = 1; else carrier_toggle = 0; //BURST_LENGTH pulse burst, followed by nothing for three BURST_LENGTHs.
          for(carrier_count = 0; carrier_count < BURST_LENGTH;); //562.5us burst length
        }
      } else {
        //send logical 0, 1.125ms Bit time
        for(int j = 0; j < 2; j++){
          if(j == 0) carrier_toggle = 1; else carrier_toggle = 0; //BURST_LENGTH pulse burst, followed by nothing for one BURST_LENGTHs.
          for(carrier_count = 0; carrier_count < BURST_LENGTH;); //562.5us burst length
        }
      }
      send_data = send_data >> 1; //shift to next bit
    }
    for(int j = 0; j < 4; j++){
      if(j == 0) carrier_toggle = 1; else carrier_toggle = 0; //BURST_LENGTH pulse burst, followed by nothing for three BURST_LENGTHs.
      for(carrier_count = 0; carrier_count < BURST_LENGTH;); //562.5us burst length
    }
  }
  return 0;
}

void interrupt_handler(void) {
  int trap_cause_code;
  __asm__ volatile(
      "csrr %0, mcause"
      : "=r"(trap_cause_code));  // check what interrupt line generated the trap
  trap_cause_code = (trap_cause_code & 0x7FFFFFFF);  // isolte lower 31 bits
  if (trap_cause_code == 16) {
    timer_ISR();  // trap caused by the Interval Timer 1 (timer has expired)
  } else {
    while (1) {  // arrived here for no good reason - go into a infinite loop
    }
  }
  return;
}

void interrupt_setup(void) {
  int mstatus, mie, mtvec,
      setup;         // values of the 3 important control & status registers
  mstatus = 0b1000;  // selects the mie bit of mstatus register
  __asm__ volatile(
      "csrc mstatus, %0" ::"r"(mstatus));  // disable global interrupts
  mtvec = (int)&interrupt_handler;         // get address of interrupt handler
  __asm__ volatile("csrw mtvec, %0" ::"r"(
      mtvec));     // write address of interrupt handler to mtvec register
  setup = 0b1001;  // stops timer and sets ITO bit HIGH
  *(TIMER_BASE + 1) = setup;
  __asm__ volatile("csrr %0, mie" : "=r"(mie));  // read the mie register
  __asm__ volatile("csrs mie, %0" ::"r"(
      mie));      // disable any IRQ line that is currently HIGH
  mie = 0x10000;  // 16th bit is HIGH (corresponds to iTimer1 IRQ line)
  __asm__ volatile("csrs mie, %0" ::"r"(mie));  // set the IRQ line 16 HIGH
  __asm__ volatile(
      "csrs mstatus, %0" ::"r"(mstatus));  // finally, enable global interrupts
  return;
}

void set_and_start_timer(void) {
  int start_value, control_register;
  start_value = (int)TIMER_LOAD;
  *(TIMER_BASE + 2) = start_value;  // sets the lower 16 bits of the start value
  start_value = (start_value >> 16);  // shift upper 16 bits to lower 16 bits
  *(TIMER_BASE + 3) = start_value;  // sets the upper 16 bits of the start value
  control_register = 0b111;         // sets START, CONT, and ITO high
  *(TIMER_BASE + 1) = control_register;  // start the timer
  return;
}

void timer_ISR(void) {
  //int LED_status;
  carrier_count++;
  *TIMER_BASE = 0;            // reset TO bit in the Timer
  //LED_status = *LED_ADDRESS;  // read the LEDs parallel port for current data
  //LED_status = (LED_status ^ 0b1);  // toggle LED lights
  if(carrier_toggle){
    //*LED_ADDRESS ^= 0b1;       // toggle LEDs
    *EXPANSION_BASE ^= 0b1;     //toggle the expansion port
  } else {
    //*LED_ADDRESS = 0;       // off to LEDs
    *EXPANSION_BASE = 0;     // off to the expansion port
  }
  return;
}
