/******************************************************************************

	m68000.c

	M68000 CPUインタフェース関数

******************************************************************************/

#include "emumain.h"

#include "fame.h"
M68K_CONTEXT F68K;
int F68K_irqstate;

/******************************************************************************
	CPS2暗号化ROM用
******************************************************************************/

#if (EMU_SYSTEM == CPS2)
static uint32_t m68k_encrypt_start;
static uint32_t m68k_encrypt_end;
static uint8_t  *m68k_decrypted_rom;

/*--------------------------------------------------------
	暗号化ROM範囲設定
--------------------------------------------------------*/

void m68000_set_encrypted_range(uint32_t start, uint32_t end, void *decrypted_rom)
{
	m68k_encrypt_start = start;
	m68k_encrypt_end   = end;
	m68k_decrypted_rom = (uint8_t *)decrypted_rom;
}


/*--------------------------------------------------------
	PC依存メモリリード (byte)
--------------------------------------------------------*/

static uint8_t m68000_read_pcrelative_8(uint32_t offset)
{
	if (offset >= m68k_encrypt_start && offset <= m68k_encrypt_end)
		return m68k_decrypted_rom[offset ^ 1];
	else
		return m68000_read_memory_8(offset);
}


/*--------------------------------------------------------
	PC依存メモリリード (word)
--------------------------------------------------------*/

static uint16_t m68000_read_pcrelative_16(uint32_t offset)
{
	if (offset >= m68k_encrypt_start && offset <= m68k_encrypt_end)
		return *(uint16_t *)&m68k_decrypted_rom[offset];
	else
		return m68000_read_memory_16(offset);
}
#endif


/******************************************************************************
	M68000インタフェース関数
******************************************************************************/

/*--------------------------------------------------------
	CPU初期化
--------------------------------------------------------*/

void m68000_set_fetch(unsigned start_addr, unsigned end_addr, uintptr_t ptr)
{
  // setup FAME fetchmap
  M68K_CONTEXT *ctx = &F68K;
  uintptr_t addr;
  int i;

  int shiftout = 24 - FAMEC_FETCHBITS;
  i = start_addr >> shiftout;
  addr = (uptr)ptr - (i << shiftout);
  for (; i <= (end_addr >> shiftout); i++)
    ctx->Fetch[i] = addr;
}

static void fame_irq_callback(unsigned level)
{
  if (F68K_irqstate == HOLD_LINE) {
    F68K_irqstate = CLEAR_LINE;
  }
  F68K.interrupts[0] = 0;
}

static uint32_t m68000_read_memory_32(uint32_t offset)
{
  return (m68000_read_memory_16(offset)<<16) | m68000_read_memory_16(offset+2);
}

static void m68000_write_memory_32(uint32_t offset, uint32_t val)
{
  m68000_write_memory_16(offset, val>>16);
  m68000_write_memory_16(offset+2, val);
}

void m68000_init(void)
{
	fm68k_init();

	F68K.read_byte = (unsigned char (*)(unsigned int))m68000_read_memory_8;
	F68K.read_word = (unsigned short (*)(unsigned int))m68000_read_memory_16;
	F68K.read_long = (unsigned int (*)(unsigned int))m68000_read_memory_32;
	F68K.write_byte = (void (*)(unsigned int, unsigned char))m68000_write_memory_8;
	F68K.write_word = (void (*)(unsigned int, unsigned short))m68000_write_memory_16;
	F68K.write_long = (void (*)(unsigned int, unsigned int))m68000_write_memory_32;
	F68K.iack_handler = fame_irq_callback;

#if (EMU_SYSTEM == CPS1)
	m68000_set_fetch(0x000000, 0x1fffff, (uintptr_t)memory_region_cpu1);
	m68000_set_fetch(0x900000, 0x92ffff, (uintptr_t)cps1_gfxram);
	m68000_set_fetch(0xff0000, 0xffffff, (uintptr_t)cps1_ram);
#elif (EMU_SYSTEM == CPS2)
	if (memory_length_user1)
		m68000_set_fetch(0x000000, 0x3fffff, (uintptr_t)memory_region_user1);
	else
		m68000_set_fetch(0x000000, 0x3fffff, (uintptr_t)memory_region_cpu1);
	m68000_set_fetch(0x660000, 0x663fff, (uintptr_t)cps2_ram);
	m68000_set_fetch(0x900000, 0x92ffff, (uintptr_t)cps1_gfxram);
	m68000_set_fetch(0xff0000, 0xffffff, (uintptr_t)cps1_ram);
	if (memory_length_user1)
	{
		C68k_Set_ReadB_PC_Relative(&C68K, m68000_read_pcrelative_8);
		C68k_Set_ReadW_PC_Relative(&C68K, m68000_read_pcrelative_16);
	}
#elif (EMU_SYSTEM == MVS)
	m68000_set_fetch(0x000000, 0x0fffff, (uintptr_t)memory_region_cpu1);
	m68000_set_fetch(0x100000, 0x10ffff, (uintptr_t)neogeo_ram);
	if (memory_length_cpu1 > 0x100000)
		m68000_set_fetch(0x200000, 0x2fffff, (uintptr_t)&memory_region_cpu1[0x100000]);
	else
		m68000_set_fetch(0x200000, 0x2fffff, (uintptr_t)memory_region_cpu1);
	m68000_set_fetch(0xc00000, 0xc00000 + (memory_length_user1 - 1), (uintptr_t)memory_region_user1);
#elif (EMU_SYSTEM == NCDZ)
	m68000_set_fetch(0x000000, 0x1fffff, (uintptr_t)memory_region_cpu1);
	m68000_set_fetch(0xc00000, 0xc7ffff, (uintptr_t)memory_region_user1);
	fm68k_reset(&F68K);
#endif
}


