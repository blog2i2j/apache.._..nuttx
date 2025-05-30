/****************************************************************************
 * arch/renesas/src/m16c/m16c_timerisr.c
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

#include <stdint.h>
#include <time.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <arch/board/board.h>

#include "clock/clock.h"
#include "renesas_internal.h"
#include "chip.h"
#include "m16c_timer.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Configuration */

#ifndef M16C_TA0_PRIO              /* Timer A0 interrupt priority */
#  define M16C_TA0_PRIO   5
#endif

/* Determine the ideal preload value for the timer.
 *
 * For example, given a 20MHz input frequency and a desired 100 Hz, clock,
 * the ideal reload value would be:
 *
 *  20,000,000 / 100 = 200,000
 *
 * The ideal prescaler value would be the one, then that drops this to
 * exactly 66535:
 *
 *  M16C_IDEAL_PRESCALER = 200,000 / 65535 = 3.05
 *
 * And any value greater than 3.05 would also work with less and less
 * precision.
 * The following calculation will produce the ideal prescaler as the next
 * integer value above any fractional values:
 */

#define M16C_DIVISOR (65535 * CLK_TCK)
#define M16C_IDEAL_PRESCALER \
  ((M16C_XIN_FREQ + M16C_DIVISOR - 1) / M16C_DIVISOR)

/* Now, given this ideal prescaler value,
 * pick between available choices: 1, 8, and 32.
 */

#if M16C_IDEAL_PRESCALER > 8
#  define M16C_PRESCALE_VALUE 32
#  define M16C_PRESCALE_BITS TAnMR_TCK_TMF32
#elif M16C_IDEL_PRESCALER > 1
#  define M16C_PRESCALE_VALUE 8
#  define M16C_PRESCALE_BITS TAnMR_TCK_TMF8
#else
#  define M16C_PRESCALE_VALUE 1
#  define M16C_PRESCALE_BITS TAnMR_TCK_TMF1
#endif

/* Timer 0 Mode Settings */

#define M16C_TA0MODE_CONFIG \
  (TAnMR_TMOD_TIMER|TAnMR_MR_TMNOOUT|TAnMR_MR_TMNOGATE|M16C_PRESCALE_BITS)

/* The actual reload value matching the selected prescaler value */

#define M16C_RELOAD_VALUE \
  ((M16C_XIN_FREQ / M16C_PRESCALE_VALUE / CLK_TCK)  - 1)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Function:  m16c_timerisr
 *
 * Description:
 *   The timer ISR will perform a variety of services for various portions
 *   of the systems.
 *
 ****************************************************************************/

static int m16c_timerisr(int irq, uint32_t *regs, void *arg)
{
  /* Process timer interrupt */

  nxsched_process_timer();
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Function:  up_timer_initialize
 *
 * Description:
 *   This function is called during start-up to initialize
 *   the timer interrupt.
 *
 ****************************************************************************/

void up_timer_initialize(void)
{
  /* Make sure that no timers are running and that all timer interrupts are
   * disabled.
   */

  putreg8(0, M16C_TABSR);
  putreg8(0, M16C_TA0IC);
  putreg8(0, M16C_TA1IC);
  putreg8(0, M16C_TA2IC);
  putreg8(0, M16C_TA3IC);
  putreg8(0, M16C_TA4IC);
  putreg8(0, M16C_TB0IC);
  putreg8(0, M16C_TB1IC);
  putreg8(0, M16C_TB2IC);

  /* Set up timer 0 mode register for timer mode with the calculated
   * prescaler value
   */

  putreg8(M16C_TA0MODE_CONFIG, M16C_TA0MR);

  /* Set the calculated reload value */

  putreg16(M16C_RELOAD_VALUE, M16C_TA0);

  /* Attach the interrupt handler */

  irq_attach(M16C_SYSTIMER_IRQ, (xcpt_t)m16c_timerisr, NULL);

  /* Enable timer interrupts */

  putreg8(1, M16C_TA0IC);

  /* Set the interrupt priority */

  putreg8(M16C_TA0_PRIO, M16C_TA0IC);

  /* Start the timer */

  putreg8(TABSR_TA0S, M16C_TABSR);
}
