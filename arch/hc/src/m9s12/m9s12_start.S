/****************************************************************************
 * arch/hc/src/m9s12/m9s12_start.S
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <arch/board/board.h>

#include "m9s12.h"
#include "m9s12_mmc.h"
#include "m9s12_crg.h"
#include "m9s12_flash.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_HCS12_NONBANKED
#  define CALL   jsr
#  define RETURN rts
#else
#  define CALL   call
#  define RETURN rtc
#endif

#define INITRG_REG (MMC_INITRG_REG(HCS12_REG_BASE))
#define INITRM_MAP (MMC_INITRM_RAM(HCS12_SRAM_BASE)|MMC_INITRM_RAMHAL)
#define INITEE_EE  (MMC_INITEE_EE(HCS12_EEPROM_BASE)|MMC_INITEE_EEON)

/****************************************************************************
 * Public Symbols
 ****************************************************************************/

	.file		"m9s12_start.S"
	.globl		__start
	.globl		nx_start
	.globl		up_lowsetup
	.globl		hcs12_boardinitialize

/****************************************************************************
 * Macros
 ****************************************************************************/

/* Print a character on the UART to show boot status. This macro will
 * modify r0, r1, r2 and r14
 */

	.macro	showprogress, code
#ifdef CONFIG_DEBUG_FEATURES
	ldab	\code
#ifdef CONFIG_HCS12_SERIALMON
	jsr		#PutChar
#else
	CALL	hc_lowputc
#endif
#endif
	.endm

/* Memory map initialization.
 *
 * The MC9S12NE64 has 64K bytes of FLASH EEPROM and 8K bytes of RAM.
 */

	.macro	MMCINIT
	/* Registers are always positioned at address 0x0000 */

	movb	#INITRG_REG, HCS12_MMC_INITRG	/* Set the register map position to 0x0000*/
	nop

	/* Position SRAM so that is ends at address 0x3fff.  This is required
	 * because the Freescale serial monitor initializes its stack to the
	 * end+1 of SRAM which it expects to be at address 0x4000
 	 */

	movb	#INITRM_MAP, HCS12_MMC_INITRM	/* Set RAM position to 0x2000-0x3fff */
	movb	#INITEE_EE, HCS12_MMC_INITEE	/* Set EEPROM position to 0x0800 */

	/* In the non-banked mode, PPAGE is set to 0x3d to create a (non-contiguous),
	 * fixed, 48Kb .text address space.
	 */

#ifdef CONFIG_HCS12_NONBANKED
	movb	#0x3d, HCS12_MMC_PPAGE
#endif

	movb	#MMC_MISC_ROMON, HCS12_MMC_MISC	/* MISC: EXSTR1=0 EXSTR0=0 ROMHM=0 ROMON=1 */
	.endm

/* System clock initialization.  If the serial monitor is used, then clocking will have
 * already been configured at 24 MHz
 */

	.macro	PLLINIT
#ifndef CONFIG_HCS12_SERIALMON
	/* Select the clock source from crystal */

	clr 	HCS12_CRG_CLKSEL

	/* Set the multiplier and divider and enable the PLL */

	bclr	*HCS12_CRG_PLLCTL #CRG_PLLCTL_PLLON
	ldab	#HCS12_SYNR_VALUE
	stab	HCS12_CRG_SYNR
	ldab	#HCS12_REFDV_VALUE
	stab	HCS12_CRG_REFDV
	bset	*HCS12_CRG_PLLCTL #CRG_PLLCTL_PLLON

	/* Wait for the PLL to lock on */

.Lpll_lock:
	brclr	*HCS12_CRG_CRGFLG #CRG_CRGFLG_LOCK .Lpll_lock

	/* Then select the PLL clock source */

	bset	*HCS12_CRG_CLKSEL #CRG_CLKSEL_PLLSEL
#endif
	.endm

/****************************************************************************
 * .text
 ****************************************************************************/

	.section	nonbanked, "x"

/****************************************************************************
 * Name: __start
 *
 * Description:
 *	 Power-up reset entry point
 *
 ****************************************************************************/

__start:
	/* Hardware setup */

	MMCINIT							/* Initialize the MMC */
	PLLINIT							/* Initialize the PLL */

	/* Setup the stack pointer */

	lds		.Lstackbase

	/* Perform early, low-level initialization */

#ifndef CONFIG_HCS12_SERIALMON
	CALL	up_lowsetup
#endif
	showprogress 'A'

	/* Clear BSS */

	ldx 	.Lsbss					/* Start of .BSS */
	ldd 	.Lebss					/* End+1 of .BSS */

.Lclearbss:
	pshd
	cpx 	2,sp+					/* Check if all BSS has been cleared */
	beq 	.Lbsscleared			/* If so, exit the loop */

	clr 	0,x						/* Clear this byte */
	inx								/* Address the next byte */
	bra 	.Lclearbss				/* And loop until all cleared */
.Lbsscleared:
	showprogress 'B'

	/* Initialize the data section */

	ldx		.Lsdata					/* Start of .DATA (destination) */
	movw	.Ledata, 0, sp			/* End of .DATA (destination) */
	ldy 	.Leronly				/* Start of .DATA (source) */

.Linitdata:
	cpx 	0, sp					/* Check if all .DATA has been initialized */
	beq 	.Ldatainitialized		/* If so, exit the loop */
	ldab	0, y					/* Fetch the next byte from the source */
	iny								/* Increment the source address */
	stab	0, x					/* Store the byte to the destination */
	inx								/* Increment the destination address */
	bra 	.Linitdata				/* And loop until all of .DATA is initialized */
.Ldatainitialized:
	showprogress 'C'

	/* Perform early board-level initialization */

	CALL	hcs12_boardinitialize
	showprogress 'D'

	/* Now, start the OS */

	showprogress '\n'
	CALL	nx_start
	bra		__start

	/* Variables:
	 * _sbss is the start of the BSS region (see ld.script)
	 * _ebss is the end of the BSS region (see ld.script)
	 * The idle task stack starts at the end of BSS and is
	 * of size CONFIG_IDLETHREAD_STACKSIZE.  The heap continues
	 * from there until the end of memory.	See g_idle_topstack
	 * below.
	 */

.Lsbss:
	.hword	_sbss
.Lebss:
	.hword	_ebss
.Lstackbase:
	.hword	_ebss+CONFIG_IDLETHREAD_STACKSIZE
.Leronly:
	.hword	_eronly /* Where .data defaults are stored in FLASH */
.Lsdata:
	.hword	_sdata	/* Where .data needs to reside in SDRAM */
.Ledata:
	.hword	_edata
	.size	__start, .-__start

/************************************************************************************
 * .rodata
 ************************************************************************************/

	.section	.rodata, "a"

/* Variables: _sbss is the start of the BSS region (see ld.script) _ebss is the end
 * of the BSS region (see ld.script). The idle task stack starts at the end of BSS
 * and is of size CONFIG_IDLETHREAD_STACKSIZE.	The IDLE thread is the thread that
 * the system boots on and, eventually, becomes the idle, do nothing task that runs
 * only when there is nothing else to run.  The heap continues from there until the
 * end of memory.  See g_idle_topstack below.
 */

	.globl	g_idle_topstack
	.type	g_idle_topstack, object
g_idle_topstack:
	.hword	_ebss+CONFIG_IDLETHREAD_STACKSIZE
	.size	g_idle_topstack, .-g_idle_topstack
	.end
