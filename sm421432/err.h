/*
 * Plik pochodzi z przykładów do zadań na laboratoria z Programowania Współbieżnego, przedmiotu prowadzonego
 * na Uniwersytecie Warszawskim w roku akademickim 2020/2021.
 */


#ifndef _ERR_
#define _ERR_

/* print system call error message and terminate */
extern void syserr(int bl, const char *fmt, ...);

/* print error message and terminate */
extern void fatal(const char *fmt, ...);

#endif
