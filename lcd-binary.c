#include <stdio.h> 
#include "gpio.h"
#include "lcd-binary.h"
#include "cw2-aux.h"


void pin_mode(volatile uint32_t *gpio, int pin, int mode)
{
    int reg = pin / 10;
    int shift = (pin % 10) * 3;
    *(gpio + reg) &= ~(7 << shift);
    if (mode == OUTPUT) {
        *(gpio + reg) |= (1 << shift);
    }
}

void digital_write (volatile uint32_t *gpio, int pin, int value)
{
    if (value == HIGH) {
        *(gpio + 7) = (1 << pin);
    } else {
        *(gpio + 10) = (1 << pin);
    }
}

int read_button(volatile uint32_t *gpio, int button)
{
    if (*(gpio + 13) & (1 << button)) {
        return HIGH; // Voltage detected (Button pressed, assuming active-high)
    } else {
        return LOW;  // No voltage detected
    }
}
