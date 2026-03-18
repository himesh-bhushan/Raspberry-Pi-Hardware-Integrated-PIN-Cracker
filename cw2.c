/* 
 *
 * F28HS CW2
 * pinCrack: button input of a sequence of numbers followed by cracking a secret PIN
 * Uses interval timers for the timeout/delay function

 * Compile:    	      make cw2
 * Run (e.g):         sudo ./cw2 -d -e -s 112
 * Run (unit-test):   sudo ./cw2 -u -s 112 -r 121

 ***********************************************************************
 * The development of this code was heavily based on the wiringPi library by Gordon Henderson.
 * This instance of the code, however, does not depend directly on the wiringPi library any more.
 *
 * wiringPi:
 *	Arduino look-a-like Wiring library for the Raspberry Pi
 *	Copyright (c) 2012-2015 Gordon Henderson
 *	Additional code for pwmSetClock by Chris Hall <chris@kchall.plus.com>
 *
 *	Thanks to code samples from Gert Jan van Loo and the
 *	BCM2835 ARM Peripherals manual, however it's missing
 *	the clock section /grr/mutter/
 ***********************************************************************
 * This file is part of wiringPi:
 *	https://projects.drogon.net/raspberry-pi/wiringpi/
 *
 *    wiringPi is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as
 *    published by the Free Software Foundation, either version 3 of the
 *    License, or (at your option) any later version.
 *
 *    wiringPi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with wiringPi.
 *    If not, see <http://www.gnu.org/licenses/>.
 ***********************************************************************
 */

/* --------------------------------------------------------------------------- */
/* Config settings */

// NOTE: most config settings are in cw2-config.h

/* --------------------------------------------------------------------------- */
/* Imports */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <assert.h>

#include "cw2-config.h"
#include "cw2-aux.h"
#include "lcd-binary.h"
#include "lcd-fcts.h"

/* --------------------------------------------------------------------------- */
/* Constants (see cw2-config.h for default values) */

// number of possible values at each position in the sequence
static  int digits = DIGITS;
// length of the sequence
static  int seqlen = SEQL;

// SECRET sequence
static int* theSeq = NULL;

// base address of GPIO memory
volatile unsigned int gpiobase ;
volatile uint32_t *gpio ;


#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))

// flag to be set in signal handler for interval times
static int timed_out = 0;

/* --------------------------------------------------------------------------- */
/* external prototypes */

/* *****************************************************************************
   HINT: use this CPP flag to select between a C and an Asm implementation 
         of the Hamming distance.
 ***************************************************************************** */

#ifdef HAMM_ASM
// prototype for the Assembler fct; only needed for an Asm implementation
int hamming(const int *x, const int *y, int seqlen);
#endif

/* --------------------------------------------------------------------------- */
// Timers and signal handlers

// time-stamps for use in signal handlers
// static uint64_t startT, stopT;

/* 
   Get a timestamp in micro-seconds 
*/
uint64_t timeInMicroseconds(void){
  struct timeval tv;
  gettimeofday (&tv, NULL);
  return (uint64_t)tv.tv_sec * (uint64_t)1000000 + (uint64_t)tv.tv_usec; // in us
}

/*
  This should be a signal handler for signals issued by the interval timer.
*/
void timer_handler (int signum)
{
    timed_out = 1;
}
/* 
   Initialise the interval timer here.
*/
void initITimer(uint64_t timeout){
    struct itimerval it_val;
    // Map the SIGALRM signal to our timer_handler function
    if (signal(SIGALRM, timer_handler) == SIG_ERR) {
          perror("Unable to catch SIGALRM");
          exit(1);
    }
    // Convert microseconds to seconds and remaining microseconds
    it_val.it_value.tv_sec = timeout / 1000000;
    it_val.it_value.tv_usec = timeout % 1000000;
    // Set interval to 0 so the timer only triggers once, not repeatedly
    it_val.it_interval.tv_sec = 0;
    it_val.it_interval.tv_usec = 0;
    // Start the timer
    if (setitimer(ITIMER_REAL, &it_val, NULL) == -1) {
        perror("Error calling setitimer()");
        exit(1);
    }
}

