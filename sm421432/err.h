/*
 * Plik pochodzi z przykładów do zadań na laboratoria z Programowania Współbieżnego, przedmiotu prowadzonego
 * na Uniwersytecie Warszawskim w roku akademickim 2020/2021.
 */


#ifndef _ERR_
#define _ERR_

/* wypisuje informacje o błędnym zakończeniu funkcji systemowej 
i kończy działanie */
extern void syserr(const char *fmt, ...);

/* wypisuje informacje o błędzie i kończy działanie */
extern void fatal(const char *fmt, ...);

#endif
