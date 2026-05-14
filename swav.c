#include "swav.h"

#include <stdio.h>

#include "util.h"

#define WAVE_CODEC_PCM       0x0001
#define WAVE_CODEC_IMA_ADPCM 0x0011

struct WavChunk_RIFF {
    uint32_t chunkID;
    uint32_t fileSize;
    uint32_t formType;
};

struct WavChunk_fmt {
    uint32_t chunkID;
    uint32_t size;              // Does not include chunkID, size, or any padding
    uint16_t wFormatTag;        // Format category
    uint16_t wChannels;         // Number of channels
    uint32_t dwSamplesPerSec;   // Sampling rate
    uint32_t dwAvgBytesPerSec;  // For buffer estimation
    uint16_t wBlockAlign;       // Data block size
    uint16_t wBitsPerSample;    // Sample size
};

struct WavChunk_data {
    uint32_t chunkID;
    uint32_t size;              // Does not include chunkID, size, or any padding
    //uint8_t *audio;
};

struct Wav_SampleLoop {
    uint32_t id;
    uint32_t type;
    uint32_t start;
    uint32_t end;
    uint32_t fraction;
    uint32_t repititions;
};

struct WavChunk_smpl {
    uint32_t chunkID;
    uint32_t size;              // Does not include chunkID, size, or any padding
    uint32_t manufacturer;
    uint32_t product;
    uint32_t samplePeriod;
    uint32_t MIDI_unityNote;
    uint32_t MIDI_pitchFraction;
    uint32_t SMPTE_format;
    uint32_t SMPTE_offset;
    uint32_t numLoops;
    uint32_t sampleData;
    //struct Wav_SampleLoop *sampleLoop;
};

struct SwavChunk_SWAV {
    uint32_t chunkID;
    uint16_t magic1; // endianness?
    uint16_t magic2;
    uint32_t fileSize;
    uint16_t size;
    uint16_t magic3; // version?
};

struct SwavChunk_DATA {
    uint32_t chunkID;
    uint32_t size;
    uint8_t encodeType;
    uint8_t loop;
    uint16_t samplingRate;
    uint16_t clockTime;
    uint16_t loopStart;
    uint32_t loopSize;
    //uint8_t *audio;
};

enum SWAV_ENCODE {
    SWAV_SIGNED_PCM8 = 0,
    SWAV_SIGNED_PCM16,
    SWAV_IMA_ADPCM,
};

static const int IMA_INDEX_TABLE[16] = {
  -1, -1, -1, -1, 2, 4, 6, 8,
  -1, -1, -1, -1, 2, 4, 6, 8
}; 

static const int IMA_STEP_TABLE[89] = { 
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 
  19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 
  50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 
  130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
  337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
  876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 
  2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
  5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 
  15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 
};

struct IMA_Prediction {
    int value;
    int index;
};

static int IMA_ADPCM_Encode(short initialValue, struct IMA_Prediction *prediction)
{
    int stepSize = IMA_STEP_TABLE[prediction->index];
    int difference = initialValue - prediction->value;
    int newSample = 0;
    if (difference < 0) {
        newSample = 8;
        difference = -difference;
    }

    int mask = 4;
    int diff = stepSize >> 3;
    for (int i = 0; i < 3; i++)
    {
        if (difference >= stepSize)
        {
            newSample |= mask;
            difference -= stepSize;
            diff += stepSize;
        }
        stepSize >>= 1;
        mask >>= 1;
    }
    if (newSample & 8) diff = -diff;
    prediction->value += diff;

    if (prediction->value > 32767) prediction->value = 32767;
    else if (prediction->value < -32767) prediction->value = -32767;

    prediction->index += IMA_INDEX_TABLE[newSample];
    if (prediction->index < 0) prediction->index = 0;
    else if (prediction->index > 88) prediction->index = 88;

    return newSample;
}

