/* lsl.c  placement file for streaming LSL info to OpenBCI or other eeg gui's'*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lsl_c.h>

typedef struct
{
    uint16_t eeg[14];

    int16_t gyro_x;
    int16_t gyro_y;

    uint8_t counter;
    uint8_t battery;

} EpocSample;

static lsl_streaminfo info = NULL;
static lsl_outlet outlet = NULL;

static const char *names[14] =
{
    "AF3",
    "F7",
    "F3",
    "FC5",
    "T7",
    "P7",
    "O1",
    "O2",
    "P8",
    "T8",
    "FC6",
    "F4",
    "F8",
    "AF4"
};

int lsl_init(const char *serial)
{
    char source_id[64];

    snprintf(source_id,
             sizeof(source_id),
             "emotiv-%s",
             serial);

    info = lsl_create_streaminfo(
                "obci_eeg1",
                "EEG",
                14,
                128.0,
                cft_float32,
                source_id);

    if (!info)
        return -1;

lsl_xml_ptr desc = lsl_get_desc(info);
lsl_append_child_value(desc, "manufacturer", "OpenBCI");

lsl_xml_ptr channels = lsl_append_child(desc, "channels");

static const char *labels[14] = {
    "AF3","F7","F3","FC5",
    "T7","P7","O1","O2",
    "P8","T8","FC6","F4",
    "F8","AF4"
};

for (int i = 0; i < 14; i++) {
    lsl_xml_ptr ch = lsl_append_child(channels, "channel");

    lsl_append_child_value(ch, "label", labels[i]);
    lsl_append_child_value(ch, "type", "EEG");
    lsl_append_child_value(ch, "unit", "counts");
}


    /* Optional metadata */

    outlet = lsl_create_outlet(info,
                               0,      /* default chunk */
                               360);   /* max buffering */

    if (!outlet)
        return -1;

    printf("LSL outlet created.\n");

    return 0;
}
void lsl_send8(const float eeg[14])
{
    lsl_push_sample_f(outlet,
                      eeg);
}

void lsl_send4(const float eeg[14])
{
    static int count = 0;

    if ((count++ & 127) == 0)
    {
        for (int i = 0; i < 14; i++)
            printf("%8.1f ", eeg[i]);

        printf("\n");
    }

    lsl_push_sample_f(outlet, eeg);
}

void lsl_send(const EpocSample *sample)
{
    float eeg[14];
 
    for (int i = 0; i < 14; i++)
        eeg[i] = (float)sample->eeg[i];
 
    static int count = 0;
 
    if ((count++ & 127) == 0)
    {
        printf("sample : ");
 
        for (int i = 0; i < 14; i++)
            printf("%7u ", sample->eeg[i]);
 
        printf("\n");
 
      //  printf("float  : ");
 
       // for (int i = 0; i < 14; i++)
        //    printf("%7.1f ", eeg[i]);
 
        printf("\n");
    }//
 
    lsl_push_sample_f(outlet, eeg);
}


void lsl_close(void)
{
    if (outlet)
    {
        lsl_destroy_outlet(outlet);
        outlet = NULL;
    }

    if (info)
    {
        lsl_destroy_streaminfo(info);
        info = NULL;
    }
}