/*--------------------------------------------------------
	CPUリセット
--------------------------------------------------------*/

void m68000_reset(void)
{
	fm68k_reset(&F68K);
}


/*--------------------------------------------------------
	CPU停止
--------------------------------------------------------*/

void m68000_exit(void)
{
}


/*--------------------------------------------------------
	CPU実行
--------------------------------------------------------*/

int m68000_execute(int cycles)
{
	return fm68k_emulate(&F68K, cycles, fm68k_reason_emulate);
}


/*--------------------------------------------------------
	CPU実行 (NEOGEO CDZ専用: ロード画面用)
--------------------------------------------------------*/

#if (EMU_SYSTEM == NCDZ)
void m68000_execute2(uint32_t start, uint32_t break_point)
{
	int nest_counter = 0;
	uint32_t pc, old_pc, opcode;
	c68k_struc C68K_temp;

	old_pc = C68k_Get_Reg(&C68K, M68K_PC);

	memcpy(&C68K_temp, &C68K, sizeof(c68k_struc));

	C68k_Set_Reg(&C68K_temp, C68K_PC, start);
	C68K_temp.A[5] = 0x108000;
	C68K_temp.A[7] -= 4 * 8 * 2;

	while ((pc = C68k_Get_Reg(&C68K_temp, M68K_PC)) != break_point)
	{
		opcode = m68000_read_memory_16(pc);
		if (opcode == 0x4e75)
		{
			// rts
			nest_counter--;
			if (nest_counter < 0) break;
		}
		else if (opcode == 0x6100)
		{
			// bsr 16
			nest_counter++;
		}
		else if ((opcode & 0xff00) == 0x6100)
		{
			// bsr 8
			nest_counter++;
		}
		else if ((opcode & 0xffc0) == 0x4e80)
		{
			// jsr
			nest_counter++;
		}

		C68k_Exec(&C68K_temp, 1);
	}

	C68k_Set_Reg(&C68K, C68K_PC, old_pc);
}
#endif


/*--------------------------------------------------------
	割り込み処理
--------------------------------------------------------*/

void m68000_set_irq_line(int irqline, int state)
{
	if (irqline == IRQ_LINE_NMI)
		irqline = 7;
	if (state == CLEAR_LINE)
		irqline = 0;

	F68K.interrupts[0] = irqline;
	F68K_irqstate = state;
}


/*--------------------------------------------------------
	割り込みコールバック関数設定
--------------------------------------------------------*/

void m68000_set_irq_callback(int32_t (*callback)(int32_t irqline))
{
	//F68K.iack_handler = callback;
}


/*--------------------------------------------------------
	レジスタ取得
--------------------------------------------------------*/

