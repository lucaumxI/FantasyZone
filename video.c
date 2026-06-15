/*
 * video.c — camada de video para Zybo Z7 (Zynq-7000, bare-metal) com saida VGA
 *
 * Pipeline no hardware (ver TUTORIAL_VIVADO.md):
 *   DDR -> AXI VDMA (read, 2 frame stores, stream 32b) -> subset conv 24b
 *       -> AXI4-Stream to Video Out (master) + VTC 640x480 fixo
 *       -> Slices 4b (R/G/B) + HS/VS -> Pmod VGA (JC + JD)
 *
 * Dois framebuffers na DDR; o jogo desenha no buffer "de tras" e video_flip()
 * faz flush de cache e aponta o VDMA (modo park) para o buffer pronto, com a
 * troca em fronteira de frame (sem tearing). video_wait_frame() trava em 60fps.
 */

#include <string.h>
#include "video.h"
#include "font.h"
#include "xaxivdma.h"
#include "xparameters.h"
#include "xil_cache.h"
#include "xtime_l.h"
#include "xstatus.h"

#ifndef VDMA_DEVICE_ID
#define VDMA_DEVICE_ID XPAR_AXI_VDMA_0_DEVICE_ID
#endif

static u32 fb[2][FRAME_PIXELS] __attribute__((aligned(64)));
static XAxiVdma vdma;
static int back = 0;
static XTime next_tick = 0;

u32 *video_backbuffer(void) { return fb[back]; }

int video_init(void)
{
    XAxiVdma_Config *cfg;
    XAxiVdma_DmaSetup rd;
    UINTPTR addr[2];
    int st;

    memset(fb, 0, sizeof(fb));
    Xil_DCacheFlushRange((UINTPTR)fb, sizeof(fb));

    cfg = XAxiVdma_LookupConfig(VDMA_DEVICE_ID);
    if (!cfg)
        return XST_FAILURE;
    st = XAxiVdma_CfgInitialize(&vdma, cfg, cfg->BaseAddress);
    if (st != XST_SUCCESS)
        return st;

    memset(&rd, 0, sizeof(rd));
    rd.VertSizeInput     = VID_H;
    rd.HoriSizeInput     = VID_W * 4;
    rd.Stride            = VID_W * 4;
    rd.FrameDelay        = 0;
    rd.EnableCircularBuf = 1;
    rd.EnableSync        = 0;
    rd.PointNum          = 0;
    rd.EnableFrameCounter = 0;
    rd.FixedFrameStoreAddr = 0;

    st = XAxiVdma_DmaConfig(&vdma, XAXIVDMA_READ, &rd);
    if (st != XST_SUCCESS)
        return st;

    addr[0] = (UINTPTR)fb[0];
    addr[1] = (UINTPTR)fb[1];
    st = XAxiVdma_DmaSetBufferAddr(&vdma, XAXIVDMA_READ, addr);
    if (st != XST_SUCCESS)
        return st;

    st = XAxiVdma_DmaStart(&vdma, XAXIVDMA_READ);
    if (st != XST_SUCCESS)
        return st;

    XAxiVdma_StartParking(&vdma, 0, XAXIVDMA_READ);
    back = 1;
    return XST_SUCCESS;
}

void video_flip(void)
{
    Xil_DCacheFlushRange((UINTPTR)fb[back], FRAME_BYTES);
    XAxiVdma_StartParking(&vdma, back, XAXIVDMA_READ);
    back ^= 1;
}

void video_wait_frame(void)
{
    XTime now;
    const XTime period = COUNTS_PER_SECOND / 60;

    if (next_tick == 0)
        XTime_GetTime(&next_tick);
    next_tick += period;

    XTime_GetTime(&now);
    if (now > next_tick + 4 * period) {
        next_tick = now;
        return;
    }
    while (now < next_tick)
        XTime_GetTime(&now);
}

/* ------------------------- primitivas ------------------------- */

void v_clear(u32 color)
{
    u32 *p = fb[back];
    int i;
    for (i = 0; i < FRAME_PIXELS; i++)
        p[i] = color;
}

void v_pixel(int x, int y, u32 color)
{
    if ((unsigned)x < VID_W && (unsigned)y < VID_H)
        fb[back][y * VID_W + x] = color;
}

void v_rect(int x, int y, int w, int h, u32 color)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > VID_W ? VID_W : x + w;
    int y1 = y + h > VID_H ? VID_H : y + h;
    int xx, yy;

    for (yy = y0; yy < y1; yy++) {
        u32 *row = &fb[back][yy * VID_W];
        for (xx = x0; xx < x1; xx++)
            row[xx] = color;
    }
}

void v_frame(int x, int y, int w, int h, int t, u32 color)
{
    v_rect(x, y, w, t, color);
    v_rect(x, y + h - t, w, t, color);
    v_rect(x, y, t, h, color);
    v_rect(x + w - t, y, t, h, color);
}

void v_circle(int cx, int cy, int r, u32 color)
{
    int dy;
    for (dy = -r; dy <= r; dy++) {
        int yy = cy + dy;
        int dx = 0, x0, x1, x;
        u32 *row;
        if ((unsigned)yy >= VID_H)
            continue;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r)
            dx++;
        x0 = cx - dx; if (x0 < 0) x0 = 0;
        x1 = cx + dx; if (x1 >= VID_W) x1 = VID_W - 1;
        row = &fb[back][yy * VID_W];
        for (x = x0; x <= x1; x++)
            row[x] = color;
    }
}

/* ------------------------- texto 5x7 ------------------------- */

static int glyph_idx(char c)
{
    int i;
    for (i = 0; FONT_MAP[i]; i++)
        if (FONT_MAP[i] == c)
            return i;
    return 10; /* espaco */
}

void v_text(int x, int y, int scale, u32 color, const char *s)
{
    for (; *s; s++) {
        int gi = glyph_idx(*s);
        int r, c;
        for (r = 0; r < 7; r++) {
            u8 bits = FONT5X7[gi][r];
            for (c = 0; c < 5; c++)
                if (bits & (0x10 >> c))
                    v_rect(x + c * scale, y + r * scale, scale, scale, color);
        }
        x += 6 * scale;
    }
}

int v_text_width(int scale, const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n * 6 * scale - scale;
}
