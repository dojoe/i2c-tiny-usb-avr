An I2C-Tiny-USB clone for ATmegaXU4 controllers
===============================================

This is a straight up clone of Till Harbaum's I2C-Tiny-USB_ adapter or Thomas Fischl's I2C-MP-USB_.
Those are both based on PICs and I needed an I2C adapter on a weekend but only had ATmega32U4 boards on hand,
so I wrote this firmware. It uses their protocol and VID/PID pair so existing drivers will work without modification.

Features:

- Supports ATmega16U4 and ATmega32U4 (used on the Arduino Leonardo)

  - Won't work on the ATmegaXU2 line since they don't have hardware I2C :(
  - May work on other USB-enabled AVRs if supported by LUFA, just give it a try.

- Fast pipelined operation up to 400 kHz with no dead time in between bytes
- Selectable baud rate from 5 kHz up to 400 kHz
- Optional LED for error indication (disabled by default)
- All the pull-ups your board happens to have ;)

Driver support
==============

For details Thomas has a great write-up at his I2C-MP-USB_ page so here are a few links:

- Linux: supported natively by the *i2c-tiny-usb* driver, or through libusb via Thomas' Python or Java libraries
- Windows: Zadig_ and one of Thomas' libraries
- Python: Thomas wrote a nice library - see https://fischl.de/i2c-mp-usb/#pyI2C_MP_USB
- Java: Thomas wrote a nice Java library too - see https://fischl.de/i2c-mp-usb/#jlI2C_MP_USB

Flashing
========

A pre-built hex file for a 16 MHz crystal is provided. Just flash it onto your XU4 via the DFU bootloader::

  avrdude -u -p atmega32u4 -P usb -c flip1 -Uflash:w:i2c-tiny-usb.hex

The same hex file *should* also be flashable as-is onto an Arduino Leonardo via the Arduino bootloader,
but I have not tried this, so YMMV::

  avrdude -u -p atmega32u4 -P <your port here> -c avr109 -Uflash:w:i2c-tiny-usb.hex

Building from source
====================

You need a recent avr-gcc, for Windows I used `Zak Kemble's builds`_.
Then just make sure your ``PATH`` points to the avr-gcc binaries and do::

  make

or if your device is already connected and in bootloader mode::

  make flash

Tweaks
------

If your board runs at a different crystal frequency than 16 MHz you will have to adapt ``F_CPU`` in the ``makefile``.

If you'd like an LED to light up on an error (i.e. when the device NACKs the address we send it) you can configure
the LED pin via some ``#define`` close to the beginning of ``i2c-tiny-usb.c``.

Acknowledgements
================

Thanks to Thomas and Till for their projects which I built upon!

This project uses Dean Camera's excellent LUFA_ library for the USB support,
without which it would not have been possible to build from scratch to working in only a few hours.

.. _I2C-Tiny-USB: https://github.com/harbaum/I2C-Tiny-USB/
.. _I2C-MP-USB: https://fischl.de/i2c-mp-usb/
.. _Zadig: http://zadig.akeo.ie/
.. _`Zak Kemble's builds`: https://blog.zakkemble.net/avr-gcc-builds/
.. _LUFA: http://www.fourwalledcubicle.com/LUFA.php
