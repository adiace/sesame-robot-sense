# Web User Interface

This directory contains the web-based user interface for controlling the Sesame Robot.

## Overview

The web UI provides a graphical interface for controlling the robot when using an ESP32 or WiFi-enabled microcontroller.

## Features

- Button controls for all available poses and animations:
  - Rest
  - Stand
  - Walk
  - Wave
  - Dance
  - Swim
  - Point
- Real-time status display
- Responsive design for mobile and desktop

## Setup

1. Configure WiFi credentials in firmware
2. Upload firmware with web server code
3. Connect to robot's WiFi network or ensure robot is on your network
4. Navigate to robot's IP address in web browser

## Development

The web UI can be:
- Embedded in firmware as HTML strings
- Served from SPIFFS/LittleFS filesystem
- Hosted externally and communicate via API

## Technologies

- HTML5
- CSS3
- JavaScript (vanilla or framework)
- WebSocket for real-time communication (optional)
- REST API for control commands

## Contributing

Improvements welcome for:
- User interface design
- Additional controls
- Status visualization
- Mobile optimization
- Accessibility features
