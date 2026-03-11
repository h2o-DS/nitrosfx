#include "util.h"
#include "sbnk.h"
#include "sdat.h"
#include "sseq.h"
#include "swav.h"
#include "swar.h"

struct CommandHandler
{
    const char *inputFileExtension;
    const char *outputFileExtension;
    void(*function)(char *inputPath, char *outputPath, int argc, char **argv);
};

static void UnknownConversion(char *UNUSED, char *UNUSED, int UNUSED, char **UNUSED);
static void ConvertSseqToMidi(char *inputPath, char *outputPath, int UNUSED, char **UNUSED);
static void ConvertWavToSwav(char *inputPath, char *outputPath, int argc, char **argv);
static void ConvertSwavToWav(char *inputPath, char *outputPath, int argc, char **argv);
static void ConvertTxtToSbnk(char *inputPath, char *outputPath, int argc, char **argv);

static void UnknownConversion(char *UNUSED, char *UNUSED, int UNUSED, char **UNUSED)
{
    FATAL_ERROR("Not implemented yet\n");
}

struct SSEQTrackData {
    unsigned char *sequence;
    int sequenceSize;
} SSEQTrackData;

struct SSEQData {
    unsigned char *data;
    struct SSEQTrackData track[16];
} SSEQData;

enum MidiFormat {
    SINGLE_MULIT_CHANNEL = 0,
    SIMULTANEOUS_TRACKS,
    SINGLE_TRACK_PATTERNS,
};

enum DeltaTimes {
    TICKS_PER_QUARTER_NOTE = 0,
    TICKS_PER_FRAME,
};

enum MidiEvents {
    EVENT_NOTE_OFF = 8,
    EVENT_NOTE_ON,
    EVENT_POLYPHONIC_PRESSURE,
    EVENT_CONTROLLER,
    EVENT_PROGRAM_CHANGE,
    EVENT_CHANNEL_PRESSURE,
    EVENT_PITCH_BEND,
};

#define MIDI_CONTROLLER_MODULATION_DEPTH 0x18 // 0x01
#define MIDI_CONTROLLER_PORTAMENTO_TIME 0x05
#define MIDI_CONTROLLER_DATA_ENTRY 0x06
#define MIDI_CONTROLLER_VOLUME 0x07
#define MIDI_CONTROLLER_PAN 0x0A
#define MIDI_CONTROLLER_VOLUME_2 0x0B
#define MIDI_CONTROLLER_MASTER_VOLUME 0x14 // 0x0c
#define MIDI_CONTROLLER_TRANSPOSE 0x15 // 0x0D
#define MIDI_CONTROLLER_PRIORITY 0x16 // 0x0E
#define MIDI_CONTROLLER_SETVAR0 0x10
#define MIDI_CONTROLLER_SETVAR1 0x11
#define MIDI_CONTROLLER_SETVAR2 0x12
#define MIDI_CONTROLLER_SETVAR3 0x13
#define MIDI_CONTROLLER_PITCH_BEND_RANGE 0x06 // 0x14
#define MIDI_CONTROLLER_MODULATION_SPEED 0x19 // 0x15
#define MIDI_CONTROLLER_MODULATION_RANGE 0x1B // 0x17
#define MIDI_CONTROLLER_MODULATION_DELAY 0x1A
#define MIDI_CONTROLLER_MODULATION_DELAY_10 0x1B
#define MIDI_CONTROLLER_SWEEP_PITCH 0x1C
#define MIDI_CONTROLLER_SWEEP_PITCH_24 0x1D
#define MIDI_CONTROLLER_PORTAMENTO_ON 0x41
#define MIDI_CONTROLLER_PORTAMENTO_CONTROL 0x54
#define MIDI_CONTROLLER_ATTACK_RATE 0x55
#define MIDI_CONTROLLER_DECAY_RATE 0x56
#define MIDI_CONTROLLER_SUSTAIN_RATE 0x57
#define MIDI_CONTROLLER_RELEASE_RATE 0x58
#define MIDI_CONTROLLER_LOOP_START 0x59
#define MIDI_CONTROLLER_LOOP_END 0x5A 
#define MIDI_CONTROLLER_RPN_LSB 0x64
#define MIDI_CONTROLLER_RPN_MSB 0x65
#define MIDI_CONTROLLER_MONO 0x7E
#define MIDI_CONTROLLER_POLY 0x7F

