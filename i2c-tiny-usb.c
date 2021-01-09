/*
 * I2C-Tiny-USB clone for ATmegaXU4
 *
 * A simple USB attached I2C adapter which supports the same USB commands as
 * https://github.com/harbaum/I2C-Tiny-USB/ and https://fischl.de/i2c-mp-usb/.
 *
 * Based on LUFA's BulkVendor demo. Tested on ATmega32U4 but should work on any
 * xU4 device as it fits into 4K of Flash. xU2s don't have hardware I2C support
 * so they don't work, sorry.
 *
 * Author: Joachim Fenkes <github@dojoe.net>
 */

/*
  Copyright 2021  Joachim Fenkes <github@dojoe.net>

  Permission to use, copy, modify, distribute, and sell this
  software and its documentation for any purpose is hereby granted
  without fee, provided that the above copyright notice appear in
  all copies and that both that the copyright notice and this
  permission notice and warranty disclaimer appear in supporting
  documentation, and that the name of the author not be used in
  advertising or publicity pertaining to distribution of the
  software without specific, written prior permission.

  The author disclaims all warranties with regard to this
  software, including all implied warranties of merchantability
  and fitness.  In no event shall the author be liable for any
  special, indirect or consequential damages or any damages
  whatsoever resulting from loss of use, data or profits, whether
  in an action of contract, negligence or other tortious action,
  arising out of or in connection with the use or performance of
  this software.
*/

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/interrupt.h>

#include "Descriptors.h"

#include <LUFA/Drivers/USB/USB.h>
#include <LUFA/Platform/Platform.h>
#include <LUFA/Drivers/Peripheral/TWI.h>

// Cheap LED abstraction for error signalling.
// Disabled by default, feel free to enable and adapt to your hardware.
#define LED_SUPPORT 0
#define LED_INVERT  0
#define LEDPORT     PORTE
#define LEDDDR      DDRE
#define LEDPIN      (1 << 6)

static inline void LED_on(void)
{
	if (LED_SUPPORT && LED_INVERT)
		LEDPORT &= ~LEDPIN;
	else if (LED_SUPPORT)
		LEDPORT |= LEDPIN;
}

static inline void LED_off(void)
{
	if (LED_SUPPORT && LED_INVERT)
		LEDPORT |= LEDPIN;
	else if (LED_SUPPORT)
		LEDPORT &= ~LEDPIN;
}

static inline void LED_Init(void)
{
	if (LED_SUPPORT)
		LEDDDR |= LEDPIN;
}

// Main USB-I2C code

#define CMD_ECHO             0
#define CMD_GET_FUNC         1
#define CMD_SET_DELAY        2
#define CMD_GET_STATUS       3
#define CMD_I2C_IO           4
#define CMD_I2C_IO_BEGIN     1
#define CMD_I2C_IO_END       2
#define CMD_START_BOOTLOADER 0x10
#define CMD_SET_BAUDRATE     0x11

#define I2C_M_RD   1

#define STATUS_IDLE 0
#define STATUS_ADDRESS_ACK 1
#define STATUS_ADDRESS_NAK 2

uint8_t I2C_Status = STATUS_IDLE;

void SetupI2CSpeed(uint16_t khz)
{
	const uint16_t F_CPU_KHZ = F_CPU / 1000;
	uint8_t prescaler = 0;
	uint16_t bit_rate;

	// Try to find the smallest prescaler that yields a bitrate value which fits the TWBR register.
	while (1) {
		bit_rate = ((( (F_CPU_KHZ >> (prescaler << 1)) / khz) - 16) >> 1);
		if (bit_rate < 256 || prescaler == 3) {
			TWI_Init(prescaler, bit_rate);
			return;
		}
		prescaler++;
	}
}

