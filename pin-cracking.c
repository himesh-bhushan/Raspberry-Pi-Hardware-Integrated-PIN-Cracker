
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

#include "config.h"
#include "aux.h"
#include "lcd-binary.h"
#include "lcd-fcts.h"

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

#ifdef HAMM_ASM
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


//This should be a signal handler for signals issued by the interval timer.
void timer_handler (int signum)
{
    timed_out = 1;
}

//Initialise the interval timer
void initITimer(uint64_t timeout){
    struct itimerval timer;
    if (signal(SIGALRM, timer_handler) == SIG_ERR) {
          perror("Failed to start the button timer");
          exit(1);
    }
    
    // Convert microseconds to seconds and remaining microseconds
    timer.it_value.tv_sec = timeout / 1000000;
    timer.it_value.tv_usec = timeout % 1000000;
    
    // Set interval to 0 so the timer only triggers once, not repeatedly
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    
    // Start the timer
    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
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

/* Helper fcts for this app                                                      */
#ifndef HAMM_ASM

/* Computer the Hamming distance between to arrays of ints. */
int hamming(const int *x, const int *y, int seqlen) {
    int distance = 0;
    for (int i = 0; i < seqlen; i++) {
        if (x[i] != y[i]) distance++;
    }
    return distance;
}
#endif // !HAMM_ASM

/* Show the Hamming distance (of @seq1@ and @seq2@) in @code@ on the terminal. */

void showHamm(int code, const int *seq1, const int *seq2) {
    printf("Sequence 1: ");
    for(int i=0; i<seqlen; i++) printf("%d ", seq1[i]);
    printf("\nSequence 2: ");
    for(int i=0; i<seqlen; i++) printf("%d ", seq2[i]);
    printf("\nHamming Distance: %d\n", code);
}

static inline
void incseq(int *seq, int seqlen, int digits) {
    for (int i = seqlen - 1; i >= 0; i--) {
        seq[i]++;
        if (seq[i] <= digits) {
            break;
        }
        seq[i] = 1;
    }
}

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
    int *input_pin = NULL, *refSeq = NULL;
    double startTime, stopTime;
    
    // variables holding Pin numbers for LEDs and button
    int green_led = LED, red_led = LED2, button = BUTTON;
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

    //Allocate memory for the user input PIN
    input_pin = calloc(seqlen, sizeof(int));
    if (input_pin == NULL) {
        failure(true, "Failed to allocate memory for input_pin");
    }
    //Allocate memory for the secret sequence
    if (theSeq == NULL) {
        theSeq = calloc(seqlen, sizeof(int));
        if (theSeq == NULL) {
            failure(true, "Failed to allocate memory for theSeq");
        }
    }
    //Allocate memory for the reference sequence (testing)
    if (refSeq == NULL) {
        refSeq = calloc(seqlen, sizeof(int));
        if (refSeq == NULL) {
            failure(true, "Failed to allocate memory for refSeq");
        }
    }
    
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
    
    /* Configuration of the LCD display */
    int bits, rows, cols ;
    
    // hard-coded: 16x2 display, using a 4-bit connection
    bits = 4;
    cols = 16;
    rows = 2;
    
    printf ("Raspberry Pi configuration: red LED: %d; green LED: %d; button: %d\n", red_led, green_led, button) ;
    printf ("Raspberry Pi LCD driver for a %dx%d display (%d-bit wiring) \n", cols, rows, bits) ;
    
    /* Check for root priveleges (needed for controlling LEDs etc) */
    if (geteuid () != 0) {
        fprintf (stderr, "setup: Must be root. (Did you forget sudo?)\n") ;
        exit(EXIT_FAILURE);
    }
    
    /* constants for RPi2/3. NOTE: RPi4 needs a different base address */
    // RPi2/3
    //gpiobase = 0x3F200000 ;
    // RPi4
    gpiobase = 0xFE200000 ;
        
    if ((fd = open ("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC) ) < 0)
        return failure (false, "setup: Unable to open /dev/mem: %s\n", strerror (errno)) ;
    
    // GPIO:
    gpio = mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, gpiobase) ;
    if ((int32_t)gpio == -1)
        return failure (false, "setup: mmap (GPIO) failed: %s\n", strerror (errno)) ;
    
    // Setting mode of pins
    pin_mode(gpio, green_led, OUTPUT);
    pin_mode(gpio, red_led, OUTPUT);
    pin_mode(gpio, button, INPUT);
    
    // Initialise the LCD display    
    lcd_init(gpio);
    
    
    // App initialisation
    /* Initialise the secret sequence */
    if (!opt_s)  initSeq(seqlen, digits);
    
    /* Use the debugging option like this for extra messages */
    if (debug) {
        printf("Secret sequence is: ");
        showSeq(theSeq,seqlen);
    }
    
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
        
    /* Print Greetings Message on LCD display */
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
    
    waitForEnter () ; 
    
    // PHASE 1: sequence input
    // Iterate over all elements of the sequence
    
    for (int i = 0; i < seqlen; i++) {
        int button_counter = 0;
        int button_state = 0; // Tracks previous button state
        timed_out = 0;
        printf("Enter digit %d: ", i + 1);
        fflush(stdout);     // Ensure the prompt prints immediately
        initITimer(TIMEOUT);
        
        while (!timed_out) {
            // Read current button state
            buttonPressed = digital_read(gpio, button);
            if (buttonPressed == 1) {
                write_LED(gpio, green_led, 1); // Turn Green LED on while pressed
                if (button_state == 0) {
                    button_counter++;
                    button_state = 1;
                }
            }
            else {
                write_LED(gpio, green_led, 0); // Turn Green LED off when released
                button_state = 0;
            }
            usleep(10000);
        }
        
        // Ensure Green LED is off when the timed_out occurs
        write_LED(gpio, green_led, 0);
        if (button_counter < 1) {
            button_counter = 1;
        }
        else if (button_counter > digits) {
            button_counter = digits;
        }
        
        input_pin[i] = button_counter;
        printf("Registered: %d\n", input_pin[i]);
        
        //Red LED blinks once to confirm input
        blinkN(gpio, red_led, 1);
        usleep(200000);
        
        //Green LED blinks everytime button pressed
        blinkN(gpio, green_led, input_pin[i]);
    }
    
    //Red control LED blinks twice to confirm full input
    usleep(300000);
    blinkN(gpio, red_led, 2);
    printf("\nInput sequence is: ");
    showSeq(input_pin, seqlen);
    
    
    // PHASE 2: Main Task: full search
    
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
    
    startTime = clock();
    
    int *guess_pin = calloc(seqlen, sizeof(int));
    int *pin_start = opt_r ? refSeq : input_pin;
    
    if (!opt_e) {
        
        // Task 4: Hamming Search
        printf("\nTask 4: Starting Search\n");
        
        attempts = 0;
        submits = 0;
        int hamming_distance = hamming(theSeq, pin_start, seqlen);
        
        // Display User PIN
        char user_pin[32] = "Your PIN: ";
        for(int i=0; i<seqlen; i++){
            sprintf(user_pin + strlen(user_pin), "%d", pin_start[i]);
        }
        
        lcd_clear(gpio); usleep(2000);
        lcd_puts(gpio, user_pin);
        usleep(2000000);
        
        printf("Initial Hamming Distance: %d\n", hamming_distance);
        
        // Display Distance
        char pin_dist[32];
        sprintf(pin_dist, "Init Dist: %d", hamming_distance);
        lcd_clear(gpio); usleep(2000); lcd_puts(gpio, pin_dist);
        usleep(2000000);
        
        if (hamming_distance == 0) {
            found = 1; found_at = 1;
            printf("Amazing! You guessed the secret PIN in one go!\n");
            blinkN(gpio, green_led, 2);
            lcd_clear(gpio); usleep(2000); lcd_puts(gpio, "PIN found");
        }
        else {
            printf("Incorrect Guess. Trying to find the pin...\n");
            for (int i = 0; i < seqlen; i++){
                guess_pin[i] = 1; // Reset to 1,1,1
            }
            while (attempts < bound) {
                attempts++;
                int input_distance = hamming(guess_pin, pin_start, seqlen);
                
                if (input_distance == hamming_distance) {
                    submits++;
                    if (submit_PIN(guess_pin, seqlen, submitDelay)) {
                        found = 1;
                        if (found_at == 0){
                            found_at = attempts;
                        }
                        blinkN(gpio, green_led, 2);
                        char pin_found[32] = "PIN found: ";
                        
                        for(int i=0; i<seqlen; i++){
                            sprintf(pin_found + strlen(pin_found), "%d", guess_pin[i]);
                        }
                        
                        lcd_clear(gpio);
                        usleep(2000);
                        lcd_puts(gpio, pin_found);
                        printf("\nPIN Found: %s <<<\n", pin_found);
                        break;
                    }
                }
                incseq(guess_pin, seqlen, digits);
            }
        }
    }
    else {
        
        //Task 5: Brute Force and variable input
        
        printf("\nTask 5: Starting Search\n");
        for (int i = 0; i < seqlen; i++) {
            guess_pin[i] = 1; // Reset to 1,1,1
        }
        attempts = 0;
        submits = 0;
        
        while (attempts < bound && !found) {
            attempts++;
            
            //Use Assembly hamming check with the secret PIN
            code = hamming(theSeq, guess_pin, seqlen);
            
            if (code == 0) {
                submits++;
                if (submit_PIN(guess_pin, seqlen, submitDelay)) {
                    found = 1;
                    found_at = attempts;
                    char pin_found[32] = "Secret: ";
                    
                    for(int i = 0; i < seqlen; i++){
                        sprintf(pin_found + strlen(pin_found), "%d", guess_pin[i]);
                    }
                    
                    lcd_clear(gpio); usleep(2000); lcd_puts(gpio, pin_found);
                    printf("\nPIN Found: %s\n", pin_found);
                    break;
                }
            }
            incseq(guess_pin, seqlen, digits); // Odometer increment
        }
    }
    
    
    stopTime = clock();
    
    printf("Runtime; %f secs\n", (stopTime-startTime)/CLOCKS_PER_SEC);
    printf("Sequence %s\n", found ? "found" : "not found");
    printf("%s search finished for %d digits and %d seqlen (expect %ld):\n%d attempts (found at %d i.e. %.2f %%), %d submits\n",
         (opt_e ? "Exhaustive" : "Non-exhaustive"), digits, seqlen, bound, attempts, found_at, (float)found_at / ((float)bound / 100.0), submits);
    printf("Secret sequence was: ");
    showSeq(theSeq,seqlen);

    //Exit Message
    lcd_clear(gpio);
    usleep(2000);
    lcd_puts(gpio, "Thank you!");
    write_LED(gpio, green_led, 0);
    write_LED(gpio, red_led, 0);
    
    free(guess_pin);
    free(input_pin);
    free(theSeq);
    free(refSeq);
    
    return 0;
}
