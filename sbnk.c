#include "sbnk.h"

#include "util.h"

enum InstrumentsType {
    INSTRUMENT_NULL = 0, // Null (empty)
    INSTRUMENT_SINGLE, // PCM
    INSTRUMENT_PSG1, // PSG
    INSTRUMENT_PSG2, // White Noise
    INSTRUMENT_PSG3, // Direct PCM
    INSTRUMENT_ZEROED, // Null (0s)
    INSTRUMENT_DRUMS = 16, // Drum Set
    INSTRUMENT_KEYSPLIT, // Key Split
};

const uint8_t instrumentsTypeValues[] = {
    INSTRUMENT_NULL,
    INSTRUMENT_SINGLE,
    INSTRUMENT_PSG1,
    INSTRUMENT_PSG2,
    INSTRUMENT_PSG3,
    INSTRUMENT_DRUMS,
    INSTRUMENT_KEYSPLIT,
};

const char *instrumentsTypeStrings[] = {
    "NULL",
    "Single",
    "PSG1",
    "PSG2",
    "PSG3",
    "ZERO",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "Drums",
    "Keysplit",
};
const char *sameAddressString = "SameAsAbove";

struct InstrumentStream {
    struct InstrumentStream *next;
    unsigned char *data;
    uint32_t size;
    uint8_t instrumentsType;
    uint16_t headerIndex;
    uint16_t address;
};

struct SbnkPackage {
    struct InstrumentStream *head;
    struct InstrumentStream *tail;
    uint32_t count;
    uint32_t size;
};

static void PackSbnkFile(struct SbnkPackage *sbnkPackage, unsigned char *data, const uint32_t size, uint8_t instrumentsType, uint16_t headerIndex)
{
    struct InstrumentStream *instrument = malloc(sizeof(struct InstrumentStream));
    instrument->next = NULL;
    instrument->data = data;
    instrument->size = size;
    instrument->instrumentsType = instrumentsType;
    instrument->headerIndex = headerIndex;
    instrument->address = sbnkPackage->size;

    sbnkPackage->size += size;

    if (sbnkPackage->count == 0) {
        sbnkPackage->head = instrument;
        sbnkPackage->tail = instrument;
        sbnkPackage->count = 1;
        return;
    }

    sbnkPackage->tail->next = instrument;
    sbnkPackage->tail = sbnkPackage->tail->next;
    sbnkPackage->count++;
}

