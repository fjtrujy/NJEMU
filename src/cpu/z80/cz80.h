/******************************************************************************
 *
 * CZ80 (Z80 CPU emulator) version 0.9
 * Compiled with Dev-C++
 * Copyright 2004-2005 Stéphane Dallongeville
 *
 * (Modified by NJ)
 *
 *****************************************************************************/

#ifndef CZ80_H
#define CZ80_H

// uintptr_t
#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*************************************/
/* Z80 core Structures & definitions */
/*************************************/

// NB this must have at least the value of (16-Z80_MEM_SHIFT)
#define CZ80_FETCH_BITS			6   // [4-12]   default = 8

#define CZ80_FETCH_SFT			(16 - CZ80_FETCH_BITS)
#define CZ80_FETCH_BANK			(1 << CZ80_FETCH_BITS)

#define CZ80_LITTLE_ENDIAN		__BYTE_ORDER == __LITTLE_ENDIAN
#define CZ80_USE_JUMPTABLE		1
#define CZ80_BIG_FLAGS_ARRAY	1
#ifdef BUILD_CPS1
#define CZ80_ENCRYPTED_ROM		1
#else
#define CZ80_ENCRYPTED_ROM		0
#endif
#define CZ80_EMULATE_R_EXACTLY	1

#define zR8(A)		(*CPU->pzR8[A])
#define zR16(A)		(CPU->pzR16[A]->W)

#define pzFA		&(CPU->FA)
#define zFA			CPU->FA.W
#define zlFA		CPU->FA.B.L
#define zhFA		CPU->FA.B.H
#define zA			zlFA
#define zF			zhFA

#define pzBC		&(CPU->BC)
#define zBC			CPU->BC.W
#define zlBC		CPU->BC.B.L
#define zhBC		CPU->BC.B.H
#define zB			zhBC
#define zC			zlBC

#define pzDE		&(CPU->DE)
#define zDE			CPU->DE.W
#define zlDE		CPU->DE.B.L
#define zhDE		CPU->DE.B.H
#define zD			zhDE
#define zE			zlDE

#define pzHL		&(CPU->HL)
#define zHL			CPU->HL.W
#define zlHL		CPU->HL.B.L
#define zhHL		CPU->HL.B.H
#define zH			zhHL
#define zL			zlHL

#define zFA2		CPU->FA2.W
#define zlFA2		CPU->FA2.B.L
#define zhFA2		CPU->FA2.B.H
#define zA2			zhFA2
#define zF2			zlFA2

#define zBC2		CPU->BC2.W
#define zDE2		CPU->DE2.W
#define zHL2		CPU->HL2.W

#define pzIX		&(CPU->IX)
#define zIX			CPU->IX.W
#define zlIX		CPU->IX.B.L
#define zhIX		CPU->IX.B.H

#define pzIY		&(CPU->IY)
#define zIY			CPU->IY.W
#define zlIY		CPU->IY.B.L
#define zhIY		CPU->IY.B.H

#define pzSP		&(CPU->SP)
#define zSP			CPU->SP.W
#define zlSP		CPU->SP.B.L
#define zhSP		CPU->SP.B.H

#define zRealPC		(PC - CPU->BasePC)
#define zPC			PC

#define zI			CPU->I
#define zIM			CPU->IM

#define zwR			CPU->R.W
#define zR1			CPU->R.B.L
#define zR2			CPU->R.B.H
#define zR			zR1

#define zIFF		CPU->IFF.W
#define zIFF1		CPU->IFF.B.L
#define zIFF2		CPU->IFF.B.H

#define CZ80_SF_SFT	 7
#define CZ80_ZF_SFT	 6
#define CZ80_YF_SFT	 5
#define CZ80_HF_SFT	 4
#define CZ80_XF_SFT	 3
#define CZ80_PF_SFT	 2
#define CZ80_VF_SFT	 2
#define CZ80_NF_SFT	 1
#define CZ80_CF_SFT	 0

#define CZ80_SF		(1 << CZ80_SF_SFT)
#define CZ80_ZF		(1 << CZ80_ZF_SFT)
#define CZ80_YF		(1 << CZ80_YF_SFT)
#define CZ80_HF		(1 << CZ80_HF_SFT)
#define CZ80_XF		(1 << CZ80_XF_SFT)
#define CZ80_PF		(1 << CZ80_PF_SFT)
#define CZ80_VF		(1 << CZ80_VF_SFT)
#define CZ80_NF		(1 << CZ80_NF_SFT)
#define CZ80_CF		(1 << CZ80_CF_SFT)

