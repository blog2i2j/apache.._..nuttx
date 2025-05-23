/****************************************************************************
 * arch/arm/src/at32/at32_serial.c
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

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/serial/serial.h>
#include <nuttx/spinlock.h>
#include <nuttx/power/pm.h>

#ifdef CONFIG_SERIAL_TERMIOS
#  include <termios.h>
#endif

#include <arch/board/board.h>

#include "chip.h"
#include "at32_uart.h"
#include "at32_dma.h"
#include "at32_rcc.h"
#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Some sanity checks *******************************************************/

/* DMA configuration */

/* If DMA is enabled on any USART, then very that other pre-requisites
 * have also been selected.
 */

#ifdef SERIAL_HAVE_RXDMA

/* The DMA buffer size when using RX DMA to emulate a FIFO.
 *
 * When streaming data, the generic serial layer will be called
 * every time the FIFO receives half this number of bytes.
 */
#  if !defined(CONFIG_AT32_SERIAL_RXDMA_BUFFER_SIZE)
#    define CONFIG_AT32_SERIAL_RXDMA_BUFFER_SIZE 32
#  endif
#  define RXDMA_MUTIPLE  4
#  define RXDMA_MUTIPLE_MASK  (RXDMA_MUTIPLE -1)
#  define RXDMA_BUFFER_SIZE   ((CONFIG_AT32_SERIAL_RXDMA_BUFFER_SIZE \
                                + RXDMA_MUTIPLE_MASK) \
                                & ~RXDMA_MUTIPLE_MASK)

/* DMA priority */

#  ifndef CONFIG_USART_RXDMAPRIO
#    if defined(CONFIG_AT32_HAVE_IP_DMA_V1)
#      define CONFIG_USART_RXDMAPRIO  DMA_CCR_PRIMED
#    else
#      error "Unknown AT32 DMA"
#    endif
#  endif
#  if defined(CONFIG_AT32_HAVE_IP_DMA_V1)
#    if (CONFIG_USART_RXDMAPRIO & ~DMA_CCR_PL_MASK) != 0
#      error "Illegal value for CONFIG_USART_RXDMAPRIO"
#    endif
#  else
#    error "Unknown AT32 DMA"
#  endif

/* DMA control word */

#    define SERIAL_RXDMA_CONTROL_WORD    \
                (DMA_CCR_CIRC          | \
                 DMA_CCR_MINC          | \
                 DMA_CCR_PSIZE_8BITS   | \
                 DMA_CCR_MSIZE_8BITS   | \
                 CONFIG_USART_RXDMAPRIO)

#endif /* SERIAL_HAVE_RXDMA */

#ifdef SERIAL_HAVE_TXDMA

/* DMA priority */

#  ifndef CONFIG_USART_TXDMAPRIO
#    if defined(CONFIG_AT32_HAVE_IP_DMA_V1)
#      define CONFIG_USART_TXDMAPRIO  DMA_CCR_PRIMED
#    else
#      error "Unknown AT32 DMA"
#    endif
#  endif
#  if defined(CONFIG_AT32_HAVE_IP_DMA_V1)
#    if (CONFIG_USART_TXDMAPRIO & ~DMA_CCR_PL_MASK) != 0
#      error "Illegal value for CONFIG_USART_TXDMAPRIO"
#    endif
#  else
#    error "Unknown AT32 DMA"
#  endif

/* DMA control word */

#  if defined(CONFIG_AT32_HAVE_IP_DMA_V1)
#    define SERIAL_TXDMA_CONTROL_WORD                         \
                (DMA_CCR_DIR           |                      \
                 DMA_CCR_MINC          |                      \
                 DMA_CCR_PSIZE_8BITS   |                      \
                 DMA_CCR_MSIZE_8BITS   |                      \
                 CONFIG_USART_TXDMAPRIO)
#  else
#    error "Unknown AT32 DMA"
#  endif

/* DMA ISR status */

#  if defined(CONFIG_AT32_HAVE_IP_DMA_V1)
#    define DMA_ISR_HTIF_BIT DMA_CHAN_HTIF_BIT
#    define DMA_ISR_TCIF_BIT DMA_CHAN_TCIF_BIT
#  else
#    error "Unknown AT32 DMA"
#  endif

#endif  /* SERIAL_HAVE_TXDMA */

/* Power management definitions */

#if defined(CONFIG_PM) && !defined(CONFIG_AT32_PM_SERIAL_ACTIVITY)
#  define CONFIG_AT32_PM_SERIAL_ACTIVITY 10
#endif

/* Since RX DMA or TX DMA or both may be enabled for a given U[S]ART.
 * We need runtime detection in up_dma_setup and up_dma_shutdown
 * We use the default struct default init value of 0 which maps to
 * AT32_DMA_MAP(DMA1,DMA_STREAM0,DMA_CHAN0) which is not a U[S]ART.
 */

#define INVALID_SERIAL_DMA_CHANNEL 0

/* Keep track if a Break was set
 *
 * Note:
 *
 * 1) This value is set in the priv->ie but never written to the control
 *    register. It must not collide with USART_CR1_USED_INTS or
 *    USART_CTRL3_ERRIEN
 * 2) USART_CTRL3_ERRIEN is also carried in the up_dev_s ie member.
 *
 * See up_restoreusartint where the masking is done.
 */

#ifdef CONFIG_AT32_SERIALBRK_BSDCOMPAT
#  define USART_CR1_IE_BREAK_INPROGRESS_SHFTS 15
#  define USART_CR1_IE_BREAK_INPROGRESS (1 << USART_CR1_IE_BREAK_INPROGRESS_SHFTS)
#endif

