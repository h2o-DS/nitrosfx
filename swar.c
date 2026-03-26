#include "swar.h"

#include "util.h"

static void PackSwarFile(struct DataPackage *swarPackage, unsigned char *data, const uint32_t size)
{
    struct DataFile *file = malloc(sizeof(struct DataFile));
    file->next = NULL;
    file->data = data + 0x18; // SWAV is stored headless
    file->size = size - 0x18;

    // Each file adds a U32 to the indexing table
    swarPackage->size += sizeof(uint32_t) + file->size;

    if (swarPackage->count == 0) {
        swarPackage->head = file;
        swarPackage->tail = file;
        swarPackage->count = 1;
        return;
    }

    swarPackage->tail->next = file;
    swarPackage->tail = file;
    swarPackage->count++;
}

void ConvertSwavToSwar(int argc, char **argv)
{
    printf("%d\t%s\n", argc, argv[2]);
}

void ConvertSwarToSwav(int argc, char **argv)
{
    printf("%d\t%s\n", argc, argv[2]);
}

// TODO:
// - make dir travel a function so it can be made recursive for subfolders
// - add naix functionality
// - - will require more robus input parsing
void ConvertPathToSwar(int argc, char **argv)
{
    char *orderPath = NULL;
    if (argc > 3)
    {
        orderPath = argv[3];
    }
    char *inputPath = argv[1];
    char *outputPath = argv[2];
    bool naix = false;

    DIR *dir = opendir(inputPath);
    if (dir == NULL) {
        FATAL_ERROR("could not open DIRECTORY “%s”: %s\n", inputPath, strerror(errno));
    }

    // collect file names
    struct StrVec *fileNames = StrVec_New(5000); // arbitary allocation
    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0)  continue;
        if (strcmp(ent->d_name, "..") == 0) continue;

        char *name = JoinPaths(inputPath, ent->d_name);
        struct stat stbuf = { 0 };
        if (stat(name, &stbuf) == -1)
        {
            FATAL_ERROR("could not access FILE “%s”: %s\n", name, strerror(errno));
        }

        if (S_ISDIR(stbuf.st_mode))
        {
            /* handle a directory */
            continue; // worry about nesting later
        }
        else {
            /* handle a file */
            char *fileExtension = GetFileExtension(name);
            if ((fileExtension != NULL) && (strcmp("swav", fileExtension) == 0)) // only collect swav files
            {
                fileNames->s[fileNames->count++] = name;
            }
        }
    }
    closedir(dir);

    // sort file names
    // use order file if provided
    struct StrVec *sortedNames = StrVec_New(fileNames->count);
    int numSorted = 0;
    char *orderMap = calloc(1, sizeof(char) * fileNames->count);
    if (orderPath != NULL)
    {
        char line[1024];
        FILE *orderFile = fopen(orderPath,"r");
        if (orderFile == NULL) {
            FATAL_ERROR("Could not open ORDER FILE “%s”: %s\n", orderPath, strerror(errno));
        }
        while (fgets(line, 1024, orderFile))
        {
            line[strcspn(line, "\r")] = 0;
            char *orderName = JoinPaths(inputPath, line);
            int i;
            for (i = 0; i < fileNames->count; i++)
            {
                if (strcmp(fileNames->s[i], orderName) == 0)
                {
                    break;
                }
            }
            if (i == fileNames->count)
            {
                FATAL_ERROR("%s is in ORDER FILE but not directory\n", line);
            }
            // check for duplicates
            if (orderMap[i] != 0)
            {
                FATAL_ERROR("%s is in ORDER FILE multiple times\n", line);
            }
            orderMap[i] = 1;
            sortedNames->s[numSorted++] = fileNames->s[i];
            sortedNames->count = numSorted;
            free(orderName);
        }
        fclose(orderFile);
    }
    for (int i = 0; i < fileNames->count; i++)
    {
        if (orderMap[i] == 0)
        {
            sortedNames->s[sortedNames->count++] = fileNames->s[i];
        }
    }
    free(orderMap);
    free(fileNames);
    // sort any remaining
    qsort(sortedNames->s + numSorted, sortedNames->count - numSorted, sizeof(char *), strcmp_q);

    // output naix file
    if (naix)
    {
        printf("TODO: naix output");
    }

    // pack swav files
    struct DataPackage *swarPackage = calloc(1, sizeof(struct DataPackage));
    swarPackage->size = 0x3C;
    if (swarPackage == NULL) {
        FATAL_ERROR("could not allocate memory for SWAR\n");
    }
    unsigned char *swavFile;
    int swavSize;
    for (int i = 0; i < sortedNames->count; i++)
    {
        swavFile = ReadWholeFile(sortedNames->s[i], &swavSize);
        if (memcmp(swavFile, "SWAV", 4) != 0)
        {
            FATAL_ERROR("%s not a valid swav file\n", sortedNames->s[i]);
        }
        free(sortedNames->s[i]);

        PackSwarFile(swarPackage, swavFile, swavSize);
    }
    free(sortedNames);

    // write header
    unsigned char swarHeader[0x3C] =
    {
        'S',  'W',  'A',  'R',  0xFF, 0xFE, 0x00, 0x01,  0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00,
        'D',  'A',  'T',  'A',  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00
    };
    WriteU32(swarHeader, 0x08, swarPackage->size); // file size
    WriteU32(swarHeader, 0x14, swarPackage->size - 0x10); // data size
    WriteU32(swarHeader, 0x38, swarPackage->count); // number of swavs

    FILE *outFile = fopen(outputPath, "wb");
    if (outFile == NULL)
        FATAL_ERROR("Failed to open \"%s\" for writing.\n", outputPath);
    fwrite(swarHeader, 1, 0x3C, outFile);

    // write indexing table
    size_t swavPointer = 0x3C + swarPackage->count * 0x04;
    struct DataFile *swav = swarPackage->head;
    for (int i = 0; i < swarPackage->count; i++)
    {
        fwrite(&swavPointer, 1, 4, outFile);
        swavPointer += swav->size;
        swav = swav->next;
    }

    // write file stream
    swav = swarPackage->head;
    struct DataFile *swavF;
    for (int i = 0; i < swarPackage->count; i++)
    {
        fwrite(swav->data, 1, swav->size, outFile);
        free(swav->data - 0x18); // get beginning of swav file
        swavF = swav;
        swav = swav->next;
        free(swavF);
    }

    free(swarPackage);
    fclose(outFile);
}

