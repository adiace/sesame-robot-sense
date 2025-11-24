# Sesame Robot - Folder Structure

This document provides an overview of the Sesame Robot project folder structure.

## Directory Layout

```
sesame-robot/
├── hardware/                 # Hardware design files
│   ├── cad/                 # 3D CAD models and STL files for printing
│   ├── schematics/          # Electronic schematics and wiring diagrams
│   ├── pcb/                 # Custom PCB design files (optional)
│   ├── bom/                 # Bill of Materials spreadsheets
│   └── manufacturing/       # Manufacturing files (Gerbers, print profiles)
│
├── firmware/                # Robot control firmware
│   ├── arduino/             # Main Arduino firmware sketches
│   └── libraries/           # Custom Arduino libraries
│
├── software/                # Control and interface software
│   ├── web-ui/              # Web-based control interface
│   └── control-app/         # Desktop and mobile control applications
│
├── docs/                    # Documentation
│   ├── assembly/            # Step-by-step assembly instructions
│   ├── build-guide/         # Complete build guide with photos
│   ├── images/              # Photos, diagrams, and illustrations
│   └── datasheets/          # Component datasheets and specs
│
├── examples/                # Example code and projects
├── tests/                   # Test code and validation procedures
├── README.md                # Main project documentation
├── LICENSE                  # Apache License 2.0
└── CONTRIBUTING.md          # Contribution guidelines
```

## Key Directories

### Hardware (`/hardware/`)
Contains all physical design files including 3D models, electronic schematics, PCB designs, and manufacturing documentation. See `/hardware/README.md` for details.

### Firmware (`/firmware/`)
Arduino-based firmware for controlling servos, managing the OLED display, and handling animations. Includes custom libraries for robot-specific functions. See `/firmware/README.md` for setup instructions.

### Software (`/software/`)
Control software including web interfaces and desktop/mobile applications for robot control. See `/software/README.md` for development information.

### Documentation (`/docs/`)
Complete documentation including assembly instructions, build guides, images, and component datasheets. See `/docs/README.md` for documentation guidelines.

### Examples (`/examples/`)
Example code for custom animations, sensor integration, and alternative control methods. Great starting point for learning and customization.

### Tests (`/tests/`)
Test code and validation procedures for hardware, firmware, and integration testing.

## Adding New Content

When contributing to the project, place files in the appropriate directory:

- **3D models** → `hardware/cad/`
- **Circuit diagrams** → `hardware/schematics/`
- **Arduino code** → `firmware/arduino/`
- **Libraries** → `firmware/libraries/`
- **Web interface** → `software/web-ui/`
- **Assembly photos** → `docs/images/`
- **Build instructions** → `docs/assembly/` or `docs/build-guide/`
- **Example projects** → `examples/`
- **Test code** → `tests/`

Each directory contains a README.md file with specific guidelines and details.

## Best Practices

1. **Maintain structure** - Keep files organized in their proper directories
2. **Document changes** - Update README files when adding new content
3. **Use clear names** - Name files descriptively (e.g., `leg_assembly_step1.jpg`)
4. **Include sources** - Provide native CAD files along with exports
5. **Add examples** - Include usage examples for new features
6. **Write tests** - Add tests for firmware changes

## Getting Help

- Read the main [README.md](README.md) for project overview
- Check [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines
- Review directory-specific README files for detailed information
- Open an issue for questions or discussions

## Resources

- Build videos: [https://www.youtube.com/@DorianToddYT/](https://www.youtube.com/@DorianToddYT/)
- Issues and discussions: GitHub repository
- Community builds and modifications: Share your creations!

---

This structure follows Open Source Hardware Association (OSHWA) best practices to ensure the project remains accessible, maintainable, and easy to contribute to.
