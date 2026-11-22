// Headless mGBA test harness: plays a ROM with scripted button presses and saves screenshots.
// Optional developer tool (the game does not need it). Build with tools/harness/build_harness.sh.
//
// Usage: gbaharness <rom.gba> <script.txt> <out_dir> [--sav file.sav]
//
// Script commands, one per line (# starts a comment):
//   wait N                 run N frames with no buttons held
//   hold KEYS N            hold KEYS (e.g. A, A+RIGHT, START) for N frames
//   tap KEYS               hold KEYS for 3 frames, then release for 3 frames
//   shot NAME              save the current screen to out_dir/NAME.png
//   mash N SEED            press random buttons for N frames (crash/softlock fuzzing; never presses L/R)
//   peek ADDR              print the byte at ADDR (hex)
// Key names: A B SELECT START RIGHT LEFT UP DOWN R L
// The game's BN_LOG / mGBA debug output is printed to stdout, prefixed with "LOG:".

#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>
#include <mgba-util/png-io.h>
#include <mgba-util/vfs.h>

#include <ctype.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_errors = 0;

static void log_cb(struct mLogger* logger, int category, enum mLogLevel level, const char* format, va_list args) {
    (void) logger;
    const char* cat = mLogCategoryName(category);
    int is_game = cat && strcmp(cat, "GBA Debug") == 0;
    if (!is_game && !(level & (mLOG_FATAL | mLOG_ERROR))) {
        return;
    }
    if (!is_game) {
        ++g_errors;
    }
    printf(is_game ? "LOG: " : "EMU %s: ", is_game ? "" : cat);
    vprintf(format, args);
    printf("\n");
}

static uint32_t parse_keys(const char* s) {
    static const char* names[] = { "A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L" };
    uint32_t keys = 0;
    char buf[128];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    for (char* tok = strtok(buf, "+"); tok; tok = strtok(NULL, "+")) {
        int found = 0;
        for (int i = 0; i < 10; ++i) {
            if (strcasecmp(tok, names[i]) == 0) {
                keys |= 1u << i;
                found = 1;
            }
        }
        if (!found && strcasecmp(tok, "NONE") != 0) {
            fprintf(stderr, "Unknown key: %s\n", tok);
            exit(2);
        }
    }
    return keys;
}

static struct mCore* core;
static color_t* framebuffer;
static unsigned width, height;
static long frame_no = 0;

static void run(uint32_t keys, int frames) {
    core->setKeys(core, keys);
    for (int i = 0; i < frames; ++i) {
        core->runFrame(core);
        ++frame_no;
    }
}

static void shot(const char* dir, const char* name) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.png", dir, name);
    struct VFile* vf = VFileOpen(path, O_CREAT | O_TRUNC | O_WRONLY);
    if (!vf) {
        fprintf(stderr, "Can't write %s\n", path);
        return;
    }
    png_structp png = PNGWriteOpen(vf);
    png_infop info = PNGWriteHeader(png, width, height);
    PNGWritePixels(png, width, height, width, framebuffer);
    PNGWriteClose(png, info);
    vf->close(vf);
    printf("SHOT: %s (frame %ld)\n", path, frame_no);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s rom.gba script.txt out_dir [--sav file.sav]\n", argv[0]);
        return 2;
    }
    const char* rom = argv[1];
    const char* script = argv[2];
    const char* outdir = argv[3];
    const char* sav = NULL;
    for (int i = 4; i < argc - 1; ++i) {
        if (strcmp(argv[i], "--sav") == 0) {
            sav = argv[i + 1];
        }
    }

    static struct mLogger logger = { .log = log_cb };
    mLogSetDefaultLogger(&logger);

    core = mCoreFind(rom);
    if (!core || !core->init(core)) {
        fprintf(stderr, "Can't load core for %s\n", rom);
        return 1;
    }
    mCoreInitConfig(core, NULL);
    core->desiredVideoDimensions(core, &width, &height);
    framebuffer = calloc(width * height, sizeof(color_t));
    core->setVideoBuffer(core, framebuffer, width);
    if (!mCoreLoadFile(core, rom)) {
        fprintf(stderr, "Can't load ROM %s\n", rom);
        return 1;
    }
    core->reset(core);

    if (sav) {
        FILE* f = fopen(sav, "rb");
        if (f) {
            static unsigned char buf[0x20000];
            size_t n = fread(buf, 1, sizeof(buf), f);
            fclose(f);
            core->savedataRestore(core, buf, n, true);
            printf("Loaded save %s (%zu bytes)\n", sav, n);
        }
    }

    FILE* sf = fopen(script, "r");
    if (!sf) {
        fprintf(stderr, "Can't open script %s\n", script);
        return 1;
    }
    char line[512];
    while (fgets(line, sizeof(line), sf)) {
        char* hash = strchr(line, '#');
        if (hash) *hash = 0;
        char cmd[64] = { 0 }, a1[256] = { 0 }, a2[64] = { 0 };
        int n = sscanf(line, "%63s %255s %63s", cmd, a1, a2);
        if (n <= 0) continue;
        if (strcmp(cmd, "wait") == 0) {
            run(0, atoi(a1));
        } else if (strcmp(cmd, "hold") == 0) {
            run(parse_keys(a1), atoi(a2));
        } else if (strcmp(cmd, "tap") == 0) {
            run(parse_keys(a1), 3);
            run(0, 3);
        } else if (strcmp(cmd, "shot") == 0) {
            shot(outdir, a1);
        } else if (strcmp(cmd, "mash") == 0) {
            int frames = atoi(a1);
            srand(n > 2 ? (unsigned) atoi(a2) : 1);
            for (int i = 0; i < frames; i += 4) {
                run((uint32_t) rand() & 0xFF, 4);
            }
        } else if (strcmp(cmd, "peek") == 0) {
            uint32_t addr = (uint32_t) strtoul(a1, NULL, 16);
            printf("PEEK %08X = %u\n", addr, core->rawRead8(core, addr, -1));
        } else {
            fprintf(stderr, "Unknown command: %s\n", cmd);
            return 2;
        }
    }
    fclose(sf);

    if (sav) {
        void* data = NULL;
        size_t size = core->savedataClone(core, &data);
        if (size && data) {
            FILE* f = fopen(sav, "wb");
            fwrite(data, 1, size, f);
            fclose(f);
            free(data);
            printf("Wrote save %s (%zu bytes)\n", sav, size);
        } else {
            printf("No save data written by the game\n");
        }
    }
    printf("DONE: %ld frames, %d emulator errors\n", frame_no, g_errors);
    core->deinit(core);
    return 0;
}
