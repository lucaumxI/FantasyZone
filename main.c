#include "video.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

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

/* Tiros */
typedef struct {
    u8 active;      // Se ta ativou ou não
    u8 is_enemy;    // Se é um tiro inimigo ou do player
    Hitbox box;
    s8 vx, vy;      // Velocidade em x e y
} Bullet;

#ifndef HOST_TEST
/* static XGpio btns; -> frescuras da zybo que to comentando pra não explodir o código, não faço ideia do que isso faz */
#endif

/* Entidades do Jogo */
#define MAX_BULLETS 20
static Bullet bullets[MAX_BULLETS];
static Enemy enemies[MAX_ENEMIES];
static Player player;

/* Variaveis de Estado */
static int  score, lives, coins;
static int  invuln;
static u32  rng;
static int g_btn = 0;

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
        
        /* Desenha o retangulo azul na coordenada corrigida */
        v_rect(tela_x, bullets[i].box.y, bullets[i].box.w, bullets[i].box.h, RGB(0, 0, 200));
    }
}

static void update_player(void) {
    /* Eixo X */
    if (g_btn & B_RIGHT) {
        player.vx += ACCEL;
        if (player.vx > MAX_SPEED) player.vx = MAX_SPEED;
    } 
    else if (g_btn & B_LEFT) {
        player.vx -= ACCEL;
        if (player.vx < -MAX_SPEED) player.vx = -MAX_SPEED;
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
    player.coins = 0;
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
        for (int j = 0; j < MAX_BULLETS; j++){
            if (!bullets[j].active || bullets[j].is_enemy)
                continue;
            if(check_aabb(bullets[j].box, enemies[i].box)){
                enemies[i].active = 0;
                bullets[j].active = 0;
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
            bullets[i].active = 1;
            bullets[i].is_enemy = 0;
            bullets[i].box.x = player.box.x;
            bullets[i].box.y = player.box.y;
            bullets[i].box.w = PLAYER_BULLET_W;
            bullets[i].box.h = PLAYER_BULLET_H;
            
            /* No futuro, o if (player.facing_left) entra aqui alterando o sinal de vx */
            bullets[i].vx = PLAYER_BULLET_VX; 
            bullets[i].vy = PLAYER_BULLET_VY;
            break; 
        }
    }
}


static void update_bullets(void) {
    if (shoot_cooldown > 0) {
        shoot_cooldown--;
    }
    
    if ((g_btn & B_SHOOT) && (shoot_cooldown == 0)) {
        init_bullet();
        shoot_cooldown = 10; /* Espera 10 frames (~160ms) para permitir o proximo tiro */
    }

    for (int i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].active) { 
            /* Movimento */
            bullets[i].box.x += bullets[i].vx;
            bullets[i].box.y += bullets[i].vy;
            
            /* A logica para desativar ao sair da tela entrara aqui depois */
        }
    }
}



static void draw_background(int camera_x){ // Itera um vetor gigantesco escrevendo pixel por pixel, se ter gargalo aqui é a primeira função a ser modificada
    int x, y;
    
    for (y = FIELD_Y; y < 480; y++) {
        int map_y = y - FIELD_Y;
        if (map_y >= MAP_H) break; 

        for (x = 0; x < 640; x++) {
            int map_x = (camera_x + x) % MAP_W;
            
            if (map_x < 0) {
                map_x += MAP_W; 
            }

            /* Extrai a cor do mapa */
            u32 pixel_color = MAPA_COMPLETO[map_y * MAP_W + map_x];
            
            /* Envia diretamente para o motor de video */
            v_pixel(x, y, pixel_color);
        }
    }
}

int main(void) {
    int running = 1;
    SDL_Event event;
    rng = 12345u;

    video_init();

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
                    
                    /* Mapeia a tecla Z para disparar o bit do tiro */
                    case SDLK_z:     mask = B_SHOOT; break; 
                }
                
                if (is_down) g_btn |= mask;
                else         g_btn &= ~mask;
            }
        }
        // Lógica
        switch (current_state){
            case ST_TITLE:
                if (g_btn & B_UP) { 
                    init_player();
                    current_state = ST_PLAY;
                }
                break;
            
            case ST_PLAY:
                update_player();
                update_enemies();
                update_bullets();
                check_collisions();
                
                if (player.lives <= 0) {
                    current_state = ST_GAMEOVER;
                }
    
                if (g_btn & B_RIGHT && player.box.x > 300) { // trocar depois para check colisions com o bloco da loja
                    current_state = ST_SHOP;
                }
                break;

            case ST_SHOP:
                /* Logica de comprar itens e descontar moedas entraria aqui */
                
                /* Para sair da loja */
                if (g_btn & B_LEFT) { 
                    current_state = ST_PLAY;
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
            
            case ST_PLAY:
                int camera_x = (player.x_sub >> 8) - 320;
                draw_background(camera_x);
                
                int tela_x = player.box.x - camera_x;
                draw_player(tela_x, player.box.y, player.box.w, player.box.h);
                /* draw_enemies(); */
                draw_bullets(camera_x);
                /* HUD*/
                char hud[20];
                snprintf(hud, sizeof(hud), "VIDAS: %d", player.lives);
                
                v_text(10, 10, 1, RGB(255, 255, 255), hud);
                break;

            case ST_SHOP:
                v_rect(50, 50, 540, 380, RGB(20, 20, 50)); /* Fundo da loja */
                v_text(100, 80, 2, RGB(255, 214, 92), "LOJA DE PEÇAS");
                v_text(100, 150, 1, RGB(255, 255, 255), "Aperte ESQUERDA para Sair");
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