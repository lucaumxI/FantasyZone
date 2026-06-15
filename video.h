#ifndef VIDEO_H
#define VIDEO_H

#ifndef HOST_TEST
/* Se estiver compilando para a Zybo Z7, usa a biblioteca da Xilinx */
#include "xil_types.h"
#else
/* Se estiver compilando para o PC, usa o padrao C e recria os tipos */
#include <stdint.h>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
#endif

/* Resolucao fixa do pipeline (VTC configurado em 640x480p no Vivado) */
#define VID_W        640
#define VID_H        480
#define FRAME_PIXELS (VID_W * VID_H)
#define FRAME_BYTES  (FRAME_PIXELS * 4)

/*
 * Saida VGA via Pmod (RGB 4-4-4). No hardware, o vid_data[23:0] do bloco
 * "AXI4-Stream to Video Out" e fatiado para os 4 bits altos de cada canal:
 *   VGA_R = vid_data[23:20], VGA_G = vid_data[15:12], VGA_B = vid_data[7:4].
 * Portanto aqui empacotamos no formato intuitivo 0x00RRGGBB.
 * Se vermelho/azul aparecerem trocados no monitor, troque as fatias R e B
 * no Block Design (ou inverta este macro) — ver tutorial, secao Troubleshooting.
 */
#define RGB(r, g, b) ((((u32)(r)) << 16) | (((u32)(g)) << 8) | ((u32)(b)))

int  video_init(void);
u32 *video_backbuffer(void);
void video_flip(void);
void video_wait_frame(void);

/* primitivas (desenham no backbuffer, com clipping) */
void v_clear(u32 color);
void v_pixel(int x, int y, u32 color);
void v_rect(int x, int y, int w, int h, u32 color);
void v_frame(int x, int y, int w, int h, int t, u32 color); /* moldura de espessura t */
void v_circle(int cx, int cy, int r, u32 color);

/* texto 5x7 escalado */
void v_text(int x, int y, int scale, u32 color, const char *s);
int  v_text_width(int scale, const char *s);

#endif
