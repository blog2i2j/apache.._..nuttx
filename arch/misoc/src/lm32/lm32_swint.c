/****************************************************************************
 * arch/misoc/src/lm32/lm32_swint.c
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
#include <string.h>
#include <syscall.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/sched.h>
#include <arch/irq.h>

#include "signal/signal.h"
#include "lm32.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: dispatch_syscall
 *
 * Description:
 *   Call the stub function corresponding to the system call.
 *
 ****************************************************************************/

#ifdef CONFIG_BUILD_KERNEL
static void dispatch_syscall(void) naked_function;
static void dispatch_syscall(void)
{
#error "Missing logic"

  /* Refer to arch/arm/src/armv7-m/up_svcall.h for how this is done for ARM */

#if 0 /* REVISIT */
  __asm__ __volatile__
  (

      /* Save registers */

      /* Get the base of the stub lookup table */

      /* Get the offset of the stub for this syscall */

      /* Load the entry of the stub for this syscall */

      /* Call the stub */

      /* Restore registers */

      /* Return from the syscall */

  );
#endif
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lm32_swint
 *
 * Description:
 *   This is software interrupt exception handler that performs context
 *   switching and manages system calls
 *
 ****************************************************************************/

int lm32_swint(int irq, void *context, void *arg)
{
  uint32_t *regs = (uint32_t *)context;

  DEBUGASSERT(regs != NULL && regs == up_current_regs());
  up_set_current_regs(regs);

  /* Software interrupt 0 is invoked with REG_A0 (REG_X10) = system call
   * command and REG_A1-6 = variable number of
   * arguments depending on the system call.
   */

#ifdef CONFIG_DEBUG_SYSCALL_INFO
  svcinfo("Entry: regs: %p cmd: %d\n", regs, regs[REG_A0]);
  up_dump_register(regs);
#endif

  /* Handle the SWInt according to the command in $a0 */

  switch (regs[REG_A0])
    {
      /* A0=SYS_save_context:  This is a save context command:
       *
       *   int up_saveusercontext(void *saveregs);
       *
       * At this point, the following values are saved in context:
       *
       *   A0 = SYS_save_context
       *   A1 = saveregs
       *
       * In this case, we simply need to copy the current registers to the
       * save register space references in the saved R1 and return.
       */

      case SYS_save_context:
        {
          DEBUGASSERT(regs[REG_A1] != 0);
          lm32_copystate((uint32_t *)regs[REG_A1], regs);
        }
        break;

      /* A0=SYS_restore_context: This a restore context command:
       *
       *   void up_fullcontextrestore(uint32_t *restoreregs)
       *     noreturn_function;
       *
       * At this point, the following values are saved in context:
       *
       *   A0 = SYS_restore_context
       *   A1 = restoreregs
       *
       * In this case, we simply need to set g_current_regs to restore
       * register area referenced in the saved R1. context == g_current_regs
       * is the normal exception return.  By setting g_current_regs =
       * context[R1], we force the return to the saved context referenced in
       * $a1.
       */

      case SYS_restore_context:
        {
          DEBUGASSERT(regs[REG_A1] != 0);
          up_set_current_regs((uint32_t *)regs[REG_A1]);
        }
        break;

      /* A0=SYS_switch_context: This a switch context command:
       *
       *   void misoc_switchcontext(uint32_t *saveregs,
       *                            uint32_t *restoreregs);
       *
       * At this point, the following values are saved in context:
       *
       *   A0 = SYS_switch_context
       *   A1 = saveregs
       *   A2 = restoreregs
       *
       * In this case, we save the context registers to the save register
       * area referenced by the saved contents of R5 and then set
       * g_current_regs to the save register area referenced by the saved
       * contents of R6.
       */

      case SYS_switch_context:
        {
          DEBUGASSERT(regs[REG_A1] != 0 && regs[REG_A2] != 0);
          lm32_copystate((uint32_t *)regs[REG_A1], regs);
          up_set_current_regs((uint32_t *)regs[REG_A2]);
        }
        break;

      /* A0=SYS_syscall_return: This a switch context command:
       *
       *   void up_sycall_return(void);
       *
       * At this point, the following values are saved in context:
       *
       *   A0 = SYS_syscall_return
       *
       * We need to restore the saved return address and return in
       * unprivileged thread mode.
       */

#ifdef CONFIG_BUILD_KERNEL
      case SYS_syscall_return:
        {
          struct tcb_s *rtcb = this_task();
          int index = (int)rtcb->xcp.nsyscalls - 1;

          /* Make sure that there is a saved syscall return address. */

          DEBUGASSERT(index >= 0);

          /* Setup to return to the saved syscall return address in
           * the original mode.
           */

          up_current_regs()[REG_EPC] = rtcb->xcp.syscall[index].sysreturn;
#error "Missing logic -- need to restore the original mode"
          rtcb->xcp.nsyscalls     = index;

          /* Handle any signal actions that were deferred while processing
           * the system call.
           */

          rtcb->flags            &= ~TCB_FLAG_SYSCALL;
          (void)nxsig_unmask_pendingsignal();
        }
        break;
#endif

      /* This is not an architecture-specify system call.  If NuttX is built
       * as a standalone kernel with a system call interface, then all of the
       * additional system calls must be handled as in the default case.
       */

      default:
        {
#ifdef CONFIG_BUILD_KERNEL
          struct tcb_s *rtcb = this_task();
          int index = rtcb->xcp.nsyscalls;

          /* Verify that the SYS call number is within range */

          DEBUGASSERT(up_current_regs()[REG_A0] < SYS_maxsyscall);

          /* Make sure that we got here that there is a no saved syscall
           * return address.  We cannot yet handle nested system calls.
           */

          DEBUGASSERT(index < CONFIG_SYS_NNEST);

          /* Setup to return to dispatch_syscall in privileged mode. */

          rtcb->xcpsyscall[index].sysreturn = regs[REG_EPC];
#error "Missing logic -- Need to save mode"
          rtcb->xcp.nsyscalls  = index + 1;

          regs[REG_EPC]        = (uint32_t)dispatch_syscall;
#error "Missing logic -- Need to set privileged mode"

          /* Offset R0 to account for the reserved values */

          up_current_regs()[REG_A0] -= CONFIG_SYS_RESERVED;

          /* Indicate that we are in a syscall handler. */

          rtcb->flags            |= TCB_FLAG_SYSCALL;
#else
          svcerr("ERROR: Bad SYS call: %d\n", regs[REG_A0]);
#endif
        }
        break;
    }

  /* Report what happened.  That might difficult in the case of a context
   * switch.
   */

#ifdef CONFIG_DEBUG_SYSCALL_INFO
  if (regs != up_current_regs())
    {
      svcinfo("SWInt Return: Context switch!\n");
      up_dump_register(up_current_regs());
    }
  else
    {
      svcinfo("SWInt Return: %d\n", regs[REG_A0]);
    }
#endif

  return OK;
}
