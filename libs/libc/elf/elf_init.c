/****************************************************************************
 * libs/libc/elf/elf_init.c
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

#include <sys/stat.h>

#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/fs/fs.h>
#include <nuttx/lib/elf.h>

#include "elf/elf.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: libelf_fileinfo
 *
 * Description:
 *  Get the info of the ELF file
 *
 * Returned Value:
 *   0 (OK) is returned on success and a negated errno is returned on
 *   failure.
 *
 ****************************************************************************/

static inline int libelf_fileinfo(FAR struct mod_loadinfo_s *loadinfo)
{
  struct stat buf;
  int ret;

  /* Get the file stats */

  ret = _NX_STAT(loadinfo->filfd, &buf);
  if (ret < 0)
    {
      int errval = _NX_GETERRNO(ret);
      berr("ERROR: Failed to stat file: %d\n", errval);
      return -errval;
    }

  /* Return some stats info of the file in the loadinfo structure */

  loadinfo->filelen  = buf.st_size;
  loadinfo->fileuid  = buf.st_uid;
  loadinfo->filegid  = buf.st_gid;
  loadinfo->filemode = buf.st_mode;
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: libelf_initialize
 *
 * Description:
 *   This function is called to configure the library to process an ELF
 *   program binary.
 *
 * Returned Value:
 *   0 (OK) is returned on success and a negated errno is returned on
 *   failure.
 *
 ****************************************************************************/

int libelf_initialize(FAR const char *filename,
                      FAR struct mod_loadinfo_s *loadinfo)
{
  int ret;

  binfo("filename: %s loadinfo: %p\n", filename, loadinfo);

  /* Clear the load info structure */

  memset(loadinfo, 0, sizeof(struct mod_loadinfo_s));

  /* Open the binary file for reading (only) */

  loadinfo->filfd = _NX_OPEN(filename, O_RDONLY);
  if (loadinfo->filfd < 0)
    {
      int errval = _NX_GETERRNO(loadinfo->filfd);
      berr("ERROR: Failed to open ELF binary %s: %d\n", filename, errval);
      return -errval;
    }

  /* Get some stats info of the file. */

  ret = libelf_fileinfo(loadinfo);
  if (ret < 0)
    {
      berr("ERROR: libelf_fileinfo failed: %d\n", ret);
      return ret;
    }

  /* Read the ELF ehdr from offset 0 */

  ret = libelf_read(loadinfo, (FAR uint8_t *)&loadinfo->ehdr,
                    sizeof(Elf_Ehdr), 0);
  if (ret < 0)
    {
      berr("ERROR: Failed to read ELF header: %d\n", ret);
      return ret;
    }

  libelf_dumpbuffer("ELF header", (FAR const uint8_t *)&loadinfo->ehdr,
                    sizeof(Elf_Ehdr));

  /* Verify the ELF header */

  ret = libelf_verifyheader(&loadinfo->ehdr);
  if (ret < 0)
    {
      /* This may not be an error because we will be called to attempt
       * loading EVERY binary.  If libelf_verifyheader() does not recognize
       * the ELF header, it will -ENOEXEC which simply informs the system
       * that the file is not an ELF file.  libelf_verifyheader() will return
       * other errors if the ELF header is not correctly formed.
       */

      berr("ERROR: Bad ELF header: %d\n", ret);
    }

  return ret;
}
