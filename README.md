# Real-Time Packet Sorting Conveyor

This repository contains the project files for the Real-Time Packet Sorting Conveyor, developed as part of the Realtime Systems course at the University of Applied Sciences Ulm. The project is a team effort involving programming in C and utilizing Real-Time Linux.

## Project Overview

The Real-Time Packet Sorting Conveyor aims to implement a system that efficiently sorts packets based on barcode scans in real-time. The conveyor system will receive packets, scan the barcodes on them, and sort them accordingly, ensuring timely and accurate sorting.

## Project Structure

The repository is organized as follows:

- [Makefile](/Makefile): A makefile to simplify the compilation process.
- [Packetsort.c](/Packetsort.c): The main source code file containing the implementation of the packet sorting conveyor.
- [Queue.h](/Queue.h): Header file containing the definition of a queue data structure.
- [Util.h](/Util.h): Header file containing utility function declarations.
- [RTAI_MODULES_LOAD.SH](/RTAI_MODULES_LOAD.SH): Loads all the necessary kernel modules.
- [USBREAD](/USBREAD): Configures the USB input for reading the barcodescanner values.

## Contributing

Contributions to this project are currently not open to the public. As the project is part of a team effort for educational purposes, only authorized team members are allowed to contribute.

## License

The Real-Time Packet Sorting Conveyor project is licensed under the MIT License. You are free to use, modify, and distribute the code for both commercial and non-commercial purposes.
