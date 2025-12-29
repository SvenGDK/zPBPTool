// main.c
// Linux: gcc -std=c11 -O2 -o zPBPTool main.c
// macOS: clang -std=c11 -O2 -Wall -Wextra -o zPBPTool main.c

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define mkdir_p(path) _mkdir(path)
#else
#define mkdir_p(path) mkdir(path, 0755)
#endif

#pragma pack(push, 1)
typedef struct {
    uint8_t  signature[4];
    uint16_t version[2];
    uint32_t offset[8];
} PBPHeader;
#pragma pack(pop)

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(PBPHeader) == 40, "PBPHeader must be 40 bytes");
#elif defined(__cplusplus) && __cplusplus >= 201103L
static_assert(sizeof(PBPHeader) == 40, "PBPHeader must be 40 bytes");
#elif defined(_MSC_VER)

typedef char static_assert_sizeof_PBPHeader[(sizeof(PBPHeader) == 40) ? 1 : -1];
#else
typedef char static_assert_sizeof_PBPHeader[(sizeof(PBPHeader) == 40) ? 1 : -1];
#endif

static const char* default_file_names[8] = {
    "PARAM.SFO",
    "ICON0.PNG",
    "ICON1.PMF",
    "PIC0.PNG",
    "PIC1.PNG",
    "SND0.AT3",
    "DATA.PSP",
    "DATA.PSAR"
};

static void print_error_and_exit(const char* msg) {
    fprintf(stderr, "Error: %s\n", msg);
    exit(1);
}

static int validate_header(const PBPHeader* h) {
    if (h->signature[1] != 'P' || h->signature[2] != 'B' || h->signature[3] != 'P') {
        return -1; // invalid signature
    }
    if (h->version[1] != 1 && h->version[0] != 0) {
        fprintf(stderr, "Invalid version: %u.%u\n", (unsigned)h->version[0], (unsigned)h->version[1]);
        return -2; // invalid version
    }
    return 0;
}

static void analyze_file(const char* file_path) {
    FILE* f = fopen(file_path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open '%s': %s\n", file_path, strerror(errno));
        exit(1);
    }

    PBPHeader header;
    if (fread(&header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        print_error_and_exit("Failed to read header");
    }

    int v = validate_header(&header);
    if (v != 0) {
        fclose(f);
        print_error_and_exit("Header validation failed");
    }

    printf("PBP Header:\n");
    printf("\tSignature:\t%c%c%c%c\n", header.signature[0], header.signature[1], header.signature[2], header.signature[3]);
    printf("\tVersion:\t%u.%u\n", (unsigned)header.version[1], (unsigned)header.version[0]);
    printf("Offsets:\n");
    for (size_t i = 0; i < 8; ++i) {
        uint32_t offset = header.offset[i];
        if (i + 1 < 8 && header.offset[i + 1] > offset) {
            printf("\t%s:\t%u\n", default_file_names[i], (unsigned)offset);
        }
        else {
            printf("\t%s:\tNULL\n", default_file_names[i]);
        }
    }

    fclose(f);
}

static unsigned char* read_file_to_buffer(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);

    unsigned char* buf = malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_len = (size_t)len;
    return buf;
}

static void unpack_pbp(const char* input_path, const char* dir_path) {
    FILE* f = fopen(input_path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open '%s': %s\n", input_path, strerror(errno));
        exit(1);
    }

    PBPHeader header;
    if (fread(&header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        print_error_and_exit("Failed to read header");
    }

    if (validate_header(&header) != 0) {
        fclose(f);
        print_error_and_exit("Header validation failed");
    }

    if (mkdir_p(dir_path) != 0 && errno != EEXIST) {
        fclose(f);
        fprintf(stderr, "Failed to create directory '%s': %s\n", dir_path, strerror(errno));
        exit(1);
    }

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); print_error_and_exit("seek failed"); }
    long file_len = ftell(f);
    if (file_len < 0) { fclose(f); print_error_and_exit("ftell failed"); }
    rewind(f);

    unsigned char* content = malloc((size_t)file_len);
    if (!content) { fclose(f); print_error_and_exit("out of memory"); }
    if (fread(content, 1, (size_t)file_len, f) != (size_t)file_len) {
        free(content);
        fclose(f);
        print_error_and_exit("failed to read file content");
    }
    fclose(f);

    for (size_t i = 0; i < 8; ++i) {
        uint32_t offset = header.offset[i];
        uint32_t file_size = 0;
        if (i + 1 < 8) {
            if (header.offset[i + 1] > offset) file_size = header.offset[i + 1] - offset;
            else file_size = 0;
        }
        else {
            if ((uint32_t)file_len > offset) file_size = (uint32_t)file_len - offset;
            else file_size = 0;
        }

        if (file_size == 0) continue;

        long corrected_offset = (long)offset - (long)sizeof(PBPHeader);
        if (corrected_offset < 0 || (size_t)corrected_offset + file_size >(size_t)file_len) {
            fprintf(stderr, "Skipping %s: invalid offset/size\n", default_file_names[i]);
            continue;
        }

        char outpath[4096];
        snprintf(outpath, sizeof(outpath), "%s/%s", dir_path, default_file_names[i]);

        FILE* out = fopen(outpath, "wb");
        if (!out) {
            fprintf(stderr, "Failed to create '%s': %s\n", outpath, strerror(errno));
            continue;
        }
        if (fwrite(content + corrected_offset, 1, file_size, out) != file_size) {
            fprintf(stderr, "Failed to write '%s'\n", outpath);
        }
        fclose(out);
    }

    free(content);
}

