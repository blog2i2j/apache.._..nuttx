# ##############################################################################
# drivers/misc/CMakeLists.txt
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################
set(SRCS)

if(CONFIG_DEV_SIMPLE_ADDRENV)
  list(APPEND SRCS addrenv.c)
endif()

if(CONFIG_DEV_NULL)
  list(APPEND SRCS dev_null.c)
endif()

if(CONFIG_DEV_ZERO)
  list(APPEND SRCS dev_zero.c)
endif()

if(CONFIG_DEV_MEM)
  list(APPEND SRCS dev_mem.c)
endif()

if(CONFIG_DEV_ASCII)
  list(APPEND SRCS dev_ascii.c)
endif()

if(CONFIG_LWL_CONSOLE)
  list(APPEND SRCS lwl_console.c)
endif()

if(NOT CONFIG_DISABLE_MOUNTPOINT)
  list(APPEND SRCS ramdisk.c)
  if(CONFIG_DRVR_MKRD)
    list(APPEND SRCS mkrd.c)
  endif()
endif()

if(CONFIG_DRVR_WRITEBUFFER)
  list(APPEND SRCS rwbuffer.c)
elseif(CONFIG_DRVR_READAHEAD)
  list(APPEND SRCS rwbuffer.c)
endif()

if(CONFIG_DEV_RPMSG)
  list(APPEND SRCS rpmsgdev.c)
endif()

if(CONFIG_DEV_RPMSG_SERVER)
  list(APPEND SRCS rpmsgdev_server.c)
endif()

if(CONFIG_BLK_RPMSG)
  list(APPEND SRCS rpmsgblk.c)
endif()

if(CONFIG_BLK_RPMSG_SERVER)
  target_include_directories(drivers PRIVATE ${NUTTX_DIR}/fs/inode
                                             ${NUTTX_DIR}/fs)
  set_source_files_properties(rpmsgblk_server.c DIRECTORY
                              ${CMAKE_CURRENT_LIST_DIR}/..)
  list(APPEND SRCS rpmsgblk_server.c)
endif()

if(NOT CONFIG_DEV_OPTEE_NONE)
  list(APPEND SRCS optee.c)
  if(CONFIG_DEV_OPTEE_SMC)
    list(APPEND SRCS optee_smc.c)
  else()
    list(APPEND SRCS optee_socket.c)
  endif()
endif()

if(CONFIG_GOLDFISH_PIPE)
  list(APPEND SRCS goldfish_pipe.c)
endif()

target_sources(drivers PRIVATE ${SRCS})
