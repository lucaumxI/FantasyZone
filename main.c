#include "video.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>   /* abs() */
#include <string.h>
#include <math.h>     /* sinf — usado na senoide da loja (HOST_TEST only) */

#define HUD_H    32            /* Altura do HUD no topo da tela */
#define FIELD_Y  HUD_H         /* Ponto onde a area de jogo comeca */
#define MAX_ENEMIES 8          /* Limite de entidades */
#define MAX_PART    64         /* Limite de particulas para explosoes NÃO SEI SE VOU FAZER ISSO*/
#define INVULN_TIME 120        /* Tempo de invulnerabilidade ao tomar dano */
#define MAP_W 2540             // Tamanho do mapa
#define MAP_H 438
extern const u32 MAPA_COMPLETO[];   // Variavel pra armazenar o mapa

/* Mapeamento dos botão */
#define B_UP    0x1
#define B_DOWN  0x2
#define B_LEFT  0x4
#define B_RIGHT 0x8
#define B_SHOOT 0x10    // Precisa ver qual o endereço do botão 4-5 na Zybo
#define B_BOMB  0x20    // Botão da bomba (lançamento oblíquo)

/* Paleta de cores da interface */
#define C_HUD_BG    RGB(24, 18, 40)
#define C_HUD_LINE  RGB(255, 196, 64)
#define C_TEXT      RGB(238, 240, 255)
#define C_ACCENT    RGB(255, 214, 92)
#define C_TITLE     RGB(120, 220, 255)

/* Parametros do tiro normal*/
#define PLAYER_BULLET_W 5
#define PLAYER_BULLET_H 5
#define PLAYER_BULLET_VX 12
#define PLAYER_BULLET_VY 0
static int shoot_cooldown = 0;

/* Parametros do tiro "bomba" (lancamento obliquo / movimento de projetil)
 * A bomba sai da nave com uma velocidade horizontal (no sentido em que a nave
 * aponta) e uma velocidade vertical inicial pra cima. A cada frame a gravidade
 * soma em vy, fazendo a trajetoria descrever uma parabola. */
#define BOMB_W        7
#define BOMB_H        7
#define BOMB_VX       5     /* velocidade horizontal (modulo) */
#define BOMB_GRAVITY  1     /* aceleracao somada a vy a cada frame */
static int bomb_cooldown = 0;

/* Parametros da loja (circulo vermelho: desce reto ate o meio, depois onda pra direita) */
#define SHOP_R            20
#define SHOP_FALL_VY      2             /* velocidade de descida (pixels/frame) */
#define SHOP_WAVE_MID_Y   (VID_H / 2)  /* y onde troca de fase */
#define SHOP_WAVE_VX      1             /* deslocamento horizontal por frame na onda */
#define SHOP_WAVE_FREQ    0.1f        /* frequencia da senoide (lenta) */
#define SHOP_WAVE_AMP     35.0f         /* amplitude da senoide em pixels */
#define SHOP_SPAWN_FRAMES 420           /* ~7s entre aparicoes */
#define SHOP_FALL         0
#define SHOP_WAVE         1
/* Custo de cada item: BIG WINGS, WIDE BEAM, HEAVY BOMB */
static const int SHOP_COSTS[3] = {100, 300, 200};

/* Lógica das entidades */
typedef struct {
    s16 x, y;
    s16 w, h;
} Hitbox;

/* Jogador */
typedef struct {
    Hitbox box;
    s32 x_sub, y_sub;   // Pra ter aceleração gradual, usa subpixeis que incrementa a velocidade aos poucos
    s32 vx, vy;         // Velocidade em x e y
    u8 lives;
    u16 coins;
    u8 current_weapon;  // EM TEORIA VAI TER MAIS ARMAS MAS VAMOS VER COMO VAI SER O DESENVOLVIMENTO
    u32 color;          // eventualmente essa variavel explode
    u8 facing_left;
} Player;

