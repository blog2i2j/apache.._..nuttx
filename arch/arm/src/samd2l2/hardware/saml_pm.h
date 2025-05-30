/****************************************************************************
 * arch/arm/src/samd2l2/hardware/saml_pm.h
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

/* References:
 *   "Atmel SAM L21E / SAM L21G / SAM L21J Smart ARM-Based Microcontroller
 *   Datasheet", Atmel-42385C-SAML21_Datasheet_Preliminary-03/20/15
 */

#ifndef __ARCH_ARM_SRC_SAMD2L2_HARDWARE_SAML_PM_H
#define __ARCH_ARM_SRC_SAMD2L2_HARDWARE_SAML_PM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include "chip.h"

#ifdef CONFIG_ARCH_FAMILY_SAML21

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* PM register offsets ******************************************************/

#define SAM_PM_CTRLA_OFFSET                0x0000  /* Control A */
#define SAM_PM_SLEEPCFG_OFFSET             0x0001  /* Sleep configuration */
#define SAM_PM_PLCFG_OFFSET                0x0002  /* Performance level configuration */
#define SAM_PM_INTENCLR_OFFSET             0x0004  /* Interrupt enable clear register */
#define SAM_PM_INTENSET_OFFSET             0x0005  /* Interrupt enable set register */
#define SAM_PM_INTFLAG_OFFSET              0x0006  /* Interrupt flag status and clear register */
#define SAM_PM_STDBYCFG_OFFSET             0x0008  /* Standby configuration */

/* PM register addresses ****************************************************/

#define SAM_PM_CTRLA                       (SAM_PM_BASE+SAM_PM_CTRLA_OFFSET)
#define SAM_PM_SLEEPCFG                    (SAM_PM_BASE+SAM_PM_SLEEPCFG_OFFSET)
#define SAM_PM_PLCFG                       (SAM_PM_BASE+SAM_PM_PLCFG_OFFSET)
#define SAM_PM_INTENCLR                    (SAM_PM_BASE+SAM_PM_INTENCLR_OFFSET)
#define SAM_PM_INTENSET                    (SAM_PM_BASE+SAM_PM_INTENSET_OFFSET)
#define SAM_PM_INTFLAG                     (SAM_PM_BASE+SAM_PM_INTFLAG_OFFSET)
#define SAM_PM_STDBYCFG                    (SAM_PM_BASE+SAM_PM_STDBYCFG_OFFSET)

/* PM register bit definitions **********************************************/

/* Control A register */

#define PM_CTRLA_IORET                     (1 << 2)  /* Bit 2: I/O retention */

/* Sleep configuration register */

#define PM_SLEEPCFG_MODE_SHIFT             (0)        /* Bits 0-2: Idle Mode Configuration */
#define PM_SLEEPCFG_MODE_MASK              (7 << PM_SLEEPCFG_MODE_SHIFT)
#  define PM_SLEEPCFG_MODE_IDLE            (2 << PM_SLEEPCFG_MODE_SHIFT) /* CPU, AHBx, APBx clocks OFF */
#  define PM_SLEEPCFG_MODE_STANDBY         (4 << PM_SLEEPCFG_MODE_SHIFT) /* All clocks OFF, except sleepwalking peripherals */
#  define PM_SLEEPCFG_MODE_BACKUP          (5 << PM_SLEEPCFG_MODE_SHIFT) /* Only backup domain is powered ON */
#  define PM_SLEEPCFG_MODE_OFF             (6 << PM_SLEEPCFG_MODE_SHIFT) /* All power domains are powered OFF */

/* Performance level configuration */

#define PM_PLCFG_PLSEL_SHIFT               (0)        /* Bits 0-1: Performance level select */
#define PM_PLCFG_PLSEL_MASK                (3 << PM_PLCFG_PLSEL_SHIFT)
#  define PM_PLCFG_PLSEL_PL0               (0 << PM_PLCFG_PLSEL_SHIFT) /* Performance level 0 */
#  define PM_PLCFG_PLSEL_PL2               (2 << PM_PLCFG_PLSEL_SHIFT) /* Performance level 2 */

#define PM_PLCFG_PLDIS                     (1 << 7)  /* Bit 7:  Performance level disable */

/* Interrupt enable clear, Interrupt enable set,
 * and Interrupt flag status and clear registers
 */

#define PM_INT_PLRDY                       (1 << 0)  /* Bit 0: Performance level ready */

/* Standby configuration */

