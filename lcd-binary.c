#include <stdio.h>  // debugging only
#include "gpio.h"
#include "lcd-binary.h"
#include "cw2-aux.h"

/* ***************************************************************************** */
/* HINT: use the CPP variable ASM with ifdef's to select Asm (or C) versions of the code. */
/* ***************************************************************************** */

/*
  Hardware Interface function.
  Set the mode for pin number @pin@ to @mode@ (can be INPUT or OUTPUT (encoded as int)).
*/
void pin_mode(volatile uint32_t *gpio, int pin, int mode)
{
    // The Function Select registers (GPFSEL0-5) determine if a pin is input or output.
    // Each pin takes 3 bits. There are 10 pins per 32-bit register.
    int reg = pin / 10;            // Which GPFSEL register (0-5)
    int shift = (pin % 10) * 3;    // Which 3-bit block within that register

    // 1. Always clear the 3 bits for this pin to 000 (INPUT mode) first
    *(gpio + reg) &= ~(7 << shift);

    // 2. If the requested mode is OUTPUT, set the lowest bit of that 3-bit block to 1 (001)
    if (mode == OUTPUT) {
        *(gpio + reg) |= (1 << shift);
    }
}

/*
  Hardware Interface function.
  Send a @value@ along pin number @pin@. Values should be LOW or HIGH (encoded as int).
*/
void digital_write (volatile uint32_t *gpio, int pin, int value)
{
    if (value == HIGH) {
        // GPSET0 is at offset 7. Writing a 1 to a specific bit turns that pin ON.
        *(gpio + 7) = (1 << pin);
    } else {
        // GPCLR0 is at offset 10. Writing a 1 to a specific bit turns that pin OFF.
        *(gpio + 10) = (1 << pin);
    }
}

/*
  Hardware Interface function.
  Read input from a button device connected to pin @button@. Result can be LOW or HIGH.
*/
int read_button(volatile uint32_t *gpio, int button)
{
    // GPLEV0 is at offset 13. It holds the current HIGH/LOW state of pins 0-31.
    // We use a bitwise AND (&) to isolate the specific bit for our button.
    if (*(gpio + 13) & (1 << button)) {
        return HIGH; // Voltage detected (Button pressed, assuming active-high)
    } else {
        return LOW;  // No voltage detected
    }
}