#ifdef USE_SERIALDRIVER
#ifdef HAVE_SERIALDRIVER

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct up_dev_s
{
  struct uart_dev_s dev;       /* Generic UART device */
  uint16_t          ie;        /* Saved interrupt mask bits value */
  uint16_t          sr;        /* Saved status bits */
  spinlock_t        lock;      /* Spinlock */

  /* Has been initialized and HW is setup. */

  bool              initialized;

  /* If termios are supported, then the following fields may vary at
   * runtime.
   */

#ifdef CONFIG_SERIAL_TERMIOS
  uint8_t           parity;    /* 0=none, 1=odd, 2=even */
  uint8_t           bits;      /* Number of bits (7 or 8) */
  bool              stopbits2; /* True: Configure with 2 stop bits instead of 1 */
#ifdef CONFIG_SERIAL_IFLOWCONTROL
  bool              iflow;     /* input flow control (RTS) enabled */
#endif
#ifdef CONFIG_SERIAL_OFLOWCONTROL
  bool              oflow;     /* output flow control (CTS) enabled */
#endif
  uint32_t          baud;      /* Configured baud */
#else
  const uint8_t     parity;    /* 0=none, 1=odd, 2=even */
  const uint8_t     bits;      /* Number of bits (7 or 8) */
  const bool        stopbits2; /* True: Configure with 2 stop bits instead of 1 */
#ifdef CONFIG_SERIAL_IFLOWCONTROL
  const bool        iflow;     /* input flow control (RTS) enabled */
#endif
#ifdef CONFIG_SERIAL_OFLOWCONTROL
  const bool        oflow;     /* output flow control (CTS) enabled */
#endif
  const uint32_t    baud;      /* Configured baud */
#endif

  const uint8_t     irq;       /* IRQ associated with this USART */
  const uint32_t    apbclock;  /* PCLK 1 or 2 frequency */
  const uint32_t    usartbase; /* Base address of USART registers */
  const uint32_t    tx_gpio;   /* U[S]ART TX GPIO pin configuration */
  const uint32_t    rx_gpio;   /* U[S]ART RX GPIO pin configuration */
#ifdef CONFIG_SERIAL_IFLOWCONTROL
  const uint32_t    rts_gpio;  /* U[S]ART RTS GPIO pin configuration */
#endif
#ifdef CONFIG_SERIAL_OFLOWCONTROL
  const uint32_t    cts_gpio;  /* U[S]ART CTS GPIO pin configuration */
#endif

  /* TX DMA state */

#ifdef SERIAL_HAVE_TXDMA
  const unsigned int txdma_channel; /* DMA channel assigned */
  DMA_HANDLE        txdma;          /* currently-open transmit DMA stream */
#endif

#ifdef SERIAL_HAVE_RXDMA
  const unsigned int rxdma_channel; /* DMA channel assigned */
#endif

  /* RX DMA state */

#ifdef SERIAL_HAVE_RXDMA
  DMA_HANDLE        rxdma;     /* currently-open receive DMA stream */
  bool              rxenable;  /* DMA-based reception en/disable */
  uint32_t          rxdmanext; /* Next byte in the DMA buffer to be read */
  char       *const rxfifo;    /* Receive DMA buffer */
#endif

#ifdef HAVE_RS485
  const uint32_t    rs485_dir_gpio;     /* U[S]ART RS-485 DIR GPIO pin cfg */
  const bool        rs485_dir_polarity; /* U[S]ART RS-485 DIR TXEN polarity */
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void up_set_format(struct uart_dev_s *dev);
static int  up_setup(struct uart_dev_s *dev);
static void up_shutdown(struct uart_dev_s *dev);
static int  up_attach(struct uart_dev_s *dev);
static void up_detach(struct uart_dev_s *dev);
static int  up_interrupt(int irq, void *context, void *arg);
static int  up_ioctl(struct file *filep, int cmd, unsigned long arg);
#if defined(SERIAL_HAVE_TXDMA_OPS) || defined(SERIAL_HAVE_NODMA_OPS)
static int  up_receive(struct uart_dev_s *dev, unsigned int *status);
static void up_rxint(struct uart_dev_s *dev, bool enable);
static bool up_rxavailable(struct uart_dev_s *dev);
#endif
#ifdef CONFIG_SERIAL_IFLOWCONTROL
static bool up_rxflowcontrol(struct uart_dev_s *dev, unsigned int nbuffered,
                             bool upper);
#endif
static void up_send(struct uart_dev_s *dev, int ch);
#if defined(SERIAL_HAVE_RXDMA_OPS) || defined(SERIAL_HAVE_NODMA_OPS) || \
    defined(CONFIG_AT32_SERIALBRK_BSDCOMPAT)
static void up_txint(struct uart_dev_s *dev, bool enable);
#endif
static bool up_txready(struct uart_dev_s *dev);

#ifdef SERIAL_HAVE_TXDMA
static void up_dma_send(struct uart_dev_s *dev);
static void up_dma_txint(struct uart_dev_s *dev, bool enable);
static void up_dma_txavailable(struct uart_dev_s *dev);
static void up_dma_txcallback(DMA_HANDLE handle, uint8_t status, void *arg);
#endif

#if defined(SERIAL_HAVE_RXDMA) || defined(SERIAL_HAVE_TXDMA)
static int  up_dma_setup(struct uart_dev_s *dev);
static void up_dma_shutdown(struct uart_dev_s *dev);
#endif

#ifdef SERIAL_HAVE_RXDMA
static int  up_dma_receive(struct uart_dev_s *dev, unsigned int *status);
static void up_dma_rxint(struct uart_dev_s *dev, bool enable);
static bool up_dma_rxavailable(struct uart_dev_s *dev);

static void up_dma_rxcallback(DMA_HANDLE handle, uint8_t status, void *arg);
#endif

#ifdef CONFIG_PM
static void up_pm_notify(struct pm_callback_s *cb, int dowmin,
                         enum pm_state_e pmstate);
static int  up_pm_prepare(struct pm_callback_s *cb, int domain,
                          enum pm_state_e pmstate);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef SERIAL_HAVE_NODMA_OPS
static const struct uart_ops_s g_uart_ops =
{
  .setup          = up_setup,
  .shutdown       = up_shutdown,
  .attach         = up_attach,
  .detach         = up_detach,
  .ioctl          = up_ioctl,
  .receive        = up_receive,
  .rxint          = up_rxint,
  .rxavailable    = up_rxavailable,
#ifdef CONFIG_SERIAL_IFLOWCONTROL
  .rxflowcontrol  = up_rxflowcontrol,
#endif
  .send           = up_send,
  .txint          = up_txint,
  .txready        = up_txready,
  .txempty        = up_txready,
};
#endif

#ifdef SERIAL_HAVE_RXTXDMA_OPS
static const struct uart_ops_s g_uart_rxtxdma_ops =
{
  .setup          = up_dma_setup,
  .shutdown       = up_dma_shutdown,
  .attach         = up_attach,
  .detach         = up_detach,
  .ioctl          = up_ioctl,
  .receive        = up_dma_receive,
  .rxint          = up_dma_rxint,
  .rxavailable    = up_dma_rxavailable,
#ifdef CONFIG_SERIAL_IFLOWCONTROL
  .rxflowcontrol  = up_rxflowcontrol,
#endif
  .send           = up_send,
  .txint          = up_dma_txint,
  .txready        = up_txready,
  .txempty        = up_txready,
  .dmatxavail     = up_dma_txavailable,
  .dmasend        = up_dma_send,
};
#endif

#ifdef SERIAL_HAVE_RXDMA_OPS
static const struct uart_ops_s g_uart_rxdma_ops =
{
  .setup          = up_dma_setup,
  .shutdown       = up_dma_shutdown,
  .attach         = up_attach,
  .detach         = up_detach,
  .ioctl          = up_ioctl,
  .receive        = up_dma_receive,
  .rxint          = up_dma_rxint,
  .rxavailable    = up_dma_rxavailable,
#ifdef CONFIG_SERIAL_IFLOWCONTROL
  .rxflowcontrol  = up_rxflowcontrol,
#endif
  .send           = up_send,
  .txint          = up_txint,
  .txready        = up_txready,
  .txempty        = up_txready,
};
#endif

#ifdef SERIAL_HAVE_TXDMA_OPS
static const struct uart_ops_s g_uart_txdma_ops =
{
  .setup          = up_dma_setup,
  .shutdown       = up_dma_shutdown,
  .attach         = up_attach,
  .detach         = up_detach,
  .ioctl          = up_ioctl,
  .receive        = up_receive,
  .rxint          = up_rxint,
  .rxavailable    = up_rxavailable,
#ifdef CONFIG_SERIAL_IFLOWCONTROL
  .rxflowcontrol  = up_rxflowcontrol,
#endif
  .send           = up_send,
  .txint          = up_dma_txint,
  .txready        = up_txready,
  .txempty        = up_txready,
  .dmatxavail     = up_dma_txavailable,
  .dmasend        = up_dma_send,
};
#endif

/* I/O buffers */

#ifdef CONFIG_AT32_USART1_SERIALDRIVER
static char g_usart1rxbuffer[CONFIG_USART1_RXBUFSIZE];
static char g_usart1txbuffer[CONFIG_USART1_TXBUFSIZE];
# ifdef CONFIG_USART1_RXDMA
static char g_usart1rxfifo[RXDMA_BUFFER_SIZE];
# endif
#endif

#ifdef CONFIG_AT32_USART2_SERIALDRIVER
static char g_usart2rxbuffer[CONFIG_USART2_RXBUFSIZE];
static char g_usart2txbuffer[CONFIG_USART2_TXBUFSIZE];
# ifdef CONFIG_USART2_RXDMA
static char g_usart2rxfifo[RXDMA_BUFFER_SIZE];
# endif
#endif

#ifdef CONFIG_AT32_USART3_SERIALDRIVER
static char g_usart3rxbuffer[CONFIG_USART3_RXBUFSIZE];
static char g_usart3txbuffer[CONFIG_USART3_TXBUFSIZE];
# ifdef CONFIG_USART3_RXDMA
static char g_usart3rxfifo[RXDMA_BUFFER_SIZE];
# endif
#endif

#ifdef CONFIG_AT32_UART4_SERIALDRIVER
static char g_uart4rxbuffer[CONFIG_UART4_RXBUFSIZE];
static char g_uart4txbuffer[CONFIG_UART4_TXBUFSIZE];
# ifdef CONFIG_UART4_RXDMA
static char g_uart4rxfifo[RXDMA_BUFFER_SIZE];
# endif
#endif

#ifdef CONFIG_AT32_UART5_SERIALDRIVER
static char g_uart5rxbuffer[CONFIG_UART5_RXBUFSIZE];
static char g_uart5txbuffer[CONFIG_UART5_TXBUFSIZE];
# ifdef CONFIG_UART5_RXDMA
static char g_uart5rxfifo[RXDMA_BUFFER_SIZE];
# endif
#endif

#ifdef CONFIG_AT32_USART6_SERIALDRIVER
static char g_usart6rxbuffer[CONFIG_USART6_RXBUFSIZE];
static char g_usart6txbuffer[CONFIG_USART6_TXBUFSIZE];
# ifdef CONFIG_USART6_RXDMA
static char g_usart6rxfifo[RXDMA_BUFFER_SIZE];
# endif
#endif

#ifdef CONFIG_AT32_UART7_SERIALDRIVER
static char g_uart7rxbuffer[CONFIG_UART7_RXBUFSIZE];
static char g_uart7txbuffer[CONFIG_UART7_TXBUFSIZE];
# ifdef CONFIG_UART7_RXDMA
static char g_uart7rxfifo[RXDMA_BUFFER_SIZE];
# endif
#endif

#ifdef CONFIG_AT32_UART8_SERIALDRIVER
static char g_uart8rxbuffer[CONFIG_UART8_RXBUFSIZE];
static char g_uart8txbuffer[CONFIG_UART8_TXBUFSIZE];
# ifdef CONFIG_UART8_RXDMA
static char g_uart8rxfifo[RXDMA_BUFFER_SIZE];
# endif
#endif

/* This describes the state of the AT32 USART1 ports. */

#ifdef CONFIG_AT32_USART1_SERIALDRIVER
static struct up_dev_s g_usart1priv =
{
  .dev =
    {
#if CONSOLE_UART == 1
      .isconsole = true,
#endif
      .recv      =
      {
        .size    = CONFIG_USART1_RXBUFSIZE,
        .buffer  = g_usart1rxbuffer,
      },
      .xmit      =
      {
        .size    = CONFIG_USART1_TXBUFSIZE,
        .buffer  = g_usart1txbuffer,
      },
#if defined(CONFIG_USART1_RXDMA) && defined(CONFIG_USART1_TXDMA)
      .ops       = &g_uart_rxtxdma_ops,
#elif defined(CONFIG_USART1_RXDMA) && !defined(CONFIG_USART1_TXDMA)
      .ops       = &g_uart_rxdma_ops,
#elif !defined(CONFIG_USART1_RXDMA) && defined(CONFIG_USART1_TXDMA)
      .ops       = &g_uart_txdma_ops,
#else
      .ops       = &g_uart_ops,
#endif
      .priv      = &g_usart1priv,
    },

  .irq           = AT32_IRQ_USART1,
  .parity        = CONFIG_USART1_PARITY,
  .bits          = CONFIG_USART1_BITS,
  .stopbits2     = CONFIG_USART1_2STOP,
  .baud          = CONFIG_USART1_BAUD,

  .apbclock      = AT32_PCLK2_FREQUENCY,

  .usartbase     = AT32_USART1_BASE,
  .tx_gpio       = GPIO_USART1_TX,
  .rx_gpio       = GPIO_USART1_RX,
#if defined(CONFIG_SERIAL_OFLOWCONTROL) && defined(CONFIG_USART1_OFLOWCONTROL)
  .oflow         = true,
  .cts_gpio      = GPIO_USART1_CTS,
#endif
#if defined(CONFIG_SERIAL_IFLOWCONTROL) && defined(CONFIG_USART1_IFLOWCONTROL)
  .iflow         = true,
  .rts_gpio      = GPIO_USART1_RTS,
#endif
#ifdef CONFIG_USART1_TXDMA
  .txdma_channel = DMAMUX_USART1_TX,
#endif
#ifdef CONFIG_USART1_RXDMA
  .rxdma_channel = DMAMUX_USART1_RX,
  .rxfifo        = g_usart1rxfifo,
#endif

#ifdef CONFIG_USART1_RS485
  .rs485_dir_gpio = GPIO_USART1_RS485_DIR,
#  if (CONFIG_USART1_RS485_DIR_POLARITY == 0)
  .rs485_dir_polarity = false,
#  else
  .rs485_dir_polarity = true,
#  endif
#endif
};
#endif

/* This describes the state of the AT32 USART2 port. */

#ifdef CONFIG_AT32_USART2_SERIALDRIVER
static struct up_dev_s g_usart2priv =
{
  .dev =
    {
#if CONSOLE_UART == 2
      .isconsole = true,
#endif
      .recv      =
      {
        .size    = CONFIG_USART2_RXBUFSIZE,
        .buffer  = g_usart2rxbuffer,
      },
      .xmit      =
      {
        .size    = CONFIG_USART2_TXBUFSIZE,
        .buffer  = g_usart2txbuffer,
      },
#if defined(CONFIG_USART2_RXDMA) && defined(CONFIG_USART2_TXDMA)
      .ops       = &g_uart_rxtxdma_ops,
#elif defined(CONFIG_USART2_RXDMA) && !defined(CONFIG_USART2_TXDMA)
      .ops       = &g_uart_rxdma_ops,
#elif !defined(CONFIG_USART2_RXDMA) && defined(CONFIG_USART2_TXDMA)
      .ops       = &g_uart_txdma_ops,
#else
      .ops       = &g_uart_ops,
#endif
      .priv      = &g_usart2priv,
    },

  .irq           = AT32_IRQ_USART2,
  .parity        = CONFIG_USART2_PARITY,
  .bits          = CONFIG_USART2_BITS,
  .stopbits2     = CONFIG_USART2_2STOP,
  .baud          = CONFIG_USART2_BAUD,
  .apbclock      = AT32_PCLK1_FREQUENCY,
  .usartbase     = AT32_USART2_BASE,
  .tx_gpio       = GPIO_USART2_TX,
  .rx_gpio       = GPIO_USART2_RX,
#if defined(CONFIG_SERIAL_OFLOWCONTROL) && defined(CONFIG_USART2_OFLOWCONTROL)
  .oflow         = true,
  .cts_gpio      = GPIO_USART2_CTS,
#endif
#if defined(CONFIG_SERIAL_IFLOWCONTROL) && defined(CONFIG_USART2_IFLOWCONTROL)
  .iflow         = true,
  .rts_gpio      = GPIO_USART2_RTS,
#endif
#ifdef CONFIG_USART2_TXDMA
  .txdma_channel = DMAMUX_USART2_TX,
#endif
#ifdef CONFIG_USART2_RXDMA
  .rxdma_channel = DMAMUX_USART2_RX,
  .rxfifo        = g_usart2rxfifo,
#endif

#ifdef CONFIG_USART2_RS485
  .rs485_dir_gpio = GPIO_USART2_RS485_DIR,
#  if (CONFIG_USART2_RS485_DIR_POLARITY == 0)
  .rs485_dir_polarity = false,
#  else
  .rs485_dir_polarity = true,
#  endif
#endif
};
#endif

/* This describes the state of the AT32 USART3 port. */

#ifdef CONFIG_AT32_USART3_SERIALDRIVER
static struct up_dev_s g_usart3priv =
{
  .dev =
    {
#if CONSOLE_UART == 3
      .isconsole = true,
#endif
      .recv      =
      {
        .size    = CONFIG_USART3_RXBUFSIZE,
        .buffer  = g_usart3rxbuffer,
      },
      .xmit      =
      {
        .size    = CONFIG_USART3_TXBUFSIZE,
        .buffer  = g_usart3txbuffer,
      },
#if defined(CONFIG_USART3_RXDMA) && defined(CONFIG_USART3_TXDMA)
      .ops       = &g_uart_rxtxdma_ops,
#elif defined(CONFIG_USART3_RXDMA) && !defined(CONFIG_USART3_TXDMA)
      .ops       = &g_uart_rxdma_ops,
#elif !defined(CONFIG_USART3_RXDMA) && defined(CONFIG_USART3_TXDMA)
      .ops       = &g_uart_txdma_ops,
#else
      .ops       = &g_uart_ops,
#endif
      .priv      = &g_usart3priv,
    },

  .irq           = AT32_IRQ_USART3,
  .parity        = CONFIG_USART3_PARITY,
  .bits          = CONFIG_USART3_BITS,
  .stopbits2     = CONFIG_USART3_2STOP,
  .baud          = CONFIG_USART3_BAUD,
  .apbclock      = AT32_PCLK1_FREQUENCY,
  .usartbase     = AT32_USART3_BASE,
  .tx_gpio       = GPIO_USART3_TX,
  .rx_gpio       = GPIO_USART3_RX,
#if defined(CONFIG_SERIAL_OFLOWCONTROL) && defined(CONFIG_USART3_OFLOWCONTROL)
  .oflow         = true,
  .cts_gpio      = GPIO_USART3_CTS,
#endif
#if defined(CONFIG_SERIAL_IFLOWCONTROL) && defined(CONFIG_USART3_IFLOWCONTROL)
  .iflow         = true,
  .rts_gpio      = GPIO_USART3_RTS,
#endif
#ifdef CONFIG_USART3_TXDMA
  .txdma_channel = DMAMUX_USART3_TX,
#endif
#ifdef CONFIG_USART3_RXDMA
  .rxdma_channel = DMAMUX_USART3_RX,
  .rxfifo        = g_usart3rxfifo,
#endif

#ifdef CONFIG_USART3_RS485
  .rs485_dir_gpio = GPIO_USART3_RS485_DIR,
#  if (CONFIG_USART3_RS485_DIR_POLARITY == 0)
  .rs485_dir_polarity = false,
#  else
  .rs485_dir_polarity = true,
#  endif
#endif
};
#endif

/* This describes the state of the AT32 UART4 port. */

#ifdef CONFIG_AT32_UART4_SERIALDRIVER
static struct up_dev_s g_uart4priv =
{
  .dev =
    {
#if CONSOLE_UART == 4
      .isconsole = true,
#endif
      .recv      =
      {
        .size    = CONFIG_UART4_RXBUFSIZE,
        .buffer  = g_uart4rxbuffer,
      },
      .xmit      =
      {
        .size    = CONFIG_UART4_TXBUFSIZE,
        .buffer  = g_uart4txbuffer,
      },
#if defined(CONFIG_UART4_RXDMA) && defined(CONFIG_UART4_TXDMA)
      .ops       = &g_uart_rxtxdma_ops,
#elif defined(CONFIG_UART4_RXDMA) && !defined(CONFIG_UART4_TXDMA)
      .ops       = &g_uart_rxdma_ops,
#elif !defined(CONFIG_UART4_RXDMA) && defined(CONFIG_UART4_TXDMA)
      .ops       = &g_uart_txdma_ops,
#else
      .ops       = &g_uart_ops,
#endif
      .priv      = &g_uart4priv,
    },

  .irq           = AT32_IRQ_UART4,
  .parity        = CONFIG_UART4_PARITY,
  .bits          = CONFIG_UART4_BITS,
  .stopbits2     = CONFIG_UART4_2STOP,
#if defined(CONFIG_SERIAL_OFLOWCONTROL) && defined(CONFIG_UART4_OFLOWCONTROL)
  .oflow         = true,
  .cts_gpio      = GPIO_UART4_CTS,
#endif
#if defined(CONFIG_SERIAL_IFLOWCONTROL) && defined(CONFIG_UART4_IFLOWCONTROL)
  .iflow         = true,
  .rts_gpio      = GPIO_UART4_RTS,
#endif
  .baud          = CONFIG_UART4_BAUD,
  .apbclock      = AT32_PCLK1_FREQUENCY,
  .usartbase     = AT32_UART4_BASE,
  .tx_gpio       = GPIO_UART4_TX,
  .rx_gpio       = GPIO_UART4_RX,
#ifdef CONFIG_UART4_TXDMA
  .txdma_channel = DMAMUX_UART4_TX,
#endif
#ifdef CONFIG_UART4_RXDMA
  .rxdma_channel = DMAMUX_UART4_RX,
  .rxfifo        = g_uart4rxfifo,
#endif

#ifdef CONFIG_UART4_RS485
  .rs485_dir_gpio = GPIO_UART4_RS485_DIR,
#  if (CONFIG_UART4_RS485_DIR_POLARITY == 0)
  .rs485_dir_polarity = false,
#  else
  .rs485_dir_polarity = true,
#  endif
#endif
};
#endif

/* This describes the state of the AT32 UART5 port. */

#ifdef CONFIG_AT32_UART5_SERIALDRIVER
static struct up_dev_s g_uart5priv =
{
  .dev =
    {
#if CONSOLE_UART == 5
      .isconsole = true,
#endif
      .recv     =
      {
        .size   = CONFIG_UART5_RXBUFSIZE,
        .buffer = g_uart5rxbuffer,
      },
      .xmit     =
      {
        .size   = CONFIG_UART5_TXBUFSIZE,
        .buffer = g_uart5txbuffer,
      },
#if defined(CONFIG_UART5_RXDMA) && defined(CONFIG_UART5_TXDMA)
      .ops       = &g_uart_rxtxdma_ops,
#elif defined(CONFIG_UART5_RXDMA) && !defined(CONFIG_UART5_TXDMA)
      .ops       = &g_uart_rxdma_ops,
#elif !defined(CONFIG_UART5_RXDMA) && defined(CONFIG_UART5_TXDMA)
      .ops       = &g_uart_txdma_ops,
#else
      .ops       = &g_uart_ops,
#endif
      .priv     = &g_uart5priv,
    },

  .irq            = AT32_IRQ_UART5,
  .parity         = CONFIG_UART5_PARITY,
  .bits           = CONFIG_UART5_BITS,
  .stopbits2      = CONFIG_UART5_2STOP,
#if defined(CONFIG_SERIAL_OFLOWCONTROL) && defined(CONFIG_UART5_OFLOWCONTROL)
  .oflow         = true,
  .cts_gpio      = GPIO_UART5_CTS,
#endif
#if defined(CONFIG_SERIAL_IFLOWCONTROL) && defined(CONFIG_UART5_IFLOWCONTROL)
  .iflow         = true,
  .rts_gpio      = GPIO_UART5_RTS,
#endif
  .baud           = CONFIG_UART5_BAUD,
  .apbclock       = AT32_PCLK1_FREQUENCY,
  .usartbase      = AT32_UART5_BASE,
  .tx_gpio        = GPIO_UART5_TX,
  .rx_gpio        = GPIO_UART5_RX,
#ifdef CONFIG_UART5_TXDMA
  .txdma_channel = DMAMUX_UART5_TX,
#endif
#ifdef CONFIG_UART5_RXDMA
  .rxdma_channel = DMAMUX_UART5_RX,
  .rxfifo        = g_uart5rxfifo,
#endif

#ifdef CONFIG_UART5_RS485
  .rs485_dir_gpio = GPIO_UART5_RS485_DIR,
#  if (CONFIG_UART5_RS485_DIR_POLARITY == 0)
  .rs485_dir_polarity = false,
#  else
  .rs485_dir_polarity = true,
#  endif
#endif
};
#endif

/* This describes the state of the AT32 USART6 port. */

#ifdef CONFIG_AT32_USART6_SERIALDRIVER
static struct up_dev_s g_usart6priv =
{
  .dev =
    {
#if CONSOLE_UART == 6
      .isconsole = true,
#endif
      .recv     =
      {
        .size   = CONFIG_USART6_RXBUFSIZE,
        .buffer = g_usart6rxbuffer,
      },
      .xmit     =
      {
        .size   = CONFIG_USART6_TXBUFSIZE,
        .buffer = g_usart6txbuffer,
      },
#if defined(CONFIG_USART6_RXDMA) && defined(CONFIG_USART6_TXDMA)
      .ops       = &g_uart_rxtxdma_ops,
#elif defined(CONFIG_USART6_RXDMA) && !defined(CONFIG_USART6_TXDMA)
      .ops       = &g_uart_rxdma_ops,
#elif !defined(CONFIG_USART6_RXDMA) && defined(CONFIG_USART6_TXDMA)
      .ops       = &g_uart_txdma_ops,
#else
      .ops       = &g_uart_ops,
#endif
      .priv     = &g_usart6priv,
    },

  .irq            = AT32_IRQ_USART6,
  .parity         = CONFIG_USART6_PARITY,
  .bits           = CONFIG_USART6_BITS,
  .stopbits2      = CONFIG_USART6_2STOP,
  .baud           = CONFIG_USART6_BAUD,
  .apbclock       = AT32_PCLK2_FREQUENCY,
  .usartbase      = AT32_USART6_BASE,
  .tx_gpio        = GPIO_USART6_TX,
  .rx_gpio        = GPIO_USART6_RX,
#if defined(CONFIG_SERIAL_OFLOWCONTROL) && defined(CONFIG_USART6_OFLOWCONTROL)
  .oflow          = true,
  .cts_gpio       = GPIO_USART6_CTS,
#endif
#if defined(CONFIG_SERIAL_IFLOWCONTROL) && defined(CONFIG_USART6_IFLOWCONTROL)
  .iflow          = true,
  .rts_gpio       = GPIO_USART6_RTS,
#endif
#ifdef CONFIG_USART6_TXDMA
  .txdma_channel = DMAMUX_USART6_TX,
#endif
#ifdef CONFIG_USART6_RXDMA
  .rxdma_channel = DMAMUX_USART6_RX,
  .rxfifo        = g_usart6rxfifo,
#endif

#ifdef CONFIG_USART6_RS485
  .rs485_dir_gpio = GPIO_USART6_RS485_DIR,
#  if (CONFIG_USART6_RS485_DIR_POLARITY == 0)
  .rs485_dir_polarity = false,
#  else
  .rs485_dir_polarity = true,
#  endif
#endif
};
#endif

/* This describes the state of the AT32 UART7 port. */

#ifdef CONFIG_AT32_UART7_SERIALDRIVER
static struct up_dev_s g_uart7priv =
{
  .dev =
    {
#if CONSOLE_UART == 7
      .isconsole = true,
#endif
      .recv     =
      {
        .size   = CONFIG_UART7_RXBUFSIZE,
        .buffer = g_uart7rxbuffer,
      },
      .xmit     =
      {
        .size   = CONFIG_UART7_TXBUFSIZE,
        .buffer = g_uart7txbuffer,
      },
#if defined(CONFIG_UART7_RXDMA) && defined(CONFIG_UART7_TXDMA)
      .ops       = &g_uart_rxtxdma_ops,
#elif defined(CONFIG_UART7_RXDMA) && !defined(CONFIG_UART7_TXDMA)
      .ops       = &g_uart_rxdma_ops,
#elif !defined(CONFIG_UART7_RXDMA) && defined(CONFIG_UART7_TXDMA)
      .ops       = &g_uart_txdma_ops,
#else
      .ops       = &g_uart_ops,
#endif
      .priv     = &g_uart7priv,
    },

  .irq            = AT32_IRQ_UART7,
  .parity         = CONFIG_UART7_PARITY,
  .bits           = CONFIG_UART7_BITS,
  .stopbits2      = CONFIG_UART7_2STOP,
  .baud           = CONFIG_UART7_BAUD,
  .apbclock       = AT32_PCLK1_FREQUENCY,
  .usartbase      = AT32_UART7_BASE,
  .tx_gpio        = GPIO_UART7_TX,
  .rx_gpio        = GPIO_UART7_RX,
#if defined(CONFIG_SERIAL_OFLOWCONTROL) && defined(CONFIG_UART7_OFLOWCONTROL)
  .oflow          = true,
  .cts_gpio       = GPIO_UART7_CTS,
#endif
#if defined(CONFIG_SERIAL_IFLOWCONTROL) && defined(CONFIG_UART7_IFLOWCONTROL)
  .iflow          = true,
  .rts_gpio       = GPIO_UART7_RTS,
#endif
#ifdef CONFIG_UART7_TXDMA
  .txdma_channel = DMAMUX_UART7_TX,
#endif
#ifdef CONFIG_UART7_RXDMA
  .rxdma_channel = DMAMUX_UART7_RX,
  .rxfifo        = g_uart7rxfifo,
#endif

#ifdef CONFIG_UART7_RS485
  .rs485_dir_gpio = GPIO_UART7_RS485_DIR,
#  if (CONFIG_UART7_RS485_DIR_POLARITY == 0)
  .rs485_dir_polarity = false,
#  else
  .rs485_dir_polarity = true,
#  endif
#endif
};
#endif

/* This describes the state of the AT32 UART8 port. */

#ifdef CONFIG_AT32_UART8_SERIALDRIVER
static struct up_dev_s g_uart8priv =
{
  .dev =
    {
#if CONSOLE_UART == 8
      .isconsole = true,
#endif
      .recv     =
      {
        .size   = CONFIG_UART8_RXBUFSIZE,
        .buffer = g_uart8rxbuffer,
      },
      .xmit     =
      {
        .size   = CONFIG_UART8_TXBUFSIZE,
        .buffer = g_uart8txbuffer,
      },
#if defined(CONFIG_UART8_RXDMA) && defined(CONFIG_UART8_TXDMA)
      .ops       = &g_uart_rxtxdma_ops,
#elif defined(CONFIG_UART8_RXDMA) && !defined(CONFIG_UART8_TXDMA)
      .ops       = &g_uart_rxdma_ops,
#elif !defined(CONFIG_UART8_RXDMA) && defined(CONFIG_UART8_TXDMA)
      .ops       = &g_uart_txdma_ops,
#else
      .ops       = &g_uart_ops,
#endif
      .priv     = &g_uart8priv,
    },

  .irq            = AT32_IRQ_UART8,
  .parity         = CONFIG_UART8_PARITY,
  .bits           = CONFIG_UART8_BITS,
  .stopbits2      = CONFIG_UART8_2STOP,
  .baud           = CONFIG_UART8_BAUD,
  .apbclock       = AT32_PCLK1_FREQUENCY,
  .usartbase      = AT32_UART8_BASE,
  .tx_gpio        = GPIO_UART8_TX,
  .rx_gpio        = GPIO_UART8_RX,
#if defined(CONFIG_SERIAL_OFLOWCONTROL) && defined(CONFIG_UART8_OFLOWCONTROL)
  .oflow          = true,
  .cts_gpio       = GPIO_UART8_CTS,
#endif
#if defined(CONFIG_SERIAL_IFLOWCONTROL) && defined(CONFIG_UART8_IFLOWCONTROL)
  .iflow          = true,
  .rts_gpio       = GPIO_UART8_RTS,
#endif
#ifdef CONFIG_UART8_TXDMA
  .txdma_channel = DMAMUX_UART8_TX,
#endif
#ifdef CONFIG_UART8_RXDMA
  .rxdma_channel = DMAMUX_UART8_RX,
  .rxfifo        = g_uart8rxfifo,
#endif

#ifdef CONFIG_UART8_RS485
  .rs485_dir_gpio = GPIO_UART8_RS485_DIR,
#  if (CONFIG_UART8_RS485_DIR_POLARITY == 0)
  .rs485_dir_polarity = false,
#  else
  .rs485_dir_polarity = true,
#  endif
#endif
};
#endif

/* This table lets us iterate over the configured USARTs */

static struct up_dev_s * const g_uart_devs[AT32_NUSART] =
{
#ifdef CONFIG_AT32_USART1_SERIALDRIVER
  [0] = &g_usart1priv,
#endif
#ifdef CONFIG_AT32_USART2_SERIALDRIVER
  [1] = &g_usart2priv,
#endif
#ifdef CONFIG_AT32_USART3_SERIALDRIVER
  [2] = &g_usart3priv,
#endif
#ifdef CONFIG_AT32_UART4_SERIALDRIVER
  [3] = &g_uart4priv,
#endif
#ifdef CONFIG_AT32_UART5_SERIALDRIVER
  [4] = &g_uart5priv,
#endif
#ifdef CONFIG_AT32_USART6_SERIALDRIVER
  [5] = &g_usart6priv,
#endif
#ifdef CONFIG_AT32_UART7_SERIALDRIVER
  [6] = &g_uart7priv,
#endif
#ifdef CONFIG_AT32_UART8_SERIALDRIVER
  [7] = &g_uart8priv,
#endif
};

#ifdef CONFIG_PM
static  struct pm_callback_s g_serialcb =
{
  .notify  = up_pm_notify,
  .prepare = up_pm_prepare,
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_serialin
 ****************************************************************************/

static inline uint32_t up_serialin(struct up_dev_s *priv, int offset)
{
  return getreg32(priv->usartbase + offset);
}

/****************************************************************************
 * Name: up_serialout
 ****************************************************************************/

static inline void up_serialout(struct up_dev_s *priv, int offset,
                                uint32_t value)
{
  putreg32(value, priv->usartbase + offset);
}

/****************************************************************************
 * Name: up_setusartint_nolock
 ****************************************************************************/

static inline void up_setusartint_nolock(struct up_dev_s *priv, uint16_t ie)
{
  uint32_t cr;

  /* Save the interrupt mask */

  priv->ie = ie;

  /* And restore the interrupt state (see the interrupt enable/usage
   * table above)
   */

  cr  = up_serialin(priv, AT32_USART_CTRL1_OFFSET);
  cr &= ~(USART_CR1_USED_INTS);
  cr |= (ie & (USART_CR1_USED_INTS));
  up_serialout(priv, AT32_USART_CTRL1_OFFSET, cr);

  cr  = up_serialin(priv, AT32_USART_CTRL3_OFFSET);
  cr &= ~USART_CTRL3_ERRIEN;
  cr |= (ie & USART_CTRL3_ERRIEN);
  up_serialout(priv, AT32_USART_CTRL3_OFFSET, cr);
}

/****************************************************************************
 * Name: up_restoreusartint
 ****************************************************************************/

#if defined(HAVE_RS485) || CONSOLE_UART > 0
static void up_restoreusartint(struct up_dev_s *priv, uint16_t ie)
{
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);

  up_setusartint_nolock(priv, ie);

  spin_unlock_irqrestore(&priv->lock, flags);
}
#endif

/****************************************************************************
 * Name: up_disableusartint
 ****************************************************************************/

static void up_disableusartint(struct up_dev_s *priv, uint16_t *ie)
{
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);

  if (ie)
    {
      uint32_t cr1;
      uint32_t cr3;

      /* USART interrupts:
       *
       * Enable               Status          Meaning             Usage
       * ------------------   --------------- ------------------- ----------
       * USART_CR1_IDLEIE     USART_SR_IDLE   Idle Line Detected  (not used)
       * USART_CTRL1_RDBFIEN  USART_STS_RDBF  Rx Data Ready
       * "              "     USART_STS_ROERR Overrun Error Detected
       * USART_CTRL1_TDCIEN   USART_STS_TDC   Transmit Complete   (RS-485)
       * USART_CTRL1_TDBEIEN  USART_STS_TDBE  Tx Data Register Empty
       * USART_CTRL1_PERRIEN  USART_SR_PE     Parity Error
       *
       * USART_CTRL2_BFIEN    USART_SR_LBD    Break Flag          (not used)
       * USART_CTRL3_ERRIEN   USART_STS_FERR  Framing Error
       * "           "        USART_STS_NERR  Noise Error
       * "           "        USART_STS_ROERR Overrun Error Detected
       * USART_CTRL3_CTSCFIEN USART_SR_CTS    CTS flag            (not used)
       */

      cr1 = up_serialin(priv, AT32_USART_CTRL1_OFFSET);
      cr3 = up_serialin(priv, AT32_USART_CTRL3_OFFSET);

      /* Return the current interrupt mask value for the used interrupts.
       * Notice that this depends on the fact that none of the used interrupt
       * enable bits overlap.  This logic would fail if we needed the break
       * interrupt!
       */

      *ie = (cr1 & (USART_CR1_USED_INTS)) | (cr3 & USART_CTRL3_ERRIEN);
    }

  /* Disable all interrupts */

  up_setusartint_nolock(priv, 0);

  spin_unlock_irqrestore(&priv->lock, flags);
}

