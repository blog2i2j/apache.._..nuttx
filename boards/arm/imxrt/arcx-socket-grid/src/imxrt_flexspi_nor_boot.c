/****************************************************************************
 * boards/arm/imxrt/arcx-socket-grid/src/imxrt_flexspi_nor_boot.c
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

#include "imxrt_flexspi_nor_boot.h"

/****************************************************************************
 * Public Data
 ****************************************************************************/

locate_data(".boot_hdr.ivt")
const struct ivt_s g_image_vector_table =
{
  IVT_HEADER,                         /* IVT Header */
  0x60002000,                         /* Image  Entry Function */
  IVT_RSVD,                           /* Reserved = 0 */
  (uint32_t)DCD_ADDRESS,              /* Address where DCD information is stored */
  (uint32_t)BOOT_DATA_ADDRESS,        /* Address where BOOT Data Structure is stored */
  (uint32_t)&g_image_vector_table,    /* Pointer to IVT Self (absolute address */
  (uint32_t)CSF_ADDRESS,              /* Address where CSF file is stored */
  IVT_RSVD                            /* Reserved = 0 */
};

locate_data(".boot_hdr.boot_data")
const struct boot_data_s g_boot_data =
{
  FLASH_BASE,                         /* boot start location */
  (FLASH_END - FLASH_BASE),           /* size */
  PLUGIN_FLAG,                        /* Plugin flag */
  0xffffffff                          /* empty - extra data word */
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/