void ConvertSwarToPath(int argc, char **argv)
{
    char *orderPath = NULL;
    if (argc > 3)
    {
        orderPath = argv[3];
    }
    char *inputPath = argv[1];
    char *outputPath = argv[2];

    // open input file
    int swarSize;
    unsigned char *swarFile = ReadWholeFile(inputPath, &swarSize);
    if (memcmp(swarFile, "SWAR", 4) != 0)
    {
        FATAL_ERROR("Not a valid swar file.\n");
    }
    uint32_t numSwavs = ReadU32_BE(swarFile, 0x38);

    // generate list of output file names
    struct StrVec *fileNames = StrVec_New(5000); // arbitary allocation
    if (orderPath != NULL)
    {
        char line[1024];
        FILE *orderFile = fopen(orderPath,"r");
        if (orderFile == NULL) {
            FATAL_ERROR("Could not open ORDER FILE “%s”: %s\n", orderPath, strerror(errno));
        }
        while (fgets(line, 1024, orderFile))
        {
            line[strcspn(line, "\r")] = 0;
            // check for file extension
            char *fileExtension = GetFileExtension(line);
            if (fileExtension == NULL)
            {
                FATAL_ERROR("%s is an invalid file name\n", line);
            }
            if (strcmp("swav", fileExtension) != 0)
            {
                FATAL_ERROR("%s does not have .swav extension\n", line);
            }

            char *orderName = JoinPaths(outputPath, line);

            // check for duplicates
            for (int i = 0; i < fileNames->count; i++)
            {
                if (strcmp(fileNames->s[i], orderName) == 0)
                {
                    FATAL_ERROR("%s is in ORDER FILE multiple times\n", line);
                }
            }

            fileNames->s[fileNames->count++] = orderName;
        }
        fclose(orderFile);
    }
    if (fileNames->count > numSwavs)
    {
        FATAL_ERROR("Too many files in ORDER FILE %s\n", orderPath);
    }
    // remaining files are named numerically
    for (int i = fileNames->count; i < numSwavs; i++)
    {
        char *numName = malloc(8);
        snprintf(numName, 8, "%02d.swav", i);
        // could collide with poorly named order files, but that feels like a user error
        // don't feel like checking for that now
        fileNames->s[fileNames->count++] = JoinPaths(outputPath, numName);
        free(numName);
    }

    // write SWAVs to output directory
    uint32_t pointerAddress = 0x3C;
    unsigned char swavHeader[0x18] =
    {
        'S',  'W',  'A',  'V',  0xFF, 0xFE, 0x00, 0x01,  0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00,
        'D',  'A',  'T',  'A',  0x00, 0x00, 0x00, 0x00
    };
    for (int i = 0; i < numSwavs; i++)
    {
        FILE *outFile = fopen(fileNames->s[i], "wb");
        if (outFile == NULL)
            FATAL_ERROR("Failed to open \"%s\" for writing.\n", fileNames->s[i]);
        free(fileNames->s[i]);

        // calc filesize
        uint32_t swavAddress = ReadU32_BE(swarFile, pointerAddress);
        pointerAddress += 4;
        uint32_t swavSize = ReadU32_BE(swarFile, pointerAddress) - swavAddress;
        if (i == numSwavs - 1)
        {
            swavSize = swarSize - swavAddress;
        }
        WriteU32(swavHeader, 0x08, swavSize + 0x18);
        WriteU32(swavHeader, 0x14, swavSize + 0x08);

        // write to file
        fwrite(swavHeader, 1, 0x18, outFile);
        fwrite(swarFile + swavAddress, 1, swavSize, outFile);
        fclose(outFile);
    }
    free(fileNames);
    free(swarFile);
}

void ConvertWavToSwar(int argc, char **argv)
{
    printf("%d\t%s\n", argc, argv[2]);
}

void ConvertSwarToWav(int argc, char **argv)
{
    printf("%d\t%s\n", argc, argv[2]);
}