/* Os Inimigos */
typedef struct {
    u8 active;          // Variavel pra dizer se o inimigo está vivo ou morto
    Hitbox box;
    s8 vx, vy;          // Velocidade em x e y
    u8 ai_type;         // Talvez eu faça MAIS de um inimigo, aqui serve pra dizer que inimigo é (muda apenas a movimentação deles, pra primeira fase teriam 4 se não me engano)
    u16 move_timer;     // Temporizador para peogramar o movimento dos inimigos (se sobe e desço isso decide quando para de descer, se é movimento de onda seria a fase)
    u16 score_value;    // Pontuação por matar esse inimigo
    u32 color;          // Essa também explode eventualmente
} Enemy;

/* Tipos de tiro */
enum { BULLET_NORMAL, BULLET_BOMB };

/* Tiros */
typedef struct {
    u8 active;      // Se ta ativou ou não
    u8 is_enemy;    // Se é um tiro inimigo ou do player
    u8 kind;        // BULLET_NORMAL (reto) ou BULLET_BOMB (lançamento oblíquo)
    u8 piercing;    // Se 1, atravessa inimigos sem desativar
    Hitbox box;
    s16 vx, vy;     // Velocidade em x e y (s16 pra suportar gravidade acumulada na bomba)
} Bullet;

#ifndef HOST_TEST
/* static XGpio btns; -> frescuras da zybo que to comentando pra não explodir o código, não faço ideia do que isso faz */
#endif

/* Loja flutuante */
typedef struct {
    u8    active;
    u8    phase;     /* SHOP_FALL ou SHOP_WAVE */
    s32   world_x;  /* coordenada do mundo (mesma origem que player.box.x) */
    s16   y;
    float t;         /* acumulador de tempo para sinf na fase WAVE */
} Shop;

/* Entidades do Jogo */
#define MAX_BULLETS 20
static Bullet bullets[MAX_BULLETS];
static Enemy  enemies[MAX_ENEMIES];
static Player player;
static Shop   shop;
static int    shop_spawn_timer = 0;

/* Variaveis de Estado */
static int  score, lives, coins;
static int  invuln;
static u32  rng;
static int g_btn      = 0;
static int g_btn_prev = 0;

/* Upgrades comprados na loja */
static u8  p_wings     = 0;   /* 0-3: cada nivel aumenta MAX_SPEED em 1 pixel/frame */
static u8  p_wide_shot = 0;   /* 0 ou 1: wide beam piercing */
static u8  p_big_bomb  = 0;   /* 0-3: cada nivel aumenta o tamanho da bomba */
static int shop_cursor = 0;   /* item selecionado na loja (0, 1 ou 2) */

enum { ST_TITLE, ST_PLAY, ST_SHOP, ST_GAMEOVER };
static int current_state = ST_TITLE;

/* O teto de velocidade da nave (aumentará ao comprar itens na loja) */
#define MAX_SPEED (4 << 8)

/* Aceleração por frame enquanto o botão é segurado */
#define ACCEL 32 

/* A perda de velocidade por frame quando soltamos o botão*/
#define FRICTION 48

static void draw_player(int x, int y, int w, int h) {   // Trocar depois por um loop iterando por um bitmap, da inclusive pra fazer uma funcao draw bitmap
    v_rect(x, y, w, h, RGB(50, 50, 50)); 
}

static void draw_enemy(int x, int y, int w, int h) {    // Aqui vai ter um switch case para cada inimigo
    v_rect(x, y, w, h, RGB(255, 255, 255));
}

static void draw_bullets(int camera_x) {
    int i;

    for (i = 0; i < MAX_BULLETS; i++) { 
        if (!bullets[i].active) {
            continue;
        }
        
        /* Converte a posicao do mundo para a posicao da tela */
        int tela_x = bullets[i].box.x - camera_x;

        /* Cor por tipo: normal=azul, wide=amarelo, bomba=laranja, bomba pesada=vermelho */
        u32 cor;
        if (bullets[i].kind == BULLET_BOMB)
            cor = bullets[i].piercing ? RGB(255, 50, 0) : RGB(255, 140, 0);
        else
            cor = bullets[i].piercing ? RGB(255, 255, 0) : RGB(0, 0, 200);
        v_rect(tela_x, bullets[i].box.y, bullets[i].box.w, bullets[i].box.h, cor);
    }
}