static int IMA_ADPCM_Decode(short initialValue, struct IMA_Prediction *prediction)
{
    int stepSize = IMA_STEP_TABLE[prediction->index];

    int difference = stepSize >> 3; 
    if (initialValue & 4) difference += stepSize;
    if (initialValue & 2) difference += stepSize >> 1;
    if (initialValue & 1) difference += stepSize >> 2;
    if (initialValue & 8) difference = -difference;
    prediction->value += difference;

    if (prediction->value > 32767) prediction->value = 32767;
    else if (prediction->value < -32768) prediction->value = -32768;

    prediction->index += IMA_INDEX_TABLE[initialValue];
    if (prediction->index < 0) prediction->index = 0;
    else if (prediction->index > 88) prediction->index = 88;

    return prediction->value;
}

static int InitialStepIndex(short initialValue, short oringinalSample)
{
    int initialStepIndex = 0;
    int minDiff = 0x10000;
    struct IMA_Prediction prediction;
    for (int i = 0; i < 89; i++)
    {
        prediction.value = initialValue;
        prediction.index = i;
        int newSample = IMA_ADPCM_Encode(oringinalSample, &prediction);
        prediction.index = i;
        int diff = IMA_ADPCM_Decode(newSample, &prediction) - oringinalSample;
        if (diff < 0) diff = -diff;
        if (diff < minDiff)
        {
            minDiff = diff;
            initialStepIndex = i;
        }
    }
    return initialStepIndex;
}

// TODO: works for some but not most
//      find an actual mathmatical solution
static short InitialValue(int nibble, int initialStepIndex, short audioOut)
{
    for (uint32_t i = 0; i < 0x10000; i++)
    {
        struct IMA_Prediction prediction;

        prediction.value = 0;
        prediction.index = initialStepIndex;
        uint8_t newValue = IMA_ADPCM_Encode(i, &prediction);
        uint8_t newInitStep = InitialStepIndex(0, i);

        if ((newValue == nibble) && (newInitStep == initialStepIndex)) return i;
    }
    printf("Error: no valid first value for %x\t%x\n", initialStepIndex, nibble);
    return audioOut;
}

uint8_t loopShift[] = {2, 1, 3}; // TODO: replace this wih block size?