/* --------------------------------------------------------------------------- */
/* Helper functions for the main app */

/* 
   Initialise the secret sequence of values (of length seqlen) 
   Uses global variables: @seqlen@ for length of sequence, @digits@ for the possible number of values
*/
void initSeq(int seqlen, int digits) {
  unsigned long value, r;

  if (theSeq==NULL) {
    theSeq = calloc(seqlen, sizeof(int));
    if (theSeq==NULL) {
      failure(true, "calloc failed");
    }
  }

  srand((unsigned int)time(NULL));
  for (int i=0; i<seqlen; i++) {
    r = rand();
    value = (r % digits) + 1;
    theSeq[i] = value;
  }
}

/* 
   Show given sequence @seq@ of length @seqlen@ on the terminal.
*/
void showSeq(const int *seq, int seqlen) {
  printf("Contents of the sequence (of length %d): ", seqlen);
  for (int i=0; i<seqlen; i++) {
    printf(" %d", seq[i]);
  }
  printf("\n");
}

/* 
   Parse an integer value @val@ as a list of digits, and put them into @seq@ 
   Needed for processing command-line with options -s or -u            
*/
void readSeq(int *seq, int seqlen, int val) {
  char valStr[32];
  int i;
  size_t strLen;

  snprintf(valStr, sizeof(valStr), "%d", val);
  strLen = strlen(valStr);
  
  for (i = 0; i < seqlen && i < (int)strLen; i++) {
    seq[i] = valStr[i] - '0';
      if (seq[i] < 1 || seq[i] > digits) {
          seq[i] = 1;
      }
  }

  // pad with 1 values if necessary
  for (; i < seqlen; i++) {
      seq[i] = 1;
  }
}

/* --------------------------------------------------------------------------- */
/* Interface fcts on top of the low-level pin I/O code                         */

/* Turning LED on/off is just a call to low-level fct digital_write() */
static inline
void write_LED(volatile uint32_t *gpio, int pin, int value) {
  digital_write (gpio, pin, value);
}

/* Read the state of a GPIO pin */
static inline
int digital_read(volatile uint32_t *gpio, int pin) {
  // GPLEV0 is at index 13 for pins 0-31. This checks if the pin's bit is a 1 or 0.
  return (*(gpio + 13) & (1 << pin)) ? 1 : 0;
}

/* Blink @led@ @c@ times */
void blinkN(volatile uint32_t *gpio, int led, int c) {
    for (int i = 0; i < c; i++) {
        write_LED(gpio, led, 1); // On
        usleep(250000);          // 0.25s
        write_LED(gpio, led, 0); // Off
        usleep(250000);          // 0.25s
    }
}
/* ----------------------------------------------------------------------------- */
/* Helper fcts for this app                                                      */

/*
  HINT: the libc function powl(x, n) computes @x@ to the power of @n@
        and is a useful function for Task~5, using arbitrary sequence length.
*/

/* ***************************************************************************** */
/* NOTE: CPP flag should select Assembler version of Hamming distance            */
/*       Set the flag in the Makefile using -DHAMM_ASM                           */
/* If the flag is NOT set (as below) a C version of the Hamming distance should be selected */
/* ***************************************************************************** */

#ifndef HAMM_ASM
/*
  Computer the Hamming distance between to arrays of ints.
  OPTIONAL: this is a C implementation of Hamming distance; this version is optional
  HINT: implement a C version and test it, before implementing an ARM Assembler version
  NOTE: if you use a C implementation, you may need to modify the Makefile and remove 'hamming.o' from the list of linked files.
  The final version of the code should use the Assembler version in file hamming.s
*/
/* Computer the Hamming distance between to arrays of ints. */
int hamming(const int *x, const int *y, int seqlen) {
    int distance = 0;
    for (int i = 0; i < seqlen; i++) {
        if (x[i] != y[i]) distance++;
    }
    return distance;
}
#endif // !HAMM_ASM

