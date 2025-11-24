# PCB Design Files

This directory contains PCB design files if custom circuit boards are developed for the Sesame Robot.

## Contents

While the basic Sesame Robot uses off-the-shelf breakout boards (Arduino, PCA9685, OLED), custom PCB designs may include:
- All-in-one controller board
- Servo driver integration board
- Power distribution board
- Sensor expansion boards

## File Formats

- Native design files (KiCad, Eagle, Altium, etc.)
- Gerber files for manufacturing
- BOM for PCB assembly
- Assembly diagrams

## Design Guidelines

When creating custom PCBs:
- Ensure compatibility with existing firmware
- Maintain I2C bus for extensibility
- Include proper power regulation
- Add mounting holes matching 3D printed parts
- Consider ease of assembly and soldering
