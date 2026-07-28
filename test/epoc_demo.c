#include <stdio.h>
#include <string.h>
#include "hidapi/hidapi.h"
#include "mcrypt/mcrypt.h"

//gcc epoc_demo.c -lmcrypt -L./ -lhidapi
//export DYLD_LIBRARY_PATH=./

//#if defined(__APPLE__) && HID_API_VERSION >= HID_API_MAKE_VERSION(0, 12, 0)
//#include "hidapi_darwin.h"
//#endif

/*
 * epoc_demo.c
 *
 * Emotiv EPOC Model 1.0 demo
 * macOS
 * hidapi + libmcrypt
 *
 * Based on original Emokit decoder.
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
 
#define VID 0x1234
#define PID 0xED02
 
/* ---------------------------------------------------- */
/* EEG bit masks (original Emokit)                      */
/* ---------------------------------------------------- */
 
const unsigned char F3_MASK[14]  ={10,11,12,13,14,15,0,1,2,3,4,5,6,7};
const unsigned char FC6_MASK[14] ={214,215,200,201,202,203,204,205,206,207,192,193,194,195};
const unsigned char P7_MASK[14]  ={84,85,86,87,72,73,74,75,76,77,78,79,64,65};
const unsigned char T8_MASK[14]  ={160,161,162,163,164,165,166,167,152,153,154,155,156,157};
const unsigned char F7_MASK[14]  ={48,49,50,51,52,53,54,55,40,41,42,43,44,45};
const unsigned char F8_MASK[14]  ={178,179,180,181,182,183,168,169,170,171,172,173,174,175};
const unsigned char T7_MASK[14]  ={66,67,68,69,70,71,56,57,58,59,60,61,62,63};
const unsigned char P8_MASK[14]  ={158,159,144,145,146,147,148,149,150,151,136,137,138,139};
const unsigned char AF4_MASK[14] ={196,197,198,199,184,185,186,187,188,189,190,191,176,177};
const unsigned char F4_MASK[14]  ={216,217,218,219,220,221,222,223,208,209,210,211,212,213};
const unsigned char AF3_MASK[14] ={46,47,32,33,34,35,36,37,38,39,24,25,26,27};
const unsigned char O2_MASK[14]  ={140,141,142,143,128,129,130,131,132,133,134,135,120,121};
const unsigned char O1_MASK[14]  ={102,103,88,89,90,91,92,93,94,95,80,81,82,83};
const unsigned char FC5_MASK[14] ={28,29,30,31,16,17,18,19,20,21,22,23,8,9};
 
/* ---------------------------------------------------- */
 
typedef struct
{
    MCRYPT td;
 
    unsigned char key[16];
 
    unsigned char raw[32];
    unsigned char frame[32];
 
    unsigned char block[16];
 
} Epoc;
 
/* ---------------------------------------------------- */
 
int get_level(unsigned char frame[32], const unsigned char bits[14])
{
    signed char i;
    char b,o;
    int level=0;
 
    for(i=13;i>=0;i--)
    {
        level <<= 1;
 
        b=(bits[i]/8)+1;
        o=bits[i]%8;
 
        level |= (frame[b]>>o)&1;
    }
 
    return level;
}
 
/* ---------------------------------------------------- */
/* Original Emokit serial->AES key                      */
/* ---------------------------------------------------- */
 
void make_crypto_key(const char *serial, unsigned char key[16])
{
    size_t l = strlen(serial);

    if (l < 4)
    {
        fprintf(stderr, "Serial too short: \"%s\"\n", serial);
        exit(EXIT_FAILURE);
    }

    char s1 = serial[l - 1];   // last character
    char s2 = serial[l - 2];
    char s3 = serial[l - 3];
    char s4 = serial[l - 4];

    /*
     * Matches the "else" branch from the uploaded emokit.c
     */

    key[0]  = s1;
    key[1]  = 0x00;
    key[2]  = s2;
    key[3]  = 'T';

    key[4]  = s3;
    key[5]  = 0x10;
    key[6]  = s4;
    key[7]  = 'B';

    key[8]  = s1;
    key[9]  = 0x00;
    key[10] = s2;
    key[11] = 'H';

    key[12] = s3;
    key[13] = 0x00;
    key[14] = s4;
    key[15] = 'P';
}
 
/* ---------------------------------------------------- */
 
void print_key(unsigned char key[16])
{
    printf("AES KEY:\n");
 
    for(int i=0;i<16;i++)
        printf("%02X ",key[i]);
 
    printf("\n");
}
 
/* ---------------------------------------------------- */
 
int crypto_init(Epoc *e)
{
    e->td = mcrypt_module_open(
            MCRYPT_RIJNDAEL_128,
            NULL,
            MCRYPT_ECB,
            NULL);
 
    if(e->td==MCRYPT_FAILED)
        return -1;
 
    if(mcrypt_generic_init(e->td,e->key,16,NULL)<0)
        return -1;
 
    return 0;
}
 
/* ---------------------------------------------------- */
 
void decrypt_frame(Epoc *e,unsigned char *packet)
{
    memcpy(e->block,packet,16);
 
    mdecrypt_generic(e->td,e->block,16);
 
    memcpy(e->frame,e->block,16);
 
    memcpy(e->block,packet+16,16);
 
    mdecrypt_generic(e->td,e->block,16);
 
    memcpy(e->frame+16,e->block,16);
}
 
/* ---------------------------------------------------- */
 
