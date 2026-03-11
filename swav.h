#ifndef SWAV_H
#define SWAV_H

#define WAVE_FORMAT_PCM 0x0001
#define WAVE_FORMAT_ADPCM 0x0103

enum SWAV_ENCODE {
    SWAV_SIGNED_PCM8 = 0,
    SWAV_SIGNED_PCM16,
    SWAV_IMA_ADPCM,
};

enum WAV_ENCODE {
    WAV_UNSIGNED_PCM8 = 0,
    WAV_SIGNED_PCM16,
    WAV_IMA_ADPCM,
};

void ReadSwav(char *inputPath, char *outputPath, enum WAV_ENCODE wavEncodeType);
void ReadWav(char *inputPath, char *outputPath, enum SWAV_ENCODE swavEncodeType);

#endif //SWAV_H