/****************************************************************************
 * Name: up_dma_nextrx
 *
 * Description:
 *   Returns the index into the RX FIFO where the DMA will place the next
 *   byte that it receives.
 *
 ****************************************************************************/

#ifdef SERIAL_HAVE_RXDMA
static int up_dma_nextrx(struct up_dev_s *priv)
{
  size_t dmaresidual;

  dmaresidual = at32_dmaresidual(priv->rxdma);

  return (RXDMA_BUFFER_SIZE - (int)dmaresidual);
}
#endif

/****************************************************************************
 * Name: up_set_format
 *
 * Description:
 *   Set the serial line format and speed.
 *
 ****************************************************************************/

#ifndef CONFIG_SUPPRESS_UART_CONFIG
static void up_set_format(struct uart_dev_s *dev)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  uint32_t regval;
  uint32_t brr;

  /* Calibrate baudrate div */

  regval = (priv->apbclock * 10 / priv->baud);
  brr = ((regval % 10) < 5) ? (regval / 10) : (regval / 10 + 1);

  up_serialout(priv, AT32_USART_BAUDR_OFFSET, brr);

  /* Load CR1 */

  regval = up_serialin(priv, AT32_USART_CTRL1_OFFSET);

  /* Configure parity mode */

  regval &= ~(USART_CTRL1_PEN | USART_CTRL1_PSEL | \
            USART_CTRL1_DBN0 | USART_CTRL1_DBN1);

  if (priv->parity == 1)       /* Odd parity */
    {
      regval |= (USART_CTRL1_PEN | USART_CTRL1_PSEL);
    }
  else if (priv->parity == 2)  /* Even parity */
    {
      regval |= USART_CTRL1_PEN;
    }

  /* Configure word length (parity uses one of configured bits)
   *
   * Default: 1 start, 8 data (no parity), n stop, OR
   *          1 start, 7 data + parity, n stop
   */

  if (priv->bits == 9 || (priv->bits == 8 && priv->parity != 0))
    {
      /* Select: 1 start, 8 data + parity, n stop, OR
       *         1 start, 9 data (no parity), n stop.
       */

      regval |= USART_CTRL1_DBN0;
    }

  up_serialout(priv, AT32_USART_CTRL1_OFFSET, regval);

  /* Configure STOP bits */

  regval = up_serialin(priv, AT32_USART_CTRL2_OFFSET);
  regval &= ~(USART_CTRL2_STOPBN_MASK);

  if (priv->stopbits2)
    {
      regval |= USART_CTRL2_STOPBN_20;
    }

  up_serialout(priv, AT32_USART_CTRL2_OFFSET, regval);

  /* Configure hardware flow control */

  regval  = up_serialin(priv, AT32_USART_CTRL3_OFFSET);
  regval &= ~(USART_CTRL3_CTSEN | USART_CTRL3_RTSEN);

