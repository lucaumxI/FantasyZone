#include <SDL2/SDL.h>
#include <string.h>
#include "video.h"
#include "font.h"

/* O mesmo framebuffer do seu hardware, mas agora na RAM do PC */
static u32 fb[2][FRAME_PIXELS];
static int back = 0;

static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;
static SDL_Texture* texture = NULL;

u32 *video_backbuffer(void) { return fb[back]; }

int video_init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return -1;
    
    window = SDL_CreateWindow("Emulador Zybo Z7 - Fantasy Zone", 
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
                              VID_W, VID_H, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    
    /* O formato ARGB8888 casa perfeitamente com a macro RGB() do seu video.h */
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, 
                                SDL_TEXTUREACCESS_STREAMING, VID_W, VID_H);
    memset(fb, 0, sizeof(fb));
    return 0;
}

void video_flip(void)   // Usa double buffer
{
    /* Copia os dados do seu framebuffer de C para a placa de vídeo do PC */
    SDL_UpdateTexture(texture, NULL, fb[back], VID_W * sizeof(u32));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
    back ^= 1;
}

void video_wait_frame(void)
{
    /* Simula a trava de 60 FPS (1000ms / 60 = 16.6ms) */
    SDL_Delay(16);
}

 /* ------------------------- primitivas ------------------------- */
void v_clear(u32 color) // Preenche todo o frame com uma única cor
{
    u32 *p = fb[back];
    int i;
    for (i = 0; i < FRAME_PIXELS; i++)
        p[i] = color;
}

void v_pixel(int x, int y, u32 color)   // Desenha um único ponto de cor color nas coordenadas (x, y)
{
    if ((unsigned)x < VID_W && (unsigned)y < VID_H)
        fb[back][y * VID_W + x] = color;
}

void v_rect(int x, int y, int w, int h, u32 color)  // Desenha um retangulo maciço de cor color nas coordenadas (x, y)
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

void v_frame(int x, int y, int w, int h, int t, u32 color)  //Desenha apenas a borda de um retangulo
{
    v_rect(x, y, w, t, color);
    v_rect(x, y + h - t, w, t, color);
    v_rect(x, y, t, h, color);
    v_rect(x + w - t, y, t, h, color);
}

void v_circle(int cx, int cy, int r, u32 color) // Desenha um circulo
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

static int glyph_idx(char c)    // Pega um caracter em ASCII e transforma em bitmap para escrever
{
    int i;
    for (i = 0; FONT_MAP[i]; i++)
        if (FONT_MAP[i] == c)
            return i;
    return 10; /* espaco */
}

void v_text(int x, int y, int scale, u32 color, const char *s)  // Desenha uma string inteira na tela
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

int v_text_width(int scale, const char *s)  // Calculo das dimensões da frase inteira escrita
{
    int n = 0;
    while (s[n])
        n++;
    return n * 6 * scale - scale;
}
