/* wav2psg.c - PSG voice data maker (PC tool, plain C99)
 *
 * Reproduces the 5 steps described in "PSG de Onsei wo Saisei suru Shikumi":
 *   1. read WAV samples as double in [-1.0, +1.0]   (0 = centre)
 *   2. peak-normalise
 *   3. resample to 8000 Hz with linear interpolation
 *   4. multiply by 4.0 and clip to [-1.0, +1.0]
 *   5. quantise to PSG volume steps 0..15:  n = round((v + 1) * 7.5)
 *
 * Supported input: WAV, mono or stereo (mixed to mono),
 *                  8/16/24/32-bit PCM or 32-bit float.
 * Output: a C header with one byte per sample (the PSG volume value itself).
 *
 * Usage:  wav2psg input.wav voice_data.h [gain]   (gain default = 4.0)
 * Build:  gcc -std=c99 -O2 -o wav2psg wav2psg.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define OUT_RATE 8000.0
#define DEFAULT_GAIN 4.0

/* ---------- tiny WAV reader (no external libraries) ---------- */
typedef struct {
    uint32_t rate;
    uint16_t channels;
    uint16_t bits;
    uint16_t audio_fmt;   /* 1 = PCM integer, 3 = IEEE float */
    double  *samples;     /* mono, in [-1, +1] */
    size_t   count;
} Wav;

static uint16_t rd16(const unsigned char *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int wav_load(const char *path, Wav *w) {
    FILE *fp = fopen(path, "rb");
    long size;
    unsigned char *buf;
    const unsigned char *data = NULL;
    uint32_t data_len = 0;
    size_t pos, i;

    if (!fp) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    fseek(fp, 0, SEEK_END); size = ftell(fp); fseek(fp, 0, SEEK_SET);
    buf = (unsigned char *)malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        fclose(fp); free(buf); return -1;
    }
    fclose(fp);

    if (size < 44 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "not a WAV file\n"); free(buf); return -1;
    }
    memset(w, 0, sizeof(*w));

    /* walk chunks: we need "fmt " and "data" */
    pos = 12;
    while (pos + 8 <= (size_t)size) {
        uint32_t len = rd32(buf + pos + 4);
        if (memcmp(buf + pos, "fmt ", 4) == 0) {
            w->audio_fmt = rd16(buf + pos + 8);
            w->channels  = rd16(buf + pos + 10);
            w->rate      = rd32(buf + pos + 12);
            w->bits      = rd16(buf + pos + 22);
        } else if (memcmp(buf + pos, "data", 4) == 0) {
            data = buf + pos + 8; data_len = len;
        }
        pos += 8 + len + (len & 1);
    }
    if (!data || w->channels == 0 || w->rate == 0) {
        fprintf(stderr, "missing fmt/data chunk\n"); free(buf); return -1;
    }

    {
        size_t bytes_per = w->bits / 8;
        size_t frames = data_len / (bytes_per * w->channels);
        w->samples = (double *)malloc(frames * sizeof(double));
        w->count = frames;
        for (i = 0; i < frames; i++) {
            double acc = 0.0;
            uint16_t ch;
            for (ch = 0; ch < w->channels; ch++) {
                const unsigned char *p = data + (i * w->channels + ch) * bytes_per;
                double v = 0.0;
                if (w->audio_fmt == 3 && w->bits == 32) {
                    float f; memcpy(&f, p, 4); v = f;
                } else if (w->bits == 24) {
                    int32_t s = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16));
                    if (s & 0x800000) s -= 0x1000000;
                    v = s / 8388608.0;            /* / 0x800000, step 1 */
                } else if (w->bits == 16) {
                    v = (int16_t)rd16(p) / 32768.0;
                } else if (w->bits == 32) {
                    v = (int32_t)rd32(p) / 2147483648.0;
                } else if (w->bits == 8) {
                    v = ((int)p[0] - 128) / 128.0;
                }
                acc += v;
            }
            w->samples[i] = acc / w->channels;
        }
    }
    free(buf);
    return 0;
}

int main(int argc, char **argv) {
    Wav w;
    double gain = DEFAULT_GAIN, peak = 0.0;
    size_t i, out_count;
    FILE *out;
    unsigned long clipped = 0, centre = 0;
    int used[16] = {0};

    if (argc < 3) {
        fprintf(stderr, "usage: %s input.wav voice_data.h [gain]\n", argv[0]);
        return 1;
    }
    if (argc >= 4) gain = atof(argv[3]);
    if (wav_load(argv[1], &w) != 0) return 1;

    /* step 2: peak normalise (the paper's material peaked at 0.2755) */
    for (i = 0; i < w.count; i++)
        if (fabs(w.samples[i]) > peak) peak = fabs(w.samples[i]);
    if (peak <= 0.0) { fprintf(stderr, "silent input\n"); return 1; }
    for (i = 0; i < w.count; i++) w.samples[i] /= peak;
    fprintf(stderr, "input: %u Hz, %zu samples, peak was %.4f\n",
            w.rate, w.count, peak);

    /* steps 3-5: resample (linear interp) -> gain+clip -> quantise */
    out_count = (size_t)((double)w.count * OUT_RATE / (double)w.rate + 0.5);
    out = fopen(argv[2], "w");
    if (!out) { perror(argv[2]); return 1; }
    fprintf(out, "/* generated by wav2psg: 8000 Hz, 1 byte = 1 PSG volume step */\n");
    fprintf(out, "#define VOICE_COUNT %zu\n", out_count);
    fprintf(out, "static const unsigned char voice_data[VOICE_COUNT] = {\n");
    for (i = 0; i < out_count; i++) {
        double src = (double)i * (double)w.rate / OUT_RATE;  /* i * 6 at 48k */
        size_t i0 = (size_t)src;
        size_t i1 = (i0 + 1 < w.count) ? i0 + 1 : i0;
        double frac = src - (double)i0;
        double v = w.samples[i0] * (1.0 - frac) + w.samples[i1] * frac;
        int n;
        v *= gain;                                            /* step 4 */
        if (v >  1.0) { v =  1.0; clipped++; }
        if (v < -1.0) { v = -1.0; clipped++; }
        n = (int)floor((v + 1.0) * 7.5 + 0.5);                /* step 5 */
        if (n < 0) n = 0;
        if (n > 15) n = 15;
        used[n] = 1;
        if (n == 7 || n == 8) centre++;
        fprintf(out, "%s%d%s", (i % 16 == 0) ? "    " : "",
                n, (i + 1 == out_count) ? "\n" : ",");
        if (i % 16 == 15 || i + 1 == out_count) fprintf(out, "\n");
    }
    fprintf(out, "};\n");
    fclose(out);

    {
        int kinds = 0, k;
        for (k = 0; k < 16; k++) kinds += used[k];
        fprintf(stderr,
            "output: %zu samples, centre(7/8) %.1f%%, clipped %.1f%%, steps used %d/16\n",
            out_count, 100.0 * centre / out_count,
            100.0 * clipped / out_count, kinds);
        fprintf(stderr,
            "paper at gain 4: centre 40.4%%, clipped 16.2%%, steps 16/16, RMS +11.4 dB vs gain 1\n");
    }
    free(w.samples);
    return 0;
}
