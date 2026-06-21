#include "swav.h"

#include <stdio.h>

#include "util.h"

#define WAVE_CODEC_PCM       0x0001
#define WAVE_CODEC_IMA_ADPCM 0x0011

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
    else if (prediction->value < -32767) prediction->value = -32767; // intentional clamp error

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
        int diff = IMA_ADPCM_Decode(newSample, &prediction);
        if (diff == -32768) diff = -32767; // intentional clamp error
        diff -= oringinalSample;
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
    //printf("Error: no valid first value for %x\t%x\n", initialStepIndex, nibble);
    return audioOut;
}

uint8_t loopShift[] = {2, 1, 3}; // TODO: replace this wih block size?

uint8_t swavTypeSamplesPer64bit[] = {4, 2, 8};

uint8_t *WavToSwav(uint8_t *wav, uint32_t wavSize, uint32_t *swavSize, uint8_t encodeType)
{
    // parse wav file
    struct WavChunk_fmt *fmt = NULL;
    struct WavChunk_data *wData = NULL;
    struct WavChunk_smpl *smpl = NULL;
    struct Wav_SampleLoop *sampleLoop = NULL;
    uint8_t *wAudio;
    uint32_t numSamples = 0;
    uint16_t samplingRate = 0;
    uint16_t clockTime = 0;
    bool loop = false;
    uint16_t loopStart = 0;
    uint32_t loopSize = 0;
    size_t offset = sizeof(struct WavChunk_RIFF);
    while (offset + 0x08 < wavSize)
    {
        uint32_t chunkSize = ReadU32_BE(wav, offset + 0x04);
        if (wavSize < offset + 0x08 + chunkSize) FATAL_ERROR("Error reading chunk size\n");
        if (memcmp(wav + offset, "fmt ", 4) == 0)
        {
            fmt = (struct WavChunk_fmt*)(wav + offset); // organize data
            // convert used variables to code endianness
            fmt->wFormatTag = ReadU16_BE((uint8_t*)&fmt->wFormatTag, 0);
            if ((fmt->wFormatTag != WAVE_CODEC_PCM) && (fmt->wFormatTag != WAVE_CODEC_IMA_ADPCM)) FATAL_ERROR("Only PCM and IMA-ADPCM files are supported\n");
            fmt->wChannels = ReadU16_BE((uint8_t*)&fmt->wChannels, 0);
            if (fmt->wChannels != 1) FATAL_ERROR("Only mono files supported\n");
            samplingRate = ReadU32_BE((uint8_t*)&fmt->dwSamplesPerSec, 0);
            clockTime = 16756991 / samplingRate;
            fmt->wBlockAlign = ReadU16_BE((uint8_t*)&fmt->wBlockAlign, 0);
            fmt->wBitsPerSample = ReadU16_BE((uint8_t*)&fmt->wBitsPerSample, 0);
        }
        else if (memcmp(wav + offset, "data", 4) == 0)
        {
            if (fmt == NULL) FATAL_ERROR("Missing fmt chunk\n"); // fmt must be before data
            wData = malloc(sizeof(struct WavChunk_data));
            wData->size = chunkSize;
            wAudio = wav + offset + 0x08;

            numSamples = chunkSize / fmt->wBlockAlign;
            if (fmt->wFormatTag == WAVE_CODEC_IMA_ADPCM) numSamples = (chunkSize - 4 * numSamples) * 2;
        }
        else if (memcmp(wav + offset, "smpl", 4) == 0)
        {
            smpl = (struct WavChunk_smpl*)(wav + offset);
            loop = 0 < ReadU32_BE((uint8_t*)&smpl->numLoops, 0);
            if (loop)
            {
                sampleLoop = (struct Wav_SampleLoop*)(wav + offset + sizeof(struct WavChunk_smpl));
                sampleLoop->start = ReadU32_BE((uint8_t*)&sampleLoop->start, 0);
                sampleLoop->end = ReadU32_BE((uint8_t*)&sampleLoop->end, 0);
                // pad
                sampleLoop->start += (swavTypeSamplesPer64bit[encodeType] - sampleLoop->start) % swavTypeSamplesPer64bit[encodeType];
                sampleLoop->end += (swavTypeSamplesPer64bit[encodeType] - sampleLoop->end) % swavTypeSamplesPer64bit[encodeType];
                numSamples = sampleLoop->end;

                loopStart = sampleLoop->start;
                loopSize = sampleLoop->end - sampleLoop->start;
            }
        }
        else if (memcmp(wav + offset, "SWAV", 4) == 0) // cheat here
        {
            clockTime = ReadU32_BE(wav, offset + 0x08);
        }
        offset += 0x08 + chunkSize + (chunkSize % 2); // chunk ID, size, and padding are not included in wav chunk size
    }
    if (fmt == NULL) FATAL_ERROR("Missing \"fmt \" chunk\n");
    if (wData == NULL) FATAL_ERROR("Missing \"data\" chunk\n");

    // Write WAV Header
    uint32_t dataSize = 0;
    switch (encodeType)
    {
        case SWAV_SIGNED_PCM8:
            dataSize = numSamples;
            break;
        case SWAV_SIGNED_PCM16:
            dataSize = numSamples * 2;
            break;
        case SWAV_IMA_ADPCM:
            dataSize = 4 + numSamples / 2 + numSamples % 2;
            break;
    }
    dataSize += (4 - dataSize) % 4; // padding
    if (loop)
    {
        if (loopStart > numSamples) FATAL_ERROR("Loop start position must be <= numSamples\n");
        if (loopSize > numSamples + loopStart) FATAL_ERROR("Loop end must be <= number of samples\n");
    }
    else
    {
        loopSize = numSamples + ((1 << loopShift[encodeType]) - numSamples) % (1 << loopShift[encodeType]);
    }

    *swavSize = sizeof(struct NitroChunk) + sizeof(struct SwavChunk_DATA) + dataSize;
    uint8_t *swav = malloc(*swavSize);

    // Write SWAV header
    WriteNitroChunk(swav, "SWAV", *swavSize);

    // Write DATA Header
    struct SwavChunk_DATA *swavData = (struct SwavChunk_DATA*)(swav + sizeof(struct NitroChunk));
    memcpy(&swavData->chunkID, "DATA", 4);
    WriteU32_BE((uint8_t*)&swavData->size, 0, dataSize + 0x14);
    swavData->encodeType = encodeType;
    swavData->loop = loop;
    WriteU16_BE((uint8_t*)&swavData->samplingRate, 0, samplingRate);
    WriteU16_BE((uint8_t*)&swavData->clockTime, 0, clockTime);
    WriteU16_BE((uint8_t*)&swavData->loopStart, 0, (loopStart >> loopShift[encodeType]) + (encodeType == SWAV_IMA_ADPCM));
    WriteU32_BE((uint8_t*)&swavData->loopSize, 0, loopSize >> loopShift[encodeType]);

    uint8_t *audioOut = swav + sizeof(struct NitroChunk) + sizeof(struct SwavChunk_DATA);

    // convert inAudio to 16 bit
    if (fmt->wFormatTag == WAVE_CODEC_IMA_ADPCM)
    {
        if (swavData->encodeType == SWAV_IMA_ADPCM)
        {
            memcpy(audioOut, wAudio, wData->size);
        }
        else
        {
            FATAL_ERROR("TODO\n");
        }
    }
    else
    {
        // convert PCM data to 16 bit
        numSamples = wData->size / fmt->wBlockAlign;
        //if (numSamples > 0x1000) numSamples = 0x1000;
        int bytesPerSample = (fmt->wBitsPerSample + 7) / 8;
        int wavAudioSize = numSamples * bytesPerSample;
        uint8_t *mediumAudio = malloc(numSamples * 2);
        if (mediumAudio == NULL) FATAL_ERROR("Failed to convert audio to 16bit it for conversion\n");
        if (wavAudioSize == 0) FATAL_ERROR("Error reading WAV audio size\n");
        uint8_t *audioStart = wAudio;
        uint8_t *auidoEnd = wAudio + wavAudioSize;
        uint8_t *convertedAudio = mediumAudio;
        if (bytesPerSample == 1)
        {
            while(audioStart < auidoEnd)
            {
                convertedAudio[0] = 0;
                convertedAudio[1] = audioStart[0] ^ 0x80;
                audioStart += fmt->wBlockAlign;
                convertedAudio += 2;
            }
        }
        else if (bytesPerSample == 2)
        {
            while(audioStart < auidoEnd)
            {
                convertedAudio[0] = audioStart[0];
                convertedAudio[1] = audioStart[1];
                audioStart += fmt->wBlockAlign;
                convertedAudio += 2;
            }
        }
        else
        {
            if (fmt->wBlockAlign < 3) FATAL_ERROR("WAV Block Align error\n");
            audioStart += fmt->wBlockAlign - 2;
            while(audioStart < auidoEnd)
            {
                convertedAudio[0] = audioStart[0];
                convertedAudio[1] = audioStart[1];
                audioStart += fmt->wBlockAlign;
                convertedAudio += 2;
            }
        }

        // output converted audio
        uint8_t *audioPointer = mediumAudio;
        switch(swavData->encodeType)
        {
            case SWAV_SIGNED_PCM8:
                audioPointer++;
                while (audioPointer < mediumAudio + numSamples*2)
                {
                    *audioOut++ = *audioPointer;
                    audioPointer += 2;
                }
                while (numSamples % 4)
                {
                    *audioOut++ = 0;
                    numSamples++;
                }
                break;
            case SWAV_SIGNED_PCM16:
                memcpy(audioOut, mediumAudio, numSamples * 2);
                if (numSamples % 2) *(uint16_t*)(audioOut + numSamples * 2) = 0;
                break;
            case SWAV_IMA_ADPCM:
                struct IMA_Prediction prediction;
                prediction.value = 0;
                prediction.index = InitialStepIndex(0, ReadU16_BE(audioPointer, 0));

                audioOut[0] = 0;
                audioOut[1] = 0;
                audioOut[2] = prediction.index;
                audioOut[3] = 0;
                int i = 8;
                while (audioPointer < mediumAudio + numSamples*2)
                {
                    uint8_t audioSample = IMA_ADPCM_Encode(ReadU16_BE(audioPointer, 0), &prediction);
                    audioPointer += 2;
                    if (i % 2)
                    {
                        audioOut[i++ / 2] |= audioSample << 4;
                    }
                    else
                    {
                        audioOut[i++ / 2] = audioSample;
                    }
                }
                while (numSamples % 8)
                {
                    uint8_t audioSample = IMA_ADPCM_Encode(0, &prediction);
                    if (i % 2)
                    {
                        audioOut[i++ / 2] |= audioSample << 4;
                    }
                    else
                    {
                        audioOut[i++ / 2] = audioSample;
                    }
                    numSamples++;
                }
                break;
            default:
                FATAL_ERROR("Impossible encode type\n");
                break;
        }
        free(mediumAudio);
    }
    free(wData);

    return swav;
}

