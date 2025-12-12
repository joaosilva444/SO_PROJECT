#include "board.h"
#include "display.h"
#include "files.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/types.h>

// Códigos de Saída
#define EXIT_RESTORE 10
#define EXIT_GAME_OVER 11

// Variável Global para controlar Saves
int has_active_save = 0;

// Estrutura auxiliar para passar argumentos às threads dos fantasmas
typedef struct {
    board_t* board;
    int id; // Índice do fantasma
} thread_arg_t;

void lock_all_rows(board_t* board) {
    // Ordem crescente obrigatória
    for (int i = 0; i < board->height; i++) {
        pthread_mutex_lock(&board->row_locks[i]);
    }
}

void unlock_all_rows(board_t* board) {
    for (int i = 0; i < board->height; i++) {
        pthread_mutex_unlock(&board->row_locks[i]);
    }
}
void screen_refresh(board_t * game_board, int mode) {
    if (mode == DRAW_MENU) lock_all_rows(game_board);
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if (mode == DRAW_MENU) unlock_all_rows(game_board);
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

// ==================================================================
// THREAD DO FANTASMA
// ==================================================================
void* ghost_thread(void* arg) {
    thread_arg_t* params = (thread_arg_t*)arg;
    board_t* board = params->board;
    int ghost_idx = params->id;
    free(params); // Libertar memória do argumento

    debug("[THREAD GHOST %d] Iniciada.\n", ghost_idx);
    ghost_t* self = &board->ghosts[ghost_idx];
    
    while (board->game_running) {
        // 1. Simular velocidade (Sleep fora do lock!)
        int sleep_time = (board->tempo > 0) ? board->tempo : 100;
        sleep_ms(sleep_time);

        // CORREÇÃO: Removido lock_all_rows daqui. O move_ghost trata dos locks.

        // Verificar se jogo acabou enquanto dormia
        if (!board->game_running) {
            break;
        }

        // 3. Mover
        command_t cmd;
        if (self->n_moves > 0) {
            cmd = self->moves[self->current_move % self->n_moves];
            move_ghost(board, ghost_idx, &cmd);
        } else {
            // Movimento aleatório se não houver ficheiro
            char opts[] = {'W','A','S','D'};
            cmd.command = opts[rand() % 4];
            move_ghost(board, ghost_idx, &cmd);
        }
    }
    return NULL;
}

// ==================================================================
// THREAD DO PACMAN
// ==================================================================
void* pacman_thread(void* arg) {
    board_t* board = (board_t*)arg;
    pacman_t* self = &board->pacmans[0];
    debug("[THREAD PACMAN] Iniciada.\n");

    while (board->game_running) {
        // Sleep pequeno para não "queimar" CPU
        sleep_ms(10); 

        // CORREÇÃO: Removido lock_all_rows daqui. O move_pacman trata dos locks.

        if (!board->game_running) {
            break;
        }

        command_t cmd;
        int moved = 0;

        // Prioridade A: Comando Manual (vindo da Main Thread)
        if (board->next_pacman_cmd != '\0') {
            cmd.command = board->next_pacman_cmd;
            cmd.turns = 1;
            board->next_pacman_cmd = '\0'; // Limpar comando
            
            int result = move_pacman(board, 0, &cmd);
            moved = 1;

            if (result == REACHED_PORTAL) {
                board->exit_status = 1; // Vitória
                board->game_running = 0;
            } else if (result == DEAD_PACMAN) {
                board->exit_status = 2; // Morte
                board->game_running = 0;
            }
        }
    // Prioridade B: Modo Automático (Ficheiro)
        else if (self->n_moves > 0) {
             cmd = self->moves[self->current_move % self->n_moves];

             // --- TRATAMENTO DE COMANDOS ESPECIAIS (G e Q) ---
             
             // Caso 1: SAVE (G)
             if (cmd.command == 'G') {
                 if (!has_active_save) { // Só pede save se nao houver um ativo
                     board->save_request = 1; 
                 }
                 self->current_move++;    
                 sleep_ms(50);            
                 continue; 
             }
             
             // Caso 2: QUIT (Q)
             if (cmd.command == 'Q') {
                 board->exit_status = 3;  // Código de saída 3 = QUIT
                 board->game_running = 0; // Para todas as threads
                 break; // Sai imediatamente do while da thread
             }
             // -----------------------------------------------

             int result = move_pacman(board, 0, &cmd);
             moved = 1;

             if (result == REACHED_PORTAL) {
                 board->exit_status = 1;
                 board->game_running = 0;
             } else if (result == DEAD_PACMAN) {
                 board->exit_status = 2;
                 board->game_running = 0;
             }
        }

        // Verificação passiva (se um fantasma me matou no turno dele)
        if (!self->alive && board->game_running) {
             board->exit_status = 2; 
             board->game_running = 0;
        }

        // Se houve movimento automático, esperar o TEMPO do jogo
        if (moved && self->n_moves > 0) sleep_ms(board->tempo);
    }
    return NULL;
}

// ==================================================================
// MAIN (UI THREAD)
// ==================================================================
int main(int argc, char** argv) {
    if (argc < 2) { printf("Usage: %s <dir>\n", argv[0]); return 1; }

    char* dir_path = argv[1];
    struct dirent **namelist;
    int n = scandir(dir_path, &namelist, filter_levels, alphasort);
    if (n < 0) { perror("scandir"); return 1; }

    srand(time(NULL));
    open_debug_file("debug.log");
    terminal_init();
    
    board_t game_board;
    int accumulated_points = 0;
    has_active_save = 0;

    for (int i = 0; i < n; i++) {
        if (load_level(&game_board, dir_path, namelist[i]->d_name, accumulated_points) != 0) {
            free(namelist[i]); continue;
        }

        // --- INICIALIZAÇÃO ---
        
        pthread_t p_thread;
        pthread_t g_threads[MAX_GHOSTS];

        // 1. Criar Threads
        pthread_create(&p_thread, NULL, pacman_thread, &game_board);
        for(int g=0; g < game_board.n_ghosts; g++) {
            thread_arg_t* args = malloc(sizeof(thread_arg_t));
            args->board = &game_board;
            args->id = g;
            pthread_create(&g_threads[g], NULL, ghost_thread, args);
        }

        screen_refresh(&game_board, DRAW_MENU);

        // --- LOOP PRINCIPAL (UI & INPUT) ---
        while (game_board.game_running) {
            
            // 1. Desenhar (Usando helper para bloquear estado)
            screen_refresh(&game_board, DRAW_MENU);

            // 2. Input
            char input = get_input();

            // =======================================================
            // LÓGICA DE SAVE (G) - TECLADO OU FICHEIRO
            // =======================================================
            if ((input == 'G' || game_board.save_request) && has_active_save == 0) {
                
                game_board.save_request = 0; // Limpar bandeira

                // 1. BLOQUEAR O PAI (STOP THE WORLD)
                lock_all_rows(&game_board);
                
                pid_t pid = fork();

                if (pid < 0) {
                    perror("Erro fork");
                    unlock_all_rows(&game_board);
                }
                else if (pid > 0) {
                    // === PROCESSO PAI (Wait & Freeze) ===
                    
                    int status;
                    waitpid(pid, &status, 0); 

                    // O Filho terminou. O Pai acorda.
                    
                    if (WIFEXITED(status)) {
                        int exit_code = WEXITSTATUS(status);
                        
                        if (exit_code == EXIT_RESTORE) {
                            // Restaurar
                            has_active_save = 0;
                            clear(); refresh();

                            // Soltamos as threads do Pai para continuarem do ponto 'G'
                            unlock_all_rows(&game_board);
                            
                            continue; // Volta ao início do loop
                        }
                        else if (exit_code == EXIT_GAME_OVER) {
                            // Quit no filho
                            game_board.exit_status = 3;
                            game_board.game_running = 0;
                        }
                    }
                    // Libertar o lock se não for restore
                    unlock_all_rows(&game_board);
                }
                else {
                    // === PROCESSO FILHO (Jogo Ativo) ===
                    
                    // O filho herda o mutex TRANCADO. Destrancar IMEDIATAMENTE.
                    unlock_all_rows(&game_board);
                    
                    has_active_save = 1;

                    // Reiniciar ncurses para o filho (CRÍTICO)
                    nodelay(stdscr, TRUE);
                    keypad(stdscr, TRUE);
                    
                    // Recriar as threads no filho (apenas a main sobreviveu ao fork)
                    pthread_create(&p_thread, NULL, pacman_thread, &game_board);
                    for(int g=0; g < game_board.n_ghosts; g++) {
                        thread_arg_t* args = malloc(sizeof(thread_arg_t));
                        args->board = &game_board;
                        args->id = g;
                        pthread_create(&g_threads[g], NULL, ghost_thread, args);
                    }
                }
            }
            // =======================================================
            // LÓGICA DE QUIT (Q)
            // =======================================================
            else if (input == 'Q') {
                lock_all_rows(&game_board);
                game_board.exit_status = 3; 
                game_board.game_running = 0;
                unlock_all_rows(&game_board);
                
                if (has_active_save) exit(EXIT_GAME_OVER);
            } 
            // =======================================================
            // INPUT DE MOVIMENTO (WASD)
            // =======================================================
            else if (input != '\0') {
                game_board.next_pacman_cmd = input;
            }

            sleep_ms(33); // ~30 FPS UI Update
        }

        // --- FIM DO NÍVEL / JOGO ---
        
        pthread_join(p_thread, NULL);
        for(int g=0; g < game_board.n_ghosts; g++) {
            pthread_join(g_threads[g], NULL);
        }
        
        int status = game_board.exit_status;

        // SE SOU FILHO E MORRI -> AVISAR PAI
        if (status == 2 && has_active_save) {
            exit(EXIT_RESTORE);
        }

        if (status == 1) { // VITÓRIA
            screen_refresh(&game_board, DRAW_WIN);
            sleep_ms(1000);
            accumulated_points = game_board.pacmans[0].points;
            unload_level(&game_board);
            free(namelist[i]);
            clear(); refresh();
        }
        else { 
            // DERROTA ou QUIT
            if (status == 2) {
                screen_refresh(&game_board, DRAW_GAME_OVER);
                sleep_ms(2000);
            }
            
            unload_level(&game_board);
            free(namelist[i]);
            break; // Sai do loop de níveis
        }
    }
    
    // Limpeza final
    free(namelist);
    terminal_cleanup();
    close_debug_file();
    return 0;
}