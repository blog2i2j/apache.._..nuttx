/************************************************************************************
 * arch/hc/src/m9s12/m9s12_vectors.S
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
 ************************************************************************************/

/************************************************************************************
 * Included Files
 ************************************************************************************/

#include <nuttx/config.h>
#include <arch/irq.h>

/************************************************************************************
 * Pre-processor Definitions
 ************************************************************************************/

#ifdef CONFIG_HCS12_NONBANKED
#  define CALL   jsr
#  define RETURN rts
#else
#  define CALL   call
#  define RETURN rtc
#endif

/************************************************************************************
 * Public Symbols
 ************************************************************************************/

	.globl		__start
	.globl		hc_doirq
	.globl		hc_fullcontextrestore
	.file		"m9s12_vectors.S"

/************************************************************************************
 * Macros
 ************************************************************************************/

/* On entry into an I- or X-interrupt, into an SWI, or into an undefined instruction
 * interrupt, the stack frame created by hardware looks like:
 *
 * Low Address       <-- SP after interrupt
 *              CCR
 *              B
 *              A
 *              XH
 *              XL
 *              YH
 *              YL
 *              PCH
 * High Address PCL  <-- SP before interrupt
 */

	.macro	HANDLER, label, irqno
\label:
	ldab	#\irqno							/* Load B=IRQ number */
	bra		vcommon							/* Jump to common logic */
	.endm

/************************************************************************************
 * Vectors
 ************************************************************************************/

	.section	vectors, "x"
	.align		2
	.globl		hcs12_vectors
	.type		hcs12_vectors, function

hcs12_vectors:
											/* ff80-ff9f: Reserved */
	.hword		villegal					/* ff80: Reserved*/
	.hword		villegal					/* ff82: Reserved */
	.hword		villegal					/* ff84: Reserved */
	.hword		villegal					/* ff86: Reserved */
	.hword		villegal					/* ff88: Reserved */
	.hword		villegal					/* ff8a: Reserved */
	.hword		villegal					/* ff8c: Reserved */
	.hword		villegal					/* ff9e: Reserved */
	.hword		villegal					/* ff90: Reserved */
	.hword		villegal					/* ff92: Reserved */
	.hword		villegal					/* ff94: Reserved */
	.hword		villegal					/* ff96: Reserved */
	.hword		villegal					/* ff98: Reserved */
	.hword		villegal					/* ff9a: Reserved */
	.hword		villegal					/* ff9c: Reserved */
	.hword		villegal					/* ff9e: Reserved */
	.hword		vemacec 					/* ffa0: EMAC excessive collision */
	.hword		vemaclc 					/* ffa2: EMAC late collision*/
	.hword		vemacbrxerr 				/* ffa4: MAC babbling receive error*/
	.hword		vemacrxbbo					/* ffa6: EMAC receive buffer B overrun */
	.hword		vemacrxbao					/* ffa8: EMAC receive buffer A overrun */
	.hword		vemacrxerr					/* ffaa: EMAC receive error */
	.hword		vemacmii					/* ffac: EMAC MII management transfer complete */
	.hword		vemacrxfc					/* ffae: EMAC receive flow control */
	.hword		vemactxc					/* ffb0: EMAC frame transmission complete */
	.hword		vemaccrxbbc 				/* ffb2: EMAC receive buffer B complete */
	.hword		vemaccrxbac 				/* ffb4: EMAC receive buffer A complete */
	.hword		vephy						/* ffb6: EPHY interrupt */
	.hword		vflash						/* ffb8: FLASH */
	.hword		villegal					/* ffba: Reserved */
	.hword		villegal					/* ffbc: Reserved */
	.hword		villegal					/* ffbe: Reserved */
	.hword		viic						/* ffc0: IIC bus */
	.hword		villegal					/* ffc2: Reserved */
	.hword		vcrgscm 					/* ffc4: CRG self clock mode */
	.hword		vcrgplllck					/* ffc6: CRG PLL lock */
	.hword		villegal					/* ffc8: Reserved */
	.hword		vportg						/* ffca: Port G */
	.hword		vporth						/* ffcc: Port H */
	.hword		vportj						/* ffcd: Port J */
	.hword		villegal					/* ffd0: Reserved */
	.hword		vatd						/* ffd2: ATD */
	.hword		vsci1						/* ffd4: SCI1 */
	.hword		vsci0						/* ffd6: SCI0 */
	.hword		vspi						/* ffd8: SPI */
	.hword		vtimpaie					/* ffda: Pulse accumulator input edge */
	.hword		vtimpaovf					/* ffdc: Pulse accumulator overflow */
	.hword		vtimovf 					/* ffde: Standard timer overflow */
	.hword		vtimch7 					/* ffe0: Standard timer channel 7 */
	.hword		vtimch6 					/* ffe2: Standard timer channel 6 */
	.hword		vtimch5 					/* ffe4: Standard timer channel 5 */
	.hword		vtimch4 					/* ffe6: Standard timer channel 4 */
	.hword		villegal					/* ffe8: Reserved */
	.hword		villegal					/* ffea: Reserved */
	.hword		villegal					/* ffec: Reserved */
	.hword		villegal					/* ffee: Reserved */
	.hword		vrti						/* fff0: Real-time interrupt */
	.hword		virq						/* fff2: IRQ */
	.hword		vxirq						/* fff4: XIRQ */
	.hword		vswi						/* fff6: SWI */
	.hword		vtrap						/* fff8: Unimplemented instruction trap */
	.hword		vcop						/* fffa: COP failure reset */
	.hword		vclkmon 					/* fffc: Clock monitor fail reset */
	.hword		__start 					/* fffe: Reset vector */

	.size	hcs12_vectors, .-hcs12_vectors

