# PCB Schematics

Electronic schematics and wiring diagrams for the Sesame Distro Board V1, a custom PCB just for the Sesame Robot Project. The Distro Board V1 pairs with the ESP32-DevKitC-32E.

> [!CAUTION]  
> UPDATE 1/20/26: Upon further testing, the Sesame distro board V1 will work, but it has a few issues that make it a little harder to assemble and will not run on teathered power (eg. USB C). Until V2 is released, I recommend using the S2 Mini / Hand Wiring approach. If you ordered a distro board V1, it will still be supported with wiring guides and firmware, and will still work on battery power.

> [!IMPORTANT]
> **ESP32 Pin Header Requirement:** The distro board V1 stacks on top of the ESP32-DevKitC-32E, so you need an ESP32 board **without pre-soldered pin headers**. If your board came with headers already soldered on the top, you will need to desolder all of the headers and flip them to the bottom side of the ESP32 board so the distro board can mount on top.


**PCBway Sponsorship**

This project was sponsored by [PCBway](https://www.pcbway.com/), who manufactured the custom distro boards. PCBway offers high-quality PCB fabrication services with fast turnaround times and excellent customer support. You can order your own Sesame Distro Boards by uploading the Gerber files (found in the hardware section) to their website.

If you're building your own Sesame Robot, PCBway is a great option for getting professional-quality distro boards manufactured at reasonable prices.

<img src="pcbs.png" alt="pcbs-from-pcbway" width="70%">

## Board Schematic:

<img src="Schematic_Sesame-Distro-Board.png" alt="Schematic_Sesame-Distro-Board" width="70%">


### PCB Layout:
<img src="SDB-layout.png" alt="SDB-Layout" width="70%">



## How to order one:

1. Download the Gerber archive `Gerber_Sesame-Distro-Board_PCB_Sesame-Distro-Board_V1.zip` from this directory.
2. Upload that zip file to PCBway as the project Gerber bundle and confirm the board thickness and finish match the Sesame Distro Board requirements.
3. Double-check the provided drill, solder mask, and silkscreen layers in the preview before placing the order to ensure the traces and cutouts stay consistent with the schematic.
4. Specify the desired quantity and shipping options, then submit the order to receive professionally fabricated Sesame Distro Boards.

