/****************************************************************************
 * sched/sched/sched_roundrobin.c
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
#include <stdbool.h>
#include <sched.h>
#include <sys/param.h>
#include <assert.h>

#include <nuttx/sched.h>
#include <nuttx/clock.h>

#include "sched/sched.h"

#if CONFIG_RR_INTERVAL > 0

#ifdef CONFIG_SMP

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_SMP
static struct smp_call_data_s g_call_data;
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct roundrobin_arg_s
{
  pid_t pid;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int nxsched_roundrobin_handler(FAR void *cookie)
{
  pid_t pid = (uintptr_t)cookie;
  FAR struct tcb_s *tcb;
  irqstate_t flags;

  flags = enter_critical_section();
  tcb = nxsched_get_tcb(pid);

  if (!tcb || tcb->task_state == TSTATE_TASK_INVALID ||
      (tcb->flags & TCB_FLAG_EXIT_PROCESSING) != 0)
    {
      /* There is no TCB with this pid or, if there is, it is not a task. */

      leave_critical_section(flags);
      return OK;
    }

  if (tcb->task_state == TSTATE_TASK_RUNNING && tcb->cpu == this_cpu() &&
      nxsched_reprioritize_rtr(tcb, tcb->sched_priority))
    {
      up_switch_context(this_task(), tcb);
    }

  leave_critical_section(flags);
  return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name:  nxsched_process_roundrobin
 *
 * Description:
 *   Check if the currently executing task has exceeded its time slice.
 *
 * Input Parameters:
 *   tcb - The TCB of the currently executing task
 *   ticks - The number of ticks that have elapsed on the interval timer.
 *   noswitches - True: Can't do context switches now.
 *
 * Returned Value:
 *   The number if ticks remaining until the next time slice expires.
 *   Zero is returned if there is no time slicing (i.e., the task at the
 *   head of the ready-to-run list does not support round robin
 *   scheduling).
 *
 *   The value one may returned under certain circumstances that probably
 *   can't happen.  The value one is the minimal timer setup and it means
 *   that a context switch is needed now, but cannot be performed because
 *   noswitches == true.
 *
 * Assumptions:
 *   - Interrupts are disabled
 *   - The task associated with TCB uses the round robin scheduling
 *     policy
 *
 ****************************************************************************/

uint32_t nxsched_process_roundrobin(FAR struct tcb_s *tcb, uint32_t ticks,
                                    bool noswitches)
{
  uint32_t ret;
  int decr;

  /* How much can we decrement the timeslice delay?  If 'ticks' is greater
   * than the timeslice value, then we ignore any excess amount.
   *
   * 'ticks' should not be greater than the remaining timeslice.  But that
   * event seems to be possible, perhaps in cases where pre-emption has been
   * disabled or the noswitches flag is set.  This might cause jitter of a
   * few ticks in the slicing because the excess amount is not handled.
   */

  DEBUGASSERT(tcb != NULL);
  decr = MIN(tcb->timeslice, ticks);

  /* Decrement the timeslice counter */

  tcb->timeslice -= decr;

  /* Did decrementing the timeslice counter cause the timeslice to expire? */

  ret = tcb->timeslice;
  if (tcb->timeslice <= 0 && !nxsched_islocked_tcb(tcb))
    {
      /* We will also suppress context switches if we were called via one
       * of the unusual cases handled by nxsched_reassess_timer().  In that
       * case, we will return a value of one so that the timer will expire
       * as soon as possible and we can perform this action in the normal
       * timer expiration context.
       *
       * This is kind of kludge, but I am not to concerned because I hope
       * that the situation is impossible or at least could only occur on
       * rare corner-cases.
       */

      if (noswitches)
        {
          ret = 1;
        }
      else
        {
          /* Reset the timeslice. */

          tcb->timeslice = MSEC2TICK(CONFIG_RR_INTERVAL);
          ret = tcb->timeslice;

          /* We know we are at the head of the ready to run prioritized
           * list.  We must be the highest priority task eligible for
           * execution.  Check the next task in the ready to run list.  If
           * it is the same priority, then we need to relinquish the CPU and
           * give that task a shot.
           */

          if (tcb->flink &&
              tcb->flink->sched_priority >= tcb->sched_priority)
            {
              FAR struct tcb_s *rtcb = this_task();

              /* Just resetting the task priority to its current value.
               * This will cause the task to be rescheduled behind any
               * other tasks at the same priority.
               */

#ifdef CONFIG_SMP
              if (tcb->task_state == TSTATE_TASK_RUNNING &&
                  tcb->cpu != this_cpu())
                {
                  nxsched_smp_call_init(&g_call_data,
                                        nxsched_roundrobin_handler,
                                        (FAR void *)(uintptr_t)tcb->pid);
                  nxsched_smp_call_single_async(tcb->cpu, &g_call_data);
                }
              else
#endif
              if (nxsched_reprioritize_rtr(tcb, tcb->sched_priority))
                {
                  up_switch_context(this_task(), rtcb);
                }
            }
        }
    }

  return ret;
}

#endif /* CONFIG_RR_INTERVAL > 0 */