#define MIDI_META_END 0x2F
#define MIDI_META_TEMPO 0x51
#define MIDI_META_TEXT 0x01

static void ConvertMidiToSseq(char *inputPath, char *outputPath, int UNUSED, char **UNUSED)
{
    int fileSize;
    unsigned char *data = ReadWholeFile(inputPath, &fileSize);
    if (memcmp(data, "MThd", 4) != 0)
    {
        FATAL_ERROR("Not a valid MIDI file.\n");
    }

    __uint32_t headerSize = ReadU32_BE(data, 0x04);

    __uint16_t format = ReadU16_BE(data, 0x08); // SDATTOOL only supports format 1
    if (format > SIMULTANEOUS_TRACKS)
    {
        FATAL_ERROR("Incompatible MIDI format.\n");
    }

    __uint16_t n = ReadU16_BE(data, 0x0a);
    if (n == 1)
    {
        format = 0;
    }

    //__uint16_t division = ReadU16_BE(data, 0x0c);

    size_t offset = 0x08 + headerSize;

    //printf("format = %x\tn = %x\tdivision = %x\n\n", format, n, division);

    unsigned char *output = malloc(fileSize * 2);
    if (output == NULL)
        FATAL_ERROR("Failed to allocate memory for output.\n");
    memset(output, 0, fileSize * 2);
    size_t outputSize = 0x1C + (n - 1) * 5;

    // write sseq header
    strcpy((char *)output, "SSEQ");
    output[0x04] = 0xFF;
    output[0x05] = 0xFE;
    output[0x07] = 0x01;
    output[0x0C] = 0x10;
    output[0x0E] = 0x01;
    // write data header
    strcpy((char *)output + 0x10, "DATA");
    output[0x18] = 0x1C;
    if (format == SIMULTANEOUS_TRACKS)
    {
        output[0x1C] = SSEQ_COMMAND_NUM_TRACKS;
        outputSize += 3;
    }

    size_t trackOffset = 0;
    __uint8_t noteOnNote[0x80] = {0};
    __uint32_t noteOnTime[0x80] = {0};
    __uint32_t noteOnAddress[0x80] = {0};
    __uint8_t numActiveNotes = 0;
    __uint32_t trackPos = 0;
    __uint8_t lastWaitSize = 0;
    __uint8_t lastSSEQCommand = 0;

    for (__uint8_t i = 0; i < n; i++)
    {
        if (memcmp(data + offset, "MTrk", 4) != 0)
        {
            FATAL_ERROR("Failed to read track %d.\n", i);
        }
        offset += 4;

        __uint32_t trackSize = ReadU32_BE(data, offset);
        offset += 4;

        trackOffset = offset;
        offset += trackSize;

        __uint32_t lastEventCode = 0;
        __uint32_t time = 0;
        __uint8_t noWait = 0;
        __uint32_t callPointer = 0;
        __uint32_t callSize = 0;
        __uint32_t callAddress = 0;
        __uint8_t numCalls = 0;
        __uint32_t callPointers[0x10] = {0};
        __uint32_t callShift = 0;
        while (trackOffset < offset)
        {
            __uint32_t deltaTime = ReadVariableLength(data, &trackOffset);
            time += deltaTime;

            __uint8_t eventCode = ReadU8(data, trackOffset);
            if (eventCode & 0x80)
            {
                lastEventCode = eventCode;
                trackOffset += 1;
            }
            else
            {
                eventCode = lastEventCode;
            }

            if ((deltaTime > 0) && (noWait == 0))
            {
                if (lastSSEQCommand == SSEQ_COMMAND_WAIT) // merge sequential waits
                {
                    outputSize -= lastWaitSize;
                    deltaTime += ReadVariableLength(output, &outputSize);
                    outputSize -= lastWaitSize;
                    lastWaitSize = WriteVariableLength(output, outputSize, deltaTime);
                    outputSize += lastWaitSize;
                }
                else
                {
                    output[outputSize] = SSEQ_COMMAND_WAIT;
                    lastSSEQCommand = SSEQ_COMMAND_WAIT;
                    outputSize += 1;
                    lastWaitSize = WriteVariableLength(output, outputSize, deltaTime);
                    outputSize += lastWaitSize;
                }
            }

            if (eventCode == 0xFF) // Meta event
            {
                eventCode = ReadU8(data, trackOffset);
                trackOffset += 1;
                __uint8_t metaLength = ReadVariableLength(data, &trackOffset);
                switch (eventCode)
                {
                    case (MIDI_META_END):
                        output[outputSize] = SSEQ_COMMAND_END;
                        lastSSEQCommand = SSEQ_COMMAND_END;
                        outputSize += 1 + callSize;
                        trackOffset += trackSize; // stop reading from track
                        break;

                    case (MIDI_META_TEMPO):
                        output[outputSize] = SSEQ_COMMAND_TEMPO;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        __uint16_t tempo = 60000000 / ReadU24_BE(data, trackOffset);
                        WriteU16(output, outputSize + 1, tempo);
                        outputSize += 3;
                        break;

                    case (MIDI_META_TEXT): // for backwards compatibility with flag-midi
                        if (metaLength > 0)
                        {
                            char text[metaLength + 1];
                            strncpy(text, (char *)data + trackOffset, metaLength);
                            text[metaLength] = '\0';

                            if (strstr(text, "PitchSweep") != NULL)
                            {
                                output[outputSize] = SSEQ_COMMAND_SWEEP_PITCH;
                                lastSSEQCommand = SSEQ_COMMAND_SWEEP_PITCH;
                                WriteU16(output, outputSize + 1, (int)strtol(text + 11, NULL, 10));
                                outputSize += 3;
                            }

                            else if (strstr(text, "Random") != NULL)
                            {
                                output[outputSize] = SSEQ_COMMAND_RANDOM;
                                lastSSEQCommand = SSEQ_COMMAND_RANDOM;
                                __uint128_t bit5 = (__uint128_t)strtoll(text + 7, NULL, 10);
                                WriteU8(output, outputSize + 1, bit5 & 0xFF);
                                WriteU16(output, outputSize + 2, (bit5 >> 8) & 0xFFFF);
                                WriteU16(output, outputSize + 4, (bit5 >> 24) & 0xFFFF);
                                outputSize += 6;
                            }

                            else if (strstr(text, "Jump") != NULL)
                            {
                                output[outputSize] = SSEQ_COMMAND_JUMP;
                                lastSSEQCommand = SSEQ_COMMAND_JUMP;
                                WriteU24(output, outputSize + 1, (int)strtol(text + 13, NULL, 16) + format * 8);
                                outputSize += 4;
                            }

                            else if (strcmp(text, "{") == 0)
                            {
                                trackOffset += 4;
                                metaLength = ReadVariableLength(data, &trackOffset);
                                char callText[metaLength + 1];
                                strncpy(callText, (char *)data + trackOffset, metaLength);
                                callText[metaLength] = '\0';
                                
                                output[outputSize] = SSEQ_COMMAND_CALL;
                                lastSSEQCommand = SSEQ_COMMAND_CALL;
                                callPointer = (int)strtol(callText + 8, NULL, 16) + format * 8;
                                WriteU24(output, outputSize + 1, callPointer);
                                outputSize += 4;
                                callAddress = outputSize;
                                outputSize = callPointer + 0x1C;
                                callShift = 0;
                            }

                            else if (strcmp(text, "}") == 0)
                            {
                                output[outputSize] = SSEQ_COMMAND_RETURN;
                                lastSSEQCommand = SSEQ_COMMAND_RETURN;
                                outputSize += 1;
                                __uint8_t uniqueCallPointer = 1;
                                for (int ptr = 0; ptr < numCalls; ptr++)
                                {
                                    if (callPointers[ptr] == callPointer)
                                    {
                                        uniqueCallPointer = 0;
                                    }
                                }
                                if (uniqueCallPointer)
                                {
                                    callSize += outputSize - (callPointer + 0x1C);
                                    callPointers[numCalls] = callPointer;
                                    numCalls += 1;
                                }
                                outputSize = callAddress + callShift;
                            }

                            else if (strstr(text, "Label") != NULL)
                            {
                                lastSSEQCommand = SSEQ_COMMAND_JUMP;
                            }

                            else if (strcmp(text, "Var") == 0)
                            {
                                output[outputSize] = SSEQ_COMMAND_VARIABLE;
                                lastSSEQCommand = SSEQ_COMMAND_VARIABLE;
                                outputSize += 1;
                            }

                            else if (strcmp(text, "TrackEnd") == 0)
                            {
                                noWait = 1;
                            }
                        }
                        break;

                    default:
                        break;
                }
                trackOffset += metaLength;
            }
            else if (eventCode == 0xF0) // Sysex event
            {
                trackOffset += ReadVariableLength(data, &trackOffset);
            }
            else if (eventCode == 0xF7) // Sysex Escape event
            {
                trackOffset += ReadVariableLength(data, &trackOffset);
            }
            else
            {
                __uint8_t eventType = (eventCode & 0xF0) >> 4;
                __uint8_t channel = eventCode & 0x0F;

                __uint8_t note = 0;
                __uint8_t velocity = 0;
                __uint8_t controllerCmd = 0;
                __uint8_t pitchBend = 0;
                switch (eventType)
                {
                case EVENT_NOTE_OFF:
                    note = ReadU8(data, trackOffset);
                    trackOffset += 1;

                    __uint8_t noteIdx = 0;
                    while ((noteOnNote[noteIdx] != note) && (noteIdx < numActiveNotes))
                    {
                        noteIdx += 1;
                    }
                    if (noteIdx == numActiveNotes)
                    {
                        FATAL_ERROR("Note Off without Note On");
                    }

                    __uint8_t timeSize = VariableLength(time - noteOnTime[noteIdx]);
                    callShift += timeSize;
                    for (int shift = outputSize - 1; shift >= noteOnAddress[noteIdx]; shift--)
                    {
                        output[shift + timeSize] = output[shift];
                    }
                    outputSize += WriteVariableLength(output, noteOnAddress[noteIdx], time - noteOnTime[noteIdx]); // note duration

                   numActiveNotes -= 1;
                    for (int z = noteIdx; z < numActiveNotes; z++)
                    {
                        noteOnNote[z] = noteOnNote[z + 1];
                        noteOnTime[z] = noteOnTime[z + 1];
                        noteOnAddress[z]  = noteOnAddress[z + 1] + timeSize;
                    }

                    trackOffset += 1; // velocity
                    break;

                case EVENT_NOTE_ON:
                    note = ReadU8(data, trackOffset);
                    trackOffset += 1;
                    velocity = ReadU8(data, trackOffset);
                    trackOffset += 1;

                    output[outputSize] = note;
                    outputSize += 1;
                    lastSSEQCommand = note;
                    output[outputSize] = velocity;
                    outputSize += 1;

                    noteOnNote[numActiveNotes] = note;
                    noteOnTime[numActiveNotes] = time;
                    noteOnAddress[numActiveNotes] = outputSize;
                    numActiveNotes += 1;
                    break;

                case EVENT_POLYPHONIC_PRESSURE: // need
                    trackOffset += 2;
                    break;

                case EVENT_CONTROLLER:
                    controllerCmd = ReadU8(data, trackOffset);
                    trackOffset += 1;

                    switch (controllerCmd)
                    {
                    case (MIDI_CONTROLLER_MONO):
                        output[outputSize] = SSEQ_COMMAND_MONO_NOTE;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = 0x01;
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_POLY):
                        output[outputSize] = SSEQ_COMMAND_MONO_NOTE;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = 0x00;
                        outputSize += 2;
                        break;

                    case (MIDI_CONTROLLER_PORTAMENTO_TIME):
                        output[outputSize] = SSEQ_COMMAND_PORTAMENTO_TIME;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_PITCH_BEND_RANGE):
                        output[outputSize] = SSEQ_COMMAND_PITCH_BEND_RANGE;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_VOLUME):
                        output[outputSize] = SSEQ_COMMAND_VOLUME;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_PAN):
                        output[outputSize] = SSEQ_COMMAND_PAN;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_VOLUME_2):
                        output[outputSize] = SSEQ_COMMAND_VOLUME_2;
                        lastSSEQCommand = SSEQ_COMMAND_VOLUME_2;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_MASTER_VOLUME):
                        output[outputSize] = SSEQ_COMMAND_MASTER_VOLUME;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_TRANSPOSE):
                        output[outputSize] = SSEQ_COMMAND_TRANSPOSE;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_PRIORITY):
                        output[outputSize] = SSEQ_COMMAND_PRIORITY;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_MODULATION_DEPTH):
                        output[outputSize] = SSEQ_COMMAND_MODULATION_DEPTH;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_MODULATION_SPEED):
                        output[outputSize] = SSEQ_COMMAND_MODULATION_SPEED;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_MODULATION_DELAY):
                        output[outputSize] = SSEQ_COMMAND_MODULATION_TYPE;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_MODULATION_RANGE):
                        output[outputSize] = SSEQ_COMMAND_MODULATION_RANGE;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_PORTAMENTO_ON):
                        output[outputSize] = SSEQ_COMMAND_PORTAMENTO_ON;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_PORTAMENTO_CONTROL):
                        output[outputSize] = SSEQ_COMMAND_PORTAMENTO_CONTROL;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_ATTACK_RATE):
                        output[outputSize] = SSEQ_COMMAND_ATTACK_RATE;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_DECAY_RATE):
                        output[outputSize] = SSEQ_COMMAND_DECAY_RATE;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_SUSTAIN_RATE):
                        output[outputSize] = SSEQ_COMMAND_SUSTAIN_RATE;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;
                    
                    case (MIDI_CONTROLLER_RELEASE_RATE):
                        output[outputSize] = SSEQ_COMMAND_RELEASE_RATE;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                        output[outputSize + 1] = ReadU8(data, trackOffset);
                        outputSize += 2;
                        break;

                    default:
                        break;
                    }
                    trackOffset += 1;
                    break;

                case EVENT_PROGRAM_CHANGE:
                    output[outputSize] = SSEQ_COMMAND_BANK;
                    lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                    output[outputSize + 1] = ReadU8(data, trackOffset);
                    outputSize += 2;
                    trackOffset += 1;
                    break;

                case EVENT_CHANNEL_PRESSURE: // need
                    trackOffset += 1;
                    break;

                case EVENT_PITCH_BEND:
                    pitchBend = ReadU8(data, trackOffset) >> 6;
                    pitchBend += ReadU8(data, trackOffset + 1) << 1;
                    pitchBend = (pitchBend + 0x80) & 0xFF;
                    trackOffset += 2;

                    output[outputSize] = SSEQ_COMMAND_PITCH_BEND;
                    lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                    output[outputSize + 1] = pitchBend;
                    outputSize += 2;
                    break;

                default:
                    FATAL_ERROR("Invalid event\n");
                    break;
                }

                if ((format == SIMULTANEOUS_TRACKS) && (output[0x1F + (i-1)*5] != SSEQ_COMMAND_TRACK_ADDRESS))
                {
                    if (channel < 8)
                    {
                        output[0x1D] |= 1 << channel;
                    }
                    else
                    {
                        output[0x1E] |= 1 << (channel - 8);
                    }

                    if (i > 0)
                    {
                        output[0x1F + (i-1)*5] = SSEQ_COMMAND_TRACK_ADDRESS;
                        output[0x1F + (i-1)*5 + 1] = channel;
                        output[0x1F + (i-1)*5 + 2] = trackPos & 0xFF;
                        output[0x1F + (i-1)*5 + 3] = (trackPos >> 8) & 0xFF;
                        output[0x1F + (i-1)*5 + 4] = (trackPos >> 16) & 0xFF;
                        lastSSEQCommand = SSEQ_COMMAND_TEMPO;
                    }
                }
            }
        }
        trackPos = outputSize - 0x1C;
    }
    /*if (output[outputSize] != 0)
    {
        while ((output[outputSize] != SSEQ_COMMAND_RETURN) && (outputSize != 0xFFFFFFFD))
        {
            outputSize += 1;
        }
        outputSize += 1;
    }*/
    output[outputSize] = SSEQ_COMMAND_END;
    outputSize += 1;

    while (outputSize % 4 != 0)
    {
        outputSize++;
    }
    
    output[0x08] = outputSize & 0xFF;
    output[0x09] = (outputSize >> 8) & 0xFF;
    output[0x0A] = (outputSize >> 16) & 0xFF;
    output[0x0B] = (outputSize >> 24) & 0xFF;
    output[0x14] = (outputSize - 0x10) & 0xFF;
    output[0x15] = ((outputSize - 0x10) >> 8) & 0xFF;
    output[0x16] = ((outputSize - 0x10) >> 16) & 0xFF;
    output[0x17] = ((outputSize - 0x10) >> 24) & 0xFF;

    FILE *fp = fopen(outputPath, "wb");
    if (fp == NULL)
    {
        FATAL_ERROR("Failed to open \"%s\" for writing.\n", outputPath);
    }
    fwrite(output, 1, outputSize, fp);
    fclose(fp);
    free(output);
}