#define PM_STDBYCFG_PDCFG_SHIFT            (0)       /* Bits 0-1: Power domain configuration */
#define PM_STDBYCFG_PDCFG_MASK             (3 << PM_STDBYCFG_PDCFG_SHIFT)
#  define PM_STDBYCFG_PDCFG_DEFAULT        (0 << PM_STDBYCFG_PDCFG_SHIFT) /* All power domains handled by HW */
#  define PM_STDBYCFG_PDCFG_PD01           (1 << PM_STDBYCFG_PDCFG_SHIFT) /* PD0 ACTIVE; PD1/2 handled by HW */
#  define PM_STDBYCFG_PDCFG_PD12           (2 << PM_STDBYCFG_PDCFG_SHIFT) /* PD0/1 ACTIVE; PD2 handled by HW */
#  define PM_STDBYCFG_PDCFG_PD012          (3 << PM_STDBYCFG_PDCFG_SHIFT) /* All power domains ACTIVE */

#define PM_STDBYCFG_DPGPD0                 (1 << 4)  /* Bit 4:  Dynamic power gating for power domain 0 */
#define PM_STDBYCFG_DPGPD1                 (1 << 5)  /* Bit 5:  Dynamic power gating for power domain 1 */
#define PM_STDBYCFG_VREGSMOD_SHIFT         (6)       /* Bits 6-7: Linked power domain */
#define PM_STDBYCFG_VREGSMOD_MASK          (3 << PM_STDBYCFG_VREGSMOD_SHIFT)
#  define PM_STDBYCFG_VREGSMOD_AUTO        (0 << PM_STDBYCFG_VREGSMOD_SHIFT) /* Automatic mode */
#  define PM_STDBYCFG_VREGSMOD_PERFORMANCE (1 << PM_STDBYCFG_VREGSMOD_SHIFT) /* Performance oriented */
#  define PM_STDBYCFG_VREGSMOD_LP          (2 << PM_STDBYCFG_VREGSMOD_SHIFT) /* Low power consumption oriented */

#define PM_STDBYCFG_LINKPD_SHIFT           (8)       /* Bits 8-9: */
#define PM_STDBYCFG_LINKPD_MASK            (3 << PM_STDBYCFG_LINKPD_SHIFT)
#  define PM_STDBYCFG_LINKPD_DEFAULT       (0 << PM_STDBYCFG_LINKPD_SHIFT) /* Power domains not linked */
#  define PM_STDBYCFG_LINKPD_PD01          (1 << PM_STDBYCFG_LINKPD_SHIFT) /* Power domains P0/1 linked */
#  define PM_STDBYCFG_LINKPD_PD12          (2 << PM_STDBYCFG_LINKPD_SHIFT) /* Power domains P1/P2 linked */
#  define PM_STDBYCFG_LINKPD_PD012         (3 << PM_STDBYCFG_LINKPD_SHIFT) /* All power domains linked */

#define PM_STDBYCFG_BBIASHS_SHIFT          (10)      /* Bits 10-11: Back bias for HMCRAMCHS */
#define PM_STDBYCFG_BBIASHS_MASK           (3 << PM_STDBYCFG_BBIASHS_SHIFT)
#  define PM_STDBYCFG_BBIASHS_RETBACK      (0 << PM_STDBYCFG_BBIASHS_SHIFT) /* Retention back biasing mode */
#  define PM_STDBYCFG_BBIASHS_STDBYBACK    (1 << PM_STDBYCFG_BBIASHS_SHIFT) /* Standby back biasing mode */
#  define PM_STDBYCFG_BBIASHS_STDBYOFF     (2 << PM_STDBYCFG_BBIASHS_SHIFT) /* Standby OFF mode */
#  define PM_STDBYCFG_BBIASHS_OFF          (3 << PM_STDBYCFG_BBIASHS_SHIFT) /* Always OFF mode */

#define PM_STDBYCFG_BBIASLP_SHIFT          (12)      /* Bits 12-13: Back bias for HMCRAMCLP */
#define PM_STDBYCFG_BBIASLP_MASK           (3 << PM_STDBYCFG_BBIASLP_SHIFT)
#  define PM_STDBYCFG_BBIASLP_RETBACK      (0 << PM_STDBYCFG_BBIASLP_SHIFT) /* Retention back biasing mode */
#  define PM_STDBYCFG_BBIASLP_STDBYBACK    (1 << PM_STDBYCFG_BBIASLP_SHIFT) /* Standby back biasing mode */
#  define PM_STDBYCFG_BBIASLP_STDBYOFF     (2 << PM_STDBYCFG_BBIASLP_SHIFT) /* Standby OFF mode */
#  define PM_STDBYCFG_BBIASLP_OFF          (3 << PM_STDBYCFG_BBIASLP_SHIFT) /* Always OFF mode */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

#endif /* CONFIG_ARCH_FAMILY_SAML21 */
#endif /* __ARCH_ARM_SRC_SAMD2L2_HARDWARE_SAML_PM_H */