// Adapted from Endpoint_Read_Control_Stream_LE with I2C access sprinkled in
// @param skip Omit I2C accesses, just drain the stream.
uint8_t I2C_Write(uint8_t skip)
{
	uint16_t len = USB_ControlRequest.wLength;

	if (!len)
		Endpoint_ClearOUT();

	while (len) {
		uint8_t USB_DeviceState_LCL = USB_DeviceState;

		if (USB_DeviceState_LCL == DEVICE_STATE_Unattached)
			return ENDPOINT_RWCSTREAM_DeviceDisconnected;
		else if (USB_DeviceState_LCL == DEVICE_STATE_Suspended)
			return ENDPOINT_RWCSTREAM_BusSuspended;
		else if (Endpoint_IsSETUPReceived())
			return ENDPOINT_RWCSTREAM_HostAborted;

		if (Endpoint_IsOUTReceived()) {
			while (len && Endpoint_BytesInEndpoint()) {
				uint8_t value = Endpoint_Read_8();
				if (!skip) {
					while (!(TWCR & (1 << TWINT)));
					TWDR = value;
					TWCR = (1 << TWINT) | (1 << TWEN);
				}
				len--;
			}
			Endpoint_ClearOUT();
		}
	}
	while (!skip && !(TWCR & (1 << TWINT)));

	while (!Endpoint_IsINReady()) {
		uint8_t USB_DeviceState_LCL = USB_DeviceState;

		if (USB_DeviceState_LCL == DEVICE_STATE_Unattached)
			return ENDPOINT_RWCSTREAM_DeviceDisconnected;
		else if (USB_DeviceState_LCL == DEVICE_STATE_Suspended)
			return ENDPOINT_RWCSTREAM_BusSuspended;
	}
	Endpoint_ClearIN();

	return 0;
}

static void I2C_Read_StartNext(uint8_t nack_last_byte, uint16_t remaining_bytes)
{
	if (nack_last_byte && remaining_bytes == 1)
		TWCR = (1 << TWINT) | (1 << TWEN);
	else
		TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
}

// Adapted from Endpoint_Write_Control_Stream_LE with I2C access sprinkled in
// @param nack_last_byte Respond to the last incoming byte with NACK instead of ACK
// @param skip Omit I2C accesses, just drain the stream.
uint8_t I2C_Read(uint8_t nack_last_byte, uint8_t skip)
{
	uint16_t len = USB_ControlRequest.wLength;
	uint8_t last_full = false;

	if (!len)
		Endpoint_ClearIN();
	else if (!skip)
		I2C_Read_StartNext(nack_last_byte, len);

	while (len || last_full) {
		uint8_t USB_DeviceState_LCL = USB_DeviceState;

		if (USB_DeviceState_LCL == DEVICE_STATE_Unattached)
			return ENDPOINT_RWCSTREAM_DeviceDisconnected;
		else if (USB_DeviceState_LCL == DEVICE_STATE_Suspended)
			return ENDPOINT_RWCSTREAM_BusSuspended;
		else if (Endpoint_IsSETUPReceived())
			return ENDPOINT_RWCSTREAM_HostAborted;
		else if (Endpoint_IsOUTReceived())
			break;

		if (Endpoint_IsINReady()) {
			uint8_t nbytes = Endpoint_BytesInEndpoint();
			while (len && (nbytes < USB_Device_ControlEndpointSize)) {
				len--;

				uint8_t value = 0;
				if (!skip) {
					while (!(TWCR & (1 << TWINT)));
					value = TWDR;
					if (len)
						I2C_Read_StartNext(nack_last_byte, len);
				}

				Endpoint_Write_8(value);
				nbytes++;
			}
			last_full = (nbytes == USB_Device_ControlEndpointSize);
			Endpoint_ClearIN();
		}
	}

	while (!Endpoint_IsOUTReceived()) {
		uint8_t USB_DeviceState_LCL = USB_DeviceState;

		if (USB_DeviceState_LCL == DEVICE_STATE_Unattached)
		  return ENDPOINT_RWCSTREAM_DeviceDisconnected;
		else if (USB_DeviceState_LCL == DEVICE_STATE_Suspended)
		  return ENDPOINT_RWCSTREAM_BusSuspended;
		else if (Endpoint_IsSETUPReceived())
		  return ENDPOINT_RWCSTREAM_HostAborted;
	}
	Endpoint_ClearOUT();

	return 0;
}

/** Event handler for the USB_ControlRequest event. This is used to catch and process control requests sent to
 *  the device from the USB host before passing along unhandled control requests to the library for processing
 *  internally.
 */
