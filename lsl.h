/* lsl.h */

#ifndef LSL_H
#define LSL_H

int lsl_init(const char *serial);

void lsl_send(const float eeg[14]);

void lsl_close(void);

#endif