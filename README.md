# Raspberry Pi Hardware-Integrated PIN Cracker

## Overview
This repository contains a hardware-software integration project built for the Raspberry Pi. It combines a central C application (`pin-cracking.c`) with ARM Assembly optimizations (`hamming.s`) to interact with GPIO components (LEDs, a push-button, and an LCD display) via direct memory mapping. The core objective is to process physical user inputs and execute both guided and brute-force PIN cracking algorithms using Hamming distance evaluations.

## Hardware Requirements
To run this project, you will need a Raspberry Pi (configured for RPi 4 base addresses in the code, but adaptable to RPi 2/3) and the following external components:
* 1x Green LED
* 1x Red LED
* 1x Push Button
* 1x 16x2 LCD Display (using 4-bit wiring)

### Wiring Diagram
![Fritzing Wiring Diagram](fritz_diagram.png)

*(Reference the diagram above for specific wiring and layout. The precise GPIO pin mappings are defined in the code.)*

## Project Structure
* **`pin-cracking.c`**: The main application controller handling CLI arguments, GPIO setup, sequence tracking, and the main cracking logic.
* **`hamming.s`**: ARM Assembly implementation for highly efficient Hamming distance calculations between sequences.
* **`lcd-fcts.c` / `lcd-fcts.h` / `lcd-binary.c` / `lcd-binary.h`**: Modularized drivers and helper functions for initializing and pushing text to the 16x2 LCD.
* **`aux.c` / `aux.h` & `config.h` & `gpio.h`**: Configuration and auxiliary helpers for timekeeping, setup, and GPIO macros.
* **`Makefile`**: Build script to compile the C and Assembly files into the executable.

## Features & Execution Tasks
The program operates in several distinct phases:
1. **Task 1 (Greeting):** Prompts the user for their surname in the terminal. The Green LED blinks for vowels and the Red LED blinks for consonants, followed by displaying the first 5 characters on the LCD.
2. **Task 2 (Sequence Input):** Allows the user to input a PIN using physical button presses. The Green LED confirms individual presses, while the Red LED signals the end of a digit's input phase.
3. **Task 4 (Guided Search):** Calculates the initial Hamming distance between the input guess and the secret PIN, displaying the results on the LCD. It then attempts to find the PIN by iterating through combinations that match that specific distance.
4. **Task 5 (Brute Force):** An exhaustive search mode that iterates through all possible sequence combinations (acting as an "odometer") until the secret sequence is cracked, outputting runtime and attempt statistics.

## Building and Running

### Compilation
Because the repository includes a `Makefile`, you can compile the project simply by running:
```bash
make
```

### Execution

**Important: Because this program uses direct memory-mapped I/O (/dev/mem) to control the GPIO pins, it must be run with root privileges.

```bash
sudo ./pin-cracking [OPTIONS]
```

### Command-Line Arguments

The program accepts several flags to modify its behavior:

* -h: Display help and usage information.

* -v: Enable verbose output (prints active settings).

* -d: Enable debug mode (reveals the secret sequence in the terminal).

* -e: Run the Exhaustive/Brute Force Search (Task 5).

* -m <maxval>: Set the maximum digit value (number of possible values per position).

* -n <seqlen>: Set the length of the PIN sequence.

* -u: Run a unit test (requires -s and -r).

* -s <secret seq>: Manually set the secret sequence to crack.

* -r <reference seq>: Set a reference sequence (for testing).

### Example

To run an exhaustive search with a sequence length of 4 and a specific secret PIN of 1234:

```Bash
sudo ./pin-cracking -e -n 4 -s 1234
```