static void update_player(void) {
    /* Velocidade maxima cresce com cada asa comprada */
    int cur_max = (4 + p_wings) << 8;

    /* Eixo X */
    if (g_btn & B_RIGHT) {
        player.vx += ACCEL;
        player.facing_left = 0;
        if (player.vx > cur_max) player.vx = cur_max;
    }
    else if (g_btn & B_LEFT) {
        player.vx -= ACCEL;
        player.facing_left = 1;
        if (player.vx < -cur_max) player.vx = -cur_max;
    }
    else {
        /* Atrito */
        if (player.vx > FRICTION) player.vx -= FRICTION;
        else if (player.vx < -FRICTION) player.vx += FRICTION;
        else player.vx = 0;
    }

    /* --- EIXO Y --- */
    if (g_btn & B_DOWN) {
        player.vy += ACCEL;
        if (player.vy > cur_max) player.vy = cur_max;
    }
    else if (g_btn & B_UP) {
        player.vy -= ACCEL;
        if (player.vy < -cur_max) player.vy = -cur_max;
    }
    else {
        if (player.vy > FRICTION) player.vy -= FRICTION;
        else if (player.vy < -FRICTION) player.vy += FRICTION;
        else player.vy = 0;
    }

    /* Atualiza o sub-pixel */
    player.x_sub += player.vx;
    player.y_sub += player.vy;

    /* Converte para as coordenadas da tela */
    player.box.x = player.x_sub >> 8;
    player.box.y = player.y_sub >> 8;
}

static void init_player(void) {     // Inicializa o jogador
    player.box.x = 100;
    player.box.y = 100;
    player.box.w = 16; 
    player.box.h = 16;
    player.x_sub = 100 << 8; 
    player.y_sub = 100 << 8;
    player.vx = 0;
    player.vy = 0;
    player.color = RGB(50, 255, 50);
    player.lives = 3;
    player.coins = 9999;  /* placeholder: ganho de moedas por inimigos ainda nao implementado */
    player.facing_left = 0;
    p_wings = 0; p_wide_shot = 0; p_big_bomb = 0; shop_cursor = 0;
}

static int check_aabb(Hitbox a, Hitbox b) {     // Checa hitbox usando o AABB (não há colisão se o objeto A está completamente a esquerda, direita, cima ou baixo)
    return (a.x < b.x + b.w &&
            a.x + a.w > b.x &&
            a.y < b.y + b.h &&
            a.y + a.h > b.y);
}

static void check_collisions(void) {        // Função para testar colisão com todas entidades
    int i, j;

    if (invuln > 0) {
        invuln--;
    }

    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            continue; 
        }

        /* Colisão Player - Enemy */
        if (invuln == 0 && check_aabb(player.box, enemies[i].box)) {
            player.lives--;
            enemies[i].active = 0; /* Destroi o inimigo no impacto */
            invuln = 120;
            continue; 
        }
        /* Colisão Bullet - Enemy */
        for (j = 0; j < MAX_BULLETS; j++){
            if (!bullets[j].active || bullets[j].is_enemy)
                continue;
            if(check_aabb(bullets[j].box, enemies[i].box)){
                enemies[i].active = 0;
                if (!bullets[j].piercing)
                    bullets[j].active = 0;  /* piercing continua ativo p/ proximos inimigos */
                // + score. TODO
                break;
            }
        }
    }
}

static void update_enemies(void){

}

