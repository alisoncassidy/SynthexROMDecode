//
//  main.c
//  SynthexROMDecode
//
//  Created by Alison Cassidy on 2/20/18.
//  Copyright © 2018 Alison Cassidy
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//


#include <errno.h>
#include <getopt.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma mark globals

char        *gProgName;

typedef enum status
{
    StatusFileNotFound  = -3,
    StatusSystemError   = -2,
    StatusAbort         = -1,
    StatusPassed        = 0
    
} status;

#pragma mark prototypes

int         main            (int, const char * []);
void        dumpBlock       (void *, ssize_t);
uint8_t     decodeData      (uint8_t);
uint16_t    decodeAddress   (uint16_t);


#pragma mark -

void dumpBlock(void *inBuf, ssize_t size)
{
    int32_t i, j;
    uint8_t *buf;
    char    lineBuf[256], *ptr;
    
    
    buf = (uint8_t *)inBuf;
    
    sprintf(lineBuf, "0000000: ");
    ptr = lineBuf + 9;
    
    for (i = 0; i < size; i++)
    {
        sprintf (ptr, "%02x", buf[i]);
        ptr += 2;
        if (i % 2)
            *(ptr++) = ' ';
        
        if ((i & 0x0f) == 0x0f)
        {
            *(ptr++) = ' ';
            for (j = 0x0f; j >= 0; j--)
                *(ptr++) = ((buf[i - j] > 0x1f) && (buf[i - j] < 0x7f)) ? (char)buf[i - j] : '.';
            *(ptr) = '\0';
            if (i < size - 1)
            {
                printf("%s\n", lineBuf);
                
                memset(lineBuf, 0, sizeof(lineBuf));
                sprintf(lineBuf, "%07x: ", i + 1);
                ptr = lineBuf + 9;
            }
        }
    }
    
    // Account for short lines
    
    if ((i & 0x0f) != 0x00)
    {
        for (j = 0; j < (0x0f - (i & 0x0f)); j++)
        {
            sprintf (ptr, "  ");
            ptr += 2;
            if (j % 2) *(ptr++) = ' ';
        }
        
        sprintf (ptr, "    ");
        ptr += 4;
        for (j = (i & 0x0f); j > 0; j--)
            *(ptr++) = ((buf[i - j] > 0x1f) && (buf[i - j] < 0x7f)) ? (char)buf[i - j] : '.';
    }
    
    printf ("%s\n", lineBuf);
}


uint16_t decodeAddress (uint16_t inWord)
{
    uint16_t    outWord = 0;
    
    // A15  A14  A13  A12  A11  A10  A09  A08  A07  A06  A05  A04  A03  A02  A01  A00       - 6502 CPU
    //  |    |    |    |    |    |    |    |    |    |    |    |    |    |    |    |
    //                     A11  A00  A01  A02  A10  A03  A04  A05  A09  A06  A08  A07       - 2532 EPROM (2 ^ 12  = 4096)

    // We just need to decode the LS 12-bits

    outWord = (((inWord & 0x0001) << 7) | ((inWord & 0x0002) << 7) | ((inWord & 0x0004) << 4) | ((inWord & 0x0008) << 6) |
               ((inWord & 0x0010) << 1) | ((inWord & 0x0020) >> 1) | ((inWord & 0x0040) >> 3) | ((inWord & 0x0080) << 3) |
               ((inWord & 0x0100) >> 6) | ((inWord & 0x0200) >> 8) | ((inWord & 0x0400) >> 10) | (inWord & 0x0800));
    
    return (outWord);
}


uint8_t decodeData (uint8_t inByte)
{
    uint8_t outByte = 0;
    
    // D7   D6   D5   D4   D3   D2   D1   D0    - 6502 CPU
    //  |    |    |    |    |    |    |    |
    // D5   D1   D4   D3   D2   D0   D6   D7    - 2532 EPROM

    outByte = (((inByte & 0x01) << 7) | ((inByte & 0x02) << 5) | ((inByte & 0x04) >> 2) | ((inByte & 0x08) >> 1) |
               ((inByte & 0x10) >> 1) | ((inByte & 0x20) >> 1) | ((inByte & 0x40) >> 5) | ((inByte & 0x80) >> 2));
    
    return (outByte);
}


int main(int argc, const char * argv[])
{
    FILE        *inFp = NULL, *outFp = NULL;
    ssize_t     bufSize, bytesRead;
    status      ourStatus = StatusPassed;
    uint8_t     *sourceBuf = NULL, *targetBuf = NULL, data;
    uint16_t    count, address;
    
    
    setlocale (LC_ALL,"");
    setvbuf(stdout, NULL, _IONBF, 0);                // Unbuffered stdout! Useful ..

    gProgName = ((strrchr((char *)argv[0], '/')) ? strrchr((char *)argv[0], '/') + 1 : (char *)argv[0]);
    
    printf ("%s - copyright (C) 2018 Alison Cassidy\n", gProgName);
    printf ("This program comes with ABSOLUTELY NO WARRANTY.\n");
    printf ("This is free software, and you are welcome to redistribute it under certain conditions.\n\n");
    
    printf ("Decoding ... %s\n", argv[1]);

    if (((argv[1]) == NULL) || (strlen(argv[1]) == 0))
    {
        fprintf (stderr, "Missing input file path - bailing\n");
        ourStatus = StatusFileNotFound;
        goto bail;
    }
    
    inFp = fopen(argv[1], "rb");
    if (inFp == NULL)
    {
        fprintf (stderr, "Error - EPROM file [%s] not found\n", argv[1]);
        fprintf (stderr, "Error - problems opening EPROM file [%s] for read: %d / %s\n", argv[1], errno, strerror(errno));
        ourStatus = StatusFileNotFound;
        goto bail;
    }
    
    fseek(inFp, 0L, SEEK_END);
    bufSize = ftell(inFp);
    sourceBuf = (uint8_t *) malloc(bufSize);
    targetBuf = (uint8_t *) malloc(bufSize);
    if ((sourceBuf == NULL) || (targetBuf == NULL))
    {
        fprintf (stderr, "Error - problems allocating memory for EPROM file: %d / %s\n", errno, strerror(errno));
        ourStatus = StatusFileNotFound;
        goto bail;
    }
    
    rewind (inFp);
    bytesRead = fread (sourceBuf, bufSize, 1, inFp);

    dumpBlock (sourceBuf, bufSize);                     // Spit the raw dump out
    printf("\n\n");
    
    // Decode the EPROM block
    
    for (count = 0; count < bufSize; count++)
    {
        // Here's the magic decoder ring
        
        address = decodeAddress(count);
        data    = decodeData(sourceBuf[count]);
        
        targetBuf[address] = data;
    }
    
    dumpBlock (targetBuf, bufSize);                     // Spit the decoded dump out

    
bail:
    
    if (inFp) fclose (inFp);
    if (outFp) fclose (outFp);
    return (ourStatus);
}