uint32_t m68000_get_reg(int regnum)
{
	switch (regnum)
	{
	case M68K_PC:  return fm68k_get_pc(&F68K);
	case M68K_USP: return F68K.asp;
	case M68K_MSP: return F68K.areg[7].D;
	case M68K_SR:  return F68K.sr;
	case M68K_D0:  return F68K.dreg[0].D;
	case M68K_D1:  return F68K.dreg[1].D;
	case M68K_D2:  return F68K.dreg[2].D;
	case M68K_D3:  return F68K.dreg[3].D;
	case M68K_D4:  return F68K.dreg[4].D;
	case M68K_D5:  return F68K.dreg[5].D;
	case M68K_D6:  return F68K.dreg[6].D;
	case M68K_D7:  return F68K.dreg[7].D;
	case M68K_A0:  return F68K.areg[0].D;
	case M68K_A1:  return F68K.areg[1].D;
	case M68K_A2:  return F68K.areg[2].D;
	case M68K_A3:  return F68K.areg[3].D;
	case M68K_A4:  return F68K.areg[4].D;
	case M68K_A5:  return F68K.areg[5].D;
	case M68K_A6:  return F68K.areg[6].D;
	case M68K_A7:  return F68K.areg[7].D;
	default: return 0;
	}
}


/*--------------------------------------------------------
	レジスタ設定
--------------------------------------------------------*/

void m68000_set_reg(int regnum, uint32_t val)
{
	switch (regnum)
	{
	case M68K_PC:  F68K.pc = val;
	case M68K_USP: F68K.asp = val;
	case M68K_MSP: F68K.areg[7].D = val;
	case M68K_SR:  F68K.sr = val;
	case M68K_D0:  F68K.dreg[0].D = val;
	case M68K_D1:  F68K.dreg[1].D = val;
	case M68K_D2:  F68K.dreg[2].D = val;
	case M68K_D3:  F68K.dreg[3].D = val;
	case M68K_D4:  F68K.dreg[4].D = val;
	case M68K_D5:  F68K.dreg[5].D = val;
	case M68K_D6:  F68K.dreg[6].D = val;
	case M68K_D7:  F68K.dreg[7].D = val;
	case M68K_A0:  F68K.areg[0].D = val;
	case M68K_A1:  F68K.areg[1].D = val;
	case M68K_A2:  F68K.areg[2].D = val;
	case M68K_A3:  F68K.areg[3].D = val;
	case M68K_A4:  F68K.areg[4].D = val;
	case M68K_A5:  F68K.areg[5].D = val;
	case M68K_A6:  F68K.areg[6].D = val;
	case M68K_A7:  F68K.areg[7].D = val;
	default: break;
	}
}


/*------------------------------------------------------
	セーブ/ロード ステート
------------------------------------------------------*/

#ifdef SAVE_STATE

STATE_SAVE( m68000 )
{
	int i;
	uint32_t pc = fm68k_get_pc(&F68K);

	for (i = 0; i < 8; i++)
		state_save_long(&F68K.dreg[i], 1);
	for (i = 0; i < 8; i++)
		state_save_long(&F68K.areg[i], 1);

	state_save_long(&F68K.flag_C, 1);
	state_save_long(&F68K.flag_V, 1);
	state_save_long(&F68K.flag_Z, 1);
	state_save_long(&F68K.flag_N, 1);
	state_save_long(&F68K.flag_X, 1);
	state_save_long(&F68K.flag_I, 1);
	state_save_long(&F68K.flag_S, 1);
	state_save_long(&F68K.asp, 1);
	state_save_long(&pc, 1);
	state_save_long(&F68K.execinfo, 1);
	state_save_long(&F68K.interrupts[0], 1);
	state_save_long(&C68K.IRQState, 1);
}

STATE_LOAD( m68000 )
{
	int i;
	uint32_t pc;

	for (i = 0; i < 8; i++)
		state_load_long(&C68K.D[i], 1);
	for (i = 0; i < 8; i++)
		state_load_long(&C68K.A[i], 1);

	state_load_long(&F68K.flag_C, 1);
	state_load_long(&F68K.flag_V, 1);
	state_load_long(&F68K.flag_Z, 1);
	state_load_long(&F68K.flag_N, 1);
	state_load_long(&F68K.flag_X, 1);
	state_load_long(&F68K.flag_I, 1);
	state_load_long(&F68K.flag_S, 1);
	state_load_long(&F68K.asp 1);
	state_load_long(&pc, 1);
	state_load_long(&F68K.execinfo, 1);
	state_load_long(&F68K.interrups[0], 1);
	state_load_long(&C68K.IRQState, 1);

	F68K.pc = pc;
}

#endif /* SAVE_STATE */
