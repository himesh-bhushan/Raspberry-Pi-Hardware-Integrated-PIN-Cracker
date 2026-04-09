#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdint.h>
#include <time.h>
#include "aux.h"

void delay(unsigned long howLong) {
      struct timespec sleeper, dummy ;
      sleeper.tv_sec  = (time_t)(howLong / 1000000L) ;
      sleeper.tv_nsec = (long)(howLong % 1000000L) * 1000L ;

#if defined(DEBUG)  && defined(VERBOSE) 
      fprintf(stderr, "delaying by %u s and %u ns (input: %lu us) ...\n", sleeper.tv_sec, sleeper.tv_nsec, howLong);
#endif

#if defined(DEBUG) && defined(VERBOSE) 
      startT = timeInMicroseconds();
      nanosleep (&sleeper, &dummy) ;
      stopT = timeInMicroseconds();
      diffT= stopT - startT;
      fprintf(stderr, "  measured delay: %f s (%d us) ...\n", ((double)diffT) / 1000000.0, diffT);
#else
      nanosleep (&sleeper, &dummy) ;
#endif
}

void delayMicroseconds (unsigned int howLong)
{
  struct timespec sleeper ;
  unsigned int uSecs = howLong % 1000000 ;
  unsigned int wSecs = howLong / 1000000 ;

  /**/ if (howLong ==   0)
    return ;
#if 0
  else if (howLong  < 100)
    delayMicrosecondsHard (howLong) ;
#endif
  else
  {
    sleeper.tv_sec  = wSecs ;
    sleeper.tv_nsec = (long)(uSecs * 1000L) ;
    nanosleep (&sleeper, NULL) ;
  }
}

void waitForEnter (void)
{
  printf ("Press ENTER to continue: ") ;
  (void)fgetc (stdin) ;
}

int failure (bool fatal, const char *message, ...)
{
  va_list argp ;
  char buffer [1024] ;

  if (!fatal)
    return -1 ;

  va_start (argp, message) ;
  vsnprintf (buffer, 1024, message, argp) ;
  va_end (argp) ;

  fprintf (stderr, "%s", buffer) ;
  exit (EXIT_FAILURE) ;

  return 0 ;
}
