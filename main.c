#include "video.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

#define HUD_H    32            /* Altura do HUD no topo da tela */
#define FIELD_Y  HUD_H         /* Ponto onde a area de jogo comeca */
#define MAX_ENEMIES 8          /* Limite de entidades */
#define MAX_PART    64         /* Limite de particulas para explosoes */
#define INVULN_TIME 100        /* Tempo de invulnerabilidade ao tomar dano */

/* Mapeamento de hardware */
#define B_UP    0x1
#define B_DOWN  0x2
#define B_LEFT  0x4
#define B_RIGHT 0x8

/* A maquina de estados global do jogo */
enum { ST_TITLE, ST_PLAY, ST_DYING, ST_CLEAR, ST_GAMEOVER };

/* Paleta de cores da interface */
#define C_HUD_BG    RGB(24, 18, 40)
#define C_HUD_LINE  RGB(255, 196, 64)
#define C_TEXT      RGB(238, 240, 255)
#define C_ACCENT    RGB(255, 214, 92)
#define C_TITLE     RGB(120, 220, 255)

/* 1. A Base Geometrica */
typedef struct {
    s16 x, y;
    s16 w, h;
} Hitbox;

/* 2. O Jogador */
typedef struct {
    Hitbox box;
    s32 x_sub, y_sub;
    s32 vx, vy;
    u8 lives;
    u16 coins;
    u8 current_weapon;
    u32 color;
} Player;

/* 3. Os Inimigos */
typedef struct {
    u8 active;
    Hitbox box;
    s8 vx, vy;
    u8 ai_type;       
    u16 move_timer;   
    u16 score_value;
    u32 color;
} Enemy;

/* 4. Os Tiros */
typedef struct {
    u8 active;
    u8 is_enemy;      
    Hitbox box;
    s8 vx, vy;
} Bullet;

#ifndef HOST_TEST
/* static XGpio btns; -> Comentado por enquanto para nao dar erro se XGpio nao estiver no video.h */
#endif

/* Double Buffering e Background */
static u32 bgbuf[FRAME_PIXELS] __attribute__((aligned(64)));
static int scroll_x = 0; 

/* Entidades do Jogo */
#define MAX_BULLETS 20
static Bullet bullets[MAX_BULLETS];
static Enemy enemies[MAX_ENEMIES];
static Player player;

/* Variaveis de Estado */
static int  score, lives, level, coins;
static int  state, state_timer, invuln;
static int  frame, prev_btn;
static u32  rng;
static int  debounced_state; 
static int g_btn = 0;

/* 1. O teto de velocidade da nave (aumentará ao comprar motores na loja) */
#define MAX_SPEED (4 << 8)

/* 2. O ganho de velocidade por frame enquanto o botão é segurado */
#define ACCEL 32 

/* 3. A perda de velocidade por frame quando soltamos o botão (Inércia) */
#define FRICTION 48

static void draw_player(int x, int y, int w, int h) {   // Trocar depois por um loop iterando por um bitmap, da inclusive pra fazer uma funcao draw bitmap
    v_rect(x, y, w, h, RGB(50, 50, 50)); 
}

static void draw_enemy(int x, int y, int w, int h) {    // Aqui vai ter um switch case para cada inimigo
    v_rect(x, y, w, h, RGB(255, 255, 255));
}

static void update_player(void) {
    /* --- EIXO X --- */
    if (g_btn & B_RIGHT) {
        player.vx += ACCEL;
        if (player.vx > MAX_SPEED) player.vx = MAX_SPEED;
    } 
    else if (g_btn & B_LEFT) {
        player.vx -= ACCEL;
        if (player.vx < -MAX_SPEED) player.vx = -MAX_SPEED;
    } 
    else {
        /* Atrito (Friction) com limite de parada para nao tremer */
        if (player.vx > FRICTION) player.vx -= FRICTION;
        else if (player.vx < -FRICTION) player.vx += FRICTION;
        else player.vx = 0; /* Para completamente */
    }

    /* --- EIXO Y --- */
    if (g_btn & B_DOWN) {
        player.vy += ACCEL;
        if (player.vy > MAX_SPEED) player.vy = MAX_SPEED;
    } 
    else if (g_btn & B_UP) {
        player.vy -= ACCEL;
        if (player.vy < -MAX_SPEED) player.vy = -MAX_SPEED;
    } 
    else {
        if (player.vy > FRICTION) player.vy -= FRICTION;
        else if (player.vy < -FRICTION) player.vy += FRICTION;
        else player.vy = 0;
    }

    /* 1. Atualiza a matematica cega do sub-pixel */
    player.x_sub += player.vx;
    player.y_sub += player.vy;

    /* 2. Converte para a matriz da tela apagando as "casas decimais" (>> 8) */
    player.box.x = player.x_sub >> 8;
    player.box.y = player.y_sub >> 8;
}

static void init_player(void) {
    player.box.x = 100;
    player.box.y = 100;
    player.box.w = 16; /* Aumentei um pouco para ficar visivel */
    player.box.h = 16;
    player.x_sub = 100 << 8; 
    player.y_sub = 100 << 8;
    player.vx = 0;
    player.vy = 0;
    player.color = RGB(50, 255, 50); /* Verde para destacar no fundo preto */
    player.lives = 3;
}

static int check_aabb(Hitbox a, Hitbox b) {
    return (a.x < b.x + b.w &&
            a.x + a.w > b.x &&
            a.y < b.y + b.h &&
            a.y + a.h > b.y);
}

static void check_collisions(void) {
    int i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        
        if (!enemies[i].active) {
            continue; 
        }

        /* Passa as caixas fisicas para a funcao matematica */
        if (check_aabb(player.box, enemies[i].box)) {
            player.lives--;
            enemies[i].active = 0; /* Destroi o inimigo no impacto */
            break; 
        }
    }
}

int main(void) {
    int running = 1;
    SDL_Event event;
    rng = 12345u;

    video_init();
    init_player();

    /* Cria o inimigo real na memoria */
    enemies[0].active = 1;
    enemies[0].box.x = 200;
    enemies[0].box.y = 200;
    enemies[0].box.w = 15;
    enemies[0].box.h = 15;

    while(running) {
        /* Processa eventos da janela (permite fechar no X) */
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                int is_down = (event.type == SDL_KEYDOWN);
                int mask = 0;
                
                /* Mapeia as setas do PC para os bits dos botoes da Zybo */
                switch (event.key.keysym.sym) {
                    case SDLK_UP:    mask = B_UP;    break;
                    case SDLK_DOWN:  mask = B_DOWN;  break;
                    case SDLK_LEFT:  mask = B_LEFT;  break;
                    case SDLK_RIGHT: mask = B_RIGHT; break;
                }
                
                if (is_down) g_btn |= mask;
                else         g_btn &= ~mask;
            }
        }
        v_clear(RGB(0, 0, 0)); /* Limpa o frame */
        update_player();
        
        draw_player(player.box.x, player.box.y, player.box.w, player.box.h);
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (enemies[i].active) {
                draw_enemy(enemies[i].box.x, enemies[i].box.y, enemies[i].box.w, enemies[i].box.h);
            }
        }

        check_collisions();
        char texto_vidas[32];
        /* Monta a string formatada (%d é substituido pelo valor da variavel) */
        snprintf(texto_vidas, sizeof(texto_vidas), "VIDAS: %d", player.lives);

        /* Imprime na tela. O '2' é a escala da fonte. */
        v_text(10, 10, 2, RGB(255, 255, 255), texto_vidas);
        

        video_flip();
        video_wait_frame();
    }
    
    SDL_Quit();
    return 0;
}