// TODO: check loop start position < loop end
// TODO: audio padding when converting
// TODO: max samples (PCM8:, PCM16:, ADPCM:0x1000)
void ConvertWavToSwav(int argc, char **argv)
{
    if (argc < 3) FATAL_ERROR("Insufficient arguments\n");
    char *inputPath = argv[1];
    char *outputPath = argv[2];

    bool smplFound = false;

    struct SwavChunk_DATA *sData = malloc(sizeof(struct SwavChunk_DATA));
    sData->encodeType = SWAV_IMA_ADPCM;
    sData->loop = false;
    sData->loopStart = 0;

    // optional args
    for (int i = 3; i < argc; i++)
    {
        if (strcmp(argv[i], "--pcm8") == 0)
        {
            sData->encodeType = SWAV_SIGNED_PCM8;
        }
        else if (strcmp(argv[i], "--pcm16") == 0)
        {
            sData->encodeType = SWAV_SIGNED_PCM16;
        }
        else if (strcmp(argv[i], "--adpcm") == 0)
        {
            sData->encodeType = SWAV_IMA_ADPCM;
        }
        else if (strcmp(argv[i], "--loop") == 0)
        {
            if (i + 2 >= argc)
            {
                FATAL_ERROR("Usage: \"-loop [loopStart] [loopSize]\"\nValues are in # of samples\n");
            }
            if (!ParseNumber(argv[++i], NULL, 10, (int*)&sData->loopStart)) FATAL_ERROR("Failed to parse loop start address.\n");
            if (!ParseNumber(argv[++i], NULL, 10, (int*)&sData->loopSize)) FATAL_ERROR("Failed to parse loop size.\n");
            sData->loop = true;
            smplFound = true;
        }
        else
        {
            FATAL_ERROR("Unrecognized argument: \"%s\"\n", argv[i]);
        }
    }

    int fileSize;
    uint8_t *wav = ReadWholeFile(inputPath, &fileSize);
    struct WavChunk_RIFF *riff = (struct WavChunk_RIFF*)wav;
    if (memcmp(&riff->chunkID, "RIFF", 4) != 0) FATAL_ERROR("%s is not a RIFF file.\n", inputPath);
    if (memcmp(&riff->formType, "WAVE", 4) != 0) FATAL_ERROR("%s does not have WAVE form type.\n", inputPath);

    // parse wav file
    struct WavChunk_fmt *fmt = NULL;
    struct WavChunk_data *wData = NULL;
    struct WavChunk_smpl *smpl = NULL;
    uint8_t *wAudio;
    uint32_t numSamples = 0;
    size_t offset = sizeof(struct WavChunk_RIFF);
    while (offset + 0x08 < fileSize)
    {
        uint32_t chunkSize = ReadU32_BE(wav, offset + 0x04);
        if (fileSize < offset + 0x08 + chunkSize) FATAL_ERROR("Error reading chunk size\n");
        if (memcmp(wav + offset, "fmt ", 4) == 0)
        {
            fmt = (struct WavChunk_fmt*)(wav + offset); // organize data
            // convert used variables to code endianness
            fmt->wFormatTag = ReadU16_BE((uint8_t*)&fmt->wFormatTag, 0);
            if ((fmt->wFormatTag != WAVE_CODEC_PCM) && (fmt->wFormatTag != WAVE_CODEC_IMA_ADPCM)) FATAL_ERROR("Only PCM and IMA-ADPCM files are supported\n");
            fmt->wChannels = ReadU16_BE((uint8_t*)&fmt->wChannels, 0);
            if (fmt->wChannels != 1) FATAL_ERROR("Only mono files supported\n");
            sData->samplingRate = ReadU32_BE((uint8_t*)&fmt->dwSamplesPerSec, 0);
            sData->clockTime = 16756991 / sData->samplingRate;
            fmt->wBlockAlign = ReadU16_BE((uint8_t*)&fmt->wBlockAlign, 0);
            fmt->wBitsPerSample = ReadU16_BE((uint8_t*)&fmt->wBitsPerSample, 0);
        }
        else if (memcmp(wav + offset, "data", 4) == 0)
        {
            if (fmt == NULL) FATAL_ERROR("File %s missing fmt chunk\n", inputPath); // fmt must be before data
            wData = malloc(sizeof(struct WavChunk_data));
            wData->size = chunkSize;
            wAudio = wav + offset + 0x08;

            numSamples = chunkSize / fmt->wBlockAlign;
            if (fmt->wFormatTag == WAVE_CODEC_IMA_ADPCM) numSamples = (chunkSize - 4 * numSamples) * 2;
        }
        else if ((memcmp(wav + offset, "smpl", 4) == 0) && !smplFound) // skip if loop values passed as args (or duplicate smpl chunk I guess)
        {
            smplFound = true;

            smpl = (struct WavChunk_smpl*)(wav + offset);
            sData->loop = 0 < ReadU32_BE((uint8_t*)&smpl->numLoops, 0);
            if (sData->loop)
            {
                struct Wav_SampleLoop *sampleLoop = (struct Wav_SampleLoop*)(wav + offset + sizeof(struct WavChunk_smpl));
                sData->loopStart = ReadU32_BE((uint8_t*)&sampleLoop[0].start, 0);
                sData->loopSize = ReadU32_BE((uint8_t*)&sampleLoop[0].end, 0) - sData->loopStart + 1;
            }
        }
        /*else if (memcmp(wav + offset, "SWAV", 4) == 0) // cheat here
        {
            sData->clockTime = ReadU32_BE(wav, offset + 0x08);
        }*/
        offset += 0x08 + chunkSize + (chunkSize % 2); // chunk ID, size, and padding are not included in wav chunk size
    }
    if (fmt == NULL) FATAL_ERROR("File %s missing fmt chunk\n", inputPath);
    if (wData == NULL) FATAL_ERROR("File %s missing fmt chunk\n", inputPath);

    // Write WAV Header
    FILE *outFile = fopen(outputPath, "wb");
    if (outFile == NULL) FATAL_ERROR("Failed to open \"%s\" for writing.\n", outputPath);

    uint32_t dataSize = 0;
    switch (sData->encodeType)
    {
        case SWAV_SIGNED_PCM8:
            dataSize = numSamples;
            break;
        case SWAV_SIGNED_PCM16:
            dataSize = numSamples * 2;
            break;
        case SWAV_IMA_ADPCM:
            dataSize = numSamples / 2 + 4;
            break;
    }
    if (sData->loop)
    {
        //if (sData->loopStart > numSamples) FATAL_ERROR("Loop start position must be <= numSamples\n");
        //if (sData->loopSize > numSamples) FATAL_ERROR("Loop size must be <= numSamples\n");
    }
    else
    {
        sData->loopSize = numSamples;
    }

    // Write SWAV header
    struct SwavChunk_SWAV *swavChunk = malloc(sizeof(struct SwavChunk_SWAV));
    memcpy(&swavChunk->chunkID, "SWAV", 4);
    WriteU16_BE((uint8_t*)&swavChunk->magic1, 0, 0xFEFF);
    WriteU16_BE((uint8_t*)&swavChunk->magic2, 0, 0x0100);
    WriteU32_BE((uint8_t*)&swavChunk->fileSize, 0, dataSize + 0x24);
    WriteU16_BE((uint8_t*)&swavChunk->size, 0, sizeof(struct SwavChunk_SWAV));
    WriteU16_BE((uint8_t*)&swavChunk->magic3, 0, 0x0001);

    fwrite(swavChunk, 1, sizeof(struct SwavChunk_SWAV), outFile);
    free(swavChunk);

    // Write DATA Header
    memcpy(&sData->chunkID, "DATA", 4);
    WriteU32_BE((uint8_t*)&sData->size, 0, dataSize + 0x14);
    // Check for endianness
    WriteU16_BE((uint8_t*)&sData->samplingRate, 0, sData->samplingRate);
    WriteU16_BE((uint8_t*)&sData->clockTime, 0, sData->clockTime);
    WriteU16_BE((uint8_t*)&sData->loopStart, 0, (sData->loopStart >> loopShift[sData->encodeType]) + (sData->encodeType == SWAV_IMA_ADPCM));
    WriteU32_BE((uint8_t*)&sData->loopSize, 0, sData->loopSize >> loopShift[sData->encodeType]);

    fwrite(sData, 1, 0x14, outFile); // Will write audio stream separately

    // write data stream
    // TODO: convert IMA-ADPCM <-> PCM
    if (fmt->wFormatTag == WAVE_CODEC_IMA_ADPCM)
    {
        if (sData->encodeType == SWAV_IMA_ADPCM)
        {
            fwrite(wAudio, 1, wData->size, outFile);
        }
        else
        {
            FATAL_ERROR("TODO\n");
        }
    }
    else
    {
        uint8_t *inAudio = wAudio;
        uint8_t *wAduioEnd = wAudio + wData->size;

        unsigned char *swavBits = malloc(sData->encodeType + 1);
        while (inAudio < wAduioEnd)
        {
            switch (sData->encodeType)
            {
                case SWAV_SIGNED_PCM8:
                    *swavBits = inAudio[fmt->wBlockAlign - 1];
                    if (fmt->wBitsPerSample <= 8) // WAV is unsigned at 8 bits or less
                    {
                        *swavBits ^= 0x80; // TODO: make this more robust for bitsize < 8
                    }
                    fwrite(swavBits, 1, 1, outFile);
                    inAudio += fmt->wBlockAlign;
                    break;
                case SWAV_SIGNED_PCM16:
                    swavBits[0] = inAudio[fmt->wBlockAlign - 2];
                    swavBits[1] = inAudio[fmt->wBlockAlign - 1];
                    if (fmt->wBitsPerSample <= 8) // WAV is unsigned at 8 bits or less
                    {
                        *swavBits ^= 0x80; // TODO: make this more robust for bitsize < 8
                    }
                    fwrite(swavBits, 1, 2, outFile);
                    inAudio += fmt->wBlockAlign;
                    break;
                case SWAV_IMA_ADPCM:
                    if ((fmt->wBlockAlign != 2) || (fmt->wBitsPerSample != 16)) FATAL_ERROR("TODO\n");
                    struct IMA_Prediction prediction;
                    prediction.value = 0;
                    prediction.index = InitialStepIndex(0, ReadU16_BE(inAudio, 0));

                    uint8_t *audioOut = malloc(dataSize);
                    audioOut[0] = 0x00;
                    audioOut[1] = 0x00;
                    audioOut[2] = prediction.index;
                    audioOut[3] = 0x00;
                    int i = 8;
                    while (inAudio < wAduioEnd)
                    {
                        uint8_t audioSample = IMA_ADPCM_Encode(ReadU16_BE(inAudio, 0), &prediction);
                        /*if (i/2 > 0x108 && i/2 < 0x118)
                        {
                            printf("%d\t%x\t%d\t%x\t%d\t%d\n", i, ReadU16_BE(inAudio, 0), (short)ReadU16_BE(inAudio, 0), audioSample, prediction.value, prediction.index);
                        }*/
                        inAudio += 2;
                        if (i % 2)
                        {
                            audioOut[i++ / 2] |= audioSample << 4;
                        }
                        else
                        {
                            audioOut[i++ / 2] = audioSample;
                        }
                    }
                    fwrite(audioOut, 1, dataSize, outFile);
                    free(audioOut);
                    break;
            }
        }
        free(swavBits);
    }

    free(wav);
    free(wData);
    free(sData);
    fclose(outFile);
}

