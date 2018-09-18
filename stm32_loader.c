/*
 * This file is part of the tumanako_vc project.
 *
 * Copyright (C) 2010 Johannes Huebner <contact@johanneshuebner.com>
 * Copyright (C) 2010 Edward Cheeseman <cheesemanedward@gmail.com>
 * Copyright (C) 2009 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define STM32F1

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/crc.h>
#include <libopencm3/cm3/scb.h>
#include "hwdefs.h"

#define PAGE_SIZE 1024
#define PAGE_WORDS (PAGE_SIZE / 4)
#define FLASH_START 0x08000000
#define APP_FLASH_START 0x08001000
#define BOOTLOADER_MAGIC 0xAA
#define DELAY_100 (1 << 20)
#define DELAY_200 (1 << 21)

static void clock_setup(void)
{
   RCC_CLOCK_SETUP();

   rcc_set_ppre1(RCC_CFGR_PPRE1_HCLK_DIV2);

   /* Enable all needed GPIOx clocks */
   rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
   rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPBEN);

   #ifdef HWCONFIG_OLIMEX
   rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_USART3EN);
   #endif
   #ifdef HWCONFIG_OLIMEX_H107
   rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_USART2EN);
   rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_AFIOEN);
   gpio_primary_remap(AFIO_MAPR_SWJ_CFG_FULL_SWJ, AFIO_MAPR_USART2_REMAP);
   #endif
   #ifdef HWCONFIG_TUMANAKO_KIWIAC
   rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_USART1EN);
   #endif

   /* Enable DMA1 clock */
   rcc_peripheral_enable_clock(&RCC_AHBENR, RCC_AHBENR_DMA1EN);

   /* Enable CRC clock */
   rcc_peripheral_enable_clock(&RCC_AHBENR, RCC_AHBENR_CRCEN);
}

static void usart_setup(void)
{
    gpio_set_mode(TERM_USART_TXPORT, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, TERM_USART_TXPIN);

    /* Setup UART parameters. */
    usart_set_baudrate(TERM_USART, USART_BAUDRATE);
    usart_set_databits(TERM_USART, 8);
    usart_set_stopbits(TERM_USART, USART_STOPBITS_2);
    usart_set_mode(TERM_USART, USART_MODE_TX_RX);
    usart_set_parity(TERM_USART, USART_PARITY_NONE);
    usart_set_flow_control(TERM_USART, USART_FLOWCONTROL_NONE);
    usart_enable_rx_dma(TERM_USART);

    /* Finally enable the USART. */
    usart_enable(TERM_USART);
}

static void dma_setup(void *data, uint32_t len)
{
   dma_disable_channel(DMA1, USART_DMA_CHAN);
   dma_set_peripheral_address(DMA1, USART_DMA_CHAN, (uint32_t)&USART3_DR);
   dma_set_memory_address(DMA1, USART_DMA_CHAN, (uint32_t)data);
   dma_set_number_of_data(DMA1, USART_DMA_CHAN, len);
   dma_set_peripheral_size(DMA1, USART_DMA_CHAN, DMA_CCR_PSIZE_8BIT);
   dma_set_memory_size(DMA1, USART_DMA_CHAN, DMA_CCR_MSIZE_8BIT);
   dma_enable_memory_increment_mode(DMA1, USART_DMA_CHAN);
   dma_enable_channel(DMA1, USART_DMA_CHAN);
   dma_clear_interrupt_flags(DMA1, USART_DMA_CHAN, DMA_TCIF);
}

static void write_flash(uint32_t addr, uint32_t *pageBuffer)
{
   flash_erase_page(addr);

   for (int idx = 0; idx < PAGE_WORDS; idx++)
   {
      flash_program_word(addr + idx * 4, pageBuffer[idx]);
   }
}

void wait(void)
{
   for (volatile uint32_t i = DELAY_100; i > 0; i--);
}

int main(void)
{
   uint32_t page_buffer[PAGE_WORDS];
   uint32_t addr = APP_FLASH_START;

   clock_setup();
   usart_setup();
   dma_setup(page_buffer, PAGE_SIZE);

   wait();
   usart_send_blocking(TERM_USART, '2');
   wait();
   char magic = usart_recv(TERM_USART);

   if (magic == BOOTLOADER_MAGIC)
   {
      usart_send_blocking(TERM_USART, 'S');
      wait();
      char numPages = usart_recv(TERM_USART);
      flash_unlock();
      flash_set_ws(2);

      while (numPages > 0)
      {
         uint32_t recvCrc = 0;
         uint32_t timeOut = DELAY_200;

         crc_reset();
         dma_setup(page_buffer, PAGE_SIZE);
         usart_send_blocking(TERM_USART, 'P');

         while (!dma_get_interrupt_flag(DMA1, USART_DMA_CHAN, DMA_TCIF))
         {
            timeOut--;

            //When the buffer is not full after about 200ms
            //Request the entire page again
            if (0 == timeOut)
            {
               timeOut = DELAY_200;
               dma_setup(page_buffer, PAGE_SIZE);
               usart_send_blocking(TERM_USART, 'T');
            }
         }

         uint32_t crc = crc_calculate_block(page_buffer, PAGE_WORDS);

         dma_setup(&recvCrc, sizeof(recvCrc));
         usart_send_blocking(TERM_USART, 'C');
         while (!dma_get_interrupt_flag(DMA1, USART_DMA_CHAN, DMA_TCIF));

         if (crc == recvCrc)
         {
            write_flash(addr, page_buffer);
            numPages--;
            addr += PAGE_SIZE;
         }
         else
         {
            usart_send_blocking(TERM_USART, 'E');
         }
      }

      flash_lock();
   }

   usart_send_blocking(TERM_USART, 'D');
   wait();
   usart_disable(TERM_USART);

   void (*app_main)(void) = (void (*)(void)) *(volatile uint32_t*)(APP_FLASH_START + 4);
   SCB_VTOR = APP_FLASH_START;
   app_main();

   return 0;
}
