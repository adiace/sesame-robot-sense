# Arduino Firmware

This directory contains the main Arduino sketch and supporting files for the Sesame Robot.

## Files

Place the main firmware sketch (.ino file) and any supporting files here.

## Configuration

Before uploading:
1. Review and configure pin assignments
2. Adjust servo calibration values if needed
3. Customize face bitmap arrays for different expressions
4. Set serial baud rate (default: 115200)

## Serial Commands

The firmware includes a Serial Command Line Interface. Connect via Serial Monitor to send commands and trigger animations.

Available commands will be documented in the firmware code comments.

## Customizing Faces

Faces are stored as byte arrays in `PROGMEM`. To create custom faces:
1. Create a 128x64 pixel monochrome image
2. Convert to hex array using a bitmap converter
3. Paste array into firmware
4. Call `updateFaceBitmap(your_bitmap_name)` in code
