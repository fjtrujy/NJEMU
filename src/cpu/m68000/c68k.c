/******************************************************************************
 *
 * C68K (68000 CPU emulator) version 0.80
 * Compiled with Dev-C++
 * Copyright 2003-2004 Stephane Dallongeville
 *
 * (Modified by NJ)
 *
 *****************************************************************************/

#include <stdio.h>
#include <string.h>
#include "emumain.h"
#include "c68k.h"


/******************************************************************************
	Macros
******************************************************************************/

#include "c68kmacro.h"


/******************************************************************************
	Global Structures
******************************************************************************/

c68k_struc C68K;
int32_t m68000_ICountBk;
int32_t ICount;
uint32_t BusErrHandling = 0;
uint32_t BusErrAdr = 0;

/******************************************************************************
	Local Variables
******************************************************************************/

static void *JumpTable[0x10000];
static uint8_t c68k_bad_address[1 << C68K_FETCH_SFT];


/******************************************************************************
	Local Functions
******************************************************************************/

/*--------------------------------------------------------
	Interrupt Callback
--------------------------------------------------------*/

static int32_t C68k_InterruptCallback(int32_t line)
{
	return C68K_INTERRUPT_AUTOVECTOR_EX + line;
}


/*--------------------------------------------------------
	Reset Callback
--------------------------------------------------------*/

static void C68k_ResetCallback(void)
{
}


/******************************************************************************
	C68K Interface Functions
******************************************************************************/

/*--------------------------------------------------------
	CPU Initialization
--------------------------------------------------------*/

void C68k_Init(c68k_struc *CPU)
{
	int i;

	memset(CPU, 0, sizeof(c68k_struc));

	CPU->Interrupt_CallBack = C68k_InterruptCallback;
	CPU->Reset_CallBack = C68k_ResetCallback;

	memset(c68k_bad_address, 0xff, sizeof(c68k_bad_address));

	for (i = 0; i < C68K_FETCH_BANK; i++)
		CPU->Fetch[i] = (uintptr_t)c68k_bad_address;

	C68k_Exec(NULL, 0);
}


/*--------------------------------------------------------
	CPU Reset
--------------------------------------------------------*/

void C68k_Reset(c68k_struc *CPU)
{
	uint32_t PC;

	memset(CPU, 0, (uintptr_t)&CPU->BasePC - (uintptr_t)CPU);

	CPU->flag_I = 7;
	CPU->flag_S = C68K_SR_S;

	CPU->A[7]  = READ_PCREL_32(0);
	PC = READ_PCREL_32(4);

	C68k_Set_Reg(CPU, C68K_PC, PC);
}


/*--------------------------------------------------------
	CPU Execution
--------------------------------------------------------*/

extern uint32_t BusErrHandling;
extern uint32_t BusErrAdr;

int32_t C68k_Exec(c68k_struc *CPU, int32_t cycles)
{
	if (CPU)
	{
		uintptr_t PC;
		uint32_t Opcode;
		uint32_t adr;
		uint32_t res;
		uintptr_t src;
		uintptr_t dst;

		PC = CPU->PC;
		CPU->ICount = cycles;

C68k_Check_Interrupt:
		CHECK_INT
		if (!CPU->HaltState)
		{

C68k_Exec_Next:
			if (CPU->ICount > 0)
			{

				if (BusErrHandling) {
					printf("BusError occured\n");
					SWAP_SP();
					PUSH_32_F(GET_PC() - 2);
					PUSH_16_F(GET_SR());
					CPU->A[7] -= 2;
					PUSH_32_F(BusErrAdr);
					CPU->A[7] -= 2;
					CPU->flag_S = C68K_SR_S;
					PC = READ_MEM_32((C68K_BUS_ERROR_EX) << 2);
					SET_PC(PC);
					BusErrHandling = 0;
				}


				Opcode = READ_IMM_16();
				PC += 2;
				goto *JumpTable[Opcode];

				#include "c68k_op.c"
			}
		}

		CPU->PC = PC;

		return cycles - CPU->ICount;
	}
	else
	{
		#include "c68k_ini.c"
	}

	return 0;
}


/*--------------------------------------------------------
	Interrupt Processing
--------------------------------------------------------*/

void C68k_Set_IRQ(c68k_struc *CPU, int32_t line, int32_t state)
{
	CPU->IRQState = state;
	if (state == CLEAR_LINE)
	{
		CPU->IRQLine = 0;
	}
	else
	{
		CPU->IRQLine = line;
		CPU->HaltState = 0;
	}
}


/*--------------------------------------------------------
	Get Register
--------------------------------------------------------*/