/*
  Show the Hamming distance (of @seq1@ and @seq2@) in @code@ on the terminal.
*/
void showHamm(int code, const int *seq1, const int *seq2) {
    printf("Seq1: ");
    for(int i=0; i<seqlen; i++) printf("%d ", seq1[i]);
    printf("\nSeq2: ");
    for(int i=0; i<seqlen; i++) printf("%d ", seq2[i]);
    printf("\nHamming Distance: %d\n", code);
}

/* 
   OPTIONAL: implement an increment in a array of arbitrary length in @seq@
             by one, assuming the max value per element is @digits@.
  HINT: this function is useful for Task 3: arbitrary length of sequence.	     
*/
static inline
void incseq(int *seq, int seqlen, int digits) {
    for (int i = seqlen - 1; i >= 0; i--) {
        seq[i]++;
        if (seq[i] <= digits) {
            break; // No carry-over needed
        }
        seq[i] = 1; // Carry-over to next column
    }
}
/* --------------------------------------------------------------------------- */

/* 
   @submit_PIN@: Submit a PIN for checking against a secret pin.  EXPENSIVE!
   @attSeq@ is the attempted sequence, submitted for testing against the secret pin.
   @seqlen@ is the length of the sequence.
   @submitDelay@ is the delay in processing the sequence (can be changed with -S cmdline option).
   NOTE: the secret sequence is in the global variable @theSeq@ which is not an argument
         because it should be hidden to the caller.
   The function tests @attSeq@ against the secret sequence @theSeq@, by computing the Hamming distance.
   If the Hamming distance is 0, both sequences are equal, and the sequence has been found (in @attSeq@)
   The return value is a boolean value whether the sequence has been found.
*/

int submit_PIN(const int *attSeq, int seqlen, int submitDelay) {
  int found = 0;
  
  // debugging only (needs additional arguments!):
  // showSeq(attSeq,seqlen);   showHamm(code, refSeq, attSeq);

  // submits++;         // now done at caller side
  usleep(submitDelay);  // simulating a slow submit action
  found = hamming(theSeq, attSeq, seqlen) == 0;
  return found;
}

