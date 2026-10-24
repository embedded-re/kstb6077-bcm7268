#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include "broadcom/common/v3d_device_info.h"
#include "broadcom/qpu/qpu_instr.h"
static struct v3d_device_info dev={.ver=33,.has_accumulators=true};
static struct v3d_qpu_instr nop(void){struct v3d_qpu_instr q;assert(v3d_qpu_instr_unpack(&dev,0x3c003186bb800000ULL,&q));return q;}
static void emit(struct v3d_qpu_instr q){uint64_t w;assert(v3d_qpu_instr_pack(&dev,&q,&w));printf("%016lx ",w);}
static void mov(int dst,int src){struct v3d_qpu_instr q=nop();q.alu.mul.op=V3D_QPU_M_MOV;q.alu.mul.waddr=dst;q.alu.mul.magic_write=true;q.alu.mul.a.mux=src;emit(q);}
int main(int argc,char **argv){
 if(argc>1){for(int i=1;i<argc;i++){struct v3d_qpu_instr q;assert(v3d_qpu_instr_unpack(&dev,strtoull(argv[i],NULL,16),&q));printf("%s %u <- %u,%u; %s %u <- %u,%u; sig tlbu=%u tlb=%u vary=%u unif=%u sw=%u\n",v3d_qpu_add_op_name(q.alu.add.op),q.alu.add.waddr,q.alu.add.a.mux,q.alu.add.b.mux,v3d_qpu_mul_op_name(q.alu.mul.op),q.alu.mul.waddr,q.alu.mul.a.mux,q.alu.mul.b.mux,q.sig.ldtlbu,q.sig.ldtlb,q.sig.ldvary,q.sig.ldunif,q.sig.thrsw);}return 0;}
 struct v3d_qpu_instr q=nop();q.sig.ldvary=true;emit(q);emit(nop());
 for(int i=0;i<4;i++) {
  q=nop();if(i)q.sig.ldtlb=true;else q.sig.ldtlbu=true;emit(q);
  for(int j=0;j<4;j++)emit(nop());
  q=nop();q.alu.mul.op=V3D_QPU_M_MOV;q.alu.mul.waddr=i;q.alu.mul.magic_write=false;q.alu.mul.a.mux=V3D_QPU_MUX_R3;emit(q);
 }
 emit(nop());emit(nop());
 q=nop();q.sig.ldunif=true;emit(q);
 q=nop();q.alu.add.op=V3D_QPU_A_SUB;q.alu.add.waddr=0;q.alu.add.magic_write=true;q.alu.add.a.mux=V3D_QPU_MUX_A;q.raddr_a=1;q.alu.add.b.mux=V3D_QPU_MUX_R5;emit(q);
 q=nop();q.alu.add.op=V3D_QPU_A_SHL;q.alu.add.waddr=0;q.alu.add.magic_write=true;q.alu.add.a.mux=V3D_QPU_MUX_R0;q.alu.add.b.mux=V3D_QPU_MUX_B;q.raddr_b=7;q.sig.small_imm_b=true;emit(q);
 q=nop();q.alu.add.op=V3D_QPU_A_ADD;q.alu.add.waddr=0;q.alu.add.magic_write=true;q.alu.add.a.mux=V3D_QPU_MUX_A;q.raddr_a=0;q.alu.add.b.mux=V3D_QPU_MUX_R0;emit(q);
 q=nop();q.alu.add.op=V3D_QPU_A_SHL;q.alu.add.waddr=0;q.alu.add.magic_write=true;q.alu.add.a.mux=V3D_QPU_MUX_R0;q.alu.add.b.mux=V3D_QPU_MUX_B;q.raddr_b=4;q.sig.small_imm_b=true;emit(q);
 q=nop();q.sig.ldunif=true;emit(q);
 q=nop();q.alu.add.op=V3D_QPU_A_ADD;q.alu.add.waddr=0;q.alu.add.magic_write=true;q.alu.add.a.mux=V3D_QPU_MUX_R0;q.alu.add.b.mux=V3D_QPU_MUX_R5;emit(q);
 for(int i=0;i<4;i++){q=nop();q.alu.mul.op=V3D_QPU_M_MOV;q.alu.mul.waddr=V3D_QPU_WADDR_TMUD;q.alu.mul.magic_write=true;q.alu.mul.a.mux=V3D_QPU_MUX_A;q.raddr_a=i;emit(q);}
 mov(V3D_QPU_WADDR_TMUAU,V3D_QPU_MUX_R0);
 q=nop();q.alu.add.op=V3D_QPU_A_TMUWT;emit(q);
 q=nop();q.sig.thrsw=true;emit(q);emit(nop());emit(nop());puts("");
}
