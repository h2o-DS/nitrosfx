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
    void(*function)(int argc, char **argv);
};

// TODO: add -h to main and all converters
int main(int argc, char **argv)
{
    if (argc < 3) FATAL_ERROR("Usage: nitrosfx INPUT_PATH OUTPUT_PATH [options...]\n");

    struct CommandHandler handlers[] =
    {
        {"mid",  "sseq", ConvertMidiToSseq},
        {"sseq",  "mid", ConvertSseqToMidi},
        {"wav",  "swav", ConvertWavToSwav},
        {"swav",  "wav", ConvertSwavToWav},
        {"txt",  "sbnk", ConvertTxtToSbnk},
        {"sbnk",  "txt", ConvertSbnkToTxt},
        {"swav", "swar", ConvertSwavToSwar},
        {"swar", "swav", ConvertSwarToSwav},
        {"wav", "swar",  ConvertWavToSwar},
        {"swar", "wav",  ConvertSwarToWav},
        {NULL,   "swar", ConvertPathToSwar}, // multiple swav from dir
        {"swar",   NULL, ConvertSwarToPath}, // multiple swav to dir
        {NULL,   "sdat", ConvertPathToSdat}, // directory + json to sdat
        {"sdat",   NULL, ConvertSdatToPath}, // sdat to directory + json
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
            handlers[i].function(argc, argv);
            return 0;
        }
    }

    FATAL_ERROR("Don't know how to convert \"%s\" to \"%s\".\n", inputPath, outputPath);
}