static void init_bullet(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].active == 0) {
            bullets[i].active   = 1;
            bullets[i].is_enemy = 0;
            bullets[i].kind     = BULLET_NORMAL;
            bullets[i].box.w    = PLAYER_BULLET_W;

            if (p_wide_shot) {
                /* Wide Beam: tiro alto que atravessa inimigos */
                bullets[i].box.h    = 60;
                bullets[i].piercing = 1;
                bullets[i].box.y    = (s16)(player.box.y + player.box.h/2 - 30);
            } else {
                bullets[i].box.h    = PLAYER_BULLET_H;
                bullets[i].piercing = 0;
                bullets[i].box.y    = player.box.y;
            }

            bullets[i].box.x = player.box.x;
            if (player.facing_left) {
                bullets[i].vx = -(PLAYER_BULLET_VX);
                bullets[i].vy = -(PLAYER_BULLET_VY);
            } else {
                bullets[i].vx = PLAYER_BULLET_VX;
                bullets[i].vy = PLAYER_BULLET_VY;
            }
            break;
        }
    }
}

/* Cria uma bomba: lançamento oblíquo a partir da frente da nave.
 * vy começa negativo (sobe) e a gravidade em update_bullets() faz a parábola. */
static void init_bomb(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].active == 0) {
            int extra = p_big_bomb * 10;   /* cada nivel adiciona 10px de tamanho */
            bullets[i].active   = 1;
            bullets[i].is_enemy = 0;
            bullets[i].kind     = BULLET_BOMB;
            bullets[i].piercing = (p_big_bomb > 0) ? 1 : 0;
            bullets[i].box.w    = (s16)(BOMB_W + extra);
            bullets[i].box.h    = (s16)(BOMB_H + extra);

            /* Sai da frente da nave, centrada verticalmente */
            if (player.facing_left) {
                bullets[i].box.x = (s16)(player.box.x - bullets[i].box.w);
                bullets[i].vx    = -BOMB_VX;
            } else {
                bullets[i].box.x = (s16)(player.box.x + player.box.w);
                bullets[i].vx    = BOMB_VX;
            }
            bullets[i].box.y = (s16)(player.box.y + player.box.h/2 - bullets[i].box.h/2);
            bullets[i].vy    = 0;
            break;
        }
    }
}


static void update_bullets(void) {
    if (shoot_cooldown > 0) {
        shoot_cooldown--;
    }
    if (bomb_cooldown > 0) {
        bomb_cooldown--;
    }

    if ((g_btn & B_SHOOT) && (shoot_cooldown == 0)) {
        init_bullet();
        shoot_cooldown = 10; /* Espera 10 frames (~160ms) para permitir o proximo tiro */
    }
    if ((g_btn & B_BOMB) && (bomb_cooldown == 0)) {
        init_bomb();
        bomb_cooldown = 30; /* Bomba recarrega mais devagar que o tiro normal */
    }

    for (int i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].active) {
            /* Lançamento oblíquo: a gravidade puxa a bomba pra baixo a cada frame,
             * curvando a trajetória numa parábola. O tiro normal não sofre gravidade. */
            if (bullets[i].kind == BULLET_BOMB) {
                bullets[i].vy += BOMB_GRAVITY;
            }

            /* Movimento */
            bullets[i].box.x += bullets[i].vx;
            bullets[i].box.y += bullets[i].vy;

            /* Desativa se sair muito longe na horizontal */
            if (abs(bullets[i].box.x - player.box.x) > 800) {
                bullets[i].active = 0;
                continue;
            }
            /* ...ou se a bomba cair pra fora da tela (por baixo) */
            if (bullets[i].box.y > VID_H) {
                bullets[i].active = 0;
            }
        }
    }
}