static void ConvertSseqToMidi(char *inputPath, char *outputPath, int UNUSED, char **UNUSED)
{
    ReadSseq(inputPath, outputPath);
}

static void ConvertWavToSwav(char *inputPath, char *outputPath, int UNUSED, char **UNUSED)
{
    ReadWav(inputPath, outputPath, SWAV_SIGNED_PCM8);
}

static void ConvertSwavToWav(char *inputPath, char *outputPath, int UNUSED, char **UNUSED)
{
    ReadSwav(inputPath, outputPath, WAV_UNSIGNED_PCM8);
}

static void ConvertTxtToSbnk(char *inputPath, char *outputPath, int UNUSED, char **UNUSED)
{
    SbnkFromTxt(inputPath, outputPath);
}

static void ConvertSbnkToTxt(char *inputPath, char *outputPath, int UNUSED, char **UNUSED)
{
    TxtFromSbnk(inputPath, outputPath);
}

static void ConvertSwavToSwar(char *inputPath, char *outputPath, int argc, char **argv)
{
    UnknownConversion(inputPath, outputPath, argc, argv);
}

static void ConvertSwarToSwav(char *inputPath, char *outputPath, int argc, char **argv)
{
    UnknownConversion(inputPath, outputPath, argc, argv);
}

static void ConvertPathToSwar(char *inputPath, char *outputPath, int argc, char **argv)
{
    char *orderPath = NULL;
    if (argc > 3)
    {
        orderPath = argv[3];
    }
    MakeSwar(inputPath, outputPath, orderPath, false);
}