void ConvertTxtToSbnk(int argc, char **argv)
{
    if (argc < 3)
    {
        FATAL_ERROR("Insufficient arguments\n");
    }
    char *inputPath = argv[1];
    char *outputPath = argv[2];

    // open input file
    char line[1024];
    FILE *txtFile = fopen(inputPath,"r");
    if (txtFile == NULL) {
        FATAL_ERROR("Could not open INPUT FILE “%s”: %s\n", inputPath, strerror(errno));
    }
    struct SbnkPackage *sbnkPackage = calloc(1, sizeof(struct DataPackage));
    const char delimiter[3] = ", ";
    char *s;
    unsigned char *instrumentData;
    uint8_t instrumentType = INSTRUMENT_NULL;
    uint16_t headerIndex = 0;
    while (fgets(line, 1024, txtFile))
    {
        // ignore comments
        line[strcspn(line, ";")] = 0;
        line[strcspn(line, "\r")] = 0;
        if (strlen(line) == 0)
        {
            continue;
        }

        if (line[0] == '\t')
        {
            // sub definitions
            // don't pack, just add to last one
            int push = sbnkPackage->tail->size;
            instrumentData = malloc(12 + push);
            for (int i = 0; i < push; i++)
            {
                instrumentData[i] = sbnkPackage->tail->data[i];
            }
            s = strtok(line, delimiter);
            if (s == NULL)
            {
                FATAL_ERROR("Read error in section %d, NULL\n", sbnkPackage->count);
            }
            WriteU16(instrumentData, push, strtod(s, NULL));
            for (int i = 0; i < 2; i++)
            {
                s = strtok(NULL, delimiter);
                if (s == NULL)
                {
                    FATAL_ERROR("Read error in section %d, NULL\n", sbnkPackage->count);
                }
                WriteU16(instrumentData, i * 2 + 2 + push, strtod(s, NULL));
            }
            for (int i = 0; i < 6; i++)
            {
                s = strtok(NULL, delimiter);
                if (s == NULL)
                {
                    FATAL_ERROR("Read error in section %d, NULL\n", sbnkPackage->count);
                }
                WriteU8(instrumentData, 6 + i + push, strtod(s, NULL));
            }
            free(sbnkPackage->tail->data);
            sbnkPackage->tail->data = instrumentData;
            sbnkPackage->tail->size += 12;
            sbnkPackage->size += 12;
        }
        else
        {
            s = strtok(line, delimiter);
            if (s == NULL)
            {
                continue;
            }
            headerIndex = strtod(s, NULL);
            if (strcmp("Unused", s) == 0)
            {
                // byte stream
                // This is probably for padding to 4
                continue;
            }
            else
            {
                s = strtok(NULL, delimiter);
                if (s == NULL)
                {
                    continue;
                }

                // identify instrument
                if (strcmp(sameAddressString, s) == 0)
                {
                    PackSbnkFile(sbnkPackage, NULL, 0, instrumentType, headerIndex);
                    continue;
                }
                if (strcmp(instrumentsTypeStrings[INSTRUMENT_NULL], s) == 0)
                {
                    PackSbnkFile(sbnkPackage, NULL, 0, INSTRUMENT_NULL, headerIndex);
                    continue;
                }
                bool typeMatch = false;
                for (int i = 1; i < 7; i++)
                {
                    if (strcmp(instrumentsTypeStrings[instrumentsTypeValues[i]], s) == 0)
                    {
                        instrumentType = instrumentsTypeValues[i];
                        typeMatch = true;
                        break;
                    }
                }
                if (typeMatch == false)
                {
                    FATAL_ERROR("Unrecognized intrument type %s\n", s);
                }

                // collect instrument values
                if (instrumentType < INSTRUMENT_DRUMS)
                {
                    instrumentData = malloc(10);
                    for (int i = 0; i < 2; i++)
                    {
                        s = strtok(NULL, delimiter);
                        if (s == NULL)
                        {
                            FATAL_ERROR("Read error in section %d, PSG\n", sbnkPackage->count);
                        }
                        WriteU16(instrumentData, i * 2, strtod(s, NULL));
                    }
                    for (int i = 0; i < 6; i++)
                    {
                        s = strtok(NULL, delimiter);
                        if (s == NULL)
                        {
                            FATAL_ERROR("Read error in section %d, PSG\n", sbnkPackage->count);
                        }
                        WriteU8(instrumentData, 4 + i, strtod(s, NULL));
                    }
                    PackSbnkFile(sbnkPackage, instrumentData, 10, instrumentType, headerIndex);
                }
                else if (instrumentType == INSTRUMENT_DRUMS)
                {
                    instrumentData = malloc(2);
                    for (int i = 0; i < 2; i++)
                    {
                        s = strtok(NULL, delimiter);
                        if (s == NULL)
                        {
                            FATAL_ERROR("Read error in section %d, DRUMS\n", sbnkPackage->count);
                        }
                        WriteU8(instrumentData, i, strtod(s, NULL));
                    }
                    PackSbnkFile(sbnkPackage, instrumentData, 2, instrumentType, headerIndex);
                }
                else if (instrumentType == INSTRUMENT_KEYSPLIT)
                {
                    instrumentData = malloc(8);
                    for (int i = 0; i < 8; i++)
                    {
                        s = strtok(NULL, delimiter);
                        if (s == NULL)
                        {
                            FATAL_ERROR("Read error in section %d, KEYSPLIT\n", sbnkPackage->count);
                        }
                        WriteU8(instrumentData, i, strtod(s, NULL));
                    }
                    PackSbnkFile(sbnkPackage, instrumentData, 8, instrumentType, headerIndex);
                }
            }
        }
    }
    fclose(txtFile);

    // add header and index table size
    uint32_t headerSize = 0x3C + sbnkPackage->count * 0x04;
    sbnkPackage->size += headerSize;

    // pad to 0x04 byte alignment
    int pad = (4 - sbnkPackage->size) % 4;
    sbnkPackage->size += pad;

    // write header
    unsigned char sbnkHeader[0x3C] =
    {
        'S',  'B',  'N',  'K',  0xFF, 0xFE, 0x00, 0x01,  0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00,
        'D',  'A',  'T',  'A',  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00
    };
    WriteU32(sbnkHeader, 0x08, sbnkPackage->size); // file size
    WriteU32(sbnkHeader, 0x14, sbnkPackage->size - 0x10); // data size
    WriteU32(sbnkHeader, 0x38, sbnkPackage->count); // number of instruments

    FILE *outFile = fopen(outputPath, "wb");
    if (outFile == NULL)
        FATAL_ERROR("Failed to open \"%s\" for writing.\n", outputPath);
    fwrite(sbnkHeader, 1, 0x3C, outFile);

    // write indexing table
    struct InstrumentStream *bnk;
    unsigned char zeroPointer[4] = {0, 0, 0, 0};
    unsigned char absolutePointer[4] = {0, 0, 0, 0};
    for (int i = 0; i < sbnkPackage->count; i++)
    {
        bnk = sbnkPackage->head;
        while (bnk->headerIndex != i)
        {
            bnk = bnk->next;
        }
        if (bnk->instrumentsType == INSTRUMENT_NULL) // check for NULL
        {
            fwrite(zeroPointer, 1, 4, outFile);
            bnk = bnk->next;
        }
        else
        {
            absolutePointer[0] = bnk->instrumentsType;
            WriteU16(absolutePointer, 1, headerSize + bnk->address);
            fwrite(absolutePointer, 1, 4, outFile);
            bnk = bnk->next;
            while ((bnk != NULL) && (bnk->size == 0)) // check for "same as above"
            {
                fwrite(absolutePointer, 1, 4, outFile);
                bnk = bnk->next;
                i++;
            }
        }
    }

    // write file stream
    bnk = sbnkPackage->head;
    struct InstrumentStream *bnkF;
    for (int i = 0; i < sbnkPackage->count; i++)
    {
        if (bnk->data != NULL)
        {
            fwrite(bnk->data, 1, bnk->size, outFile);
            free(bnk->data);
        }
        bnkF = bnk;
        bnk = bnk->next;
        free(bnkF);
    }

    if (pad)
    {
        unsigned char *padding = calloc(1, pad);
        fwrite(padding, 1, pad, outFile);
        free(padding);
    }

    free(sbnkPackage);
    fclose(outFile);
}