void EVENT_USB_Device_ControlRequest(void)
{
	if (((USB_ControlRequest.bmRequestType & CONTROL_REQTYPE_TYPE) != REQTYPE_CLASS)
	 || ((USB_ControlRequest.bmRequestType & CONTROL_REQTYPE_RECIPIENT) != REQREC_DEVICE))
		return;

	switch (USB_ControlRequest.bRequest) {
		case CMD_SET_BAUDRATE:
			Endpoint_ClearSETUP();
			TWI_Disable();
			SetupI2CSpeed(USB_ControlRequest.wValue);
			PORTE ^= 1 << 6;
			Endpoint_ClearStatusStage();
			break;

		case CMD_GET_STATUS:
			Endpoint_ClearSETUP();
			while (!Endpoint_IsINReady());
			Endpoint_Write_8(I2C_Status);
			Endpoint_ClearIN();
			PORTE &= ~(1 << 6);
			while (!Endpoint_IsOUTReceived())
			Endpoint_ClearOUT();
			break;

		case CMD_I2C_IO:
		case CMD_I2C_IO | CMD_I2C_IO_BEGIN:
		case CMD_I2C_IO | CMD_I2C_IO_END:
		case CMD_I2C_IO | CMD_I2C_IO_BEGIN | CMD_I2C_IO_END:
		{
			Endpoint_ClearSETUP();
			const uint8_t start = USB_ControlRequest.bRequest & CMD_I2C_IO_BEGIN;
			const uint8_t stop = USB_ControlRequest.bRequest & CMD_I2C_IO_END;
			const uint8_t read = USB_ControlRequest.wValue & I2C_M_RD;

			if (start) {
 				if (TWI_StartTransmission(USB_ControlRequest.wIndex, 25)) {
					I2C_Status = STATUS_ADDRESS_NAK;
					LED_on();
				} else {
					I2C_Status = STATUS_ADDRESS_ACK;
					LED_off();
				}
			}

			// In case of error we complete the request but skip the I2C accesses
			const uint8_t skip_and_exit = (I2C_Status == STATUS_ADDRESS_NAK);
			if (read)
				I2C_Read(stop, skip_and_exit);
			else
				I2C_Write(skip_and_exit);

			if (stop && !skip_and_exit) {
				TWI_StopTransmission();
			}
		}
		break;
	}
}

/** Event handler for the USB_ConfigurationChanged event. This is fired when the host set the current configuration
 *  of the USB device after enumeration - the device endpoints are configured.
 */
void EVENT_USB_Device_ConfigurationChanged(void)
{
	Endpoint_ConfigureEndpoint(VENDOR_IN_EPADDR,  EP_TYPE_BULK, VENDOR_IO_EPSIZE, 1);
	Endpoint_ConfigureEndpoint(VENDOR_OUT_EPADDR, EP_TYPE_BULK, VENDOR_IO_EPSIZE, 1);
}

/** Configures the board hardware and chip peripherals for the demo's functionality. */
void SetupHardware(void)
{
	/* Disable watchdog if enabled by bootloader/fuses */
	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	/* Disable clock division */
	clock_prescale_set(clock_div_1);

	/* Hardware Initialization */
	LED_Init();
	USB_Init();
	SetupI2CSpeed(100);
}

/** Main program entry point. This routine configures the hardware required by the application, then
 *  enters a loop to run the application tasks in sequence.
 */
int main(void)
{
	SetupHardware();
	GlobalInterruptEnable();

	for (;;)
	{
		USB_USBTask();

		uint8_t ReceivedData[VENDOR_IO_EPSIZE];
		memset(ReceivedData, 0x00, sizeof(ReceivedData));

		Endpoint_SelectEndpoint(VENDOR_OUT_EPADDR);
		if (Endpoint_IsOUTReceived())
		{
			Endpoint_Read_Stream_LE(ReceivedData, VENDOR_IO_EPSIZE, NULL);
			Endpoint_ClearOUT();

			Endpoint_SelectEndpoint(VENDOR_IN_EPADDR);
			Endpoint_Write_Stream_LE(ReceivedData, VENDOR_IO_EPSIZE, NULL);
			Endpoint_ClearIN();
		}
	}
}
