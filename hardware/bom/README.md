# Bill of Materials

Complete bill of materials for building the Sesame Robot.

The Sesame Robot Project is split into two main versions, one using a Custom PCB and one using a proto-board distributor. 
For more details on which version of Sesame is best for you, see the [Sesame Build Tutorial](../../docs/build-guide/README.md).

## Core Components
*   **Microcontroller:** ESP32 S2 Mini Development Board with WiFi connectivity
*   **Actuators:** 8x MG90 Micro Metal Gear Servo Motors.
*   **Display:** 0.96" I2C OLED Display (SSD1306 driver, 128x64).
*   **Power:** 5V Power Supply suitable for 8 servos (3s 450mah Li-Po, or 3x 10440 Li-Ion cells).
*   **Body:** 3D Printed Parts (Files available in the `/Hardware` directory).

## Power Requirements
> [!NOTE]
> Sesame requires a minimum power source of 5v 3A. This can be provided by most modern Type-C PD cables and will run Sesame tethered to a computer or power supply. To run Sesame wireless, a battery rated for 5-12v can be connected to the power terminal and switch on the Custom PCB or the buck converter and switch in the non-pcb version. Sesame will not work on USB-A 5v 1.5A, or 3xAAA 4.5v due to limitations in current draw for all motors. See the wiring diagram for both versions of Sesame for more details. Details with visuals are also available in the Sesame build tutorial.