int InstrumentAddressCmp_q(const void *i1, const void *i2)
{
    const struct InstrumentStream *a = (struct InstrumentStream*)i1;
    const struct InstrumentStream *b = (struct InstrumentStream*)i2;
    int diff = (a->address > b->address) - (a->address < b->address);
    if (diff == 0)
    {
        // maintain order of null and shared pointers
        diff = (a->headerIndex > b->headerIndex) - (a->headerIndex < b->headerIndex);
    }
    return diff;
}

void ConvertSbnkToTxt(int argc, char **argv)
{
    if (argc < 3)
    {
        FATAL_ERROR("Insufficient arguments\n");
    }
    char *inputPath = argv[1];
    char *outputPath = argv[2];

    // open input file
    int sbnkSize;
    unsigned char *sbnkFile = ReadWholeFile(inputPath, &sbnkSize);
    if (memcmp(sbnkFile, "SBNK", 4) != 0)
    {
        FATAL_ERROR("Not a valid sbnk file.\n");
    }
    uint32_t numInstruments = ReadU32_BE(sbnkFile, 0x38);

    // collect data elements
    struct InstrumentStream *instrumentStream = malloc(sizeof(struct InstrumentStream) * numInstruments);
    for (int i = 0; i < numInstruments; i++)
    {
        instrumentStream[i].headerIndex = i;
        instrumentStream[i].instrumentsType = ReadU8(sbnkFile, 0x3C + i * 4);
        instrumentStream[i].address = ReadU16_BE(sbnkFile, 0x3D + i * 4);
    }

    // sort by addresses
    qsort(instrumentStream, numInstruments, sizeof(struct InstrumentStream), InstrumentAddressCmp_q);

    // Write to file
    FILE *outFile = fopen(outputPath, "wb");
    if (outFile == NULL)
    {
        FATAL_ERROR("Failed to open \"%s\" for writing.\n", outputPath);
    }

    // write data stream
    uint16_t lastAddress = 0;
    for (int i = 0; i < numInstruments; i++)
    {
        uint8_t headerIndex = instrumentStream[i].headerIndex;
        uint16_t address = instrumentStream[i].address;
        uint8_t instrumentType = instrumentStream[i].instrumentsType;
        char *line = NULL;

        // check for NULL
        if (instrumentType == INSTRUMENT_NULL)
        {
            line = malloc(12);
            snprintf(line, 12, "%d, %s\r\n", headerIndex, instrumentsTypeStrings[INSTRUMENT_NULL]);
            fprintf(outFile, line);
            free(line);
            continue;
        }

        // check for reused address
        if (address == lastAddress)
        {
            line = malloc(19);
            snprintf(line, 19, "%d, %s\r\n", headerIndex, sameAddressString);
            fprintf(outFile, line);
            free(line);
            continue;
        }
        lastAddress = address;

        // print standard types
        int end = 0;
        if (i == numInstruments - 1)
        {
            end = sbnkSize - 4; // subtract 4 to account for potential padding
        }
        else
        {
            end = instrumentStream[i + 1].address;
        }
        free(instrumentStream);

        // read data depending upon instrument type
        if (instrumentType < INSTRUMENT_ZEROED)
        {
            line = malloc(58);
            snprintf(line, 58, "%d, %s, %d, %d, %d, %d, %d, %d, %d, %d\r\n",
                headerIndex,
                instrumentsTypeStrings[instrumentType],
                ReadU16_BE(sbnkFile, address),
                ReadU16_BE(sbnkFile, address + 2),
                ReadU8(sbnkFile, address + 4),
                ReadU8(sbnkFile, address + 5),
                ReadU8(sbnkFile, address + 6),
                ReadU8(sbnkFile, address + 7),
                ReadU8(sbnkFile, address + 8),
                ReadU8(sbnkFile, address + 9));
            address += 10;
        }
        else if (instrumentType == INSTRUMENT_DRUMS)
        {
            line = malloc(23);
            snprintf(line, 23, "%d, %s, %d, %d\r\n",
                headerIndex,
                instrumentsTypeStrings[instrumentType],
                ReadU8(sbnkFile, address),
                ReadU8(sbnkFile, address + 1));
            address += 2;
        }
        else if (instrumentType == INSTRUMENT_KEYSPLIT)
        {
            line = malloc(56);
            snprintf(line, 56, "%d, %s, %d, %d, %d, %d, %d, %d, %d, %d\r\n",
                headerIndex,
                instrumentsTypeStrings[instrumentType],
                ReadU8(sbnkFile, address),
                ReadU8(sbnkFile, address + 1),
                ReadU8(sbnkFile, address + 2),
                ReadU8(sbnkFile, address + 3),
                ReadU8(sbnkFile, address + 4),
                ReadU8(sbnkFile, address + 5),
                ReadU8(sbnkFile, address + 6),
                ReadU8(sbnkFile, address + 7));
            address += 8;
        }
        else
        {
            FATAL_ERROR("Unrecognized intrument type %d\n", instrumentType);
        }
        fprintf(outFile, line);
        free(line);

        // check for extra lines
        while (address < end)
        {
            line = malloc(53);
            snprintf(line, 53, "\t%d, %d, %d, %d, %d, %d, %d, %d, %d\r\n",
                ReadU16_BE(sbnkFile, address),
                ReadU16_BE(sbnkFile, address + 2),
                ReadU16_BE(sbnkFile, address + 4),
                ReadU8(sbnkFile, address + 6),
                ReadU8(sbnkFile, address + 7),
                ReadU8(sbnkFile, address + 8),
                ReadU8(sbnkFile, address + 9),
                ReadU8(sbnkFile, address + 10),
                ReadU8(sbnkFile, address + 11));
            address += 12;
            fprintf(outFile, line);
            free(line);
        }
    }

    free(sbnkFile);
    fclose(outFile);
}