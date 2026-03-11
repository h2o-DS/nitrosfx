#include "sdat.h"

#include "cJSON.h"
#include "util.h"

struct InfoStream {
    struct InfoStream *next;
    uint32_t size;
    char *name;
    void *info;
};

struct InfoPackage {
    struct InfoStream *head;
    struct InfoStream *tail;
    uint32_t count;
    uint32_t size;
};

static void PackInfo(struct InfoPackage *package, void *info, const uint32_t size, char *name)
{
    struct InfoStream *stream = malloc(sizeof(struct InfoStream));
    stream->next = NULL;
    stream->info = info;
    stream->size = size;
    stream->name = name;

    package->size += size;

    if (package->count == 0) {
        package->head = stream;
        package->tail = stream;
        package->count = 1;
        return;
    }

    package->tail->next = stream;
    package->tail = stream;
    package->count++;
}

struct SseqInfo {
    uint32_t fileID;
    uint16_t bankID;
    uint8_t volume;
    uint8_t channelPriority;
    uint8_t playerPriority;
    uint8_t playerID;
    uint16_t padding;
};

struct SsarInfo {
    uint32_t fileID;
};

struct SbnkInfo {
    uint32_t fileID;
    short swar0;
    short swar1;
    short swar2;
    short swar3;
};

struct SwarInfo {
    uint32_t fileID;
};

struct PlayerInfo {
    uint16_t maxSseq;
    uint16_t channels;
    uint32_t heapSize;
};

enum FileType {
    SSEQ = 0,
    SBNK,
    SWAR,
    SSAR,
};

struct GroupEntry {
    uint8_t fileType;
    uint8_t load;
    uint16_t padding;
    uint32_t entryID;
};

struct GroupInfo {
    uint32_t count;
    struct GroupEntry *groupEntry;
};

struct StreamPlayerInfo {
    uint8_t numChannels;
    uint8_t left_channel;
    uint8_t right_channel;
};

struct StreamInfo {
    uint32_t fileID;
    uint8_t volume;
    uint8_t priority;
    uint8_t streamPlayerID;
};

struct FileStream {
    struct FileStream *next;
    unsigned char *data;
    uint32_t size;
    char *fileName;
};

struct FilePackage {
    struct FileStream *head;
    struct FileStream *tail;
    uint32_t count;
    uint32_t size;
};

// Packs new files. Returns fileID
static uint32_t PackFile(struct FilePackage *package, unsigned char *data, const uint32_t size, char *fileName)
{
    struct FileStream *stream = malloc(sizeof(struct FileStream));
    stream->next = NULL;
    stream->data = data;
    stream->size = size;
    stream->fileName = fileName;

    if (package->count == 0) {
        package->head = stream;
        package->tail = stream;
        package->count = 1;
        package->size = size + PADDINGSIZE(size, 0x20);
        return 0;
    }

    uint32_t fileID = 0;
    for (struct FileStream *prePacked = package->head; prePacked != NULL; prePacked = prePacked->next)
    {
        if (strcmp(fileName, prePacked->fileName) == 0)
        {
            free(stream);
            free(fileName);
            return fileID;
        }
        fileID++;
    }
    package->tail->next = stream;
    package->tail = stream;
    package->count++;
    package->size += size + PADDINGSIZE(size, 0x20);
    return fileID;
}

enum DataTypes {
    SEQ = 0,
    SEQARC,
    BANK,
    WAVARC,
    PLAYER,
    GROUP,
    STRMPLAYER,
    STRM,
    NUM_DATATYPES
};

static uint16_t GetJsonIndex_U16(cJSON *infoArray, char *name)
{
    uint16_t i = 0;
    for (cJSON *item = cJSON_GetArrayItem(infoArray, 0); item != NULL; item = item->next)
    {
        if (strcmp(name, cJSON_GetObjectItemCaseSensitive(item, "name")->valuestring) == 0)
        {
            return i;
        }
        i++;
    }
    FATAL_ERROR("%s not present in %s\n", name, infoArray->string);
    return i;
}

static short GetJsonIndex_S16(cJSON *infoArray, char *name)
{
    if (strcmp(name, "") == 0)
    {
        return 0xFFFF;
    }

    short i = 0;
    for (cJSON *item = cJSON_GetArrayItem(infoArray, 0); item != NULL; item = item->next)
    {
        if (strcmp(name, cJSON_GetObjectItemCaseSensitive(item, "name")->valuestring) == 0)
        {
            return i;
        }
        i++;
    }
    FATAL_ERROR("%s not present in %s\n", name, infoArray->string);
    return i;
}

