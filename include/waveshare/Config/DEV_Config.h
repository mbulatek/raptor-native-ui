/*
 * Raptor-customized Waveshare DEV_Config compatibility layer.
 *
 * This file preserves the vendor-facing API name expected by the copied
 * Waveshare sources, but the implementation behind it is maintained by the
 * raptor-ui-service project and uses libgpiod + spidev.
 */
#ifndef _DEV_CONFIG_H_
#define _DEV_CONFIG_H_

#include "Debug.h"
#include <gpiod.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define USE_SPI 1
#define USE_IIC 0

#define IIC_CMD 0x00
#define IIC_RAM 0x40

#define UBYTE uint8_t
#define UWORD uint16_t
#define UDOUBLE uint32_t

#define OLED_CS waveshare_dev_config_cs_pin()
#define OLED_RST waveshare_dev_config_reset_pin()
#define OLED_DC waveshare_dev_config_dc_pin()

#define OLED_CS_0 DEV_Digital_Write(OLED_CS, 0)
#define OLED_CS_1 DEV_Digital_Write(OLED_CS, 1)
#define OLED_RST_0 DEV_Digital_Write(OLED_RST, 0)
#define OLED_RST_1 DEV_Digital_Write(OLED_RST, 1)
#define OLED_DC_0 DEV_Digital_Write(OLED_DC, 0)
#define OLED_DC_1 DEV_Digital_Write(OLED_DC, 1)

typedef struct {
    int gpiochip;
    int spi_device;
    int spi_channel;
    int spi_speed_hz;
    int spi_flags;
    int cs_gpio;
    int reset_gpio;
    int dc_gpio;
} waveshare_dev_hardware_config_t;

void waveshare_dev_config_set_hardware(const waveshare_dev_hardware_config_t* config);
UWORD waveshare_dev_config_cs_pin(void);
UWORD waveshare_dev_config_reset_pin(void);
UWORD waveshare_dev_config_dc_pin(void);

UBYTE DEV_ModuleInit(void);
void DEV_ModuleExit(void);

void DEV_GPIO_Mode(UWORD Pin, UWORD Mode);
void DEV_Digital_Write(UWORD Pin, UBYTE Value);
UBYTE DEV_Digital_Read(UWORD Pin);
void DEV_Delay_ms(UDOUBLE xms);

void I2C_Write_Byte(uint8_t value, uint8_t Cmd);
void DEV_SPI_WriteByte(UBYTE Value);
void DEV_SPI_Write_nByte(uint8_t* pData, uint32_t Len);

#endif