#if defined(CONFIG_SERIAL_IFLOWCONTROL) && \
   !defined(CONFIG_AT32_FLOWCONTROL_BROKEN)
  if (priv->iflow && (priv->rts_gpio != 0))
    {
      regval |= USART_CTRL3_RTSEN;
    }
#endif

#ifdef CONFIG_SERIAL_OFLOWCONTROL
  if (priv->oflow && (priv->cts_gpio != 0))
    {
      regval |= USART_CTRL3_CTSEN;
    }
#endif

  up_serialout(priv, AT32_USART_CTRL3_OFFSET, regval);
}
#endif /* CONFIG_SUPPRESS_UART_CONFIG */

/****************************************************************************
 * Name: up_set_apb_clock
 *
 * Description:
 *   Enable or disable APB clock for the USART peripheral
 *
 * Input Parameters:
 *   dev - A reference to the UART driver state structure
 *   on  - Enable clock if 'on' is 'true' and disable if 'false'
 *
 ****************************************************************************/

static void up_set_apb_clock(struct uart_dev_s *dev, bool on)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  uint32_t rcc_en;
  uint32_t regaddr;

  /* Determine which USART to configure */

  switch (priv->usartbase)
    {
    default:
      return;
#ifdef CONFIG_AT32_USART1_SERIALDRIVER
    case AT32_USART1_BASE:
      rcc_en = CRM_APB2EN_USART1EN;
      regaddr = AT32_CRM_APB2EN;
      break;
#endif
#ifdef CONFIG_AT32_USART2_SERIALDRIVER
    case AT32_USART2_BASE:
      rcc_en = CRM_APB1EN_USART2EN;
      regaddr = AT32_CRM_APB1EN;
      break;
#endif
#ifdef CONFIG_AT32_USART3_SERIALDRIVER
    case AT32_USART3_BASE:
      rcc_en = CRM_APB1EN_USART3EN;
      regaddr = AT32_CRM_APB1EN;
      break;
#endif
#ifdef CONFIG_AT32_UART4_SERIALDRIVER
    case AT32_UART4_BASE:
      rcc_en = CRM_APB1EN_UART4EN;
      regaddr = AT32_CRM_APB1EN;
      break;
#endif
#ifdef CONFIG_AT32_UART5_SERIALDRIVER
    case AT32_UART5_BASE:
      rcc_en = CRM_APB1EN_UART5EN;
      regaddr = AT32_CRM_APB1EN;
      break;
#endif
#ifdef CONFIG_AT32_USART6_SERIALDRIVER
    case AT32_USART6_BASE:
      rcc_en = CRM_APB2EN_USART6EN;
      regaddr = AT32_CRM_APB2EN;
      break;
#endif
#ifdef CONFIG_AT32_UART7_SERIALDRIVER
    case AT32_UART7_BASE:
      rcc_en = CRM_APB1EN_UART7EN;
      regaddr = AT32_CRM_APB1EN;
      break;
#endif
#ifdef CONFIG_AT32_UART8_SERIALDRIVER
    case AT32_UART8_BASE:
      rcc_en = CRM_APB1EN_UART8EN;
      regaddr = AT32_CRM_APB1EN;
      break;
#endif
    }

  /* Enable/disable APB 1/2 clock for USART */

  if (on)
    {
      modifyreg32(regaddr, 0, rcc_en);
    }
  else
    {
      modifyreg32(regaddr, rcc_en, 0);
    }
}