void SdatFromDir(char *inputPath, char *outputPath, char *orderPath)
{
    // get files from json
    int jsonLength;
    unsigned char *jsonString = ReadWholeFile(orderPath, &jsonLength);

    cJSON *json = cJSON_Parse((const char *)jsonString);
    if (json == NULL)
    {
        const char *errorPtr = cJSON_GetErrorPtr();
        FATAL_ERROR("Error in line \"%s\"\n", errorPtr);
    }

    cJSON **jsonFields = malloc(sizeof(cJSON*) * NUM_DATATYPES);
    jsonFields[SEQ] = cJSON_GetObjectItemCaseSensitive(json, "seqInfo");
    jsonFields[SEQARC] = cJSON_GetObjectItemCaseSensitive(json, "seqarcInfo");
    jsonFields[BANK] = cJSON_GetObjectItemCaseSensitive(json, "bankInfo");
    jsonFields[WAVARC] = cJSON_GetObjectItemCaseSensitive(json, "wavarcInfo");
    jsonFields[PLAYER] = cJSON_GetObjectItemCaseSensitive(json, "playerInfo");
    jsonFields[GROUP] = cJSON_GetObjectItemCaseSensitive(json, "groupInfo");
    jsonFields[STRMPLAYER] = cJSON_GetObjectItemCaseSensitive(json, "player2Info");
    jsonFields[STRM] = cJSON_GetObjectItemCaseSensitive(json, "strmInfo");

    struct InfoPackage **infoPackages = malloc(sizeof(struct InfoPackage*) * NUM_DATATYPES); // TODO: free this and its entries
    for (int i = 0; i < NUM_DATATYPES; i++)
    {
        infoPackages[i] = calloc(1, sizeof(struct InfoPackage));
    }
    struct FilePackage *filePackage = calloc(1, sizeof(struct FilePackage));

    // define block sizes
    uint32_t *symbSize = calloc(1, sizeof(uint32_t) * NUM_DATATYPES);

    // collect data from json file
    for (int i = 0; i < NUM_DATATYPES; i++)
    {
        for (cJSON *jFile = cJSON_GetArrayItem(jsonFields[i], 0); jFile != NULL; jFile = jFile->next)
        {
            cJSON *name = cJSON_GetObjectItemCaseSensitive(jFile, "name");
            if (strcmp("", name->valuestring) == 0)
            {
                PackInfo(infoPackages[i], NULL, 0, name->valuestring);
                symbSize[i] += 4;
            }
            else
            {
                char *inFPath;
                int inputFileSize;
                unsigned char *inputFile;
                symbSize[i] += 5 + strlen(name->valuestring);
                switch (i)
                {
                    case SEQ:
                        inFPath = JoinPaths(inputPath, cJSON_GetObjectItemCaseSensitive(jFile, "fileName")->valuestring);
                        inputFile = ReadWholeFile(inFPath, &inputFileSize);
                        if (memcmp(inputFile, "SSEQ", 4) != 0)
                        {
                            FATAL_ERROR("%s not a valid sseq file\n", inFPath);
                        }
                        
                        struct SseqInfo *sseqInfo = malloc(sizeof(struct SseqInfo));
                        sseqInfo->fileID = PackFile(filePackage, inputFile, inputFileSize, inFPath);
                        sseqInfo->bankID = GetJsonIndex_U16(jsonFields[BANK], cJSON_GetObjectItemCaseSensitive(jFile, "bnk")->valuestring);
                        sseqInfo->volume = cJSON_GetObjectItemCaseSensitive(jFile, "vol")->valueint;
                        sseqInfo->channelPriority = cJSON_GetObjectItemCaseSensitive(jFile, "cpr")->valueint;
                        sseqInfo->playerPriority = cJSON_GetObjectItemCaseSensitive(jFile, "ppr")->valueint;
                        sseqInfo->playerID = GetJsonIndex_U16(jsonFields[PLAYER], cJSON_GetObjectItemCaseSensitive(jFile, "ply")->valuestring);
                        sseqInfo->padding = 0;
                        PackInfo(infoPackages[i], sseqInfo, sizeof(struct SseqInfo), name->valuestring);
                        break;
                    case SEQARC:
                        inFPath = JoinPaths(inputPath, cJSON_GetObjectItemCaseSensitive(jFile, "fileName")->valuestring);
                        inputFile = ReadWholeFile(inFPath, &inputFileSize);
                        if (memcmp(inputFile, "SSAR", 4) != 0)
                        {
                            FATAL_ERROR("%s not a valid swar file\n", inFPath);
                        }

                        struct SsarInfo *ssarInfo = malloc(sizeof(struct SsarInfo));
                        ssarInfo->fileID = PackFile(filePackage, inputFile, inputFileSize, inFPath);
                        PackInfo(infoPackages[i], ssarInfo, sizeof(struct SsarInfo), name->valuestring);
                        break;
                    case BANK:
                        inFPath = JoinPaths(inputPath, cJSON_GetObjectItemCaseSensitive(jFile, "fileName")->valuestring);
                        inputFile = ReadWholeFile(inFPath, &inputFileSize);
                        if (memcmp(inputFile, "SBNK", 4) != 0)
                        {
                            FATAL_ERROR("%s not a valid sbnk file\n", inFPath);
                        }

                        struct SbnkInfo *sbnkInfo = malloc(sizeof(struct SbnkInfo));
                        sbnkInfo->fileID = PackFile(filePackage, inputFile, inputFileSize, inFPath);
                        cJSON *wa = cJSON_GetObjectItemCaseSensitive(jFile, "wa");
                        sbnkInfo->swar0 = GetJsonIndex_S16(jsonFields[WAVARC], cJSON_GetArrayItem(wa, 0)->valuestring);
                        sbnkInfo->swar1 = GetJsonIndex_S16(jsonFields[WAVARC], cJSON_GetArrayItem(wa, 1)->valuestring);
                        sbnkInfo->swar2 = GetJsonIndex_S16(jsonFields[WAVARC], cJSON_GetArrayItem(wa, 2)->valuestring);
                        sbnkInfo->swar3 = GetJsonIndex_S16(jsonFields[WAVARC], cJSON_GetArrayItem(wa, 3)->valuestring);
                        PackInfo(infoPackages[i], sbnkInfo, sizeof(struct SbnkInfo), name->valuestring);
                        break;
                    case WAVARC:
                        inFPath = JoinPaths(inputPath, cJSON_GetObjectItemCaseSensitive(jFile, "fileName")->valuestring);
                        inputFile = ReadWholeFile(inFPath, &inputFileSize);
                        if (memcmp(inputFile, "SWAR", 4) != 0)
                        {
                            FATAL_ERROR("%s not a valid swar file\n", inFPath);
                        }

                        struct SwarInfo *swarInfo = malloc(sizeof(struct SwarInfo));
                        swarInfo->fileID = PackFile(filePackage, inputFile, inputFileSize, inFPath);
                        PackInfo(infoPackages[i], swarInfo, sizeof(struct SwarInfo), name->valuestring);
                        break;
                    case PLAYER:
                        struct PlayerInfo *playerInfo = malloc(sizeof(struct PlayerInfo));
                        cJSON *channels = cJSON_GetObjectItemCaseSensitive(jFile, "padding");
                        uint8_t channel0 = cJSON_GetArrayItem(channels, 0)->valueint;
                        uint8_t channel1 = cJSON_GetArrayItem(channels, 1)->valueint;
                        uint8_t channel2 = cJSON_GetArrayItem(channels, 2)->valueint;
                        playerInfo->maxSseq = cJSON_GetObjectItemCaseSensitive(jFile, "unkA")->valueint + (channel0 << 8);
                        playerInfo->channels = channel1 + (channel2 << 8);
                        playerInfo->heapSize = cJSON_GetObjectItemCaseSensitive(jFile, "unkB")->valueint;
                        PackInfo(infoPackages[i], playerInfo, sizeof(struct PlayerInfo), name->valuestring);
                        break;
                    case GROUP:
                        struct GroupInfo *groupInfo = malloc(sizeof(struct GroupInfo));
                        groupInfo->count = cJSON_GetObjectItemCaseSensitive(jFile, "count")->valueint;
                        groupInfo->groupEntry = malloc(sizeof(struct GroupEntry) * groupInfo->count);
                        for (int subgroupI = 0; subgroupI < groupInfo->count; subgroupI++)
                        {
                            cJSON *subGroup = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(jFile, "subGroup"), subgroupI);
                            uint32_t type = cJSON_GetObjectItemCaseSensitive(subGroup, "type")->valueint;
                            groupInfo->groupEntry[subgroupI].fileType = type;
                            groupInfo->groupEntry[subgroupI].load = type >> 8;
                            groupInfo->groupEntry[subgroupI].padding = 0;
                            groupInfo->groupEntry[subgroupI].entryID = cJSON_GetObjectItemCaseSensitive(subGroup, "entry")->valueint;
                        }
                        PackInfo(infoPackages[i], groupInfo, 4 + 8 * groupInfo->count, name->valuestring);
                        break;
                    case STRMPLAYER: // TODO
                        struct StreamPlayerInfo *streamPlayerInfo = malloc(sizeof(struct StreamPlayerInfo));
                        streamPlayerInfo->numChannels = 0;
                        streamPlayerInfo->left_channel = 0;
                        streamPlayerInfo->right_channel = 0;
                        PackInfo(infoPackages[i], streamPlayerInfo, sizeof(struct StreamPlayerInfo), name->valuestring);
                        break;
                    case STRM: // TODO
                        struct StreamInfo *streamInfo = malloc(sizeof(struct StreamInfo));
                        streamInfo->fileID = 0;
                        streamInfo->volume = 0;
                        streamInfo->priority = 0;
                        streamInfo->streamPlayerID = 0;
                        PackInfo(infoPackages[i], streamInfo, sizeof(struct StreamInfo), name->valuestring);
                        break;
                }
            }
        }
    }

    // write header
    unsigned char sdatHeader[0x40] =
    {
        'S',  'D',  'A',  'T',  0xFF, 0xFE, 0x00, 0x01,  0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    uint32_t symbBlockSize = 0x40 + 0x04 * NUM_DATATYPES;
    uint32_t infoBlockSize = 0x40 + 0x04 * NUM_DATATYPES;
    uint32_t fatBlockSize = 0x0C + 0x10 * filePackage->count;
    for (int i = 0; i < NUM_DATATYPES; i++)
    {
        symbBlockSize += symbSize[i];
        infoBlockSize += infoPackages[i]->size + 4 * infoPackages[i]->count;
    }
    // add padding to each block;
    uint8_t symbPad = PADDINGSIZE(symbBlockSize, 0x04);
    symbBlockSize += symbPad;
    uint8_t infoPad = PADDINGSIZE(infoBlockSize, 0x04);
    infoBlockSize += infoPad;

    uint32_t offset = 0x0E;
    if (true) // symb check
    {
        WriteU16(sdatHeader, offset, 4);
        WriteU32(sdatHeader, offset+2, 0x40);
        WriteU32(sdatHeader, offset+6, symbBlockSize);
        offset = 0x18;
    }
    else
    {
        WriteU16(sdatHeader, offset, 3);
        offset = 0x10;
        symbBlockSize = 0;
    }
    WriteU32(sdatHeader, offset, 0x40 + symbBlockSize);
    WriteU32(sdatHeader, offset+4, infoBlockSize);
    WriteU32(sdatHeader, offset+8, 0x40 + symbBlockSize + infoBlockSize);
    WriteU32(sdatHeader, offset+12, fatBlockSize);
    uint32_t fileOffset = 0x40 + symbBlockSize + infoBlockSize + fatBlockSize;
    WriteU32(sdatHeader, offset+16, fileOffset);
    filePackage->size += PADDINGSIZE(fileOffset, 0x20);
    WriteU32(sdatHeader, offset+20, filePackage->size);
    uint32_t sdatSize = fileOffset + filePackage->size;
    WriteU32(sdatHeader, 0x08, sdatSize);

    FILE *outFile = fopen(outputPath, "wb");
    if (outFile == NULL)
        FATAL_ERROR("Failed to open \"%s\" for writing.\n", outputPath);
    fwrite(sdatHeader, 1, 0x40, outFile);

    // TODO: SYMB block is optional
    // write SYMB block
    unsigned char symbHeader[0x40] =
    {
        'S',  'Y',  'M',  'B',  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    WriteU32(symbHeader, 0x04, symbBlockSize);
    // name table addresses
    offset = 0x40;
    for (int i = 0; i < NUM_DATATYPES; i++)
    {
        WriteU32(symbHeader, 0x08 + 4*i, offset);
        offset += 4 + infoPackages[i]->count * 4;
    }
    fwrite(symbHeader, 1, 0x40, outFile);

    // name tables
    unsigned char u32Buffer[4] = {0, 0, 0, 0};
    for (int i = 0; i < NUM_DATATYPES; i++)
    {
        WriteU32(u32Buffer, 0, infoPackages[i]->count);
        fwrite(u32Buffer, 1, 4, outFile);
        for (struct InfoStream *j = infoPackages[i]->head; j != NULL; j = j->next)
        {
            if (strcmp(j->name, "") == 0)
            {
                WriteU32(u32Buffer, 0, 0);
            }
            else
            {
                WriteU32(u32Buffer, 0, offset);
                offset += strlen(j->name) + 1;
            }
            fwrite(u32Buffer, 1, 4, outFile);
        }
    }
    // names
    for (int i = 0; i < NUM_DATATYPES; i++)
    {
        for (struct InfoStream *j = infoPackages[i]->head; j != NULL; j = j->next)
        {
            if (strcmp(j->name, "") != 0)
            {
                fwrite(j->name, 1, strlen(j->name) + 1, outFile);
            }
        }
    }
    WriteU32(u32Buffer, 0, 0);
    fwrite(u32Buffer, 1, symbPad, outFile);

    // INFO block
    unsigned char infoHeader[0x40] =
    {
        'I',  'N',  'F',  'O',  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    WriteU32(infoHeader, 0x04, infoBlockSize);
    // info table addresses
    offset = 0x40;
    for (int i = 0; i < NUM_DATATYPES; i++)
    {
        WriteU32(infoHeader, 0x08 + 4*i, offset);
        offset += 4 + infoPackages[i]->count * 4 + infoPackages[i]->size;
    }
    fwrite(infoHeader, 1, 0x40, outFile);
    // info tables + info
    offset = 0x40;
    for (int i = 0; i < NUM_DATATYPES; i++)
    {
        WriteU32(u32Buffer, 0, infoPackages[i]->count);
        fwrite(u32Buffer, 1, 4, outFile);
        offset += 4 + 4 * infoPackages[i]->count;
        for (struct InfoStream *j = infoPackages[i]->head; j != NULL; j = j->next)
        {
            if (strcmp(j->name, "") == 0)
            {
                WriteU32(u32Buffer, 0, 0);
            }
            else
            {
                WriteU32(u32Buffer, 0, offset);
                offset += j->size;
            }
            fwrite(u32Buffer, 1, 4, outFile);
        }
        for (struct InfoStream *j = infoPackages[i]->head; j != NULL; j = j->next)
        {
            if (strcmp(j->name, "") != 0)
            {
                if (i == GROUP)
                {
                    struct GroupInfo *groupInfo = j->info;
                    fwrite(&(groupInfo->count), 1, 4, outFile);
                    fwrite(groupInfo->groupEntry, 1, sizeof(struct GroupEntry) * groupInfo->count, outFile);
                    free(groupInfo->groupEntry);
                }
                else
                {
                    fwrite(j->info, 1, j->size, outFile);
                }
                free(j->info);
            }
        }
        free(infoPackages[i]);
    }
    free(infoPackages);
    WriteU32(u32Buffer, 0, 0);
    fwrite(u32Buffer, 1, infoPad, outFile);

    // FAT block
    unsigned char fatHeader[0x0C] =
    {
        'F',  'A',  'T',  ' ',  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00
    };
    WriteU32(fatHeader, 0x04, 0x0C + filePackage->count * 0x10);
    WriteU32(fatHeader, 0x08, filePackage->count);
    fwrite(fatHeader, 1, 0x0C, outFile);

    fileOffset += 0x0C;
    offset = fileOffset;
    unsigned char *fileEntry = calloc(1, 0x10);
    for (struct FileStream *fileStream = filePackage->head; fileStream != NULL; fileStream = fileStream->next)
    {
        WriteU32(fileEntry, 0x00, offset);
        WriteU32(fileEntry, 0x04, fileStream->size);
        fwrite(fileEntry, 1, 0x10, outFile);

        offset += fileStream->size + PADDINGSIZE(fileStream->size, 0x20);
        free(fileStream->fileName);
    }
    free(fileEntry);

    // FILE block
    unsigned char fileHeader[0x0C] =
    {
        'F',  'I',  'L',  'E',  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00
    };
    WriteU32(fileHeader, 0x04, filePackage->size);
    WriteU32(fileHeader, 0x08, filePackage->count);
    fwrite(fileHeader, 1, 0x0C, outFile);

    unsigned char *padding = calloc(1, 0x20);
    uint16_t paddingSize = PADDINGSIZE(fileOffset, 0x20);
    if (paddingSize > 0)
    {
        fwrite(padding, 1, paddingSize, outFile);
    }

    for (struct FileStream *fileStream = filePackage->head; fileStream != NULL; fileStream = fileStream->next)
    {
        fwrite(fileStream->data, 1, fileStream->size, outFile);
        free(fileStream->data);

        paddingSize = PADDINGSIZE(fileStream->size, 0x20);
        if (paddingSize > 0)
        {
            fwrite(padding, 1, paddingSize, outFile);
        }
    }
    free(padding);
    free(filePackage);

    fclose(outFile);
}

void DirFromSdat(char *inputPath, char *outputPath)
{
    printf("%s\t%s\n", inputPath, outputPath);
}