static u32 rng_next(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void spawn_shop(void) {
    shop.active      = 1;
    shop_spawn_timer = 0;
    shop.phase       = SHOP_FALL;
    shop.world_x     = player.box.x;   /* aparece no centro da tela */
    shop.y           = (s16)(FIELD_Y + SHOP_R);
    shop.t           = 0.0f;
}

static void update_shop(void) {
    if (!shop.active) return;

    if (shop.phase == SHOP_FALL) {
        /* Fase 1: desce em linha reta ate o meio da tela */
        shop.y += SHOP_FALL_VY;
        if (shop.y >= SHOP_WAVE_MID_Y) {
            shop.y     = SHOP_WAVE_MID_Y;
            shop.phase = SHOP_WAVE;
        }
    } else {
        /* Fase 2: desloca para a direita com y oscilando em senoide */
        shop.world_x += SHOP_WAVE_VX;
        shop.t       += SHOP_WAVE_FREQ;
        shop.y        = (s16)(SHOP_WAVE_MID_Y + (int)(SHOP_WAVE_AMP * sinf(shop.t)));

        /* Sumiu da tela pela direita (ou esquerda se o jogador foi rapido) */
        int cam = (player.x_sub >> 8) - 320;
        int sx  = (int)shop.world_x - cam;
        if (sx > VID_W + SHOP_R || sx < -SHOP_R) {
            shop.active      = 0;
            shop_spawn_timer = 0;
            return;
        }
    }

    /* Colisao com o jogador (ambos em coordenadas do mundo) */
    Hitbox shop_box = {
        (s16)(shop.world_x - SHOP_R),
        (s16)(shop.y       - SHOP_R),
        2 * SHOP_R, 2 * SHOP_R
    };
    if (check_aabb(player.box, shop_box)) {
        shop.active      = 0;
        shop_spawn_timer = 0;
        current_state    = ST_SHOP;
    }
}

static void draw_shop(int camera_x) {
    if (!shop.active) return;
    int sx = (int)shop.world_x - camera_x;
    int sy = (int)shop.y;
    v_circle(sx, sy, SHOP_R, RGB(220, 30, 30));
    int tw = v_text_width(1, "SHOP");
    v_text(sx - tw / 2, sy - 3, 1, RGB(255, 255, 255), "SHOP");
}

static void draw_shop_ui(void) {
    int i;
    static const int  bx[3]              = {35, 230, 425};
    static const char * const nomes[3]   = {"BIG WINGS",  "WIDE BEAM",    "HEAVY BOMB"};
    static const char * const nomes2[3]  = {"ASA",        "TIRO LARGO",   "BOMBA PESADA"};
    int by = 75, bw = 180, bh = 210;

    /* Background */
    v_rect (25, 20, 590, 430, RGB(8, 6, 28));
    v_frame(25, 20, 590, 430, 2, RGB(255, 196, 64));

    /* Titulo centralizado */
    {
        const char *title = "LOJA DE PECAS";
        v_text(320 - v_text_width(2, title) / 2, 32, 2, RGB(255, 214, 92), title);
    }

    for (i = 0; i < 3; i++) {
        int sel = (i == shop_cursor);
        v_rect (bx[i], by, bw, bh, sel ? RGB(22, 22, 70) : RGB(12, 12, 42));
        v_frame(bx[i], by, bw, bh, 2,   sel ? RGB(255, 196, 64) : RGB(55, 55, 100));

        int cx = bx[i] + bw / 2;
        int iy = by + 15;   /* topo da area do icone */

        /* --- Icones desenhados com primitivas --- */
        if (i == 0) {
            /* BIG WINGS: nave estilizada com asas */
            v_rect(cx-22, iy+30, 44,  9, RGB(80, 180, 255));   /* fuselagem */
            v_rect(cx-22, iy+18, 16, 12, RGB(60, 150, 220));   /* asa esq */
            v_rect(cx+6,  iy+18, 16, 12, RGB(60, 150, 220));   /* asa dir */
            v_rect(cx+20, iy+31, 10,  7, RGB(140, 220, 255));  /* bico */
        } else if (i == 1) {
            /* WIDE BEAM: feixe largo amarelo */
            v_rect(cx-50, iy+28, 18, 12, RGB(80, 180, 255));   /* nau */
            v_rect(cx-32, iy+22, 82, 26, RGB(200, 200, 40));   /* feixe externo */
            v_rect(cx-32, iy+27, 82, 16, RGB(255, 255, 180));  /* brilho interno */
        } else {
            /* HEAVY BOMB: bomba grande vermelha */
            v_rect  (cx-3,  iy+8,  6, 30, RGB(160, 100, 40));  /* cabo */
            v_rect  (cx-9,  iy+6, 18,  6, RGB(160, 100, 40));  /* cima do cabo */
            v_circle(cx, iy+52, 22,        RGB(200,  25, 25));  /* esfera */
            v_rect  (cx-12, iy+41, 8,  9, RGB(255, 100, 100)); /* brilho */
        }

        /* --- Nomes --- */
        v_text(bx[i]+8, by+90,  1, RGB(255, 255, 255), nomes[i]);
        v_text(bx[i]+8, by+102, 1, RGB(160, 200, 160), nomes2[i]);

        /* --- Status --- */
        {
            char status[20];
            u32  sc;
            if (i == 0) {
                if (p_wings >= 3) { snprintf(status, sizeof(status), "NIVEL MAXIMO"); sc = RGB(150,150,60); }
                else              { snprintf(status, sizeof(status), "NV %d DE 3", p_wings); sc = RGB(160,220,160); }
            } else if (i == 1) {
                if (p_wide_shot) { snprintf(status, sizeof(status), "ATIVO");   sc = RGB(80,255,80); }
                else             { snprintf(status, sizeof(status), "INATIVO"); sc = RGB(200,100,100); }
            } else {
                if (p_big_bomb >= 3) { snprintf(status, sizeof(status), "NIVEL MAXIMO"); sc = RGB(150,150,60); }
                else                 { snprintf(status, sizeof(status), "NV %d DE 3", p_big_bomb); sc = RGB(160,220,160); }
            }
            v_text(bx[i]+8, by+118, 1, sc, status);
        }

        /* --- Custo --- */
        {
            int maxed = (i == 0 && p_wings    >= 3) ||
                        (i == 1 && p_wide_shot)     ||
                        (i == 2 && p_big_bomb >= 3);
            int can_buy = !maxed && (int)player.coins >= SHOP_COSTS[i];
            char cost_str[20];
            if (maxed) snprintf(cost_str, sizeof(cost_str), "JA COMPRADO");
            else        snprintf(cost_str, sizeof(cost_str), "CUSTO: %d", SHOP_COSTS[i]);
            v_text(bx[i]+8, by+136, 1, can_buy ? RGB(255,196,64) : RGB(110,75,75), cost_str);
        }

        /* --- Indicador de selecao --- */
        if (sel) {
            const char *sel_txt = "SELECIONADO";
            v_text(bx[i] + bw/2 - v_text_width(1, sel_txt)/2, by+158, 1, RGB(255,255,100), sel_txt);
        }
    }

    /* Moedas */
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "MOEDAS: %d", (int)player.coins);
        v_text(35, 300, 1, RGB(255, 196, 64), buf);
    }
    v_text(35, 318, 1, RGB(140, 140, 210), "Z: COMPRAR   X: SAIR   SETAS: NAVEGAR");
}

