#include "swav.h"

#include <stdio.h>

#include "util.h"

void ConvertWavToSwav(int argc, char **argv)
{
    if (argc < 3)
    {
        FATAL_ERROR("Insufficient arguments\n");
    }
    char *inputPath = argv[1];
    char *outputPath = argv[2];

    int wavEncodeType = WAV_UNSIGNED_PCM8;

    int fileSize;
    unsigned char *swav = ReadWholeFile(inputPath, &fileSize);
    if (memcmp(swav, "SWAV", 4) != 0)
    {
        FATAL_ERROR("Not a valid swav file.\n");
    }

    __uint16_t dataAddress = ReadU16_BE(swav, 12);
    if (memcmp(swav + dataAddress, "DATA", 4) != 0)
    {
        FATAL_ERROR("Missing DATA chunk.\n");
    }
    size_t offset = dataAddress + 8;

    //__uint8_t swavEncodeType = ReadU8(swav, offset);
    ////// starting from PMC8 swav
    ////// expand later
    offset += 1;
    //__uint8_t loop = ReadU8(swav, offset);
    offset += 1;
    __uint16_t samplingRate = ReadU16_BE(swav, offset);
    offset += 2;
    //__uint16_t clockTime = ReadU16_BE(swav, offset);
    offset += 2;
    //__uint16_t loopAddress = ReadU16_BE(swav, offset);
    offset += 2;
    //__uint16_t loopLength = ReadU32_BE(swav, offset);
    offset += 4;
    // rest is data stream

    // Write WAV Header
    FILE *outFile = fopen(outputPath, "wb");
    if (outFile == NULL)
    {
        FATAL_ERROR("Failed to open \"%s\" for writing.\n", outputPath);
    }

    unsigned char WAVHeader[0x2c] =
    {
        'R',  'I',  'F',  'F',  0x00, 0x00, 0x00, 0x00,  'W',  'A',  'V',  'E',  'f',  'm',  't',  ' ',
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 'd',  'a',  't',  'a',   0x00, 0x00, 0x00, 0x00
    };

    __uint32_t dataSize = 0;
    int bitPerSample = 0;
    switch (wavEncodeType)
    {
        case WAV_UNSIGNED_PCM8:
            dataSize = fileSize - 0x24;
            bitPerSample = 1;
            WriteU16(WAVHeader, 0x14, 0x0001);
            break;
        case WAV_SIGNED_PCM16:
            dataSize = (fileSize - 0x24) * 2;
            bitPerSample = 2;
            WriteU16(WAVHeader, 0x14, 0x0001);
            break;
        case WAV_IMA_ADPCM:
            WriteU16(WAVHeader, 0x14, 0x0103);
            
            FATAL_ERROR("unsupported encode type\n");
            break;
        default:
            FATAL_ERROR("unsupported encode type\n");
            break;
    }
    WriteU32(WAVHeader, 0x04, dataSize + 0x24);
    WriteU32(WAVHeader, 0x18, samplingRate);
    WriteU32(WAVHeader, 0x1C, samplingRate * bitPerSample);
    WriteU16(WAVHeader, 0x20, bitPerSample);
    WriteU16(WAVHeader, 0x22, bitPerSample * 8);
    WriteU32(WAVHeader, 0x28, dataSize);

    fwrite(WAVHeader, 1, 0x2C, outFile);

    // write data stream
    switch (wavEncodeType)
    {
        case WAV_UNSIGNED_PCM8:
            unsigned char unsigned8Bit[1] = {0};
            while (offset < fileSize)
            {
                unsigned8Bit[0] = swav[offset] ^ 0x80;
                fwrite(unsigned8Bit, 1, 1, outFile);
                offset += 1;
            }
            break;
        case WAV_SIGNED_PCM16:
            unsigned char signed16Bit[2] = {0, 0};
            while (offset < fileSize)
            {
                if (swav[offset] & 0x80)
                {
                    signed16Bit[0] = 1;
                    signed16Bit[1] = swav[offset];
                    offset += 1;
                    fwrite(signed16Bit, 1, 2, outFile);
                }
                else
                {
                    signed16Bit[0] = 0;
                    signed16Bit[1] = swav[offset];
                    offset += 1;
                    fwrite(signed16Bit, 1, 2, outFile);
                }
            }
            break;
        case WAV_IMA_ADPCM:
        default:
            FATAL_ERROR("unsupported encode type\n");
            break;
    }

    free(swav);
    fclose(outFile);
}

void ConvertSwavToWav(int argc, char **argv)
{
    if (argc < 3)
    {
        FATAL_ERROR("Insufficient arguments\n");
    }
    char *inputPath = argv[1];
    //char *outputPath = argv[2];

    int swavEncodeType = SWAV_SIGNED_PCM8;

    int fileSize;
    unsigned char *wav = ReadWholeFile(inputPath, &fileSize);
    if (memcmp(wav, "RIFF", 4) != 0)
    {
        FATAL_ERROR("Not a valid wav file.\n");
    }
    if (memcmp(wav + 8, "WAVE", 4) != 0)
    {
        FATAL_ERROR("Not a valid data file.\n");
    }
    if (memcmp(wav + 12, "fmt ", 4) != 0)
    {
        FATAL_ERROR("Missing format chunk.\n");
    }

    // Programs must expect (and ignore) any unknown chunks
    // encountered, as with all RIFF forms. However, <fmt-ck> must always occur before
    // <wave-data>, and both of these chunks are mandatory in a WAVE file

    // fix above to jump to WAVE, fmt , and data chunks

    size_t offset = 0x14;
    //__uint32_t chunkSize = ReadU32_BE(wav, offset);
    offset += 4;
    __uint16_t formatCode = ReadU16_BE(wav, offset);
    offset += 2;
    __uint16_t numChannels = ReadU16_BE(wav, offset);
    offset += 2;
    if (numChannels > 1)
    {
        FATAL_ERROR("Only mono files are supported.\n");
    }
    //__uint32_t samplingRate = ReadU32_BE(wav, offset);
    offset += 4;
    //__uint32_t bytePerSecond = ReadU32_BE(wav, offset);
    offset += 4;
    //__uint16_t blockAlign = ReadU16_BE(wav, offset);
    offset += 2;
    __uint16_t bitsPerSample = ReadU16_BE(wav, offset);
    offset += 2;

    char wavEncodeType = -1;
    switch (formatCode)
    {
        case WAVE_FORMAT_PCM:
            switch (bitsPerSample)
            {
                case 0x08:
                    wavEncodeType = WAV_UNSIGNED_PCM8;
                    break;
                case 0x10:
                    wavEncodeType = WAV_SIGNED_PCM16;
                    break;
                default:
                    FATAL_ERROR("unsupported bits per sample\n");
                    break;
            }
            break;
        case WAVE_FORMAT_ADPCM:
            wavEncodeType = WAV_IMA_ADPCM;
            break;
        default:
            FATAL_ERROR("unsupported encode type\n");
            break;
    }
    if (swavEncodeType == -1)
    {
        swavEncodeType = wavEncodeType;
    }

    if (memcmp(wav + offset, "data", 4) != 0)
    {
        FATAL_ERROR("Missing data chunk.\n");
    }
    offset += 4;
    //chunkSize = ReadU32_BE(wav, offset);
    offset += 4;

    //size_t dataOffset = 0;
    switch (swavEncodeType)
    {
        case SWAV_SIGNED_PCM8:
            break;
        case SWAV_SIGNED_PCM16:
            break;
        case SWAV_IMA_ADPCM:
            break;
        default:
            FATAL_ERROR("unsupported encode type\n");
            break;
    }

    free(wav);
}