/****************************************************************************
 * Name: up_setup
 *
 * Description:
 *   Configure the USART baud, bits, parity, etc. This method is called the
 *   first time that the serial port is opened.
 *
 ****************************************************************************/

static int up_setup(struct uart_dev_s *dev)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;

#ifndef CONFIG_SUPPRESS_UART_CONFIG
  uint32_t regval;

  /* Note: The logic here depends on the fact that that the USART module
   * was enabled in at32_lowsetup().
   */

  /* Enable USART APB1/2 clock */

  up_set_apb_clock(dev, true);

  /* Configure pins for USART use */

  at32_configgpio(priv->tx_gpio);
  at32_configgpio(priv->rx_gpio);

#ifdef CONFIG_SERIAL_OFLOWCONTROL
  if (priv->cts_gpio != 0)
    {
      at32_configgpio(priv->cts_gpio);
    }
#endif

#ifdef CONFIG_SERIAL_IFLOWCONTROL
  if (priv->rts_gpio != 0)
    {
      uint32_t config = priv->rts_gpio;

#ifdef CONFIG_AT32_FLOWCONTROL_BROKEN
      /* Instead of letting hw manage this pin, we will bitbang */

      config = (config & ~GPIO_MODE_MASK) | GPIO_OUTPUT;
#endif
      at32_configgpio(config);
    }
#endif

#ifdef HAVE_RS485
  if (priv->rs485_dir_gpio != 0)
    {
      at32_configgpio(priv->rs485_dir_gpio);
      at32_gpiowrite(priv->rs485_dir_gpio, !priv->rs485_dir_polarity);
    }
#endif

  /* Configure CR2
   * Clear STOP, CLKEN, CPOL, CPHA, LBCL, and interrupt enable bits
   */

  regval  = up_serialin(priv, AT32_USART_CTRL2_OFFSET);
  regval &= ~(USART_CTRL2_STOPBN_MASK | USART_CTRL2_CLKEN | \
            USART_CTRL2_CLKPOL | USART_CTRL2_CLKPHA | \
            USART_CTRL2_LBCP | USART_CTRL2_BFIEN);

  /* Configure STOP bits */

  if (priv->stopbits2)
    {
      regval |= USART_CTRL2_STOPBN_20;
    }

  up_serialout(priv, AT32_USART_CTRL2_OFFSET, regval);

  /* Configure CR1
   * Clear TE, REm and all interrupt enable bits
   */

  regval  = up_serialin(priv, AT32_USART_CTRL1_OFFSET);
  regval &= ~(USART_CTRL1_TEN | USART_CTRL1_REN | USART_CTRL1_ALLINTS);

  up_serialout(priv, AT32_USART_CTRL1_OFFSET, regval);

  /* Configure CR3
   * Clear CTSE, RTSE, and all interrupt enable bits
   */

  regval  = up_serialin(priv, AT32_USART_CTRL3_OFFSET);
  regval &= ~(USART_CTRL3_CTSCFIEN | USART_CTRL3_CTSEN | USART_CTRL3_RTSEN |
              USART_CTRL3_ERRIEN);

  up_serialout(priv, AT32_USART_CTRL3_OFFSET, regval);

  /* Configure the USART line format and speed. */

  up_set_format(dev);

  /* Enable Rx, Tx, and the USART */

  regval      = up_serialin(priv, AT32_USART_CTRL1_OFFSET);
  regval     |= (USART_CTRL1_UEN | USART_CTRL1_TEN | USART_CTRL1_REN);
  up_serialout(priv, AT32_USART_CTRL1_OFFSET, regval);

#endif /* CONFIG_SUPPRESS_UART_CONFIG */

  /* Set up the cached interrupt enables value */

  priv->ie    = 0;

  spin_lock_init(&priv->lock);

  /* Mark device as initialized. */

  priv->initialized = true;

  return OK;
}

/****************************************************************************
 * Name: up_dma_setup
 *
 * Description:
 *   Configure the USART baud, bits, parity, etc. This method is called the
 *   first time that the serial port is opened.
 *
 ****************************************************************************/

#if defined(SERIAL_HAVE_RXDMA) || defined(SERIAL_HAVE_TXDMA)
static int up_dma_setup(struct uart_dev_s *dev)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  int result;

  /* Do the basic UART setup first, unless we are the console */

  if (!dev->isconsole)
    {
      result = up_setup(dev);
      if (result != OK)
        {
          return result;
        }
    }

#if defined(SERIAL_HAVE_TXDMA)
  /* Acquire the Tx DMA channel.  This should always succeed. */

  if (priv->txdma_channel != INVALID_SERIAL_DMA_CHANNEL)
    {
      priv->txdma = at32_dmachannel(priv->txdma_channel);

      /* Enable receive Tx DMA for the UART */

      modifyreg32(priv->usartbase + AT32_USART_CTRL3_OFFSET,
                  0, USART_CTRL3_DMATEN);
    }
#endif

#if defined(SERIAL_HAVE_RXDMA)
  /* Acquire the DMA channel.  This should always succeed. */

  if (priv->rxdma_channel != INVALID_SERIAL_DMA_CHANNEL)
    {
      priv->rxdma = at32_dmachannel(priv->rxdma_channel);

      /* Configure for circular DMA reception into the RX fifo */

      at32_dmasetup(priv->rxdma,
                     priv->usartbase + AT32_USART_RDR_OFFSET,
                     (uint32_t)priv->rxfifo,
                     RXDMA_BUFFER_SIZE,
                     SERIAL_RXDMA_CONTROL_WORD);

      /* Reset our DMA shadow pointer to match the address just
       * programmed above.
       */

      priv->rxdmanext = 0;

      /* Enable receive Rx DMA for the UART */

      modifyreg32(priv->usartbase + AT32_USART_CTRL3_OFFSET,
                  0, USART_CTRL3_DMAREN);

      /* Start the DMA channel, and arrange for callbacks at the half and
       * full points in the FIFO.  This ensures that we have half a FIFO
       * worth of time to claim bytes before they are overwritten.
       */

      at32_dmastart(priv->rxdma, up_dma_rxcallback, (void *)priv, true);
    }
#endif

  return OK;
}
#endif

/****************************************************************************
 * Name: up_shutdown
 *
 * Description:
 *   Disable the USART.  This method is called when the serial
 *   port is closed
 *
 ****************************************************************************/

static void up_shutdown(struct uart_dev_s *dev)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  uint32_t regval;

  /* Mark device as uninitialized. */

  priv->initialized = false;

  /* Disable all interrupts */

  up_disableusartint(priv, NULL);

  /* Disable USART APB1/2 clock */

  up_set_apb_clock(dev, false);

  /* Disable Rx, Tx, and the UART */

  regval  = up_serialin(priv, AT32_USART_CTRL1_OFFSET);
  regval &= ~(USART_CTRL1_UEN | USART_CTRL1_TEN | USART_CTRL1_REN);
  up_serialout(priv, AT32_USART_CTRL1_OFFSET, regval);

  /* Release pins. "If the serial-attached device is powered down, the TX
   * pin causes back-powering, potentially confusing the device to the point
   * of complete lock-up."
   *
   * REVISIT:  Is unconfiguring the pins appropriate for all device?  If not,
   * then this may need to be a configuration option.
   */

  at32_unconfiggpio(priv->tx_gpio);
  at32_unconfiggpio(priv->rx_gpio);

#ifdef CONFIG_SERIAL_OFLOWCONTROL
  if (priv->cts_gpio != 0)
    {
      at32_unconfiggpio(priv->cts_gpio);
    }
#endif

#ifdef CONFIG_SERIAL_IFLOWCONTROL
  if (priv->rts_gpio != 0)
    {
      at32_unconfiggpio(priv->rts_gpio);
    }
#endif

#ifdef HAVE_RS485
  if (priv->rs485_dir_gpio != 0)
    {
      at32_unconfiggpio(priv->rs485_dir_gpio);
    }
#endif
}

/****************************************************************************
 * Name: up_dma_shutdown
 *
 * Description:
 *   Disable the USART.  This method is called when the serial
 *   port is closed
 *
 ****************************************************************************/

#if defined(SERIAL_HAVE_RXDMA) || defined(SERIAL_HAVE_TXDMA)
static void up_dma_shutdown(struct uart_dev_s *dev)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;

  /* Perform the normal UART shutdown */

  up_shutdown(dev);

#if defined(SERIAL_HAVE_RXDMA)
  /* Stop the RX DMA channel */

  if (priv->rxdma_channel != INVALID_SERIAL_DMA_CHANNEL)
    {
      at32_dmastop(priv->rxdma);

      /* Release the RX DMA channel */

      at32_dmafree(priv->rxdma);
      priv->rxdma = NULL;
    }
#endif

#if defined(SERIAL_HAVE_TXDMA)
  /* Stop the TX DMA channel */

  if (priv->txdma_channel != INVALID_SERIAL_DMA_CHANNEL)
    {
      at32_dmastop(priv->txdma);

      /* Release the TX DMA channel */

      at32_dmafree(priv->txdma);
      priv->txdma = NULL;
    }
#endif
}
#endif

/****************************************************************************
 * Name: up_attach
 *
 * Description:
 *   Configure the USART to operation in interrupt driven mode.  This method
 *   is called when the serial port is opened.  Normally, this is just after
 *   the setup() method is called, however, the serial console may operate
 *   in a non-interrupt driven mode during the boot phase.
 *
 *   RX and TX interrupts are not enabled when by the attach method (unless
 *   the hardware supports multiple levels of interrupt enabling).  The RX
 *   and TX interrupts are not enabled until the txint() and rxint() methods
 *   are called.
 *
 ****************************************************************************/

static int up_attach(struct uart_dev_s *dev)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  int ret;

  /* Attach and enable the IRQ */

  ret = irq_attach(priv->irq, up_interrupt, priv);
  if (ret == OK)
    {
      /* Enable the interrupt (RX and TX interrupts are still disabled
       * in the USART
       */

      up_enable_irq(priv->irq);
    }

  return ret;
}

/****************************************************************************
 * Name: up_detach
 *
 * Description:
 *   Detach USART interrupts.  This method is called when the serial port is
 *   closed normally just before the shutdown method is called.  The
 *   exception is the serial console which is never shutdown.
 *
 ****************************************************************************/

static void up_detach(struct uart_dev_s *dev)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  up_disable_irq(priv->irq);
  irq_detach(priv->irq);
}

/****************************************************************************
 * Name: up_interrupt
 *
 * Description:
 *   This is the UART interrupt handler.  It will be invoked when an
 *   interrupt is received on the 'irq'.  It should call uart_xmitchars or
 *   uart_recvchars to perform the appropriate data transfers.  The
 *   interrupt handling logic must be able to map the 'arg' to the
 *   appropriate uart_dev_s structure in order to call these functions.
 *
 ****************************************************************************/