/************************************************************************************
 * .text
 ************************************************************************************/

	.section	nonbanked, "x"
	.type	handlers, function
handlers:

	HANDLER vemacec, HCS12_IRQ_VEMACEC			/* EMAC excessive collision */
	HANDLER vemaclc, HCS12_IRQ_VEMACLC			/* EMAC late collision */
	HANDLER vemacbrxerr, HCS12_IRQ_VEMACBRXERR	/* EMAC babbling receive error */
	HANDLER vemacrxbbo, HCS12_IRQ_VEMACRXBBO 	/* EMAC receive buffer B overrun */
	HANDLER vemacrxbao, HCS12_IRQ_VEMACRXBAO 	/* EMAC receive buffer A overrun */
	HANDLER vemacrxerr, HCS12_IRQ_VEMACRXERR 	/* EMAC receive error */
	HANDLER vemacmii, HCS12_IRQ_VEMACMII 		/* EMAC MII management transfer complete */
	HANDLER vemacrxfc, HCS12_IRQ_VEMACRXFC		/* EMAC receive flow control */
	HANDLER vemactxc, HCS12_IRQ_VEMACTXC 		/* EMAC frame transmission complete */
	HANDLER vemaccrxbbc, HCS12_IRQ_VEMACCRXBBC	/* EMAC receive buffer B complete */
	HANDLER vemaccrxbac, HCS12_IRQ_VEMACCRXBAC	/* EMAC receive buffer A complete */
	HANDLER vephy, HCS12_IRQ_VEPHY				/* EPHY interrupt */
	HANDLER vflash, HCS12_IRQ_VFLASH 			/* FLASH */
	HANDLER viic, HCS12_IRQ_VIIC 				/* IIC bus */
	HANDLER vcrgscm, HCS12_IRQ_VCRGSCM			/* CRG self clock mode */
	HANDLER vcrgplllck, HCS12_IRQ_VCRGPLLLCK 	/* CRG PLL lock */
	HANDLER vportg, HCS12_IRQ_VPORTG 			/* Port G */
	HANDLER vporth, HCS12_IRQ_VPORTH 			/* Port H */
	HANDLER vportj, HCS12_IRQ_VPORTJ 			/* Port J */
	HANDLER vatd, HCS12_IRQ_VATD 				/* ATD */
	HANDLER vsci1, HCS12_IRQ_VSCI1				/* SCI1 */
	HANDLER vsci0, HCS12_IRQ_VSCI0				/* SCI0 */
	HANDLER vspi, HCS12_IRQ_VSPI 				/* SPI */
	HANDLER vtimpaie, HCS12_IRQ_VTIMPAIE 		/* Pulse accumulator input edge */
	HANDLER vtimpaovf, HCS12_IRQ_VTIMPAOVF		/* Pulse accumulator overflow */
	HANDLER vtimovf, HCS12_IRQ_VTIMOVF			/* Standard timer overflow */
	HANDLER vtimch7, HCS12_IRQ_VTIMCH7			/* Standard timer channel 7 */
	HANDLER vtimch6, HCS12_IRQ_VTIMCH6			/* Standard timer channel 6 */
	HANDLER vtimch5, HCS12_IRQ_VTIMCH5			/* Standard timer channel 5 */
	HANDLER vtimch4, HCS12_IRQ_VTIMCH4			/* Standard timer channel 4 */
	HANDLER vrti, HCS12_IRQ_VRTI 				/* Real-time interrupt */
	HANDLER virq, HCS12_IRQ_VIRQ 				/* IRQ */
	HANDLER vxirq, HCS12_IRQ_VXIRQ				/* XIRQ */
	HANDLER vswi, HCS12_IRQ_VSWI 				/* SWI */
	HANDLER vtrap, HCS12_IRQ_VTRAP				/* Unimplemented instruction trap */
	HANDLER vcop, HCS12_IRQ_VCOP 				/* COP failure reset*/
	HANDLER vclkmon, HCS12_IRQ_VCLKMON			/* Clock monitor fail reset */
	HANDLER villegal, HCS12_IRQ_VILLEGAL 		/* Any reserved vector */

