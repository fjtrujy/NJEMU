/******************************************************************************
 *
 * CZ80 (Z80 CPU emulator) version 0.9
 * Compiled with Dev-C++
 * Copyright 2004-2005 Stéphane Dallongeville
 *
 * (Modified by NJ)
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emumain.h"
#include "cz80.h"


#define CF					0x01
#define NF					0x02
#define PF					0x04
#define VF					PF
#define XF					0x08
#define HF					0x10
#define YF					0x20
#define ZF					0x40
#define SF					0x80


/******************************************************************************
	Macros
******************************************************************************/

#include "cz80macro.h"


/******************************************************************************
	Global Structures
******************************************************************************/

cz80_struc ALIGN_DATA CZ80;


/******************************************************************************
	Local Variables
******************************************************************************/

static uint8_t ALIGN_DATA cz80_bad_address[1 << CZ80_FETCH_SFT];

static uint8_t ALIGN_DATA SZ[256];
static uint8_t ALIGN_DATA SZP[256];
static uint8_t ALIGN_DATA SZ_BIT[256];
static uint8_t ALIGN_DATA SZHV_inc[256];
static uint8_t ALIGN_DATA SZHV_dec[256];
#if CZ80_BIG_FLAGS_ARRAY
static uint8_t ALIGN_DATA SZHVC_add[2*256*256];
static uint8_t ALIGN_DATA SZHVC_sub[2*256*256];
#endif


/******************************************************************************
	Local Functions
******************************************************************************/

/*--------------------------------------------------------
	Interrupt Callback
--------------------------------------------------------*/

static int32_t Cz80_Interrupt_Callback(int32_t line)
{
	return 0xff;
}


/******************************************************************************
	CZ80 Interface Functions
******************************************************************************/

/*--------------------------------------------------------
	CPU Initialization
--------------------------------------------------------*/

void Cz80_Init(cz80_struc *CPU)
{
	uint32_t i, j, p;
#if CZ80_BIG_FLAGS_ARRAY
	int oldval, newval, val;
	uint8_t *padd, *padc, *psub, *psbc;
#endif

	memset(CPU, 0, sizeof(cz80_struc));

	memset(cz80_bad_address, 0xff, sizeof(cz80_bad_address));

	for (i = 0; i < CZ80_FETCH_BANK; i++)
	{
		CPU->Fetch[i] = (uintptr_t)cz80_bad_address - (i << CZ80_FETCH_SFT);
#if CZ80_ENCRYPTED_ROM
		CPU->OPFetch[i] = 0;
#endif
	}

	// flags tables initialisation
	for (i = 0; i < 256; i++)
	{
		SZ[i] = i & (SF | YF | XF);
		if (!i) SZ[i] |= ZF;

		SZ_BIT[i] = i & (SF | YF | XF);
		if (!i) SZ_BIT[i] |= ZF | PF;

		for (j = 0, p = 0; j < 8; j++) if (i & (1 << j)) p++;
		SZP[i] = SZ[i];
		if (!(p & 1)) SZP[i] |= PF;

		SZHV_inc[i] = SZ[i];
		if(i == 0x80) SZHV_inc[i] |= VF;
		if((i & 0x0f) == 0x00) SZHV_inc[i] |= HF;

		SZHV_dec[i] = SZ[i] | NF;
		if (i == 0x7f) SZHV_dec[i] |= VF;
		if ((i & 0x0f) == 0x0f) SZHV_dec[i] |= HF;
	}

#if CZ80_BIG_FLAGS_ARRAY
	padd = &SZHVC_add[  0*256];
	padc = &SZHVC_add[256*256];
	psub = &SZHVC_sub[  0*256];
	psbc = &SZHVC_sub[256*256];

	for (oldval = 0; oldval < 256; oldval++)
	{
		for (newval = 0; newval < 256; newval++)
		{
			/* add or adc w/o carry set */
			val = newval - oldval;
			*padd = (newval) ? ((newval & 0x80) ? SF : 0) : ZF;
			*padd |= (newval & (YF | XF));	/* undocumented flag bits 5+3 */
			if ((newval & 0x0f) < (oldval & 0x0f)) *padd |= HF;
			if (newval < oldval ) *padd |= CF;
			if ((val ^ oldval ^ 0x80) & (val ^ newval) & 0x80) *padd |= VF;
			padd++;

			/* adc with carry set */
			val = newval - oldval - 1;
			*padc = (newval) ? ((newval & 0x80) ? SF : 0) : ZF;
			*padc |= (newval & (YF | XF));	/* undocumented flag bits 5+3 */
			if ((newval & 0x0f) <= (oldval & 0x0f)) *padc |= HF;
			if (newval <= oldval) *padc |= CF;
			if ((val ^ oldval ^ 0x80) & (val ^ newval) & 0x80) *padc |= VF;
			padc++;

			/* cp, sub or sbc w/o carry set */
			val = oldval - newval;
			*psub = NF | ((newval) ? ((newval & 0x80) ? SF : 0) : ZF);
			*psub |= (newval & (YF | XF));	/* undocumented flag bits 5+3 */
			if ((newval & 0x0f) > (oldval & 0x0f)) *psub |= HF;
			if (newval > oldval) *psub |= CF;
			if ((val^oldval) & (oldval^newval) & 0x80) *psub |= VF;
			psub++;

			/* sbc with carry set */
			val = oldval - newval - 1;
			*psbc = NF | ((newval) ? ((newval & 0x80) ? SF : 0) : ZF);
			*psbc |= (newval & (YF | XF));	/* undocumented flag bits 5+3 */
			if ((newval & 0x0f) >= (oldval & 0x0f)) *psbc |= HF;
			if (newval >= oldval) *psbc |= CF;
			if ((val ^ oldval) & (oldval^newval) & 0x80) *psbc |= VF;
			psbc++;
		}
	}
#endif

	CPU->pzR8[0] = &zB;
	CPU->pzR8[1] = &zC;
	CPU->pzR8[2] = &zD;
	CPU->pzR8[3] = &zE;
	CPU->pzR8[4] = &zH;
	CPU->pzR8[5] = &zL;
	CPU->pzR8[6] = &zF;	// Swapped with A for processing convenience
	CPU->pzR8[7] = &zA;	// Swapped with F for processing convenience

	CPU->pzR16[0] = pzBC;
	CPU->pzR16[1] = pzDE;
	CPU->pzR16[2] = pzHL;
	CPU->pzR16[3] = pzFA;

	zIX = zIY = 0xffff;

	CPU->Interrupt_Callback = Cz80_Interrupt_Callback;
}


