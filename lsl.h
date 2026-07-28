/* lsl.h */

#ifndef LSL_H
#define LSL_H

typedef struct
{
    uint16_t eeg[14];

    int16_t gyro_x;
    int16_t gyro_y;

    uint8_t counter;
    uint8_t battery;

} EpocSample;

int lsl_init(const char *serial);

void lsl_send(const float eeg[14]);

void lsl_close(void);

#endif