/************************************************************************************
 * Name: vcommon
 *
 * Description:
 *	 Common IRQ handling logic
 *
 *   On entry in to vcommon: (1) The interrupt stack frame is in place, and (2) the
 *   IRQ number is in B.
 *
 *   On entry into an I- or X-interrupt, into an SWI, or into an undefined
 *   instruction interrupt, the stack frame created by hardware looks like:
 *
 *   Low Address        <-- SP after interrupt
 *                CCR
 *                B
 *                A
 *                XH
 *                XL
 *                YH
 *                YL
 *                PCH
 *   High Address PCL   <-- SP before interrupt
 *
 *   This function will create the following stack frame and will call hc_doirq():
 *
 *   Low Address        <-- SP after state save
 *                [PPAGE]
 *                [soft registers]
 *                XYH
 *                XYL
 *                ZH
 *                ZL
 *                TMPH
 *                TMPL
 *                FRAMEH
 *                FRAMEL
 *                SP    <-- SP after interrupt
 *                CCR
 *                B
 *                A
 *                XH
 *                XL
 *                YH
 *                YL
 *                PCH
 *   High Address PCL   <-- SP before interrupt
 *
 ************************************************************************************/

vcommon:
	/* Save the IRQ number currently in B */

	stab	.Lbsave

	/* Save the SP at the time of the interrupt */

	tfr		sp, d
	addd	#INTFRAME_SIZE
	staa	1, -sp
	stab	1, -sp

	/* Save the rest of the frame */

	movw	_.frame, 2, -sp
	movw	_.tmp, 2, -sp
	movw	_.z, 2, -sp
	movw	_.xy, 2, -sp

	/* Save the soft registers */

#if CONFIG_HCS12_MSOFTREGS > 2
#  error "Need to save more registers"
#endif
#if CONFIG_HCS12_MSOFTREGS > 1
	movw	_.d2, 2, -sp
#endif
#if CONFIG_HCS12_MSOFTREGS > 0
	movw	_.d1, 2, -sp
#endif

	/* Save the PPAGE register */

#ifndef CONFIG_HCS12_NONBANKED
	movb	HCS12_MMC_PPAGE, 1, -sp
