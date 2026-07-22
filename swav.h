#ifndef SWAV_H
#define SWAV_H

#include <stdint.h>
#include <stdbool.h>

enum SWAV_ENCODE: uint8_t
{
    SWAV_SIGNED_PCM8 = 0,
    SWAV_SIGNED_PCM16,
    SWAV_IMA_ADPCM,
};

struct WavChunk_RIFF
{
    uint32_t chunkID;
    uint32_t fileSize;
    uint32_t formType;
};

uint8_t *WavToSwav(uint8_t *wav, uint32_t wavSize, uint32_t *swavSize, uint8_t encodeType);
uint8_t *SwavToWav(uint8_t *swav, uint32_t swavSize, uint32_t *wavSize, bool pcm16);
void ConvertWavToSwav(int argc, char **argv);
void ConvertSwavToWav(int argc, char **argv);

#endif //SWAV_H