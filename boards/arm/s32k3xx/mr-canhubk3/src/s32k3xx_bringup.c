/****************************************************************************
 * boards/arm/s32k3xx/mr-canhubk3/src/s32k3xx_bringup.c
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

/* Copyright 2022 NXP */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef CONFIG_INPUT_BUTTONS
#  include <nuttx/input/buttons.h>
#endif

#ifdef CONFIG_USERLED
#  include <nuttx/leds/userled.h>
#endif

#ifdef CONFIG_S32K3XX_FLEXCAN
#  include "s32k3xx_flexcan.h"
#endif

#ifdef CONFIG_S32K3XX_ENET
#  include "s32k3xx_emac.h"
#endif

#ifdef CONFIG_S32K3XX_QSPI
#  include <stdio.h>
#  include <nuttx/mtd/mtd.h>
#  include <nuttx/spi/qspi.h>
#  include "s32k3xx_qspi.h"
#endif

#include <arch/board/board.h>
#include "mr-canhubk3.h"

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef HAVE_MX25L
struct qspi_dev_s *g_qspi;
struct mtd_dev_s *g_mtd_fs;
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: s32k3xx_bringup
 *
 * Description:
 *   Perform architecture-specific initialization
 *
 *   CONFIG_BOARD_LATE_INITIALIZE=y :
 *     Called from board_late_initialize().
 *
 *   CONFIG_BOARD_LATE_INITIALIZE=n && CONFIG_BOARDCTL=y :
 *     Called from the NSH library
 *
 ****************************************************************************/