uint8_t *SwavToWav(uint8_t *swav, uint32_t swavSize, uint32_t *wavSize, bool pcm16)
{
    // read swav file
    size_t offset = ReadU16_BE(swav, 0x0C);
    struct SwavChunk_DATA *sData = NULL;
    uint8_t *swavAudio;

    while (offset + 0x08 < swavSize)
    {
        uint32_t chunkSize = ReadU32_BE(swav, offset + 0x04);
        if ((swavSize < offset + chunkSize) || (chunkSize < 8))
        {
            FATAL_ERROR("Error reading chunk size\n");
        }
        if (memcmp(swav + offset, "DATA", 4) == 0)
        {
            sData = (struct SwavChunk_DATA*)(swav + offset);
            sData->size = chunkSize - sizeof(struct SwavChunk_DATA);
            sData->samplingRate = ReadU16_BE((uint8_t*)&sData->samplingRate, 0);
            sData->clockTime = ReadU16_BE((uint8_t*)&sData->clockTime, 0);
            sData->loopStart = (ReadU16_BE((uint8_t*)&sData->loopStart, 0) - (sData->encodeType == SWAV_IMA_ADPCM)) << loopShift[sData->encodeType];
            sData->loopSize = ReadU32_BE((uint8_t*)&sData->loopSize, 0) << loopShift[sData->encodeType];
            swavAudio = swav + offset + sizeof(struct SwavChunk_DATA);
        }
        offset += chunkSize;
    }
    if (sData == NULL) FATAL_ERROR("File missing DATA chunk\n");

    uint32_t wavAudioSize = sData->size;
    uint16_t formatTag;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    if (pcm16)
    {
        formatTag = WAVE_CODEC_PCM;
        blockAlign = sizeof(short);
        bitsPerSample = sizeof(short) * 8;
        if (sData->encodeType == SWAV_IMA_ADPCM)
        {
            wavAudioSize = (sData->size - 4) * 4;
        }
        else if (sData->encodeType == SWAV_SIGNED_PCM8)
        {
            wavAudioSize *= 2;
        }
    }
    else
    {
        if (sData->encodeType == SWAV_IMA_ADPCM)
        {
            formatTag = WAVE_CODEC_IMA_ADPCM;
            blockAlign = sData->size;
            bitsPerSample = 4;
        }
        else
        {
            formatTag = WAVE_CODEC_PCM;
            blockAlign = sData->encodeType + 1; // same pcm size as swav
            bitsPerSample = blockAlign * 8;
        }
    }
    bool clockBroken = sData->clockTime != 16756991 / sData->samplingRate;

    *wavSize = sizeof(struct WavChunk_RIFF) + sizeof(struct WavChunk_fmt) + sizeof(struct WavChunk_data) + wavAudioSize + (wavAudioSize % 2) + sData->loop * (sizeof(struct WavChunk_smpl) + sizeof(struct Wav_SampleLoop)) + clockBroken * 12;
    uint8_t *wav = malloc(*wavSize);

    // Write WAV File
    struct WavChunk_RIFF *riff = (struct WavChunk_RIFF*)wav;
    memcpy(&riff->chunkID, "RIFF", 4);
    WriteU32_BE((uint8_t*)&riff->fileSize, 0, *wavSize - 0x08);
    memcpy(&riff->formType, "WAVE", 4);

    struct WavChunk_fmt *fmt = (struct WavChunk_fmt*)(wav + sizeof(struct WavChunk_RIFF));
    memcpy(&fmt->chunkID, "fmt ", 4);
    WriteU32_BE((uint8_t*)&fmt->size, 0, sizeof(struct WavChunk_fmt) - 0x08);
    WriteU16_BE((uint8_t*)&fmt->wFormatTag, 0, formatTag);
    WriteU16_BE((uint8_t*)&fmt->wChannels, 0, 1);
    WriteU32_BE((uint8_t*)&fmt->dwSamplesPerSec, 0, sData->samplingRate);
    WriteU32_BE((uint8_t*)&fmt->dwAvgBytesPerSec, 0, sData->samplingRate * blockAlign);
    WriteU16_BE((uint8_t*)&fmt->wBlockAlign, 0, blockAlign);
    WriteU16_BE((uint8_t*)&fmt->wBitsPerSample, 0, bitsPerSample);

    struct WavChunk_data *wData = (struct WavChunk_data*)(wav + sizeof(struct WavChunk_RIFF) + sizeof(struct WavChunk_fmt));
    memcpy(&wData->chunkID, "data", 4);
    WriteU32_BE((uint8_t*)&wData->size, 0, sizeof(struct WavChunk_data) - 0x08 + wavAudioSize);

    // write data stream
    uint8_t *wavAudio = wav + sizeof(struct WavChunk_RIFF) + sizeof(struct WavChunk_fmt) + sizeof(struct WavChunk_data);
    uint8_t *inAudio = swavAudio;
    uint8_t *audioEnd = swavAudio + sData->size;
    if (sData->encodeType == SWAV_SIGNED_PCM8)
    {
        if (pcm16)
        {
            while (inAudio < audioEnd)
            {
                *wavAudio++ = 0;
                *wavAudio++ = *inAudio++;
            }
        }
        else
        {
            while (inAudio < audioEnd)
            {
                *wavAudio++ = *inAudio++ ^ 0x80; // WAV is unsigned at 8 bits or less
            }
        }
    }
    else if (sData->encodeType == SWAV_IMA_ADPCM)
    {
        if (pcm16)
        {
            struct IMA_Prediction prediction;
            prediction.value = ReadU16_BE(swavAudio, 0);
            prediction.index = swavAudio[2]; // TODO: clamp these
            inAudio += 0x04;
            int i = 0;
            while (inAudio < audioEnd)
            {
                char bottomNibble = *inAudio & 0x0F;
                char topNibble = (*inAudio++ >> 4) & 0x0F;
                WriteU16_BE(wavAudio, i, IMA_ADPCM_Decode(bottomNibble, &prediction));
                i += 2;
                WriteU16_BE(wavAudio, i, IMA_ADPCM_Decode(topNibble, &prediction));
                i += 2;
            }
            if (InitialStepIndex(0, ReadU16_BE(wavAudio, 0)) != swavAudio[2]) WriteU16_BE(wavAudio, 0, InitialValue(swavAudio[4] & 0x0F, swavAudio[2], ReadU16_BE(wavAudio, 0)));
            wavAudio += i;
        }
        else
        {
            while (inAudio < audioEnd)
            {
                *wavAudio++ = *inAudio++;
            }
        }
    }
    else
    {
        while (inAudio < audioEnd)
        {
            *wavAudio++ = *inAudio++;
        }
    }

    if (wavAudioSize % 2) *wavAudio++ = 0; // padding

    if (sData->loop)
    {
        struct WavChunk_smpl *smpl = (struct WavChunk_smpl*)wavAudio; // place after audio
        memcpy(&smpl->chunkID, "smpl", 4);
        WriteU32_BE((uint8_t*)&smpl->size, 0, sizeof(struct WavChunk_smpl) + sizeof(struct Wav_SampleLoop) - 0x08);
        smpl->manufacturer = 0;
        smpl->product = 0;
        smpl->samplePeriod = 0;
        smpl->MIDI_unityNote = 0;
        smpl->MIDI_pitchFraction = 0;
        smpl->SMPTE_format = 0;
        smpl->SMPTE_offset = 0;
        WriteU32_BE((uint8_t*)&smpl->numLoops, 0, 1);
        smpl->sampleData = 0;
        wavAudio += sizeof(struct WavChunk_smpl);

        struct Wav_SampleLoop *sampleLoop = (struct Wav_SampleLoop*)wavAudio;
        sampleLoop->id = 0;
        sampleLoop->type = 0;
        WriteU32_BE((uint8_t*)&sampleLoop->start, 0, sData->loopStart);
        WriteU32_BE((uint8_t*)&sampleLoop->end, 0, sData->loopStart + sData->loopSize - 1);
        sampleLoop->fraction = 0;
        sampleLoop->repititions = 0;
        wavAudio += sizeof(struct Wav_SampleLoop);
    }

    if (clockBroken) // this is cheating but I don't care
    {
        // only necessary for matching original swav
        memcpy(wavAudio, "SWAV", 4);
        WriteU32_BE(wavAudio + 0x04, 0, 4);
        WriteU32_BE(wavAudio + 0x08, 0, sData->clockTime);
    }

    return wav;
}