uint32_t C68k_Get_Reg(c68k_struc *CPU, int32_t regnum)
{
	switch (regnum)
	{
	case C68K_PC:  return (uint32_t)(CPU->PC - CPU->BasePC);
	case C68K_USP: return (CPU->flag_S ? CPU->USP : CPU->A[7]);
	case C68K_MSP: return (CPU->flag_S ? CPU->A[7] : CPU->USP);
	case C68K_SR:  return GET_SR();
	case C68K_D0:  return CPU->D[0];
	case C68K_D1:  return CPU->D[1];
	case C68K_D2:  return CPU->D[2];
	case C68K_D3:  return CPU->D[3];
	case C68K_D4:  return CPU->D[4];
	case C68K_D5:  return CPU->D[5];
	case C68K_D6:  return CPU->D[6];
	case C68K_D7:  return CPU->D[7];
	case C68K_A0:  return CPU->A[0];
	case C68K_A1:  return CPU->A[1];
	case C68K_A2:  return CPU->A[2];
	case C68K_A3:  return CPU->A[3];
	case C68K_A4:  return CPU->A[4];
	case C68K_A5:  return CPU->A[5];
	case C68K_A6:  return CPU->A[6];
	case C68K_A7:  return CPU->A[7];
	default: return 0;
	}
}


/*--------------------------------------------------------
	Set Register
--------------------------------------------------------*/

void C68k_Set_Reg(c68k_struc *CPU, int32_t regnum, uint32_t val)
{
	switch (regnum)
	{
	case C68K_PC:
		CPU->BasePC = CPU->Fetch[(val >> C68K_FETCH_SFT) & C68K_FETCH_MASK];
		CPU->BasePC -= val & 0xff000000;
		CPU->PC = val + CPU->BasePC;
		break;

	case C68K_USP:
		if (CPU->flag_S) CPU->USP = val;
		else CPU->A[7] = val;
		break;

	case C68K_MSP:
		if (CPU->flag_S) CPU->A[7] = val;
		else CPU->USP = val;
		break;

	case C68K_SR: SET_SR(val); break;
	case C68K_D0: CPU->D[0] = val; break;
	case C68K_D1: CPU->D[1] = val; break;
	case C68K_D2: CPU->D[2] = val; break;
	case C68K_D3: CPU->D[3] = val; break;
	case C68K_D4: CPU->D[4] = val; break;
	case C68K_D5: CPU->D[5] = val; break;
	case C68K_D6: CPU->D[6] = val; break;
	case C68K_D7: CPU->D[7] = val; break;
	case C68K_A0: CPU->A[0] = val; break;
	case C68K_A1: CPU->A[1] = val; break;
	case C68K_A2: CPU->A[2] = val; break;
	case C68K_A3: CPU->A[3] = val; break;
	case C68K_A4: CPU->A[4] = val; break;
	case C68K_A5: CPU->A[5] = val; break;
	case C68K_A6: CPU->A[6] = val; break;
	case C68K_A7: CPU->A[7] = val; break;
	default: break;
	}

}


/*--------------------------------------------------------
	Set Fetch Address
--------------------------------------------------------*/

void C68k_Set_Fetch(c68k_struc *CPU, uint32_t low_adr, uint32_t high_adr, uintptr_t fetch_adr)
{
	uint32_t i, j;

	i = (low_adr >> C68K_FETCH_SFT) & C68K_FETCH_MASK;
	j = (high_adr >> C68K_FETCH_SFT) & C68K_FETCH_MASK;
	fetch_adr -= i << C68K_FETCH_SFT;
	while (i <= j) CPU->Fetch[i++] = fetch_adr;
}


/*--------------------------------------------------------
	Set Memory Read/Write Functions
--------------------------------------------------------*/

void C68k_Set_ReadB(c68k_struc *CPU, uint8_t (*Func)(uint32_t address))
{
	CPU->Read_Byte = Func;
	CPU->Read_Byte_PC_Relative = Func;
}

void C68k_Set_ReadW(c68k_struc *CPU, uint16_t (*Func)(uint32_t address))
{
	CPU->Read_Word = Func;
	CPU->Read_Word_PC_Relative = Func;
}

void C68k_Set_ReadB_PC_Relative(c68k_struc *CPU, uint8_t (*Func)(uint32_t address))
{
	CPU->Read_Byte_PC_Relative = Func;
}

void C68k_Set_ReadW_PC_Relative(c68k_struc *CPU, uint16_t (*Func)(uint32_t address))
{
	CPU->Read_Word_PC_Relative = Func;
}

void C68k_Set_WriteB(c68k_struc *CPU, void (*Func)(uint32_t address, uint8_t data))
{
	CPU->Write_Byte = Func;
}

void C68k_Set_WriteW(c68k_struc *CPU, void (*Func)(uint32_t address, uint16_t data))
{
	CPU->Write_Word = Func;
}


/*--------------------------------------------------------
	Set Callback Functions
--------------------------------------------------------*/

void C68k_Set_IRQ_Callback(c68k_struc *CPU, int32_t (*Func)(int32_t irqline))
{
	CPU->Interrupt_CallBack = Func;
}

void C68k_Set_Reset_Callback(c68k_struc *CPU, void (*Func)(void))
{
	CPU->Reset_CallBack = Func;
}