/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */

    int main(int argc, char **argv){
        int found = 0, code = 0, refCode = 0;
        int buttonPressed = 0;
        
        // use these to count: number of comparisons in total, found after how many attempts, total number of submits
        int attempts = 0, found_at = 0, submits = 0;
        int *attemptSeq = NULL, *refSeq = NULL;
        double startTime, stopTime;
        
        // variables holding Pin numbers for LEDs and button
        int pinLED = LED, pinLED2 = LED2, pinButton = BUTTON;
        // int fSel, shift, pin,  clrOff, setOff, off, res;
        int   fd ;
        
        // strings for temporary usage (e.g. writing to LCD display)
        // char str1[32];
        // char str2[32];
        
        // useful for interval timers
        // struct timeval t1, t2 ;
        
        // variables for command-line processing
        // command-line options
        bool opt_e = false, opt_l = false;
        int opt_m = 0, opt_n = 0, opt_S = 0, opt_s = 0, opt_r = 0;
        // variables derived from command line options
        bool verbose = false, help = false, debug = false, unit_test = false;
        int submitDelay = SUBMIT_DELAY;
        
        // -------------------------------------------------------
        // process command-line arguments
        
        // see: man 3 getopt for docu and an example of command line parsing
        { // see the CW spec for the intended meaning of these options
            int opt;
            while ((opt = getopt(argc, argv, "hvdeluS:s:r:m:n:")) != -1) {
                switch (opt) {
                    case 'v':
                        verbose = true;
                        break;
                    case 'h':
                        help = true;
                        break;
                    case 'd':
                        debug = true;
                        break;
                    case 'e':
                        opt_e = true;
                        break;
                    case 'l': // LCD test only
                        opt_l = true;
                        break;
                    case 'u':
                        unit_test = true;
                        break;
                    case 'S':
                        opt_S = atoi(optarg);
                        submitDelay = opt_S;
                        break;
                    case 's':
                        opt_s = atoi(optarg);
                        break;
                    case 'r':
                        opt_r = atoi(optarg);
                        break;
                    case 'm':
                        opt_m = atoi(optarg);
                        digits = opt_m;
                        break;
                    case 'n':
                        opt_n = atoi(optarg);
                        seqlen = opt_n;
                        break;
                    default: /* '?' */
                        fprintf(stderr, "Usage: %s [-h] [-v] [-d] [-e] [-m <maxval> ] [-n <seqlen>] [-u <seq1> <seq2>] [-s <secret seq>] [-r <reference seq>]  \n", argv[0]);
                        exit(EXIT_FAILURE);
                }
            }
        }
        
        if (help) {
            fprintf(stderr, "pinCrack program, running on a Raspberry Pi, with connected LED, button and LCD display\n");
            fprintf(stderr, "Use the button for input of numbers. The LCD display will show the matches with the secret sequence.\n");
            fprintf(stderr, "For full specification of the program see: https://www.macs.hw.ac.uk/~hwloidl/Courses/F28HS/F28HS_CW2_2026.pdf\n");
            fprintf(stderr, "Usage: %s [-h] [-v] [-d] [-e] [-u <seq1> <seq2>] [-s <secret seq>] [-r <reference seq>]  \n", argv[0]);
            exit(EXIT_SUCCESS);
        }
        
        if (verbose) {
            printf("Settings for running the program\n");
            printf("Verbose is %s\n", (verbose ? "ON" : "OFF"));
            printf("Debug is %s\n", (debug ? "ON" : "OFF"));
            printf("Unittest is %s\n", (unit_test ? "ON" : "OFF"));
            printf("Exhaustive search is %s\n", (opt_e ? "ON" : "OFF"));
            printf("Submit delay is %d\n", submitDelay);
            if (opt_s)  printf("Secret sequence set to %d\n", opt_s);
            if (opt_r)  printf("Reference sequence set to %d\n", opt_r);
        }
        
        if (verbose) {
            printf("Hint: remember to compute the Hamming distance in each iteration and assign it to variable code; current (unused) value: %d\n", code);
            printf("Code style requirement: collect the values of the input sequence in the variable attemptSeq; current (unused) value: %p\n", attemptSeq);
        }
        
        /* ***************************************************************************** */
        /* COMPLETE THIS CODE */
        /* Initialise the sequences that you need here, before using them  */
        /* ***************************************************************************** */
        
        if (opt_s) { // if -s option is given, use the sequence as SECRET sequence
            if (theSeq==NULL) {
                theSeq = calloc(seqlen, sizeof(int));
                if (theSeq==NULL) {
                    failure(true, "calloc failed");
                }
            }
            readSeq(theSeq, seqlen, opt_s);
            if (verbose) {
                fprintf(stderr, "Running program with secret sequence:\n");
                showSeq(theSeq,seqlen);
            }
        }
        
        if (opt_r) { // if -r option is given, use the sequence as REFERENCE sequence
            if (refSeq==NULL) {
                refSeq = calloc(seqlen, sizeof(int));
                if (refSeq==NULL) {
                    failure(true, "calloc failed");
                }
            }
            readSeq(refSeq, seqlen, opt_r);
            if (verbose) {
                fprintf(stderr, "Running program with reference sequence:\n");
                showSeq(refSeq,seqlen);
            }
        }
        
        /* --------------------------------------------------------------------------- */
        /* Configuration of the LCD display */
        int bits, rows, cols ;
        
        // hard-coded: 16x2 display, using a 4-bit connection
        bits = 4;
        cols = 16;
        rows = 2;
        
        printf ("Raspberry Pi configuration: red LED: %d; green LED: %d; button: %d\n", pinLED2, pinLED, pinButton) ;
        printf ("Raspberry Pi LCD driver for a %dx%d display (%d-bit wiring) \n", cols, rows, bits) ;
        
        /* --------------------------------------------------------------------------- */
        /* Check for root priveleges (needed for controlling LEDs etc) */
        
        if (geteuid () != 0) {
            fprintf (stderr, "setup: Must be root. (Did you forget sudo?)\n") ;
            exit(EXIT_FAILURE);
        }
        
        /* --------------------------------------------------------------------------- */
        /* constants for RPi2/3. NOTE: RPi4 needs a different base address */
        // -----------------------------------------------------------------------------
        // RPi2/3
        //gpiobase = 0x3F200000 ;
        // RPi4
        gpiobase = 0xFE200000 ;
        
        // -----------------------------------------------------------------------------
        // memory mapping
        // Open the master /dev/memory device
        
        if ((fd = open ("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC) ) < 0)
            return failure (false, "setup: Unable to open /dev/mem: %s\n", strerror (errno)) ;
        
        // GPIO:
        gpio = mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpiobase) ;
        if ((int32_t)gpio == -1)
            return failure (false, "setup: mmap (GPIO) failed: %s\n", strerror (errno)) ;
        
        // -----------------------------------------------------------------------------
        // Setting mode of pins
        // Setting mode of pins
        pin_mode(gpio, pinLED, OUTPUT);
        pin_mode(gpio, pinLED2, OUTPUT);
        pin_mode(gpio, pinButton, INPUT);
        
        // Initialize LCD
        lcd_init(gpio);
        
        // -----------------------------------------------------------------------------
        // Initialise the LCD display
        
        // Initialize the LCD screen so it can receive text commands
        lcd_init(gpio);
        // -----------------------------------------------------------------------------
        // Initialise the LCD display
        
        /* ***************************************************************************** */
        /* COMPLETE THIS CODE */
        /* Initialise the LCD display */
        /* ***************************************************************************** */
        
        
        if (opt_l) { // TESTING only: show some text on the LCD display to demonstrate it's working in principle
            /* ***************************************************************************** */
            /* OPTIONAL CODE to show that the LCD display is working */
            /* ***************************************************************************** */
            
            exit(2);
        }
        
        // -----------------------------------------------------------------------------
        // App initialisation
        
        /* Initialise the secret sequence */
        if (!opt_s)  initSeq(seqlen, digits);
        
        /* Use the debugging option like this for extra messages */
        if (debug) {
            printf("Secret sequence is: ");
            showSeq(theSeq,seqlen);
        }
        
        // -----------------------------------------------------------------------------
        // Unit testing: check the Hamming distance between two given sequences
        
        if (unit_test) { // unit test: just print the Hamming distance
            
            if (!opt_r) {
                fprintf(stderr, "Need to use both -s and -r for unit testing (with -u)\n");
                exit(EXIT_FAILURE);
            }
            
            // output to screen
            refCode = hamming(theSeq, refSeq, seqlen);
            showSeq(theSeq,seqlen);
            showSeq(refSeq,seqlen);
            showHamm(refCode, theSeq, refSeq);
            exit(EXIT_SUCCESS);
        }
        
        // -----------------------------------------------------------------------------
        
        /* Print Greetings Message on LCD display */
        /* ***************************************************************************** */
        /* COMPLETE THIS CODE */
        /* ***************************************************************************** */
        char surname[256];
        char display_surname[6]={0};
        printf("\n TASK 1: Greeting\n");
        printf("Enter Surname: ");
        scanf("%255s", surname);
        
        for(int i=0; i<5; i++) {
            char sn = surname[i];
            display_surname[i] = sn;
            
            if ((sn >= 'A' && sn <= 'Z') || (sn >= 'a' && sn <= 'z')) {
                if (sn == 'A' || sn == 'E' || sn == 'I' || sn == 'O' || sn == 'U' || sn == 'a' || sn == 'e' || sn == 'i' || sn == 'o' || sn == 'u') {
                    // Blink Green LED (Vowel)
                    write_LED(gpio, LED, 1);
                    usleep(500000);
                    write_LED(gpio, LED, 0);
                    usleep(200000);
                }
                else {
                    // Blink Red LED (Consonant)
                    write_LED(gpio, LED2, 1);
                    usleep(500000);
                    write_LED(gpio, LED2, 0);
                    usleep(200000);
                }
            }
        }
        // Display the captured first 5 letters on the LCD
        lcd_clear(gpio);
        usleep(2000);  // WAIT 2ms for the screen to finish clearing
        lcd_puts(gpio, display_surname);
        printf("%s\n", display_surname);
        
        int flush_char;
        while ((flush_char = getchar()) != '\n' && flush_char != EOF);
        printf("\nPress ENTER to begin Task 2\n");
        
        /* OPTIONAL: wait for ENTER key before continuing */
        waitForEnter () ; // -------------------------------------------------------
        
        /* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
        /* Main part of the application  */
        
        // -------------------------------------------------------
        // PHASE 1: sequence input
        
        // Dynamically allocate memory for the attempt sequence
        attemptSeq = calloc(seqlen, sizeof(int));
        if (attemptSeq == NULL) {
            failure(true, "calloc failed for attemptSeq");
        }
        printf("Task 2: Input Sequence");
        printf("Enter a %d-digit PIN\n", seqlen);
        
        
        // ...........................................................................
        // Iterate over all elements of the sequence
        
        for (int i = 0; i < seqlen; i++) {
            int press_count = 0;
            int last_state = 0; // Tracks previous button state for edge detection
            timed_out = 0;      // Reset the timeout flag for the new digit
            printf("Enter digit %d: ", i + 1);
            fflush(stdout);     // Ensure the prompt prints immediately
            // Start the interval timer for this digit (TIMEOUT is defined in cw2-config.h)
            initITimer(TIMEOUT);
            
            // Polling loop: runs until the interval timer triggers and sets timed_out to 1
            while (!timed_out) {
                // Read current button state
                buttonPressed = digital_read(gpio, pinButton);
                if (buttonPressed == 1) {
                    write_LED(gpio, pinLED, 1); // Turn Green LED ON while pressed
                    if (last_state == 0) { // Only count when it transitions from unpressed to pressed
                        press_count++;
                        last_state = 1;
                    }
                }
                else {
                    write_LED(gpio, pinLED, 0); // Turn Green LED OFF when released
                    last_state = 0;
                }
                usleep(10000);
            }
            
            // Ensure Green LED is off when the timeout occurs
            write_LED(gpio, pinLED, 0);
            // Handle values out-of-range
            if (press_count < 1) {
                press_count = 1; // Default to 1 if user didn't press anything
            }
            else if (press_count > digits) {
                press_count = digits; // Cap at the maximum allowed digits
            }
            
            // Store the bounded value
            attemptSeq[i] = press_count;
            printf("Registered: %d\n", attemptSeq[i]);
            
            // Acknowledge input: Red control LED blinks once
            blinkN(gpio, pinLED2, 1);
            usleep(200000); // Brief pause
            
            // Repeat input: Green data LED blinks as many times as pressed
            blinkN(gpio, pinLED, attemptSeq[i]);
        }
        
        // End of sequence indicator: Red control LED blinks twice
        usleep(300000);
        blinkN(gpio, pinLED2, 2);
        printf("\nInput sequence is: ");
        showSeq(attemptSeq, seqlen);
        
        
        // -------------------------------------------------------
        // PHASE 2: Main Task: full search
        
        // Print the version of the code this is running; set values in cw2-config.h
        printf("--------------------- \n");
        printf(">> Version %d: %s with %d digits and %d sequence length\n", VERSION, VERSION_STR, digits, seqlen);
#ifdef HAMM_ASM
        printf(">> HAMM_ASM version: Hamming distance in ARM Assembler\n");
#else
        printf(">> Hamming in C version\n");
#endif
        printf("--------------------- \n");
        
        if (debug) {
            printf("Debug mode\nThe secret sequence is:");
            showSeq(theSeq,seqlen);
        }
        
        // calculate the total range of possible sequences
        unsigned long bound = powl(digits, seqlen);
        
        // time-stamp
        startTime = clock();
        
        int *currentTestSeq = calloc(seqlen, sizeof(int));
        
        // Choose starting sequence based on command line args
        if (opt_r) {
            for(int i=0; i<seqlen; i++) currentTestSeq[i] = refSeq[i];
        } else {
            for(int i=0; i<seqlen; i++) currentTestSeq[i] = attemptSeq[i];
        }
        
        printf("\nStarting Search...\n");
                
                // --- NEW: Display the user's entered PIN on the LCD ---
                char enteredStr[32] = "Your PIN: ";
                for(int i=0; i<seqlen; i++) {
                    sprintf(enteredStr + strlen(enteredStr), "%d", currentTestSeq[i]);
                }
                lcd_clear(gpio);
                usleep(2000);    // 2ms hardware delay to clear screen
                lcd_puts(gpio, enteredStr);
                
                usleep(2000000); // Pause for 2 seconds so you can read it!

                // --- 1. Check initial user input & Calculate Distance ---
                attempts++;
                submits++;
                code = hamming(theSeq, currentTestSeq, seqlen);
                
                printf("Initial Input Hamming Distance: %d\n", code);
                
                // --- 2. Display initial distance on LCD ---
                char distStr[32];
                sprintf(distStr, "Init Dist: %d", code);
                lcd_clear(gpio);
                usleep(2000); // 2ms hardware delay to clear screen
                lcd_puts(gpio, distStr);
                
                usleep(2000000); // Pause for 2 seconds so you can read it!
                
                if (code == 0) {
                    found = 1;
                    found_at = attempts;
                    printf("Wow! You guessed the secret sequence on the first try!\n");
                    
                    lcd_clear(gpio);
                    usleep(2000);
                    lcd_puts(gpio, "PIN Cracked!");
                    
                } else {
                    printf("Initial guess incorrect. Running silent brute-force search...\n");
                    
                    // Reset to lowest sequence (e.g., 1, 1, 1)
                    for (int i = 0; i < seqlen; i++) currentTestSeq[i] = 1;
                    
                    // --- 3. The Silent, High-Speed Loop ---
                    while (attempts < bound && !found) {
                        attempts++;
                        submits++;
                        
                        // Check if it is the correct PIN
                        if (submit_PIN(currentTestSeq, seqlen, submitDelay)) {
                            found = 1;
                            found_at = attempts;
                            
                            // --- 4. Display the Secret PIN ---
                            char foundStr[32] = "Secret: ";
                            for(int i=0; i<seqlen; i++) {
                                sprintf(foundStr + strlen(foundStr), "%d", currentTestSeq[i]);
                            }
                            lcd_clear(gpio);
                            usleep(2000);
                            lcd_puts(gpio, foundStr);
                            printf("\n>>> SUCCESS: %s <<<\n", foundStr);
                            
                            break; // Exit the loop
                        }
                        
                        incseq(currentTestSeq, seqlen, digits); // Generate next combination
                    }
                }
        free(currentTestSeq);
                
                stopTime = clock();
                
                printf("Runtime; %f secs\n", (stopTime-startTime)/CLOCKS_PER_SEC);
                printf("Sequence %s\n", found ? "found" : "not found");
                printf("%s search finished for %d digits and %d seqlen (expect %ld):\n%d attempts (found at %d i.e. %.2f %%), %d submits\n",
                       (opt_e ? "Exhaustive" : "Non-exhaustive"), digits, seqlen, bound, attempts, found_at, (float)found_at / ((float)bound / 100.0), submits);
                printf("Secret sequence was: ");
                showSeq(theSeq,seqlen);
                
                // Turn off LEDs before exiting
                write_LED(gpio, pinLED, 0);
                write_LED(gpio, pinLED2, 0);
                
                free(theSeq);
                free(refSeq);
                
                return 0;
}
