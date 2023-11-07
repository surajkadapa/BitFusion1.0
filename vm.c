#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>

#define OPC(i) ((i)>>12)
#define NOPS (16)
#define SECTIMM(i) sect(IMM(i), 5)
#define DR(i) (((i)>>9)&0x7)
#define SR1(i) (((i)>>6)&0x7)
#define SR2(i) ((i)&0x7)
#define FIMM(i) ((i>>5)&0x1)
#define IMM(i) ((i)&0x1F)
#define POFF9(i) sect((i)&0x1FF, 9)
#define POFF(i) sect((i)&0x3F, 6)
#define POFF11(i) sect((i)&0x7FF, 11)
#define FCND(i) (((i)>>9)&0x7)  
#define FL(i) (((i)>>11)&1)
#define BR(i) (((i)>>6)&0x7)
#define TRP(i) ((i)&0xFF)

bool running = true;

enum regist {R0=0, R1, R2, R3, R4, R5, R6, R7, RPC, RCND, RCNT};
enum flags { FP = 1 << 0, FZ = 1 << 1, FN = 1 << 2 };

uint16_t reg[RCNT] = {0};
uint16_t PC_START = 0x3000;
uint16_t mem[UINT16_MAX] = {0}; //UINT16_MAX = 65535
uint16_t instr;

typedef void (*op_ex_f)(uint16_t instruction);
static inline uint16_t mr(uint16_t address) { return mem[address];  }
static inline void mw(uint16_t address, uint16_t val) { mem[address] = val; }
static inline void uf(enum regist r) {
    if (reg[r]==0) reg[RCND] = FZ;
    else if (reg[r]>>15) reg[RCND] = FN;
    else reg[RCND] = FP;
}
static inline uint16_t sect(uint16_t n, int b){
  return ((n>>(b-1))&1) ? //if the bth bit is 1, then it is negative
    (n|(0xFFF << b)) : n; //fill the remaining bits with 1 else return the same number
}
static inline void rti(uint16_t i) {} //unused
static inline void res(uint16_t i) {} //unused


//implement all operations
static inline void add(uint16_t i){
  reg[DR(i)] = reg[SR1(i)] + 
    (FIMM(i) ?  //if 5th bit is 1
      SECTIMM(i) :  //sign extend IMM and add it to SR1
        reg[SR2(i)]); //else add the value of SR1 to SR2

  uf(DR(i)); //updating the conditional register depending on the value of DR1
}

static inline void and(uint16_t i){
  reg[DR(i)] = reg[SR2(i)] &
    (FIMM(i) ?
      SECTIMM(i): 
        reg[SR2(i)]);
  uf(DR(i));
}

static inline void ld(uint16_t i){
  reg[DR(i)] = mr(reg[RPC] + POFF9(i));
  uf(DR(i));
}

static inline void ldi(uint16_t i){
  reg[DR(i)] = mr(mr(reg[RPC]+POFF9(i))); //performing two memory reads
  uf(DR(i));
}

static inline void ldr(uint16_t i){ //load data to destination register from base register(instead of RPC)
  reg[DR(i)] = mr(reg[SR1(i)]+POFF(i));
  uf(DR(i));
}

static inline void lea(uint16_t i){ //load address of the register which is the sum of RPC and offset
  reg[DR(i)] = reg[RPC] + POFF9(i);
  uf(DR(i));
}

static inline void not(uint16_t i){
  reg[DR(i)] = ~reg[SR1(i)];
  uf(DR(i));
}

static inline void st(uint16_t i){
  mw(reg[RPC] + POFF9(i), reg[DR(i)]); //writes the value of DR(i) to the memory address specified
}

static inline void sti(uint16_t i){ //store indirect
  mw(mr(reg[RPC] + POFF(i)), reg[DR(i)]); 
}

static inline void str(uint16_t i){ //use some other register as base instead of RPC
  mw(reg[SR1(i)]+POFF(i), reg[DR(i)]);
}

static inline void jmp(uint16_t i){
  reg[RPC] = reg[BR(i)];
}

static inline void jsr(uint16_t i){
  reg[R7] = reg[RPC];
  reg[RPC] = (FL(i)) ? //chekcing bit 11
    reg[RPC] + POFF11(i): //rpc+offset
    reg[BR(i)]; //base register
}

static inline void br(uint16_t i){
  if(reg[RCND] & FCND(i)){ //if the condition is met
    reg[RPC] += POFF9(i); //branch off to the instruction
  }
}

//trap instructions

static inline void tgetc(){
  reg[R0] = getchar();
}

static inline void tout(){
  fprintf(stdout, "%c", (char)reg[R0]);
}

static inline void tputs(){
  uint16_t *p = mem+reg[R0];
  while(*p){
    fprintf(stdout, "%c", (char)*p);
    p++;
  }
}
static inline void tin(){
  reg[R0] = getchar();
  fprintf(stdout, "%c", reg[R0]);
}

static inline void tputsp() { /* Not Implemented */ }

static inline void thalt(){
  running = false;
}
static inline void tinu16(){
  fscanf(stdin, "%hu", &reg[R0]);
}
static inline void toutu16(){
  fprintf(stdout, "%hu\n", reg[R0]);
}

enum {trp_offset = 0x20};
typedef void (*trp_ex_f)();
trp_ex_f trp_ex[8] = {tgetc, tout, tputs, tin, tputsp, thalt, tinu16, toutu16};

static inline void trap(uint16_t i){
  trp_ex[TRP(i)-trp_offset]();
}



//operations end here

op_ex_f op_ex[NOPS]={
 br, add, ld, st, jsr, and, ldr, str, rti, not, ldi, sti, jmp, res, lea, trap
};

//main loop of VM


void start(uint16_t offset){
  reg[RPC] = PC_START + offset; //The RPC is set
  while(running){
    uint16_t i = mr(reg[RPC]++); //extract instruction from RPC and increment automatically
    op_ex[OPC(i)](i);
  }
}

//ability to load programs
void ld_img(char *fname, uint16_t offset){
  //open the binary file to the VM
  FILE *in = fopen(fname, "rb");
  if(NULL==in){
    fprintf(stderr, "Cannot open file %s.\n", fname);
    exit(1);
  }
  uint16_t *p = mem + PC_START + offset; //position from the memory to start copying file to memory
  fread(p, sizeof(uint16_t), (UINT16_MAX-PC_START), in); //loading the program to memory
  fclose(in); //close the file
}

int main(int argc, char **argv){
  ld_img(argv[1], 0x0);
  start(0x0);
  return 0;
}