// TODO: test with PCM16 swav
// TODO: allow output other than pcm16
void ConvertSwavToWav(int argc, char **argv)
{
    if (argc < 3)
    {
        FATAL_ERROR("Insufficient arguments\n");
    }
    char *inputPath = argv[1];
    char *outputPath = argv[2];

    int fileSize;
    uint8_t *swav = ReadWholeFile(inputPath, &fileSize);
    if (memcmp(swav, "SWAV", 4) != 0)
    {
        FATAL_ERROR("Not a valid swav file.\n");
    }

    // read swav file
    size_t offset = ReadU16_BE(swav, 0x0C);
    bool DATAFound = false;
    // DATA info
    enum SWAV_ENCODE swavEncodeType;
    bool loop;
    uint16_t samplingRate;
    uint16_t clockTime;
    uint16_t loopStart;
    uint16_t loopLength;
    uint32_t audioAddress;
    uint32_t audioSize;
    while (offset < fileSize)
    {
        uint32_t chunkSize = ReadU32_BE(swav, offset + 0x04);
        if ((fileSize < offset + chunkSize) || (chunkSize < 8))
        {
            FATAL_ERROR("Error reading chunk size\n");
        }
        if (memcmp(swav + offset, "DATA", 4) == 0)
        {
            DATAFound = true;

            swavEncodeType = ReadU8(swav, offset + 0x08);
            if (swavEncodeType > SWAV_IMA_ADPCM)
            {
                FATAL_ERROR("Unsupported encode type\n");
            }
            loop = ReadU8(swav, offset + 0x09);
            samplingRate = ReadU16_BE(swav, offset + 0x0A);
            clockTime = ReadU16_BE(swav, offset + 0x0C);
            loopStart = (ReadU16_BE(swav, offset + 0x0E) - (swavEncodeType == SWAV_IMA_ADPCM)) << loopShift[swavEncodeType];
            loopLength = ReadU32_BE(swav, offset + 0x10) << loopShift[swavEncodeType];
            audioAddress = offset + 0x14;
            audioSize = chunkSize - 0x14;

            offset = fileSize; // don't need to read any other chunks
        }
        offset += chunkSize;
    }
    if (DATAFound == false)
    {
        FATAL_ERROR("File %s missing DATA chunk\n", inputPath);
    }

    // Write WAV Header
    FILE *outFile = fopen(outputPath, "wb");
    if (outFile == NULL)
    {
        FATAL_ERROR("Failed to open \"%s\" for writing.\n", outputPath);
    }

    uint8_t WAVHeader[] =
    {
        'R',  'I',  'F',  'F',  0x00, 0x00, 0x00, 0x00,  'W',  'A',  'V',  'E',  'f',  'm',  't',  ' ',
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,  'd',  'a',  't',  'a',  0x00, 0x00, 0x00, 0x00
    };

    uint16_t blockAlign;
    uint16_t bitPerSample;
    if (swavEncodeType == SWAV_IMA_ADPCM)
    {
        /*WriteU16_BE(WAVHeader, 0x14, WAVE_CODEC_IMA_ADPCM);
        blockAlign = audioSize;
        bitPerSample = 4;*/
        WriteU16_BE(WAVHeader, 0x14, WAVE_CODEC_PCM);
        blockAlign = 2;
        bitPerSample = 16;
        audioSize = (audioSize - 4) * 4;
    }
    else
    {
        WriteU16_BE(WAVHeader, 0x14, WAVE_CODEC_PCM);
        blockAlign = swavEncodeType + 1;
        bitPerSample = blockAlign << 3;
    }
    bool clockBroken = clockTime != 16756991 / samplingRate;

    WriteU32_BE(WAVHeader, 0x04, sizeof(WAVHeader) + audioSize + (audioSize % 2) + loop * 0x44 + clockBroken * 0x0C - 8); // file size - 8
    WriteU32_BE(WAVHeader, 0x18, samplingRate);
    WriteU32_BE(WAVHeader, 0x1C, samplingRate * blockAlign); // bytes per second
    WriteU16_BE(WAVHeader, 0x20, blockAlign);
    WriteU16_BE(WAVHeader, 0x22, bitPerSample);
    WriteU32_BE(WAVHeader, 0x28, audioSize);

    fwrite(WAVHeader, 1, sizeof(WAVHeader), outFile);

    // write data stream
    if (swavEncodeType == SWAV_SIGNED_PCM8)
    {
        uint32_t audioEnd = audioAddress + audioSize;
        offset = audioAddress;
        while (offset < audioEnd)
        {
            swav[offset] ^= 0x80; // WAV is unsigned at 8 bits or less
            fwrite(swav + offset, 1, 1, outFile);
            offset += 1;
        }
    }
    else
    {
        if (swavEncodeType == SWAV_IMA_ADPCM)
        {
            uint32_t audioEnd = audioAddress + audioSize / 4 + 4;
            offset = audioAddress;

            struct IMA_Prediction prediction;
            prediction.value = ReadU16_BE(swav, offset);
            prediction.index = swav[offset + 2]; // TODO: clamp these
            offset += 4;
            short *audioOut = malloc(audioSize);
            int i = 0;
            while (offset < audioEnd)
            {
                char bottomNibble = swav[offset] & 0x0F;
                char topNibble = (swav[offset++] >> 4) & 0x0F;
                audioOut[i++] = IMA_ADPCM_Decode(bottomNibble, &prediction);
                audioOut[i++] = IMA_ADPCM_Decode(topNibble, &prediction);
            }
            audioOut[0] = InitialValue(swav[audioAddress + 4] & 0x0F, swav[audioAddress + 2], audioOut[0]);
            fwrite(audioOut, 1, audioSize, outFile);
            free(audioOut);
        }
        else
        {
            fwrite(swav + audioAddress, 1, audioSize, outFile);
        }
    }
    if (audioSize % 2)
    {
        uint8_t pad = 0;
        fwrite(&pad, 1, 1, outFile);
    }

    if (loop)
    {
        struct WavChunk_smpl *smpl = calloc(1, sizeof(struct WavChunk_smpl));
        if (smpl == NULL) FATAL_ERROR("Failed to store smpl chunk\n");
        memcpy(&smpl->chunkID, "smpl", 4);
        WriteU32_BE((uint8_t*)&smpl->size, 0, sizeof(struct WavChunk_smpl) + sizeof(struct Wav_SampleLoop) - 0x08);
        WriteU32_BE((uint8_t*)&smpl->numLoops, 0, 1);
        fwrite(smpl, 1, sizeof(struct WavChunk_smpl), outFile);
        free(smpl);

        struct Wav_SampleLoop *sampleLoop = calloc(1, sizeof(struct Wav_SampleLoop));
        if (sampleLoop == NULL) FATAL_ERROR("Failed to store sample loop\n");
        WriteU32_BE((uint8_t*)&sampleLoop->start, 0, loopStart);
        WriteU32_BE((uint8_t*)&sampleLoop->end, 0, loopStart + loopLength - 1);
        fwrite(sampleLoop, 1, sizeof(struct Wav_SampleLoop), outFile);
        free(sampleLoop);
    }

    /*if (clockBroken) // this is cheating but I don't care
    {
        uint8_t swavHeader[] = 
        {
            'S',  'W',  'A',  'V',  0x04, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00
        };
        WriteU32_BE(swavHeader, 0x08, clockTime); // hide in otherwise nonsense chunk
        // only necessary for matching original swav
        fwrite(swavHeader, 1, 0x0C, outFile);
    }*/

    free(swav);
    fclose(outFile);
}
