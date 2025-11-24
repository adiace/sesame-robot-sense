# Custom Libraries

This directory contains custom libraries developed for the Sesame Robot.

## Contents

Custom Arduino libraries for:
- Sesame-specific kinematics
- Animation sequencing
- Face/emote management
- Sensor integration helpers
- Control interface abstractions

## Library Structure

Each library should follow Arduino library conventions:
```
LibraryName/
  ├── src/
  │   ├── LibraryName.h
  │   └── LibraryName.cpp
  ├── examples/
  │   └── ExampleSketch/
  │       └── ExampleSketch.ino
  ├── keywords.txt
  ├── library.properties
  └── README.md
```

## Using Libraries

Libraries can be installed by:
1. Copying to Arduino libraries folder
2. Installing via Library Manager (if published)
3. Including directly in project

## Contributing Libraries

When contributing custom libraries:
- Follow Arduino library guidelines
- Include examples
- Document all public functions
- Add keywords.txt for syntax highlighting
- Create library.properties file