#define CZ80_IFF_SFT	CZ80_PF_SFT
#define CZ80_IFF		CZ80_PF

#define	CZ80_HAS_INT	0x1
#define	CZ80_HAS_NMI	0x2
#define	CZ80_HALTED	0x4

#ifndef IRQ_LINE_STATE
#define IRQ_LINE_STATE
#define CLEAR_LINE		0		/* clear (a fired, held or pulsed) line */
#define ASSERT_LINE		1		/* assert an interrupt immediately */
#define HOLD_LINE		2		/* hold interrupt line until acknowledged */
#define PULSE_LINE		3		/* pulse interrupt line for one instruction */
#define IRQ_LINE_NMI	127		/* IRQ line for NMIs */
#endif

enum
{
	CZ80_PC = 1,
	CZ80_SP,
	CZ80_FA,
	CZ80_BC,
	CZ80_DE,
	CZ80_HL,
	CZ80_IX,
	CZ80_IY,
	CZ80_FA2,
	CZ80_BC2,
	CZ80_DE2,
	CZ80_HL2,
	CZ80_R,
	CZ80_I,
	CZ80_IM,
	CZ80_IFF1,
	CZ80_IFF2,
	CZ80_HALT,
	CZ80_IRQ
};

typedef union
{
	struct
	{
#if CZ80_LITTLE_ENDIAN
		uint8_t L;
		uint8_t H;
#else
		uint8_t H;
		uint8_t L;
#endif
	} B;
	uint16_t W;
} union16;

typedef struct cz80_t
{
	union
	{
		uint8_t r8[8];
		union16 r16[4];
		struct
		{
			union16 BC;
			union16 DE;
			union16 HL;
			union16 FA;
		};
	};

	union16 IX;
	union16 IY;
	union16 SP;
	uint32_t unusedPC;	/* left for binary compat */

	union16 BC2;
	union16 DE2;
	union16 HL2;
	union16 FA2;

	union16 R;
	union16 IFF;

	uint8_t I;
	uint8_t IM;
	uint8_t Status;
	uint8_t dummy;

	int32_t IRQLine;
	int32_t IRQState;
	int32_t ICount;
	int32_t ExtraCycles;

	uintptr_t BasePC;
	uintptr_t PC;
	uintptr_t Fetch[CZ80_FETCH_BANK];
#if CZ80_ENCRYPTED_ROM
	uintptr_t OPBase;
	uintptr_t OPFetch[CZ80_FETCH_BANK];
#endif

	uint8_t *pzR8[8];
	union16 *pzR16[4];

	uint8_t   (*Read_Byte)(uint32_t address);
	void (*Write_Byte)(uint32_t address, uint8_t data);

	uint8_t   (*IN_Port)(uint16_t port);
	void (*OUT_Port)(uint16_t port, uint8_t value);

	int32_t  (*Interrupt_Callback)(int32_t irqline);

} cz80_struc;


/*************************/
/* Publics Z80 variables */
/*************************/

extern cz80_struc CZ80;

/*************************/
/* Publics Z80 functions */
/*************************/

void Cz80_Init(cz80_struc *CPU);

void Cz80_Reset(cz80_struc *CPU);

int32_t  Cz80_Exec(cz80_struc *CPU, int32_t cycles);

void Cz80_Set_IRQ(cz80_struc *CPU, int32_t line, int32_t state);

uint32_t  Cz80_Get_Reg(cz80_struc *CPU, int32_t regnum);
void Cz80_Set_Reg(cz80_struc *CPU, int32_t regnum, uint32_t value);

void Cz80_Set_Fetch(cz80_struc *CPU, uint32_t low_adr, uint32_t high_adr, uintptr_t fetch_adr);
#if CZ80_ENCRYPTED_ROM
void Cz80_Set_Encrypt_Range(cz80_struc *CPU, uint32_t low_adr, uint32_t high_adr, uintptr_t decrypted_rom);
#endif

void Cz80_Set_ReadB(cz80_struc *CPU, uint8_t (*Func)(uint32_t address));
void Cz80_Set_WriteB(cz80_struc *CPU, void (*Func)(uint32_t address, uint8_t data));

void Cz80_Set_INPort(cz80_struc *CPU, uint8_t (*Func)(uint16_t port));
void Cz80_Set_OUTPort(cz80_struc *CPU, void (*Func)(uint16_t port, uint8_t value));

void Cz80_Set_IRQ_Callback(cz80_struc *CPU, int32_t (*Func)(int32_t irqline));

#ifdef __cplusplus
};
#endif

#endif	/* CZ80_H */