static int up_interrupt(int irq, void *context, void *arg)
{
  struct up_dev_s *priv = (struct up_dev_s *)arg;
  int  passes;
  bool handled;

  DEBUGASSERT(priv != NULL);

  /* Report serial activity to the power management logic */

#if defined(CONFIG_PM) && CONFIG_AT32_PM_SERIAL_ACTIVITY > 0
  pm_activity(PM_IDLE_DOMAIN, CONFIG_AT32_PM_SERIAL_ACTIVITY);
#endif

  /* Loop until there are no characters to be transferred or,
   * until we have been looping for a long time.
   */

  handled = true;
  for (passes = 0; passes < 256 && handled; passes++)
    {
      handled = false;

      /* Get the masked USART status word. */

      priv->sr = up_serialin(priv, AT32_USART_STS_OFFSET);

      /* USART interrupts:
       *
       * Enable               Status          Meaning                Usage
       * ------------------   --------------- ------------------- ----------
       * USART_CR1_IDLEIE     USART_SR_IDLE   Idle Line Detected  (not used)
       * USART_CTRL1_RDBFIEN  USART_STS_RDBF  Rx Data Ready
       * "              "     USART_STS_ROERR Overrun Error Detected
       * USART_CTRL1_TDCIEN   USART_STS_TDC   Tx Complete         (RS-485)
       * USART_CTRL1_TDBEIEN  USART_STS_TDBE  Tx Data Register Empty
       * USART_CTRL1_PERRIEN  USART_SR_PE     Parity Error
       *
       * USART_CTRL2_BFIEN    USART_SR_LBD    Break Flag          (not used)
       * USART_CTRL3_ERRIEN   USART_STS_FERR  Framing Error
       * "           "        USART_STS_NERR  Noise Error
       * "           "        USART_STS_ROERR Overrun Error Detected
       * USART_CTRL3_CTSCFIEN USART_SR_CTS    CTS flag            (not used)
       *
       * NOTE: Some of these status bits must be cleared by explicitly
       * writing zero to the SR register: USART_SR_CTS, USART_SR_LBD. Note of
       * those are currently being used.
       */

#ifdef HAVE_RS485
      /* Transmission of whole buffer is over - TC is set, TXEIE is cleared.
       * Note - this should be first, to have the most recent TC bit value
       * from SR register - sending data affects TC, but without refresh we
       * will not know that...
       */

      if (((priv->sr & USART_STS_TDC) != 0) &&
          ((priv->ie & USART_CTRL1_TDCIEN) != 0) &&
          ((priv->ie & USART_CTRL1_TDBEIEN) == 0))
        {
          at32_gpiowrite(priv->rs485_dir_gpio, !priv->rs485_dir_polarity);
          up_restoreusartint(priv, priv->ie & ~USART_CTRL1_TDCIEN);
        }
#endif

      /* Handle incoming, receive bytes. */

      if (((priv->sr & USART_STS_RDBF) != 0) &&
          ((priv->ie & USART_CTRL1_RDBFIEN) != 0))
        {
          /* Received data ready... process incoming bytes.  NOTE the check
           * for RXNEIE:  We cannot call uart_recvchards of RX interrupts are
           * disabled.
           */

          uart_recvchars(&priv->dev);
          handled = true;
        }

      /* We may still have to read from the DR register to clear any pending
       * error conditions.
       */

      else if ((priv->sr & (USART_STS_ROERR | \
              USART_STS_NERR | USART_STS_FERR)) != 0)
        {
          /* If an error occurs, read from DR to clear the error (data has
           * been lost).  If ORE is set along with RXNE then it tells you
           * that the byte *after* the one in the data register has been
           * lost, but the data register value is correct.  That case will
           * be handled above if interrupts are enabled. Otherwise, that
           * good byte will be lost.
           */

          up_serialin(priv, AT32_USART_DT_OFFSET);
        }

      /* Handle outgoing, transmit bytes */

      if (((priv->sr & USART_STS_TDBE) != 0) &&
          ((priv->ie & USART_CTRL1_TDBEIEN) != 0))
        {
          /* Transmit data register empty ... process outgoing bytes */

          uart_xmitchars(&priv->dev);
          handled = true;
        }
    }

  return OK;
}

/****************************************************************************
 * Name: up_ioctl
 *
 * Description:
 *   All ioctl calls will be routed through this method
 *
 ****************************************************************************/

static int up_ioctl(struct file *filep, int cmd, unsigned long arg)
{
#if defined(CONFIG_SERIAL_TERMIOS) || defined(CONFIG_SERIAL_TIOCSERGSTRUCT) \
    || defined(CONFIG_AT32_SERIALBRK_BSDCOMPAT)
  struct inode      *inode = filep->f_inode;
  struct uart_dev_s *dev   = inode->i_private;
#endif
#if defined(CONFIG_SERIAL_TERMIOS) || defined(CONFIG_AT32_SERIALBRK_BSDCOMPAT)
  struct up_dev_s   *priv  = (struct up_dev_s *)dev->priv;
#endif
  int                ret   = OK;

  switch (cmd)
    {
#ifdef CONFIG_SERIAL_TIOCSERGSTRUCT
    case TIOCSERGSTRUCT:
      {
        struct up_dev_s *user = (struct up_dev_s *)arg;
        if (!user)
          {
            ret = -EINVAL;
          }
        else
          {
            memcpy(user, dev, sizeof(struct up_dev_s));
          }
      }
      break;
#endif

#ifdef CONFIG_AT32_USART_SINGLEWIRE
    case TIOCSSINGLEWIRE:
      {
        /* Change the TX port to be open-drain/push-pull and enable/disable
         * half-duplex mode.
         */

        uint32_t cr = up_serialin(priv, AT32_USART_CTRL3_OFFSET);

        if ((arg & SER_SINGLEWIRE_ENABLED) != 0)
          {
            uint32_t gpio_val = (arg & SER_SINGLEWIRE_PUSHPULL) ==
                                 SER_SINGLEWIRE_PUSHPULL ?
                                 GPIO_PUSHPULL : GPIO_OPENDRAIN;
            gpio_val |= ((arg & SER_SINGLEWIRE_PULL_MASK) ==
                         SER_SINGLEWIRE_PULLUP) ? GPIO_PULLUP
                                                : GPIO_FLOAT;
            gpio_val |= ((arg & SER_SINGLEWIRE_PULL_MASK) ==
                         SER_SINGLEWIRE_PULLDOWN) ? GPIO_PULLDOWN
                                                  : GPIO_FLOAT;
            at32_configgpio((priv->tx_gpio & ~(GPIO_PUPD_MASK |
                                                GPIO_OPENDRAIN)) |
                             gpio_val);
            cr |= USART_CTRL3_SLBEN;
          }
        else
          {
            at32_configgpio((priv->tx_gpio & ~(GPIO_PUPD_MASK |
                                                GPIO_OPENDRAIN)) |
                             GPIO_PUSHPULL);
            cr &= ~USART_CTRL3_SLBEN;
          }

        up_serialout(priv, AT32_USART_CTRL3_OFFSET, cr);
      }
     break;
#endif

#ifdef CONFIG_SERIAL_TERMIOS
    case TCGETS:
      {
        struct termios *termiosp = (struct termios *)arg;

        if (!termiosp)
          {
            ret = -EINVAL;
            break;
          }

        /* Note that since we only support 8/9 bit modes and
         * there is no way to report 9-bit mode, we always claim 8.
         */

        termiosp->c_cflag =
          ((priv->parity != 0) ? PARENB : 0) |
          ((priv->parity == 1) ? PARODD : 0) |
          ((priv->stopbits2) ? CSTOPB : 0) |
#ifdef CONFIG_SERIAL_OFLOWCONTROL
          ((priv->oflow) ? CCTS_OFLOW : 0) |
#endif
#ifdef CONFIG_SERIAL_IFLOWCONTROL
          ((priv->iflow) ? CRTS_IFLOW : 0) |
#endif
          CS8;

        cfsetispeed(termiosp, priv->baud);

        /* TODO: CRTS_IFLOW, CCTS_OFLOW */
      }
      break;

    case TCSETS:
      {
        struct termios *termiosp = (struct termios *)arg;

        if (!termiosp)
          {
            ret = -EINVAL;
            break;
          }

        /* Perform some sanity checks before accepting any changes */

        if (((termiosp->c_cflag & CSIZE) != CS8)
#ifdef CONFIG_SERIAL_OFLOWCONTROL
            || ((termiosp->c_cflag & CCTS_OFLOW) && (priv->cts_gpio == 0))
#endif
#ifdef CONFIG_SERIAL_IFLOWCONTROL
            || ((termiosp->c_cflag & CRTS_IFLOW) && (priv->rts_gpio == 0))
#endif
           )
          {
            ret = -EINVAL;
            break;
          }

        if (termiosp->c_cflag & PARENB)
          {
            priv->parity = (termiosp->c_cflag & PARODD) ? 1 : 2;
          }
        else
          {
            priv->parity = 0;
          }

        priv->stopbits2 = (termiosp->c_cflag & CSTOPB) != 0;
#ifdef CONFIG_SERIAL_OFLOWCONTROL
        priv->oflow = (termiosp->c_cflag & CCTS_OFLOW) != 0;
#endif
#ifdef CONFIG_SERIAL_IFLOWCONTROL
        priv->iflow = (termiosp->c_cflag & CRTS_IFLOW) != 0;
#endif

        /* Note that since there is no way to request 9-bit mode
         * and no way to support 5/6/7-bit modes, we ignore them
         * all here.
         */

        /* Note that only cfgetispeed is used because we have knowledge
         * that only one speed is supported.
         */

        priv->baud = cfgetispeed(termiosp);

        /* Effect the changes immediately - note that we do not implement
         * TCSADRAIN / TCSAFLUSH
         */

        up_set_format(dev);
      }
      break;
#endif /* CONFIG_SERIAL_TERMIOS */

#ifdef CONFIG_AT32_USART_BREAKS
#  ifdef CONFIG_AT32_SERIALBRK_BSDCOMPAT
    case TIOCSBRK:  /* BSD compatibility: Turn break on, unconditionally */
      {
        irqstate_t flags;
        uint32_t tx_break;

        flags = spin_lock_irqsave(&priv->lock);

        /* Disable any further tx activity */

        priv->ie |= USART_CR1_IE_BREAK_INPROGRESS;

        up_txint_nolock(dev, false);

        /* Configure TX as a GPIO output pin and Send a break signal */

        tx_break = GPIO_OUTPUT | (~(GPIO_MODE_MASK | GPIO_OUTPUT_SET) &
                                  priv->tx_gpio);
        at32_configgpio(tx_break);

        spin_unlock_irqrestore(&priv->lock, flags);
      }
      break;

    case TIOCCBRK:  /* BSD compatibility: Turn break off, unconditionally */
      {
        irqstate_t flags;

        flags = spin_lock_irqsave(&priv->lock);

        /* Configure TX back to U(S)ART */

        at32_configgpio(priv->tx_gpio);

        priv->ie &= ~USART_CR1_IE_BREAK_INPROGRESS;

        /* Enable further tx activity */

        up_txint_nolock(dev, true);

        spin_unlock_irqrestore(&priv->lock, flags);
      }
      break;
#  else
    case TIOCSBRK:  /* No BSD compatibility: Turn break on for M bit times */
      {
        uint32_t cr1;
        irqstate_t flags;

        flags = spin_lock_irqsave(&priv->lock);
        cr1   = up_serialin(priv, AT32_USART_CTRL1_OFFSET);
        up_serialout(priv, AT32_USART_CTRL1_OFFSET, cr1 | USART_CR1_SBK);
        spin_unlock_irqrestore(&priv->lock, flags);
      }
      break;

    case TIOCCBRK:  /* No BSD compatibility: May turn off break too soon */
      {
        uint32_t cr1;
        irqstate_t flags;

        flags = spin_lock_irqsave(&priv->lock);
        cr1   = up_serialin(priv, AT32_USART_CTRL1_OFFSET);
        up_serialout(priv, AT32_USART_CTRL1_OFFSET, cr1 & ~USART_CR1_SBK);
        spin_unlock_irqrestore(&priv->lock, flags);
      }
      break;
#  endif
#endif

    default:
      ret = -ENOTTY;
      break;
    }

  return ret;
}

/****************************************************************************
 * Name: up_receive
 *
 * Description:
 *   Called (usually) from the interrupt level to receive one
 *   character from the USART.  Error bits associated with the
 *   receipt are provided in the return 'status'.
 *
 ****************************************************************************/