hid_device *open_epoc(Epoc *e)
{
    hid_device *dev;
    wchar_t wserial[256];
    char serial[17];
 
struct hid_device_info *list, *cur;

/* Disable exclusive opens */

 dev=hid_open(VID,PID,NULL);
 
//list = hid_enumerate(VID, PID);

//for (cur = list; cur; cur = cur->next)
//{
 //   printf("path=%s\n", cur->path);
 //   printf("interface=%d\n", cur->interface_number);
 //   printf("usage_page=%04X usage=%04X\n",
 //          cur->usage_page,
 //          cur->usage);
 //   printf("\n");
//}
//printf("Before hid_open_path\n");
//dev = hid_open_path(cur->path);
//printf("After hid_open_path: %p\n", (void *)dev);

if (!dev) {
    printf("hid_open_path failed\n");
    return NULL;
}

printf("Before hid_get_serial_number_string\n");

if (hid_get_serial_number_string(dev, wserial, 256) == 0)
    printf("Got serial\n");
else
    printf("Failed to get serial\n");

printf("Before crypto_init\n");

if (crypto_init(e) != 0) {
    printf("crypto_init failed\n");
    hid_close(dev);
    return NULL;
}

printf("Returning device\n");
//hid_free_enumeration(list);
//if (!dev)
//{
//    printf("receiver not found\n");
//    return NULL;
//}
 
//    if(!dev)
 //       return NULL;
 
    memset(wserial,0,sizeof(wserial));
 
    if(hid_get_serial_number_string(dev,wserial,256)==0)
    {
        printf("Serial: ");
 
for (int i = 0; i < 16; i++)
{
    serial[i] = (char)(wserial[i] & 0xff);
}

serial[16] = 0;

printf("Serial: %s\n", serial);

for (int i = 0; i < 16; i++)
{
    printf("%2d : %02X '%c'\n",
           i,
           (unsigned char)serial[i],
           serial[i]);
}
 
        printf("\n");
 
        make_crypto_key(serial,e->key);
 
        print_key(e->key);
    }
    else
    {
        printf("Unable to read serial\n");
    }
 
    if(crypto_init(e)!=0)
    {
        printf("Crypto init failed\n");
       // hid_close(dev);
        return NULL;
    }
    
    
 
    return dev;
}
 
/* ===================== PART 2 CONTINUES ===================== */
/* ---------------------------------------------------- */
/* EEG display                                          */
/* ---------------------------------------------------- */

void show_frame(Epoc *e)
{
    unsigned char *f = e->frame;

    printf("\n");
    printf("Counter : %02X\n", f[0]);

    printf("AF3  %5d\n", get_level(f,AF3_MASK));
    printf("F7   %5d\n", get_level(f,F7_MASK));
    printf("F3   %5d\n", get_level(f,F3_MASK));
    printf("FC5  %5d\n", get_level(f,FC5_MASK));
    printf("T7   %5d\n", get_level(f,T7_MASK));
    printf("O1   %5d\n", get_level(f,O1_MASK));
    printf("P7   %5d\n", get_level(f,P7_MASK));

    printf("P8   %5d\n", get_level(f,P8_MASK));
    printf("O2   %5d\n", get_level(f,O2_MASK));
    printf("T8   %5d\n", get_level(f,T8_MASK));
    printf("FC6  %5d\n", get_level(f,FC6_MASK));
    printf("F4   %5d\n", get_level(f,F4_MASK));
    printf("F8   %5d\n", get_level(f,F8_MASK));
    printf("AF4  %5d\n", get_level(f,AF4_MASK));

    /*
     * Original Emokit gyro decode.
     */
    printf("Gyro X %4d\n", f[29] - 102);
    printf("Gyro Y %4d\n", f[30] - 104);
}

/* ---------------------------------------------------- */

void dump_encrypted(unsigned char *p)
{
    printf("\nRAW ENCRYPTED:\n");

    for(int i=0;i<32;i++)
    {
        printf("%02X ",p[i]);

        if((i&15)==15)
            printf("\n");
    }
}

/* ---------------------------------------------------- */

void dump_decrypted(unsigned char *p)
{
    printf("\nRAW DECRYPTED:\n");

    for(int i=0;i<32;i++)
    {
        printf("%02X ",p[i]);

        if((i&15)==15)
            printf("\n");
    }
}

/* ---------------------------------------------------- */

int main(void)
{
    Epoc epoc;

    memset(&epoc,0,sizeof(epoc));

    if(hid_init())
    {
        printf("hid_init failed\n");
        return 1;
    }
//hid_darwin_set_open_exclusive(0);

    hid_device *dev = open_epoc(&epoc);

    if(!dev)
    {
        printf("Unable to open headset\n");
        return 1;
    }
hid_set_nonblocking(dev, 0);

const wchar_t *err = hid_error(dev);

if (err)
    wprintf(L"hid_error: %ls\n", err);
else
    printf("hid_error: (none)\n");

    printf("Connected.\n");
    
    wchar_t str[256];

if (!hid_get_manufacturer_string(dev, str, 256))
    wprintf(L"Manufacturer: %ls\n", str);

if (!hid_get_product_string(dev, str, 256))
    wprintf(L"Product: %ls\n", str);

   while(1)
    {
        int n = hid_read_timeout(
                    dev,
                    epoc.raw,
                    sizeof(epoc.raw),
                    1000);
 //    printf("read = %d\n", n);
    fflush(stdout);

        if(n < 0)
        {
            printf("Read error\n");
            break;
        }
 
        if(n == 0)
            continue;
 
        if(n != 32)
        {
            printf("Unexpected packet size %d\n", n);
            continue;
        }
 
        dump_encrypted(epoc.raw);
 
        decrypt_frame(&epoc, epoc.raw);
 
        dump_decrypted(epoc.frame);
 
        show_frame(&epoc);
    }


    mcrypt_generic_deinit(epoc.td);
    mcrypt_module_close(epoc.td);

    hid_close(dev);
    hid_exit();

    return 0;
}