// TODO: check loop start position < loop end
void ConvertWavToSwav(int argc, char **argv)
{
    if (argc < 3) FATAL_ERROR("Insufficient arguments\n");
    char *inputPath = argv[1];
    char *outputPath = argv[2];

    // optional args
    uint8_t encodeType = SWAV_IMA_ADPCM;
    for (int i = 3; i < argc; i++)
    {
        if (strcmp(argv[i], "--pcm8") == 0)
        {
            encodeType = SWAV_SIGNED_PCM8;
        }
        else if (strcmp(argv[i], "--pcm16") == 0)
        {
            encodeType = SWAV_SIGNED_PCM16;
        }
        else if (strcmp(argv[i], "--adpcm") == 0)
        {
            encodeType = SWAV_IMA_ADPCM;
        }
        else
        {
            FATAL_ERROR("Unrecognized argument: \"%s\"\n", argv[i]);
        }
    }

    int wavSize;
    uint8_t *wav = ReadWholeFile(inputPath, &wavSize);
    struct WavChunk_RIFF *riff = (struct WavChunk_RIFF*)wav;
    if (memcmp(&riff->chunkID, "RIFF", 4) != 0) FATAL_ERROR("%s is not a RIFF file.\n", inputPath);
    if (memcmp(&riff->formType, "WAVE", 4) != 0) FATAL_ERROR("%s does not have WAVE form type.\n", inputPath);

    uint32_t swavSize;
    uint8_t *swav = WavToSwav(wav, wavSize, &swavSize, encodeType);
    free(wav);

    FILE *outFile = fopen(outputPath, "wb");
    if (outFile == NULL) FATAL_ERROR("Failed to open \"%s\" for writing.\n", outputPath);
    fwrite(swav, 1, swavSize, outFile);
    fclose(outFile);
    free(swav);
}