static void draw_background(int camera_x) {
    /* Pega o ponteiro do frame atual direto do motor de video */
    u32 *draw_buffer = video_backbuffer(); 

    int y;
    for (y = FIELD_Y; y < 480; y++) {
        
        int map_y = y - FIELD_Y;
        if (map_y >= MAP_H) break; 

        int map_x = camera_x % MAP_W;
        if (map_x < 0) {
            map_x += MAP_W;
        }

        /* O resto continua igualzinho: */
        u32 *tela_linha = &draw_buffer[y * 640];
        const u32 *mapa_linha = &MAPA_COMPLETO[map_y * MAP_W];

        /* Logica da dobra do Memcpy */
        if (map_x + 640 <= MAP_W) {
            memcpy(tela_linha, &mapa_linha[map_x], 640 * sizeof(u32));
        } else {
            int pixels_restantes_direita = MAP_W - map_x;
            int pixels_dobrados_esquerda = 640 - pixels_restantes_direita;
            
            memcpy(tela_linha, &mapa_linha[map_x], pixels_restantes_direita * sizeof(u32));
            memcpy(&tela_linha[pixels_restantes_direita], &mapa_linha[0], pixels_dobrados_esquerda * sizeof(u32));
        }
    }
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    int running = 1;
    SDL_Event event;
    rng = 12345u;

    video_init();

    while(running) {
        int just_pressed;
        g_btn_prev   = g_btn;

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
                    
                    /* Mapeia a tecla Z para disparar o bit do tiro */
                    case SDLK_z:     mask = B_SHOOT; break;
                    /* Mapeia a tecla X para a bomba (lançamento oblíquo) */
                    case SDLK_x:     mask = B_BOMB;  break;
                }
                
                if (is_down) g_btn |= mask;
                else         g_btn &= ~mask;
            }
        }
        just_pressed = g_btn & ~g_btn_prev;  /* botoes que foram pressionados NESTE frame */

        // Lógica
        switch (current_state){
            case ST_TITLE:
                if (g_btn & B_UP) {
                    init_player();
                    shop.active      = 0;
                    shop_spawn_timer = 0;
                    current_state    = ST_PLAY;
                }
                break;
            
            case ST_PLAY:
                update_player();
                update_enemies();
                update_bullets();
                check_collisions();

                if (!shop.active) {
                    shop_spawn_timer++;
                    if (shop_spawn_timer >= SHOP_SPAWN_FRAMES)
                        spawn_shop();
                }
                update_shop();

                if (player.lives <= 0)
                    current_state = ST_GAMEOVER;
                break;

            case ST_SHOP:
                if (just_pressed & B_LEFT)  shop_cursor = (shop_cursor + 2) % 3;
                if (just_pressed & B_RIGHT) shop_cursor = (shop_cursor + 1) % 3;
                if (just_pressed & B_SHOOT) {
                    int maxed = (shop_cursor == 0 && p_wings    >= 3) ||
                                (shop_cursor == 1 && p_wide_shot)     ||
                                (shop_cursor == 2 && p_big_bomb >= 3);
                    if (!maxed && (int)player.coins >= SHOP_COSTS[shop_cursor]) {
                        player.coins -= (u16)SHOP_COSTS[shop_cursor];
                        if      (shop_cursor == 0) p_wings++;
                        else if (shop_cursor == 1) p_wide_shot = 1;
                        else                       p_big_bomb++;
                    }
                }
                if (just_pressed & B_BOMB) {
                    shop_spawn_timer = 0;
                    current_state    = ST_PLAY;
                }
                break;
            
            case ST_GAMEOVER:
                if (g_btn & B_UP) { 
                    current_state = ST_TITLE;
                }
                break;
        }

        v_clear(RGB(0, 0, 0)); /* Limpa o frame */
        
        switch (current_state){
            case ST_TITLE:
                v_text(100, 100, 3, RGB(120, 220, 255), "FANTASY ZONE");
                v_text(100, 200, 1, RGB(255, 255, 255), "Aperte CIMA para Iniciar");
                break;
            
            case ST_PLAY: {
                int camera_x = (player.x_sub >> 8) - 320;
                draw_background(camera_x);

                int tela_x = player.box.x - camera_x;
                draw_player(tela_x, player.box.y, player.box.w, player.box.h);
                /* draw_enemies(); */
                draw_bullets(camera_x);
                draw_shop(camera_x);
                /* HUD*/
                char hud[20];
                snprintf(hud, sizeof(hud), "VIDAS: %d", player.lives);

                v_text(10, 10, 1, RGB(255, 255, 255), hud);
                break;
            }

            case ST_SHOP:
                draw_shop_ui();
                break;

            case ST_GAMEOVER:
                v_text(200, 200, 3, RGB(255, 50, 50), "GAME OVER");
                break;
        
        }

        video_flip();
        video_wait_frame();
    }
    
    SDL_Quit();
    return 0;
}