int s32k3xx_bringup(void)
{
  int ret = OK;
#if defined(HAVE_MX25L_LITTLEFS)
  char blockdev[32];
#elif defined(HAVE_MX25L_CHARDEV)
  char mtddev[32];
#endif /* HAVE_MX25L_LITTLEFS */

#ifdef CONFIG_S32K3XX_LPSPI
  /* Initialize SPI driver */

  ret = s32k3xx_spidev_initialize();
  if (ret < 0)
    {
      _err("s32k3xx_spidev_initialize() failed: %d\n", ret);
    }
  else
    {
      _info("s32k3xx_spidev_initialize() successful\n");
    }
#endif

#ifdef CONFIG_INPUT_BUTTONS
  /* Register the BUTTON driver */

  ret = btn_lower_initialize("/dev/buttons");
  if (ret < 0)
    {
      _err("btn_lower_initialize() failed: %d\n", ret);
    }
  else
    {
      _info("btn_lower_initialize() successful\n");
    }
#endif

#ifdef CONFIG_USERLED
  /* Register the LED driver */

  ret = userled_lower_initialize("/dev/userleds");
  if (ret < 0)
    {
      _err("userled_lower_initialize() failed: %d\n", ret);
    }
  else
    {
      _info("userled_lower_initialize() successful\n");
    }
#endif

#ifdef CONFIG_FS_PROCFS
  /* Mount the procfs file system */

  ret = nx_mount(NULL, "/proc", "procfs", 0, NULL);
  if (ret < 0)
    {
      _err("Mounting procfs at /proc failed: %d\n", ret);
    }
  else
    {
      _info("Mounting procfs at /proc successful\n");
    }
#endif

#ifdef CONFIG_S32K3XX_LPI2C
  /* Initialize I2C driver */

  ret = s32k3xx_i2cdev_initialize();
  if (ret < 0)
    {
      _err("s32k3xx_i2cdev_initialize() failed: %d\n", ret);
    }
  else
    {
      _info("s32k3xx_i2cdev_initialize() successful\n");
    }
#endif

#ifdef CONFIG_S32K3XX_PROGMEM
  struct mtd_dev_s *mtd;

  mtd = progmem_initialize();
  if (mtd == NULL)
    {
      syslog(LOG_ERR, "ERROR: progmem_initialize() failed\n");
    }

  ret = register_mtddriver("/dev/progmem0", mtd, 0755, NULL);

  if (ret != OK)
    {
      _err("register_mtddriver() failed: %d\n", ret);
    }
#  ifdef CONFIG_FS_LITTLEFS
  else
    {
      _info("register_mtddriver() successful\n");

      ret = nx_mount("/dev/progmem0", "/mnt/progmem", "littlefs", 0, NULL);

      if (ret < 0)
        {
          ret = nx_mount("/dev/progmem0", "/mnt/progmem", "littlefs", 0,
                    "forceformat");

          if (ret < 0)
            {
              _err("nx_mount() failed: %d\n", ret);
            }
          else
            {
              _info("nx_mount() successful\n");
            }
        }
    }
#  endif
#endif

#ifdef HAVE_MX25L
  /* Create an instance of the S32K3XX QSPI device driver */

  g_qspi = s32k3xx_qspi_initialize(0);
  if (!g_qspi)
    {
      _err("s32k3xx_qspi_initialize() failed\n");
    }
  else
    {
      _info("s32k3xx_qspi_initialize() successful\n");

      /* Use the QSPI device instance to initialize the MX25 device */

      g_mtd_fs = mx25rxx_initialize(g_qspi, true);
      if (!g_mtd_fs)
        {
          _err("mx25rxx_initialize() failed\n");
        }
      else
        {
          _info("mx25rxx_initialize() successful\n");

#  ifdef HAVE_MX25L_LITTLEFS
          /* Configure the device with no partition support */

          snprintf(blockdev, sizeof(blockdev), "/dev/mtdqspi%d",
                   MX25L_MTD_MINOR);

          ret = register_mtddriver(blockdev, g_mtd_fs, 0755, NULL);
          if (ret != OK)
            {
              _err("register_mtddriver() failed: %d\n", ret);
            }
          else
            {
              _info("register_mtddriver() successful\n");

              ret = nx_mount(blockdev, "/mnt/qspi", "littlefs", 0, NULL);
              if (ret < 0)
                {
                  ret = nx_mount(blockdev, "/mnt/qspi", "littlefs", 0,
                                 "forceformat");
                  if (ret < 0)
                    {
                      _err("nx_mount() failed: %d\n", ret);
                    }
                  else
                    {
                      _info("nx_mount() successful\n");
                    }
                }
            }

#  elif defined(HAVE_MX25L_NXFFS)
          /* Initialize to provide NXFFS on the N25QXXX MTD interface */

          ret = nxffs_initialize(g_mtd_fs);
          if (ret < 0)
            {
              _err("nxffs_initialize() failed: %d\n", ret);
            }
          else
            {
              _info("nxffs_initialize() successful\n");

              /* Mount the file system at /mnt/qspi */

              ret = nx_mount(NULL, "/mnt/qspi", "nxffs", 0, NULL);
              if (ret < 0)
                {
                  _err("nx_mount() failed: %d\n", ret);
                }
              else
                {
                  _info("nx_mount() successful\n");
                }
            }

#  else /* if defined(HAVE_MX25L_CHARDEV) */
          /* Use the minor number to create device paths */

          snprintf(mtddev, sizeof(mtddev), "/dev/mtd%d", MX25L_MTD_MINOR);

          /* Register the MTD driver */

          ret = register_mtddriver(mtddev, g_mtd_fs, 0755, NULL);
          if (ret < 0)
            {
              _err("register_mtddriver for %s failed: %d\n", mtddev, ret);
            }
#  endif
        }
    }
#endif

#ifdef CONFIG_S32K3XX_SELFTEST
  s32k3xx_selftest();
#endif /* CONFIG_S32K3XX_SELFTEST */

#ifdef CONFIG_NETDEV_LATEINIT
#  ifdef CONFIG_S32K3XX_ENET
  s32k3xx_netinitialize(0);
#  endif /* CONFIG_S32K3XX_ENET */
#  ifdef CONFIG_S32K3XX_FLEXCAN0
  s32k3xx_caninitialize(0);
#  endif /* CONFIG_S32K3XX_FLEXCAN0 */
#  ifdef CONFIG_S32K3XX_FLEXCAN1
  s32k3xx_caninitialize(1);
#  endif /* CONFIG_S32K3XX_FLEXCAN1 */
#  ifdef CONFIG_S32K3XX_FLEXCAN2
  s32k3xx_caninitialize(2);
#  endif /* CONFIG_S32K3XX_FLEXCAN2 */
#  ifdef CONFIG_S32K3XX_FLEXCAN3
  s32k3xx_caninitialize(3);
#  endif /* CONFIG_S32K3XX_FLEXCAN3 */
#  ifdef CONFIG_S32K3XX_FLEXCAN4
  s32k3xx_caninitialize(4);
#    ifdef CONFIG_S32K3XX_TJA1153
  s32k3xx_tja1153_initialize(4);
#    endif /* CONFIG_S32K3XX_TJA1153 */
#  endif /* CONFIG_S32K3XX_FLEXCAN4 */
#  ifdef CONFIG_S32K3XX_FLEXCAN5
  s32k3xx_caninitialize(5);
#    ifdef CONFIG_S32K3XX_TJA1153
  s32k3xx_tja1153_initialize(5);
#    endif /* CONFIG_S32K3XX_TJA1153 */
#  endif /* CONFIG_S32K3XX_FLEXCAN5 */
#endif /* CONFIG_NETDEV_LATEINIT */

  _info("MR-CANHUBK3 board bringup complete\n");

  return ret;
}