#if defined(SERIAL_HAVE_TXDMA_OPS) || defined(SERIAL_HAVE_NODMA_OPS)
static int up_receive(struct uart_dev_s *dev, unsigned int *status)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  uint32_t rdr;

  /* Get the Rx byte */

  rdr      = up_serialin(priv, AT32_USART_DT_OFFSET);

  /* Get the Rx byte plux error information.  Return those in status */

  *status  = priv->sr << 16 | rdr;
  priv->sr = 0;

  /* Then return the actual received byte */

  return rdr & 0xff;
}
#endif

/****************************************************************************
 * Name: up_rxint
 *
 * Description:
 *   Call to enable or disable RX interrupts
 *
 ****************************************************************************/

#if defined(SERIAL_HAVE_TXDMA_OPS) || defined(SERIAL_HAVE_NODMA_OPS)
static void up_rxint_nolock(struct uart_dev_s *dev, bool enable)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  uint16_t ie;

  /* USART receive interrupts:
   *
   * Enable               Status          Meaning                   Usage
   * ------------------   --------------- ---------------------- ----------
   * USART_CR1_IDLEIE     USART_SR_IDLE   Idle Line Detected     (not used)
   * USART_CTRL1_RDBFIEN  USART_STS_RDBF  Rx Data Ready to be Read
   * "              "     USART_STS_ROERR Overrun Error Detected
   * USART_CTRL1_PERRIEN  USART_SR_PE     Parity Error
   *
   * USART_CTRL2_BFIEN    USART_SR_LBD    Break Flag             (not used)
   * USART_CTRL3_ERRIEN   USART_STS_FERR  Framing Error
   * "           "        USART_STS_NERR  Noise Error
   * "           "        USART_STS_ROERR Overrun Error Detected
   */

  ie = priv->ie;
  if (enable)
    {
      /* Receive an interrupt when their is anything in the Rx data register
       * (or an Rx timeout occurs).
       */

#ifndef CONFIG_SUPPRESS_SERIAL_INTS
#ifdef CONFIG_USART_ERRINTS
      ie |= (USART_CTRL1_RDBFIEN | USART_CTRL1_PERRIEN | USART_CTRL3_ERRIEN);
#else
      ie |= USART_CTRL1_RDBFIEN;
#endif
#endif
    }
  else
    {
      ie &= ~(USART_CTRL1_RDBFIEN | USART_CTRL1_PERRIEN | \
      USART_CTRL3_ERRIEN);
    }

  /* Then set the new interrupt state */

  up_setusartint_nolock(priv, ie);
}

static void up_rxint(struct uart_dev_s *dev, bool enable)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);
  up_rxint_nolock(dev, enable);
  spin_unlock_irqrestore(&priv->lock, flags);
}
#endif

/****************************************************************************
 * Name: up_rxavailable
 *
 * Description:
 *   Return true if the receive register is not empty
 *
 ****************************************************************************/

#if defined(SERIAL_HAVE_TXDMA_OPS) || defined(SERIAL_HAVE_NODMA_OPS)
static bool up_rxavailable(struct uart_dev_s *dev)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  return ((up_serialin(priv, AT32_USART_STS_OFFSET) & USART_STS_RDBF) != 0);
}
#endif

/****************************************************************************
 * Name: up_rxflowcontrol
 *
 * Description:
 *   Called when Rx buffer is full (or exceeds configured watermark levels
 *   if CONFIG_SERIAL_IFLOWCONTROL_WATERMARKS is defined).
 *   Return true if UART activated RX flow control to block more incoming
 *   data
 *
 * Input Parameters:
 *   dev       - UART device instance
 *   nbuffered - the number of characters currently buffered
 *               (if CONFIG_SERIAL_IFLOWCONTROL_WATERMARKS is
 *               not defined the value will be 0 for an empty buffer or the
 *               defined buffer size for a full buffer)
 *   upper     - true indicates the upper watermark was crossed where
 *               false indicates the lower watermark has been crossed
 *
 * Returned Value:
 *   true if RX flow control activated.
 *
 ****************************************************************************/

#ifdef CONFIG_SERIAL_IFLOWCONTROL
static bool up_rxflowcontrol(struct uart_dev_s *dev,
                             unsigned int nbuffered, bool upper)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;

#if defined(CONFIG_SERIAL_IFLOWCONTROL_WATERMARKS) && \
    defined(CONFIG_AT32_FLOWCONTROL_BROKEN)
  if (priv->iflow && (priv->rts_gpio != 0))
    {
      /* Assert/de-assert nRTS set it high resume/stop sending */

      at32_gpiowrite(priv->rts_gpio, upper);

      if (upper)
        {
          /* With heavy Rx traffic, RXNE might be set and data pending.
           * Returning 'true' in such case would cause RXNE left unhandled
           * and causing interrupt storm. Sending end might be also be slow
           * to react on nRTS, and returning 'true' here would prevent
           * processing that data.
           *
           * Therefore, return 'false' so input data is still being processed
           * until sending end reacts on nRTS signal and stops sending more.
           */

          return false;
        }

      return upper;
    }

#else
  if (priv->iflow)
    {
      /* Is the RX buffer full? */

      if (upper)
        {
          /* Disable Rx interrupt to prevent more data being from
           * peripheral.  When hardware RTS is enabled, this will
           * prevent more data from coming in.
           *
           * This function is only called when UART recv buffer is full,
           * that is: "dev->recv.head + 1 == dev->recv.tail".
           *
           * Logic in "uart_read" will automatically toggle Rx interrupts
           * when buffer is read empty and thus we do not have to re-
           * enable Rx interrupts.
           */

          uart_disablerxint(dev);
          return true;
        }

      /* No.. The RX buffer is empty */

      else
        {
          /* We might leave Rx interrupt disabled if full recv buffer was
           * read empty.  Enable Rx interrupt to make sure that more input is
           * received.
           */

          uart_enablerxint(dev);
        }
    }
#endif

  return false;
}
#endif

/****************************************************************************
 * Name: up_dma_receive
 *
 * Description:
 *   Called (usually) from the interrupt level to receive one
 *   character from the USART.  Error bits associated with the
 *   receipt are provided in the return 'status'.
 *
 ****************************************************************************/

#ifdef SERIAL_HAVE_RXDMA
static int up_dma_receive(struct uart_dev_s *dev, unsigned int *status)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  int c = 0;

  if (up_dma_nextrx(priv) != priv->rxdmanext)
    {
      c = priv->rxfifo[priv->rxdmanext];

      priv->rxdmanext++;
      if (priv->rxdmanext == RXDMA_BUFFER_SIZE)
        {
          priv->rxdmanext = 0;
        }
    }

  return c;
}
#endif

/****************************************************************************
 * Name: up_dma_rxint
 *
 * Description:
 *   Call to enable or disable RX interrupts
 *
 ****************************************************************************/

#ifdef SERIAL_HAVE_RXDMA
static void up_dma_rxint(struct uart_dev_s *dev, bool enable)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;

  /* En/disable DMA reception.
   *
   * Note that it is not safe to check for available bytes and immediately
   * pass them to uart_recvchars as that could potentially recurse back
   * to us again.  Instead, bytes must wait until the next up_dma_poll or
   * DMA event.
   */

  priv->rxenable = enable;
}
#endif

/****************************************************************************
 * Name: up_dma_rxavailable
 *
 * Description:
 *   Return true if the receive register is not empty
 *
 ****************************************************************************/

#ifdef SERIAL_HAVE_RXDMA
static bool up_dma_rxavailable(struct uart_dev_s *dev)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;

  /* Compare our receive pointer to the current DMA pointer, if they
   * do not match, then there are bytes to be received.
   */

  return (up_dma_nextrx(priv) != priv->rxdmanext);
}
#endif

/****************************************************************************
 * Name: up_dma_txcallback
 *
 * Description:
 *   This function clears dma buffer at complete of DMA transfer and wakes up
 *   threads waiting for space in buffer.
 *
 ****************************************************************************/

#ifdef SERIAL_HAVE_TXDMA
static void up_dma_txcallback(DMA_HANDLE handle, uint8_t status, void *arg)
{
  struct up_dev_s *priv = (struct up_dev_s *)arg;

  /* Update 'nbytes' indicating number of bytes actually transferred by DMA.
   * This is important to free TX buffer space by 'uart_xmitchars_done'.
   */

  if (status & DMA_ISR_TCIF_BIT)
    {
      priv->dev.dmatx.nbytes += priv->dev.dmatx.length;
      if (priv->dev.dmatx.nlength)
        {
          /* Set up DMA on next buffer */

          at32_dmasetup(priv->txdma,
                         priv->usartbase + AT32_USART_DT_OFFSET,
                         (uint32_t) priv->dev.dmatx.nbuffer,
                         (size_t) priv->dev.dmatx.nlength,
                         SERIAL_TXDMA_CONTROL_WORD);

          /* Set length for the next completion */

          priv->dev.dmatx.length  = priv->dev.dmatx.nlength;
          priv->dev.dmatx.nlength = 0;

          /* Start transmission with the callback on DMA completion */

          at32_dmastart(priv->txdma, up_dma_txcallback,
                         (void *)priv, false);

          return;
        }
    }
  else if (status & DMA_ISR_HTIF_BIT)
    {
      priv->dev.dmatx.nbytes += priv->dev.dmatx.length / 2;
    }

  /* Adjust the pointers */

  uart_xmitchars_done(&priv->dev);
}
#endif

/****************************************************************************
 * Name: up_dma_txavailable
 *
 * Description:
 *        Informs DMA that Tx data is available and is ready for transfer.
 *
 ****************************************************************************/

#ifdef SERIAL_HAVE_TXDMA
static void up_dma_txavailable(struct uart_dev_s *dev)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;

  /* Only send when the DMA is idle */

  if (at32_dmaresidual(priv->txdma) == 0)
    {
      uart_xmitchars_dma(dev);
    }
}
#endif

/****************************************************************************
 * Name: up_dma_send
 *
 * Description:
 *   Called (usually) from the interrupt level to start DMA transfer.
 *   (Re-)Configures DMA Stream updating buffer and buffer length.
 *
 ****************************************************************************/

#ifdef SERIAL_HAVE_TXDMA
static void up_dma_send(struct uart_dev_s *dev)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;

  /* We need to stop DMA before reconfiguration */

  at32_dmastop(priv->txdma);

  /* Reset the number sent */

  dev->dmatx.nbytes = 0;

  /* Make use of setup function to update buffer and its length for
   * next transfer
   */

  at32_dmasetup(priv->txdma,
                 priv->usartbase + AT32_USART_DT_OFFSET,
                 (uint32_t) dev->dmatx.buffer,
                 (size_t) dev->dmatx.length,
                 SERIAL_TXDMA_CONTROL_WORD);

  /* Start transmission with the callback on DMA completion */

  at32_dmastart(priv->txdma, up_dma_txcallback, (void *)priv, false);
}
#endif

/****************************************************************************
 * Name: up_send
 *
 * Description:
 *   This method will send one byte on the USART
 *
 ****************************************************************************/

static void up_send(struct uart_dev_s *dev, int ch)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
#ifdef HAVE_RS485
  if (priv->rs485_dir_gpio != 0)
    {
      at32_gpiowrite(priv->rs485_dir_gpio, priv->rs485_dir_polarity);
    }
#endif

  up_serialout(priv, AT32_USART_DT_OFFSET, (uint32_t)ch);
}

/****************************************************************************
 * Name: up_dma_txint
 *
 * Description:
 *   Call to enable or disable TX interrupts from the UART.
 *
 ****************************************************************************/

#ifdef SERIAL_HAVE_TXDMA
static void up_dma_txint(struct uart_dev_s *dev, bool enable)
{
  /* Nothing to do. */

  /* In case of DMA transfer we do not want to make use of UART interrupts.
   * Instead, we use DMA interrupts that are activated once during boot
   * sequence. Furthermore we can use up_dma_txcallback() to handle staff at
   * half DMA transfer or after transfer completion (depending configuration,
   * see at32_dmastart(...) ).
   */
}
#endif

/****************************************************************************
 * Name: up_txint
 *
 * Description:
 *   Call to enable or disable TX interrupts
 *
 ****************************************************************************/

#if defined(SERIAL_HAVE_RXDMA_OPS) || defined(SERIAL_HAVE_NODMA_OPS) || \
    defined(CONFIG_AT32_SERIALBRK_BSDCOMPAT)