/*--------------------------------------------------------
	CPU Reset
--------------------------------------------------------*/

void Cz80_Reset(cz80_struc *CPU)
{
	// I, R, CPU and interrupts logic is reset, registers are untouched
	memset(&CPU->R, 0, (uintptr_t)&CPU->BasePC - (uintptr_t)&CPU->R);
	Cz80_Set_Reg(CPU, CZ80_PC, 0);
}

/*--------------------------------------------------------
	CPU Execution
--------------------------------------------------------*/

int32_t Cz80_Exec(cz80_struc *CPU, int32_t cycles)
{
#if CZ80_USE_JUMPTABLE
#include "cz80jmp.c"
#endif

	uintptr_t PC;
#if CZ80_ENCRYPTED_ROM
	uintptr_t OPBase;
#endif
	uint32_t Opcode;
	uint32_t adr = 0;
	uintptr_t res;
	uintptr_t val;
	union16 *data;

	PC = CPU->PC;
#if CZ80_ENCRYPTED_ROM
	OPBase = CPU->OPBase;
#endif
	CPU->ICount = cycles - CPU->ExtraCycles;
	CPU->ExtraCycles = 0;

Cz80_Exec:
	if (CPU->Status)
	{
		if (CPU->Status & CZ80_HAS_NMI)
		{
			zIFF1 = 0;
			CPU->Status &= ~(CZ80_HALTED | CZ80_HAS_NMI);
			CPU->ExtraCycles += 11;
			PUSH_16(zRealPC);
			SET_PC(0x66);
		} else if (CPU->Status & CZ80_HAS_INT)
		{
			CHECK_INT
		} else if (CPU->Status & CZ80_HALTED)
		{
			goto Cz80_Exec_End;
		}
		CPU->ICount -= CPU->ExtraCycles;
		CPU->ExtraCycles = 0;
	}

	if (CPU->ICount > 0)
	{
Cz80_Exec_nocheck:
		data = pzHL;
		Opcode = READ_OP();
#if CZ80_EMULATE_R_EXACTLY
		zR++;
#endif
		#include "cz80_op.c"
	}

Cz80_Exec_End:
	CPU->PC = PC;
#if CZ80_ENCRYPTED_ROM
	CPU->OPBase = OPBase;
#endif
	if (!((CPU->Status & CZ80_HALTED) && CPU->ICount > 0))
		cycles -= CPU->ICount;
	CPU->ICount = 0;
#if !CZ80_EMULATE_R_EXACTLY
	zR = (zR + (cycles >> 2)) & 0x7f;
#endif

	return cycles;
}


/*--------------------------------------------------------
	Interrupt Processing
--------------------------------------------------------*/

void Cz80_Set_IRQ(cz80_struc *CPU, int32_t line, int32_t state)
{
	if (line == IRQ_LINE_NMI)
	{
		if (state)
			CPU->Status |= CZ80_HAS_NMI;
		else
			CPU->Status &= ~CZ80_HAS_NMI;
	} else
	{
		CPU->IRQLine = line;
		CPU->IRQState = state;
		if (state)
		{
			if (zIFF1)
				CPU->Status |= CZ80_HAS_INT;
		} else
		{
			CPU->Status &= ~CZ80_HAS_INT;
		}
	}
}


/*--------------------------------------------------------
	Get Register
--------------------------------------------------------*/

