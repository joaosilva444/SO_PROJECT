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

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
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

        // 2. Bloquear Tabuleiro
        pthread_mutex_lock(&board->board_lock);

        // Verificar se jogo acabou enquanto dormia
        if (!board->game_running) {
            pthread_mutex_unlock(&board->board_lock);
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

        // 4. Desbloquear
        pthread_mutex_unlock(&board->board_lock);
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

        pthread_mutex_lock(&board->board_lock);

        if (!board->game_running) {
            pthread_mutex_unlock(&board->board_lock);
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
                 board->save_request = 1; // Ativa a flag criada no passo anterior
                 self->current_move++;    // Avança para não ficar preso no G
                 pthread_mutex_unlock(&board->board_lock); 
                 sleep_ms(50);            // Dá tempo à Main para processar
                 continue; 
             }
             
             // Caso 2: QUIT (Q)
             if (cmd.command == 'Q') {
                 board->exit_status = 3;  // Código de saída 3 = QUIT
                 board->game_running = 0; // Para todas as threads
                 pthread_mutex_unlock(&board->board_lock);
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

        pthread_mutex_unlock(&board->board_lock);
        
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

        // --- INICIALIZAÇÃO PARA THREADS ---
        pthread_mutex_init(&game_board.board_lock, NULL);
        game_board.game_running = 1;
        game_board.exit_status = 0;
        game_board.next_pacman_cmd = '\0';

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

        draw_board(&game_board, DRAW_MENU);

        // --- LOOP PRINCIPAL (UI & INPUT) ---
        while (game_board.game_running) {
            // 1. Desenhar (Protegido por Mutex)
            pthread_mutex_lock(&game_board.board_lock);
            draw_board(&game_board, DRAW_MENU);
            pthread_mutex_unlock(&game_board.board_lock);
            refresh_screen();

            // 2. Input
            char input = get_input();

            // =======================================================
            // LÓGICA DE SAVE (G) COM THREADS
            // =======================================================
            if ((input == 'G'|| game_board.save_request) && has_active_save == 0) {
                // Bloqueia TUDO para garantir que o save é consistente 
                game_board.save_request=0;
                pthread_mutex_lock(&game_board.board_lock);
                
                pid_t pid = fork();

                if (pid < 0) {
                    perror("Erro fork");
                    pthread_mutex_unlock(&game_board.board_lock);
                }
                else if (pid > 0) {
                    
                    int status;
                    waitpid(pid, &status, 0); // Espera pelo filho

                    if (WIFEXITED(status)) {
                        int exit_code = WEXITSTATUS(status);
                        
                        if (exit_code == EXIT_RESTORE) {
                            // Filho morreu -> Restaurar
                            has_active_save = 0;
                            clear(); refresh();
                            pthread_mutex_unlock(&game_board.board_lock);
                            continue; // Volta ao loop (Threads do pai continuam vivas)
                        }
                        else if (exit_code == EXIT_GAME_OVER) {
                            // Filho fez Quit -> Pai também sai
                            game_board.exit_status = 3;
                            game_board.game_running = 0;
                        }
                    }
                    pthread_mutex_unlock(&game_board.board_lock);
                }
                else {
                    // === FILHO (Jogo Ativo) ===
                    // IMPORTANTE: Aqui só existe a thread MAIN. As outras morreram.
                    
                    // 1. Corrigir Mutex (pode ter vindo bloqueado do pai)
                    pthread_mutex_unlock(&game_board.board_lock);
                    
                    has_active_save = 1;
                    nodelay(stdscr, TRUE);
                    keypad(stdscr, TRUE);
                    
                    // 2. RECRIAR AS THREADS NO FILHO
                    // Sem isto, o jogo para no filho porque não há threads a mexer os bonecos
                    pthread_create(&p_thread, NULL, pacman_thread, &game_board);

                    for(int g=0; g < game_board.n_ghosts; g++) {
                        thread_arg_t* args = malloc(sizeof(thread_arg_t));
                        args->board = &game_board;
                        args->id = g;
                        pthread_create(&g_threads[g], NULL, ghost_thread, args);
                    }
                    // O filho continua o loop while e joga normalmente
                }
            }
            // =======================================================
            // LÓGICA DE QUIT (Q)
            // =======================================================
            else if (input == 'Q') {
                pthread_mutex_lock(&game_board.board_lock);
                game_board.exit_status = 3; // QUIT
                game_board.game_running = 0;
                pthread_mutex_unlock(&game_board.board_lock);
                
                if (has_active_save) exit(EXIT_GAME_OVER);
            } 
            // =======================================================
            // INPUT DE MOVIMENTO (WASD)
            // =======================================================
            else if (input != '\0') {
                // Passa o comando para a thread do Pacman executar
                game_board.next_pacman_cmd = input;
            }

            sleep_ms(33); // ~30 FPS UI Update
        }

        // --- FIM DO NÍVEL / JOGO ---
        
        // Esperar threads terminarem
        pthread_join(p_thread, NULL);
        for(int g=0; g < game_board.n_ghosts; g++) {
            pthread_join(g_threads[g], NULL);
        }
        pthread_mutex_destroy(&game_board.board_lock);

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
            // Se for Quit (3), sai direto sem Game Over, ou mostra msg se quiseres
            
            unload_level(&game_board);
            free(namelist[i]);
            break; // Sai do loop de níveis
        }
    }
    
    // Limpeza final
    for (int k = 0; k < n; k++) { /* ... */ } // (Opcional: free do resto)
    free(namelist);
    terminal_cleanup();
    close_debug_file();
    return 0;
}