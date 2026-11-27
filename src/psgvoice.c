/* psgvoice.c - PSGVOICE.COM : voice playback on MSX PSG (MSX-DOS / Ese-DOS)
 *
 * Plays the bundled sample voice ("a i u e o. konnichiwa, PSG desu.",
 * AI-synthesised for this project, no third-party recording) through
 * the PSG volume register used as a 4-bit DAC.
 *
 * Method (see slides/ and README):
 *   - Mixer R7 = 0xBF turns every tone/noise generator OFF, so the
 *     channel-A volume register R8 becomes a 16-step D/A converter.
 *   - voice_data[] (8000 Hz, 1 byte = 1 volume step) is streamed into
 *     R8 at a constant cycle interval by a small inline-asm loop.
 *   - The wait count W is calibrated at startup.
 *
 * Calibration note: the original article (Voich, KAKAO SOFT) times
 * the loop with JIFFY. Two measured pitfalls on WebMSX led this
 * sample to count VDP vertical-blank flags under DI instead:
 *   - with interrupts enabled, the per-frame BIOS ISR itself costs
 *     ~14,000 cycles on WebMSX (its PSG I/O is heavy), inflating the
 *     measured cost by ~22 cycles/sample (631 samples/frame);
 *   - a chunk is 512 samples (about 0.67 frame at W=1), so one
 *     status read per chunk can never skip a frame boundary.
 *     The measured cost on WebMSX (~83 cycles at W=1) already
 *     includes that environment's heavier PSG output instruction,
 *     which is exactly what the calibration is for. W is rounded
 *     to the nearest integer (truncation cost a full step of 16
 *     cycles, about 3.6% of the sample interval).
 *
 * Build (z88dk):
 *   zcc +msx -create-app -O3 -subtype=msxdos -DMSX psgvoice.c -bn PSGVOICE.COM
 */
#include <stdio.h>
#include <intrinsic.h>   /* intrinsic_di() / intrinsic_ei() */

#include "voice_data.h"  /* VOICE_COUNT + voice_data[], made by wav2psg */

/* ---- hand-off variables: file scope (Z80 has no stack-relative load;
 *      a fixed address costs one instruction per access) ------------- */
static const unsigned char *voice_ptr;
static unsigned int         voice_left;
static unsigned char        voice_wait = 24;
static unsigned char        vdp_seen_tmp;
static unsigned int         calib_spf, calib_cost;  /* for the report */

static const unsigned char silence_data[512] = { 8 };

/* ---- playback loop: 61 fixed cycles + (16*W - 5) wait per sample --- */
static void voice_run(void)
{
#asm
    ld de,(_voice_ptr)
    ld bc,(_voice_left)
    ld a,8                  ; R8 = channel A volume
    out ($a0),a             ; select once; selection is latched
.ksd_vloop
    ld a,(de)               ;  7
    inc de                  ;  6
    out ($a1),a             ; 11  voltage changes here
    ld a,(_voice_wait)      ; 13
.ksd_vwait
    dec a                   ;  4
    jr nz,ksd_vwait         ; 12 (last turn 7: hence 16*W - 5)
    dec bc                  ;  6
    ld a,b                  ;  4
    or c                    ;  4
    jp nz,ksd_vloop         ; 10
#endasm
}

/* ---- one PSG register write, outside the hot loop ------------------- */
static unsigned char psg_reg_tmp, psg_val_tmp;
static void psg_write_g(void)
{
#asm
    ld a,(_psg_reg_tmp)
    out ($a0),a
    ld a,(_psg_val_tmp)
    out ($a1),a
#endasm
}
static void psg_set(unsigned char reg, unsigned char val)
{
    psg_reg_tmp = reg; psg_val_tmp = val; psg_write_g();
}

/* ---- VDP status #0 bit 7 (vertical blank). Reading clears the flag,
 *      so one read per chunk counts at most one frame boundary. ------ */
static void vdp_read_status(void)
{
#asm
    in a,($99)
    and $80
    ld (_vdp_seen_tmp),a
#endasm
}

/* ---- calibration: run the real loop on silence for 30 frames and
 *      back-solve W. Measured with W=1, so cost already contains
 *      16*1 - 5 = 11 cycles of wait:
 *        16*W = 447 - (cost - 11) + 5 = 463 - cost
 *      Runs under DI, in exactly the playback conditions. ----------- */
static void voice_calibrate(void)
{
    unsigned int samples = 0, frames = 0, spf, cost;
    int w;

    voice_wait = 1;
    vdp_read_status();              /* clear any pending flag */
    while (frames < 30) {
        voice_ptr  = silence_data;
        voice_left = 512;
        voice_run();
        samples += 512;
        vdp_read_status();
        if (vdp_seen_tmp) frames++;
    }
    spf  = samples / frames;        /* samples per frame */
    cost = 59659 / spf;             /* cycles per sample (3579545/60) */
    w = (463 - (int)cost + 8) / 16;   /* round to nearest, not down */
    if (w < 1)  w = 1;              /* slow machine: fastest possible */
    if (w > 60) w = 60;
    voice_wait = (unsigned char)w;
    calib_spf = spf; calib_cost = cost;   /* printed in main */
}

int main(void)
{
    printf("PSGVOICE - PSG volume register as a 4-bit DAC\r\n");
    printf("sample: \"a i u e o. konnichiwa, PSG desu.\" (%u samples)\r\n",
           (unsigned)VOICE_COUNT);

    intrinsic_di();                 /* BIOS ISR touches the PSG */
    psg_set(7, 0xBF);               /* mixer: all tone + noise OFF */
    voice_calibrate();
    voice_ptr  = voice_data;
    voice_left = VOICE_COUNT;
    voice_run();
    psg_set(8, 0);                  /* volume 0 */
    psg_set(7, 0xB8);               /* mixer back to the usual state:
                                       tones on, noise off, port A in */
    intrinsic_ei();

    printf("calibration: %u samples/frame, cost %u, W=%u\r\n",
           calib_spf, calib_cost, (unsigned)voice_wait);
    printf("done.\r\n");
    return 0;
}