static void pack_pbp(const char* output_path, const char* input_paths[8]) {
    PBPHeader header;
    memset(&header, 0, sizeof(header));
    header.signature[0] = 0x00;
    header.signature[1] = 'P';
    header.signature[2] = 'B';
    header.signature[3] = 'P';
    header.version[0] = 0;
    header.version[1] = 1;

    unsigned char* contents[8] = { 0 };
    size_t sizes[8] = { 0 };

    uint32_t curr_offset = (uint32_t)sizeof(PBPHeader);
    for (size_t i = 0; i < 8; ++i) {
        header.offset[i] = curr_offset;
        if (input_paths[i] && strcmp(input_paths[i], "NULL") == 0) {
            contents[i] = NULL;
            sizes[i] = 0;
            continue;
        }
        size_t len = 0;
        unsigned char* buf = read_file_to_buffer(input_paths[i], &len);
        if (!buf) {
            for (size_t j = 0; j < i; ++j) free(contents[j]);
            fprintf(stderr, "Failed to read input file '%s'\n", input_paths[i]);
            exit(1);
        }
        contents[i] = buf;
        sizes[i] = len;
        curr_offset += (uint32_t)len;
    }

    FILE* out = fopen(output_path, "wb");
    if (!out) {
        for (size_t i = 0; i < 8; ++i) free(contents[i]);
        fprintf(stderr, "Failed to create output '%s': %s\n", output_path, strerror(errno));
        exit(1);
    }

    if (fwrite(&header, 1, sizeof(header), out) != sizeof(header)) {
        fclose(out);
        for (size_t i = 0; i < 8; ++i) free(contents[i]);
        print_error_and_exit("Failed to write header");
    }

    for (size_t i = 0; i < 8; ++i) {
        if (sizes[i] == 0) continue;
        if (fwrite(contents[i], 1, sizes[i], out) != sizes[i]) {
            fclose(out);
            for (size_t j = 0; j < 8; ++j) free(contents[j]);
            print_error_and_exit("Failed to write file contents");
        }
    }

    fclose(out);
    for (size_t i = 0; i < 8; ++i) free(contents[i]);
}

static void print_usage_and_exit(void) {
    fprintf(stderr, "Usage: pbptool <pack | unpack | analyze | help>\n");
    exit(1);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage_and_exit();
    }

    const char* cmd = argv[1];

    if (strcmp(cmd, "pack") == 0) {
        if (argc < 10) {
            fprintf(stderr, "Usage: pbptool pack <output.pbp> <param.sfo> <icon0.png> <icon1.pmf> <pic0.png> <pic1.png> <snd0.at3> <data.psp> <data.psar>\n");
            return 1;
        }
        const char* output = argv[2];
        const char* inputs[8];
        for (int i = 0; i < 8; ++i) inputs[i] = argv[3 + i];
        pack_pbp(output, inputs);
    }
    else if (strcmp(cmd, "unpack") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: pbptool unpack <input.pbp> <output_dir>\n");
            return 1;
        }
        unpack_pbp(argv[2], argv[3]);
    }
    else if (strcmp(cmd, "analyze") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: pbptool analyze <input.pbp>\n");
            return 1;
        }
        analyze_file(argv[2]);
    }
    else if (strcmp(cmd, "help") == 0) {
        printf("Usage: pbptool <pack | unpack | analyze | help>\n");
        return 0;
    }
    else {
        fprintf(stderr, "Error: Invalid argument '%s'\n", cmd);
        return 1;
    }

    return 0;
}