static void ConvertSwarToPath(char *inputPath, char *outputPath, int argc, char **argv)
{
    char *orderPath = NULL;
    if (argc > 3)
    {
        orderPath = argv[3];
    }
    SplitSwar(inputPath, outputPath, orderPath);
}

static void ConvertPathToSdat(char *inputPath, char *outputPath, int argc, char **argv)
{
    if (argc < 4)
    {
        FATAL_ERROR("Insufficient arguments\n");
    }
    SdatFromDir(inputPath, outputPath, argv[3]);
}

static void ConvertSdatToPath(char *inputPath, char *outputPath, int UNUSED, char **UNUSED)
{
    DirFromSdat(inputPath, outputPath);
}

// TODO:
// - refactor all the useless functions above
// - - just send argc argv to the appropriate functions
int main(int argc, char **argv)
{
    if (argc < 3) FATAL_ERROR("Usage: nitrosfx INPUT_PATH OUTPUT_PATH [options...]\n");

    // 4/14
    struct CommandHandler handlers[] =
    {
        {"mid",  "sseq", ConvertMidiToSseq}, // finish
        {"sseq",  "mid", ConvertSseqToMidi}, // finish
        {"wav",  "swav", ConvertWavToSwav}, // finish
        {"swav",  "wav", ConvertSwavToWav}, // finish
        {"txt",  "sbnk", ConvertTxtToSbnk},
        {"sbnk",  "txt", ConvertSbnkToTxt},
        {"swav", "swar", ConvertSwavToSwar}, // TODO
        {"swar", "swav", ConvertSwarToSwav}, // TODO
        {"wav", "swar",  UnknownConversion}, // TODO
        {"swar", "wav",  UnknownConversion}, // TODO
        {NULL,   "swar", ConvertPathToSwar}, // multiple swav from dir
        {"swar",   NULL, ConvertSwarToPath}, // multiple swav to dir
        {NULL,   "sdat", ConvertPathToSdat}, // TODO
        {"sdat",   NULL, ConvertSdatToPath}, // TODO
    };

    char *inputPath = argv[1];
    char *outputPath = argv[2];
    char *inputFileExtension = GetFileExtension(inputPath);
    char *outputFileExtension = GetFileExtension(outputPath);

    if (inputFileExtension == NULL) FATAL_ERROR("Input file \"%s\" has no extension.\n", inputPath);

    if (outputFileExtension == NULL) FATAL_ERROR("Output file \"%s\" has no extension.\n", outputPath);

    for (int i = 0; handlers[i].function != NULL; i++)
    {
        if (((handlers[i].inputFileExtension == NULL) || (strcmp(handlers[i].inputFileExtension, inputFileExtension) == 0)) &&
            ((handlers[i].outputFileExtension == NULL) || (strcmp(handlers[i].outputFileExtension, outputFileExtension) == 0)))
        {
            handlers[i].function(inputPath, outputPath, argc, argv);
            return 0;
        }
    }

    FATAL_ERROR("Don't know how to convert \"%s\" to \"%s\".\n", inputPath, outputPath);
}