uint32_t Cz80_Get_Reg(cz80_struc *CPU, int32_t regnum)
{
	switch (regnum)
	{
	case CZ80_PC:   return (uint32_t)(CPU->PC - CPU->BasePC);
	case CZ80_SP:   return zSP;
	case CZ80_FA:   return zFA;
	case CZ80_BC:   return zBC;
	case CZ80_DE:   return zDE;
	case CZ80_HL:   return zHL;
	case CZ80_IX:   return zIX;
	case CZ80_IY:   return zIY;
	case CZ80_FA2:  return zFA2;
	case CZ80_BC2:  return zBC2;
	case CZ80_DE2:  return zDE2;
	case CZ80_HL2:  return zHL2;
	case CZ80_R:    return zR;
	case CZ80_I:    return zI;
	case CZ80_IM:   return zIM;
	case CZ80_IFF1: return zIFF1;
	case CZ80_IFF2: return zIFF2;
	case CZ80_HALT: return CPU->Status & CZ80_HALTED;
	case CZ80_IRQ:  return CPU->IRQState;
	default: return 0;
	}
}


/*--------------------------------------------------------
	Set Register
--------------------------------------------------------*/

void Cz80_Set_Reg(cz80_struc *CPU, int32_t regnum, uint32_t val)
{
	switch (regnum)
	{
	case CZ80_PC:
		CPU->BasePC = CPU->Fetch[val >> CZ80_FETCH_SFT];
#if CZ80_ENCRYPTED_ROM
		CPU->OPBase = CPU->OPFetch[val >> CZ80_FETCH_SFT];
#endif
		CPU->PC = val + CPU->BasePC;
		break;

	case CZ80_SP:   zSP = val; break;
	case CZ80_FA:   zFA = val; break;
	case CZ80_BC:   zBC = val; break;
	case CZ80_DE:   zDE = val; break;
	case CZ80_HL:   zHL = val; break;
	case CZ80_IX:   zIX = val; break;
	case CZ80_IY:   zIY = val; break;
	case CZ80_FA2:  zFA2 = val; break;
	case CZ80_BC2:  zBC2 = val; break;
	case CZ80_DE2:  zDE2 = val; break;
	case CZ80_HL2:  zHL2 = val; break;
	case CZ80_R:    zR = val; break;
	case CZ80_I:    zI = val; break;
	case CZ80_IM:   zIM = val; break;
	case CZ80_IFF1: zIFF1 = val ? (1 << 2) : 0; break;
	case CZ80_IFF2: zIFF2 = val ? (1 << 2) : 0; break;
	case CZ80_HALT: CPU->Status = !!val * CZ80_HALTED; break;
	case CZ80_IRQ:  CPU->IRQState = val; break;
	default: break;
	}
}


/*--------------------------------------------------------
	Set Fetch Address
--------------------------------------------------------*/

void Cz80_Set_Fetch(cz80_struc *CPU, uint32_t low_adr, uint32_t high_adr, uintptr_t fetch_adr)
{
	int i, j;

	i = low_adr >> CZ80_FETCH_SFT;
	j = high_adr >> CZ80_FETCH_SFT;
	fetch_adr -= i << CZ80_FETCH_SFT;

	while (i <= j)
	{
		CPU->Fetch[i] = fetch_adr;
#if CZ80_ENCRYPTED_ROM
		CPU->OPFetch[i] = 0;
#endif
		i++;
	}
}


/*--------------------------------------------------------
	Set Fetch Address (Encrypted ROM Support)
--------------------------------------------------------*/

#if CZ80_ENCRYPTED_ROM
void Cz80_Set_Encrypt_Range(cz80_struc *CPU, uint32_t low_adr, uint32_t high_adr, uint32_t decrypted_rom)
{
	int i, j;

	i = low_adr >> CZ80_FETCH_SFT;
	j = high_adr >> CZ80_FETCH_SFT;
	decrypted_rom -= i << CZ80_FETCH_SFT;

	while (i <= j)
	{
		CPU->OPFetch[i] = (int32_t)decrypted_rom - (int32_t)CPU->Fetch[i];
		i++;
	}
}
#endif


/*--------------------------------------------------------
	Set Memory Read/Write Functions
--------------------------------------------------------*/

void Cz80_Set_ReadB(cz80_struc *CPU, uint8_t (*Func)(uint32_t address))
{
	CPU->Read_Byte = Func;
}

void Cz80_Set_WriteB(cz80_struc *CPU, void (*Func)(uint32_t address, uint8_t data))
{
	CPU->Write_Byte = Func;
}


/*--------------------------------------------------------
	Set Port Read/Write Functions
--------------------------------------------------------*/

void Cz80_Set_INPort(cz80_struc *CPU, uint8_t (*Func)(uint16_t port))
{
	CPU->IN_Port = Func;
}

void Cz80_Set_OUTPort(cz80_struc *CPU, void (*Func)(uint16_t port, uint8_t value))
{
	CPU->OUT_Port = Func;
}


/*--------------------------------------------------------
	Set Callback Functions
--------------------------------------------------------*/

void Cz80_Set_IRQ_Callback(cz80_struc *CPU, int32_t (*Func)(int32_t irqline))
{
	CPU->Interrupt_Callback = Func;
}
