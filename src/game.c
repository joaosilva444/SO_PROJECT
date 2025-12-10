#include "board.h"
#include "display.h"
#include "files.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/types.h>


// Estrutura auxiliar para passar argumentos às threads
typedef struct {
    board_t* board;
    int id; // Índice do fantasma ou do pacman
} thread_arg_t;

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

// ------------------------------------------------------------
// THREAD DO FANTASMA
// ------------------------------------------------------------
void* ghost_thread(void* arg) {
    thread_arg_t* params = (thread_arg_t*)arg;
    board_t* board = params->board;
    int ghost_idx = params->id;
    ghost_t* self = &board->ghosts[ghost_idx];
    
    free(params); // Libertar a estrutura auxiliar

    while (board->game_running) {
        // 1. Simular o tempo de espera (velocidade do monstro)
        // Se o monstro tiver ficheiro, usa o 'PASSO' ou o tempo do board
        int sleep_time = (board->tempo > 0) ? board->tempo * 10 : 100; // *10 para ser visível
        sleep_ms(sleep_time);

        // 2. Proteção Crítica (Mutex)
        // Ninguém mexe no tabuleiro enquanto este monstro decide o movimento
        pthread_mutex_lock(&board->board_lock);

        // Verificar se jogo ainda corre depois de acordar
        if (!board->game_running) {
            pthread_mutex_unlock(&board->board_lock);
            break;
        }

        // 3. Calcular e Executar Movimento
        command_t cmd;
        if (self->n_moves > 0) {
            // Modo Automático (Ficheiro)
            cmd = self->moves[self->current_move % self->n_moves];
            move_ghost(board, ghost_idx, &cmd);
        } else {
            // Modo Aleatório (Se não tiver ficheiro)
            cmd.command = "WASD"[rand() % 4];
            move_ghost(board, ghost_idx, &cmd);
        }

        // 4. Libertar Mutex
        pthread_mutex_unlock(&board->board_lock);
    }
    return NULL;
}

// ------------------------------------------------------------
// THREAD DO PACMAN
// ------------------------------------------------------------
void* pacman_thread(void* arg) {
    board_t* board = (board_t*)arg;
    pacman_t* self = &board->pacmans[0];

    while (board->game_running) {
        // Pacman espera um pouco menos para ser responsivo, ou usa o tempo do jogo
        sleep_ms(10); 

        pthread_mutex_lock(&board->board_lock);

        if (!board->game_running) {
            pthread_mutex_unlock(&board->board_lock);
            break;
        }

        command_t cmd;
        int moved = 0;

        // Prioridade 1: Comando do Teclado (vindo da Main Thread)
        if (board->next_pacman_cmd != '\0') {
            cmd.command = board->next_pacman_cmd;
            cmd.turns = 1;
            board->next_pacman_cmd = '\0'; // Consumir comando
            move_pacman(board, 0, &cmd);
            moved = 1;
        }
        // Prioridade 2: Modo Automático (Ficheiro) se não houver teclado
        else if (self->n_moves > 0) {
            // Pequeno delay para o automático não ser instantâneo
            // (Na prática deveria ter lógica de timing melhor, mas serve para o exemplo)
             cmd = self->moves[self->current_move % self->n_moves];
             move_pacman(board, 0, &cmd);
             moved = 1;
        }

        // Verificar colisões ou morte após movimento
        if (!self->alive) {
            board->game_running = 0; // Sinalizar fim de jogo a todos
        }

        pthread_mutex_unlock(&board->board_lock);
        
        // Se foi movimento automático, dormir um pouco
        if (moved && self->n_moves > 0) sleep_ms(board->tempo);
    }
    return NULL;
}

// ------------------------------------------------------------
// MAIN (UI THREAD)
// ------------------------------------------------------------
int main(int argc, char** argv) {
    if (argc < 2) { printf("Usage: %s <dir>\n", argv[0]); return 1; }

    // ... (Código de scandir igual ao anterior) ...
    char* dir_path = argv[1];
    struct dirent **namelist;
    int n = scandir(dir_path, &namelist, filter_levels, alphasort);
    if (n < 0) return 1;

    srand(time(NULL));
    terminal_init();
    
    board_t game_board;
    int accumulated_points = 0;

    for (int i = 0; i < n; i++) {
        if (load_level(&game_board, dir_path, namelist[i]->d_name, accumulated_points) != 0) {
            free(namelist[i]); continue;
        }

        // --- INICIALIZAR THREADS ---
        pthread_t p_thread;
        pthread_t g_threads[MAX_GHOSTS];

        // 1. Criar Thread Pacman
        pthread_create(&p_thread, NULL, pacman_thread, &game_board);

        // 2. Criar Threads Fantasmas
        for(int g=0; g < game_board.n_ghosts; g++) {
            thread_arg_t* args = malloc(sizeof(thread_arg_t));
            args->board = &game_board;
            args->id = g;
            pthread_create(&g_threads[g], NULL, ghost_thread, args);
        }

        // --- LOOP PRINCIPAL (INTERFACE) ---
        // Apenas desenha e lê input. Não move lógica.
        
        int level_result = 0;
        draw_board(&game_board, DRAW_MENU);

        while (game_board.game_running) {
            // 1. Desenhar (Protegido por Mutex para leitura consistente)
            pthread_mutex_lock(&game_board.board_lock);
            draw_board(&game_board, DRAW_MENU);
            
            // Verificar condições de vitória/derrota dentro do lock
            if (!game_board.pacmans[0].alive) {
                game_board.game_running = 0;
                level_result = 2; // QUIT/DIE
            }
            // Verificar se ganhou (ex: portal) - Adaptar move_pacman para setar flag
            // Por simplicidade, assuma que move_pacman gere isto ou verifique portal aqui
            pthread_mutex_unlock(&game_board.board_lock);

            refresh_screen();

            // 2. Ler Input (Non-blocking)
            char input = get_input();
            if (input == 'Q') {
                game_board.game_running = 0;
                level_result = 2;
            }
            else if (input != '\0') {
                // Passar input para a thread do Pacman processar
                game_board.next_pacman_cmd = input;
            }

            // 3. Frame Rate (~30 FPS)
            sleep_ms(33); 
        }

        // --- JOIN THREADS (Esperar que acabem) ---
        pthread_join(p_thread, NULL);
        for(int g=0; g < game_board.n_ghosts; g++) {
            pthread_join(g_threads[g], NULL);
        }

        if (level_result == 2) { // Game Over
            screen_refresh(&game_board, DRAW_GAME_OVER);
            sleep_ms(2000);
            break; 
        }
        
        // Acumular pontos e limpar
        accumulated_points = game_board.pacmans[0].points;
        unload_level(&game_board);
        free(namelist[i]);
    }
    
    free(namelist);
    terminal_cleanup();
    return 0;
}