#include "sseq.h"

#include <stdio.h>

#include "util.h"

void ReadSseq(char *inputPath, char *outputPath)
{
    printf("%s\t%s\n", inputPath, outputPath);

    int fileSize;
    unsigned char *sseq = ReadWholeFile(inputPath, &fileSize);
    if (memcmp(sseq, "SSEQ", 4) != 0)
    {
        FATAL_ERROR("Not a valid sseq file.\n");
    }

    __uint16_t dataAddress = ReadU16_LE(sseq, 12);
    if (memcmp(sseq + dataAddress, "DATA", 4) != 0)
    {
        FATAL_ERROR("Missing DATA chunk.\n");
    }
    size_t offset = dataAddress + 12;

    __uint16_t numTracks = 1;
    if (ReadU8(sseq, offset) == SSEQ_COMMAND_NUM_TRACKS)
    {
        offset += 1;
        numTracks = ReadU16_LE(sseq, offset);
        offset += 2;
    }
    __uint32_t trackAddresses[numTracks - 1];
    memset(trackAddresses, 0, sizeof(trackAddresses));
    
    for (int t = 0; t < numTracks - 1; t++)
    {
        if (ReadU8(sseq, offset) != SSEQ_COMMAND_TRACK_ADDRESS)
        {
            FATAL_ERROR("Missing track %d address.\n", t);
        }
        offset += 1;
        __uint8_t trackNumber = ReadU8(sseq, offset);
        if (trackAddresses[trackNumber - 1] != 0)
        {
            FATAL_ERROR("Track %d assigned twice.\n", trackNumber);
        }
        offset += 1;
        trackAddresses[trackNumber - 1] = ReadU24_LE(sseq, offset);
        offset += 3;
    }

    for (int t = 0; t < numTracks; t++)
    {
        if (t > 0)
        {
            offset = dataAddress + trackAddresses[t - 1];
        }

        while (offset < fileSize)
        {
            __uint8_t command = ReadU8(sseq, offset);
            offset += 1;
            if (command < 0x80) // note command
            {
                __uint8_t velocity = ReadU8(sseq, offset);
                offset += 1;
                __uint32_t duration = ReadVariableLength(sseq, &offset);
                printf("NOTE %x\t%x\t%x\n", command, velocity, duration);
            }
            else
            {
                switch (ReadU8(sseq, offset - 1))
                {
                    case SSEQ_COMMAND_WAIT:
                        printf("WAIT %x\n", ReadVariableLength(sseq, &offset));
                        break;
                    case SSEQ_COMMAND_BANK:
                        printf("BANK %x\n", ReadU8(sseq, offset));
                        offset += 1;
                        break;
                    case SSEQ_COMMAND_PAN:
                        printf("PAN %x\n", ReadU8(sseq, offset));
                        offset += 1;
                        break;
                    case SSEQ_COMMAND_VOLUME:
                        printf("VOLUME %x\n", ReadU8(sseq, offset));
                        offset += 1;
                        break;
                    case SSEQ_COMMAND_MONO_NOTE:
                        printf("MONO %x\n", ReadU8(sseq, offset));
                        offset += 1;
                        break;
                    case SSEQ_COMMAND_TEMPO:
                        printf("TEMPO %x\n", ReadU16_LE(sseq, offset));
                        offset += 2;
                        break;
                    case SSEQ_COMMAND_END:
                        printf("END TRACK\n");
                        offset = fileSize;
                        break;
                    default:
                        FATAL_ERROR("Unknown command %x at address %lx.\n", command, offset);
                        break;
                }
            }
        }
    }
}