static void up_txint_nolock(struct uart_dev_s *dev, bool enable)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;

  /* USART transmit interrupts:
   *
   * Enable             Status          Meaning                 Usage
   * ------------------ --------------- ----------------------- ----------
   * USART_CTRL1_TDCIEN     USART_STS_TDC     Tx Complete       (RS-485)
   * USART_CTRL1_TDBEIEN    USART_STS_TDBE    Tx Data Register Empty
   * USART_CTRL3_CTSCFIEN   USART_SR_CTS      CTS flag          (not used)
   */

  if (enable)
    {
      /* Set to receive an interrupt when the TX data register is empty */

#ifndef CONFIG_SUPPRESS_SERIAL_INTS
      uint16_t ie = priv->ie | USART_CTRL1_TDBEIEN;

      /* If RS-485 is supported on this U[S]ART, then also enable the
       * transmission complete interrupt.
       */

#  ifdef HAVE_RS485
      if (priv->rs485_dir_gpio != 0)
        {
          ie |= USART_CTRL1_TDCIEN;
        }
#  endif

#  ifdef CONFIG_AT32_SERIALBRK_BSDCOMPAT
      if (priv->ie & USART_CR1_IE_BREAK_INPROGRESS)
        {
          return;
        }
#  endif

      up_setusartint_nolock(priv, ie);

#else
      /* Fake a TX interrupt here by just calling uart_xmitchars() with
       * interrupts disabled (note this may recurse).
       */

      uart_xmitchars(dev);
#endif
    }
  else
    {
      /* Disable the TX interrupt */

      up_setusartint_nolock(priv, priv->ie & ~USART_CTRL1_TDBEIEN);
    }
}

static void up_txint(struct uart_dev_s *dev, bool enable)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);
  up_txint_nolock(dev, enable);
  spin_unlock_irqrestore(&priv->lock, flags);
}

#endif

/****************************************************************************
 * Name: up_txready
 *
 * Description:
 *   Return true if the transmit data register is empty
 *
 ****************************************************************************/

static bool up_txready(struct uart_dev_s *dev)
{
  struct up_dev_s *priv = (struct up_dev_s *)dev->priv;
  return ((up_serialin(priv, AT32_USART_STS_OFFSET) & USART_STS_TDBE) != 0);
}

/****************************************************************************
 * Name: up_dma_rxcallback
 *
 * Description:
 *   This function checks the current DMA state and calls the generic
 *   serial stack when bytes appear to be available.
 *
 ****************************************************************************/

#ifdef SERIAL_HAVE_RXDMA
static void up_dma_rxcallback(DMA_HANDLE handle, uint8_t status, void *arg)
{
  struct up_dev_s *priv = (struct up_dev_s *)arg;

  if (priv->rxenable && up_dma_rxavailable(&priv->dev))
    {
      uart_recvchars(&priv->dev);
    }
}
#endif

/****************************************************************************
 * Name: up_pm_notify
 *
 * Description:
 *   Notify the driver of new power state. This callback is  called after
 *   all drivers have had the opportunity to prepare for the new power state.
 *
 * Input Parameters:
 *
 *    cb - Returned to the driver. The driver version of the callback
 *         structure may include additional, driver-specific state data at
 *         the end of the structure.
 *
 *    pmstate - Identifies the new PM state
 *
 * Returned Value:
 *   None - The driver already agreed to transition to the low power
 *   consumption state when when it returned OK to the prepare() call.
 *
 *
 ****************************************************************************/

#ifdef CONFIG_PM
static void up_pm_notify(struct pm_callback_s *cb, int domain,
                         enum pm_state_e pmstate)
{
  switch (pmstate)
    {
      case (PM_NORMAL):
        {
          /* Logic for PM_NORMAL goes here */
        }
        break;

      case (PM_IDLE):
        {
          /* Logic for PM_IDLE goes here */
        }
        break;

      case (PM_STANDBY):
        {
          /* Logic for PM_STANDBY goes here */
        }
        break;

      case (PM_SLEEP):
        {
          /* Logic for PM_SLEEP goes here */
        }
        break;

      default:
        {
          /* Should not get here */
        }
        break;
    }
}
#endif

/****************************************************************************
 * Name: up_pm_prepare
 *
 * Description:
 *   Request the driver to prepare for a new power state. This is a warning
 *   that the system is about to enter into a new power state. The driver
 *   should begin whatever operations that may be required to enter power
 *   state. The driver may abort the state change mode by returning a
 *   non-zero value from the callback function.
 *
 * Input Parameters:
 *
 *    cb - Returned to the driver. The driver version of the callback
 *         structure may include additional, driver-specific state data at
 *         the end of the structure.
 *
 *    pmstate - Identifies the new PM state
 *
 * Returned Value:
 *   Zero - (OK) means the event was successfully processed and that the
 *          driver is prepared for the PM state change.
 *
 *   Non-zero - means that the driver is not prepared to perform the tasks
 *              needed achieve this power setting and will cause the state
 *              change to be aborted. NOTE: The prepare() method will also
 *              be called when reverting from lower back to higher power
 *              consumption modes (say because another driver refused a
 *              lower power state change). Drivers are not permitted to
 *              return non-zero values when reverting back to higher power
 *              consumption modes!
 *
 *
 ****************************************************************************/

#ifdef CONFIG_PM
static int up_pm_prepare(struct pm_callback_s *cb, int domain,
                         enum pm_state_e pmstate)
{
  /* Logic to prepare for a reduced power state goes here. */

  return OK;
}
#endif
#endif /* HAVE_SERIALDRIVER */
#endif /* USE_SERIALDRIVER */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef USE_SERIALDRIVER

/****************************************************************************
 * Name: at32_serial_get_uart
 *
 * Description:
 *   Get serial driver structure for AT32 USART
 *
 ****************************************************************************/

#ifdef HAVE_SERIALDRIVER
uart_dev_t *at32_serial_get_uart(int uart_num)
{
  int uart_idx = uart_num - 1;

  if (uart_idx < 0 || uart_idx >= AT32_NUSART || !g_uart_devs[uart_idx])
    {
      return NULL;
    }

  if (!g_uart_devs[uart_idx]->initialized)
    {
      return NULL;
    }

  return &g_uart_devs[uart_idx]->dev;
}
#endif /* HAVE_SERIALDRIVER */

/****************************************************************************
 * Name: arm_earlyserialinit
 *
 * Description:
 *   Performs the low level USART initialization early in debug so that the
 *   serial console will be available during boot up.  This must be called
 *   before arm_serialinit.
 *
 ****************************************************************************/

#ifdef USE_EARLYSERIALINIT
void arm_earlyserialinit(void)
{
#ifdef HAVE_SERIALDRIVER
  unsigned i;

  /* Disable all USART interrupts */

  for (i = 0; i < AT32_NUSART; i++)
    {
      if (g_uart_devs[i])
        {
          up_disableusartint(g_uart_devs[i], NULL);
        }
    }

  /* Configure whichever one is the console */

#if CONSOLE_UART > 0
  up_setup(&g_uart_devs[CONSOLE_UART - 1]->dev);
#endif
#endif /* HAVE UART */
}
#endif

/****************************************************************************
 * Name: arm_serialinit
 *
 * Description:
 *   Register serial console and serial ports.  This assumes
 *   that arm_earlyserialinit was called previously.
 *
 ****************************************************************************/

void arm_serialinit(void)
{
#ifdef HAVE_SERIALDRIVER
  char devname[16];
  unsigned i;
  unsigned minor = 0;
#ifdef CONFIG_PM
  int ret;
#endif

  /* Register to receive power management callbacks */

#ifdef CONFIG_PM
  ret = pm_register(&g_serialcb);
  DEBUGASSERT(ret == OK);
  UNUSED(ret);
#endif

  /* Register the console */

#if CONSOLE_UART > 0
  uart_register("/dev/console", &g_uart_devs[CONSOLE_UART - 1]->dev);

#ifndef CONFIG_AT32_SERIAL_DISABLE_REORDERING
  /* If not disabled, register the console UART to ttyS0 and exclude
   * it from initializing it further down
   */

  uart_register("/dev/ttyS0", &g_uart_devs[CONSOLE_UART - 1]->dev);
  minor = 1;
#endif

#if defined(SERIAL_HAVE_CONSOLE_RXDMA) || defined(SERIAL_HAVE_CONSOLE_TXDMA)
  /* If we need to re-initialise the console to enable DMA do that here. */

  up_dma_setup(&g_uart_devs[CONSOLE_UART - 1]->dev);
#endif
#endif /* CONSOLE_UART > 0 */

  /* Register all remaining USARTs */

  strcpy(devname, "/dev/ttySx");

  for (i = 0; i < AT32_NUSART; i++)
    {
      /* Don't create a device for non-configured ports. */

      if (g_uart_devs[i] == 0)
        {
          continue;
        }

#ifndef CONFIG_AT32_SERIAL_DISABLE_REORDERING
      /* Don't create a device for the console - we did that above */

      if (g_uart_devs[i]->dev.isconsole)
        {
          continue;
        }
#endif

      /* Register USARTs as devices in increasing order */

      devname[9] = '0' + minor++;
      uart_register(devname, &g_uart_devs[i]->dev);
    }
#endif /* HAVE UART */
}

/****************************************************************************
 * Name: at32_serial_dma_poll
 *
 * Description:
 *   Checks receive DMA buffers for received bytes that have not accumulated
 *   to the point where the DMA half/full interrupt has triggered.
 *
 *   This function should be called from a timer or other periodic context.
 *
 ****************************************************************************/

#ifdef SERIAL_HAVE_RXDMA
void at32_serial_dma_poll(void)
{
    irqstate_t flags;

    flags = spin_lock_irqsave(&priv->lock);

#ifdef CONFIG_USART1_RXDMA
  if (g_usart1priv.rxdma != NULL)
    {
      up_dma_rxcallback(g_usart1priv.rxdma, 0, &g_usart1priv);
    }
#endif

#ifdef CONFIG_USART2_RXDMA
  if (g_usart2priv.rxdma != NULL)
    {
      up_dma_rxcallback(g_usart2priv.rxdma, 0, &g_usart2priv);
    }
#endif

#ifdef CONFIG_USART3_RXDMA
  if (g_usart3priv.rxdma != NULL)
    {
      up_dma_rxcallback(g_usart3priv.rxdma, 0, &g_usart3priv);
    }
#endif

#ifdef CONFIG_UART4_RXDMA
  if (g_uart4priv.rxdma != NULL)
    {
      up_dma_rxcallback(g_uart4priv.rxdma, 0, &g_uart4priv);
    }
#endif

#ifdef CONFIG_UART5_RXDMA
  if (g_uart5priv.rxdma != NULL)
    {
      up_dma_rxcallback(g_uart5priv.rxdma, 0, &g_uart5priv);
    }
#endif

#ifdef CONFIG_USART6_RXDMA
  if (g_usart6priv.rxdma != NULL)
    {
      up_dma_rxcallback(g_usart6priv.rxdma, 0, &g_usart6priv);
    }
#endif

#ifdef CONFIG_UART7_RXDMA
  if (g_uart7priv.rxdma != NULL)
    {
      up_dma_rxcallback(g_uart7priv.rxdma, 0, &g_uart7priv);
    }
#endif

#ifdef CONFIG_UART8_RXDMA
  if (g_uart8priv.rxdma != NULL)
    {
      up_dma_rxcallback(g_uart8priv.rxdma, 0, &g_uart8priv);
    }
#endif

  spin_unlock_irqrestore(&priv->lock, flags);
}
#endif

/****************************************************************************
 * Name: up_putc
 *
 * Description:
 *   Provide priority, low-level access to support OS debug writes
 *
 ****************************************************************************/

void up_putc(int ch)
{
#if CONSOLE_UART > 0
  struct up_dev_s *priv = g_uart_devs[CONSOLE_UART - 1];
  uint16_t ie;

  up_disableusartint(priv, &ie);
  arm_lowputc(ch);
  up_restoreusartint(priv, ie);
#endif
}

#else /* USE_SERIALDRIVER */

/****************************************************************************
 * Name: up_putc
 *
 * Description:
 *   Provide priority, low-level access to support OS debug writes
 *
 ****************************************************************************/

void up_putc(int ch)
{
#if CONSOLE_UART > 0
  arm_lowputc(ch);
#endif
}

#endif /* USE_SERIALDRIVER */
