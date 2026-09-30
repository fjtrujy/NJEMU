/******************************************************************************

	memory.h

	Core memory interface definitions. (compatible for M.A.M.E.)

******************************************************************************/

#ifndef MEMORY_H
#define MEMORY_H

#define READ8_HANDLER(name)    uint8_t name(uint32_t offset __attribute__((unused)))
#define WRITE8_HANDLER(name)   void name(uint32_t offset __attribute__((unused)), uint8_t data __attribute__((unused)))
#define READ16_HANDLER(name)	uint16_t name(uint32_t offset __attribute__((unused)), uint16_t mem_mask __attribute__((unused)))
#define WRITE16_HANDLER(name)	void name(uint32_t offset __attribute__((unused)), uint16_t data __attribute__((unused)), uint16_t mem_mask __attribute__((unused)))
#define READ32_HANDLER(name)   uint32_t name(uint32_t offset __attribute__((unused)))
#define WRITE32_HANDLER(name)  void name(uint32_t offset __attribute__((unused)), uint32_t data __attribute__((unused)))

/* ----- 16/32-bit memory accessing ----- */
#define COMBINE_DATA(varptr)		(*(varptr) = (*(varptr) & mem_mask) | (data & ~mem_mask))
#define COMBINE_SWABDATA(varptr)	(*(varptr) = (*(varptr) & (mem_mask << 8)) | (data & ~(mem_mask >> 8)))

/* ----- 16-bit memory accessing ----- */
#define ACCESSING_LSB			((mem_mask & 0x00ff) == 0)
#define ACCESSING_MSB			((mem_mask & 0xff00) == 0)

#endif /* MEMORY_H */