#endif

    /* Handle the IRQ:  AB=irqno, TOS=register save structure.  Return value in d */
	/* SP now points o the bottom of the frame - 1 */

	tfr		sp, x
	inx
	std		.Lspsave
	std		2, -sp

	/* Recover the IRQ number and call hc_doirq() */

	ldab	.Lbsave
	CALL	hc_doirq
	leas	2, sp

	/* Check if the return value in d is the same as regs parameter passed in the TOS */

	cpd		.Lspsave
	bne		hc_fullcontextrestore

	/* Restore registers and return */

	/* Restore the PPAGE register */

#ifndef CONFIG_HCS12_NONBANKED
	movb	1, sp+, HCS12_MMC_PPAGE
#endif

	/* Restore the soft registers */

#if CONFIG_HCS12_MSOFTREGS > 0
	movw	2, sp+, _.d1
#endif
#if CONFIG_HCS12_MSOFTREGS > 1
	movw	2, sp+, _.d2
#endif

	/* Skip over the saved stack pointer */

	ins
	ins

	movw	2, sp+, _.xy
	movw	2, sp+, _.z
	movw	2, sp+, _.tmp
	movw	2, sp+, _.frame
	rti
	.size	handlers, .-handlers

/************************************************************************************
 * Name: hc_fullcontextrestore
 *
 * Description:
 *   Given a pointer to a register save block that was previously created by either
 *   interrupt handler or by up_saveusercontext(), restore the context of the saved
 *   thread, thereby completing a context switch.
 *
 *   Low Address  [PPAGE]
 *                [soft registers]
 *                XYH
 *                XYL
 *                ZH
 *                ZL
 *                TMPH
 *                TMPL
 *                FRAMEH
 *                FRAMEL
 *                SP
 *                CCR
 *                B
 *                A
 *                XH
 *                XL
 *                YH
 *                YL
 *                PCH
 *   High Address PCL
 *
 * On entry:
 *   D = Address of the context switch save block
 *
 ************************************************************************************/

hc_fullcontextrestore:
	/* Make sure that interrupts are disabled */

	orcc	#0x50

	/* Exchange D with X.  Now X points to the save structure. */

	xgdx

	/* Recover PPAGE */

#ifndef CONFIG_HCS12_NONBANKED
	movb	1, x+, HCS12_MMC_PPAGE
#endif

	/* Recover _.xy, _.z, _.tmp, _.frame */

	movw	2, x+, _.xy
	movw	2, x+, _.z
	movw	2, x+, _.tmp
	movw	2, x+, _.frame

	/* Recover SP "before" the interrupt occurred */

	ldd		2, x+
	tfr		d, sp

	/* Now, create a new interrupt return frame */

	ldab	#(INTFRAME_SIZE-1)	/* Offset to PCL */
	abx							/* X now points to last byte */

	/* Copy the interrupt frame onto the stack */

	movw	2, -sp, 2, -x		/* Copy the PC */
	movw	2, -sp, 2, -x		/* Copy Y */
	movw	2, -sp, 2, -x		/* Copy X */
	movw	2, -sp, 2, -x		/* Copy A:B */
	movw	1, -sp, 1, -x		/* Copy CCR */
	rti							/* And return from interrupt */
	.size	hc_fullcontextrestore, .-hc_fullcontextrestore

/************************************************************************************
 * .bss
 ************************************************************************************/
/************************************************************************************
 *	Name: Temporaries used within vcommon
 ************************************************************************************/

	.comm	.Lbsave, 1, 1
	.comm	.Lspsave, 2, 1

/************************************************************************************
 *	Name: g_intstackalloc/g_intstacktop
 *
 * Description:
 *	 If CONFIG_ARCH_INTERRUPTSTACK is defined, this sets aside memory for the
 *   interrupt stack.
 *
 ************************************************************************************/

#if CONFIG_ARCH_INTERRUPTSTACK > 1
	.comm	.g_intstackalloc:, CONFIG_ARCH_INTERRUPTSTACK, 1
g_intstacktop:
	.size	g_intstackalloc, .-g_intstackalloc
#endif
	.end