// TODO: test with PCM16 swav
// TODO: allow output other than pcm16
void ConvertSwavToWav(int argc, char **argv)
{
    if (argc < 3) FATAL_ERROR("Insufficient arguments\n");
    char *inputPath = argv[1];
    char *outputPath = argv[2];

    // optional args
    bool pcm16 = true;
    for (int i = 3; i < argc; i++)
    {
        if (strcmp(argv[i], "--pcm16") == 0)
        {
            pcm16 = true;
        }
        else if (strcmp(argv[i], "--same") == 0)
        {
            pcm16 = false;
        }
        else
        {
            FATAL_ERROR("Unrecognized argument: \"%s\"\n", argv[i]);
        }
    }

    int swavSize;
    uint8_t *swav = ReadWholeFile(inputPath, &swavSize);
    if (memcmp(swav, "SWAV", 4) != 0) FATAL_ERROR("%s is not a valid swav file.\n", inputPath);

    uint32_t wavSize;
    uint8_t *wav = SwavToWav(swav, swavSize, &wavSize, pcm16);
    free(swav);

    FILE *outFile = fopen(outputPath, "wb");
    if (outFile == NULL) FATAL_ERROR("Failed to open \"%s\" for writing.\n", outputPath);
    fwrite(wav, 1, wavSize, outFile);
    free(wav);
    fclose(outFile);
}
