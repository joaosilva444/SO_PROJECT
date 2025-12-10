#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h> 
#include <sys/wait.h>  // Necessário para waitpid
#include <sys/types.h> // Necessário para pid_t
#include "files.h"
#define _DEFAULT_SOURCE

// Códigos de saída para comunicação Processo Filho -> Processo Pai
#define EXIT_RESTORE 10   // Filho morreu, Pai deve restaurar
#define EXIT_GAME_OVER 11 // Filho saiu (Q), Pai deve sair

// Flag Global:
// 0 = Sou o processo original (ou acabei de restaurar um save). Posso gravar.
// 1 = Sou um processo filho (cópia descartável). Não posso gravar.
int has_active_save = 0; 

// ... (Resto das funções auxiliares como filter_levels mantêm-se)

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define EXIT_RESTORE 10


void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

int play_board(board_t * game_board) {
    pacman_t* pacman = &game_board->pacmans[0];
    command_t* play = NULL;
    
    // Declaração segura no topo
    command_t manual_command; 
    
    char action_char = '\0'; 

    // 1. Ler Input do Teclado
    // Lemos na mesma para limpar o buffer do ncurses, mas vamos ignorar o valor
    // se estivermos em modo automático.
    char input = get_input();

    // 2. SELEÇÃO DE MODO DE JOGO (AUTOMÁTICO vs MANUAL)
    if (pacman->n_moves > 0) { 
        // =======================================================
        // MODO AUTOMÁTICO (FICHEIRO) - O TECLADO É IGNORADO
        // =======================================================
        
        play = &pacman->moves[pacman->current_move % pacman->n_moves];
        action_char = play->command; // A ação vem EXCLUSIVAMENTE do ficheiro
        
        // Verificação de Saída: Apenas se o FICHEIRO tiver 'Q'
        if (action_char == 'Q') {
             if (has_active_save) exit(EXIT_GAME_OVER);
             return QUIT_GAME;
        }
    }
    else { 
        // =======================================================
        // MODO MANUAL (TECLADO) - O ÚNICO SÍTIO ONDE O TECLADO CONTA
        // =======================================================
        
        // Verificação de Saída: Apenas aqui o teclado 'Q' funciona
        if (input == 'Q') {
            if (has_active_save) exit(EXIT_GAME_OVER);
            return QUIT_GAME;
        }

        // Configurar comando manual
        if (input != '\0') {
            manual_command.command = input;
            manual_command.turns = 1;
            
            play = &manual_command;
            action_char = input; // A ação vem do teclado
        }
    }

    // -----------------------------------------------------------
    // 3. LÓGICA DE QUICKSAVE (Comando 'G')
    // -----------------------------------------------------------
    // O action_char agora reflete estritamente a fonte (Ficheiro OU Teclado, nunca misturado)
    if (action_char == 'G') {
        
        if (has_active_save == 0) {
            // Sou o Pai -> Faço o Save
            debug("Iniciando Quicksave (PID Pai: %d)...\n", getpid());
            
            pid_t pid = fork();

            if (pid < 0) {
                perror("Erro no fork");
            }
            else if (pid > 0) {
                // === PROCESSO PAI ===
                int status;
                waitpid(pid, &status, 0); 

                if (WIFEXITED(status)) {
                    int exit_code = WEXITSTATUS(status);
                    if (exit_code == EXIT_RESTORE) {
                        has_active_save = 0; 
                        
                        // Avançar índice se for automático
                        if (pacman->n_moves > 0) pacman->current_move++;
                        
                        clear(); refresh(); screen_refresh(game_board, DRAW_MENU); 
                        return CONTINUE_PLAY;
                    }
                    else if (exit_code == EXIT_GAME_OVER) {
                        return QUIT_GAME;
                    }
                }
                return CONTINUE_PLAY; 
            }
            else {
                // === PROCESSO FILHO ===
                has_active_save = 1; 
                // Avançar índice se for automático
                if (pacman->n_moves > 0) pacman->current_move++;
            }
        }
        else {
            // Sou o Filho -> Ignoro o 'G'
            // Avançar índice se for automático para não encravar
            if (pacman->n_moves > 0) {
                 pacman->current_move++;
            }
        }
        play = NULL; // 'G' não é movimento
    }

    // 4. MOVIMENTO DO PACMAN
    if (play != NULL && play->command != 'G') {
        int result = move_pacman(game_board, 0, play);
        
        if (result == REACHED_PORTAL) return NEXT_LEVEL;
        
        if (result == DEAD_PACMAN) {
            if (has_active_save) exit(EXIT_RESTORE);
            return QUIT_GAME; 
        }
    }

    // 5. FANTASMAS
    for (int i = 0; i < game_board->n_ghosts; i++) {
        ghost_t* ghost = &game_board->ghosts[i];
        if (ghost->n_moves > 0) {
             move_ghost(game_board, i, &ghost->moves[ghost->current_move % ghost->n_moves]);
        }
    }

    // 6. VERIFICAÇÃO FINAL
    if (!game_board->pacmans[0].alive) {
        if (has_active_save) {
            exit(EXIT_RESTORE);
        }
        return QUIT_GAME;
    }      

    return CONTINUE_PLAY;  
}

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("Usage: %s <level_directory>\n", argv[0]);
        return 1;
    }

    char* dir_path = argv[1];
    struct dirent **namelist;
    int n;

    // POSIX scandir para encontrar ficheiros .lvl e ordená-los
    n = scandir(dir_path, &namelist, filter_levels, alphasort);
    if (n < 0) {
        perror("scandir");
        return 1;
    }

    srand((unsigned int)time(NULL));
    open_debug_file("debug.log");
    terminal_init();
    
    int accumulated_points = 0;
    board_t game_board;

    // Loop through levels
    for (int i = 0; i < n; i++) {
        char* level_file = namelist[i]->d_name;
        
        // Carregar nível
        if (load_level(&game_board, dir_path, level_file, accumulated_points) != 0) {
            debug("Failed to load level %s\n", level_file);
            free(namelist[i]);
            continue;
        }

        draw_board(&game_board, DRAW_MENU);
        refresh_screen();

        int level_result = CONTINUE_PLAY;
        bool end_game = false;

        while (!end_game) {
            level_result = play_board(&game_board); 

            if(level_result == NEXT_LEVEL) {
                screen_refresh(&game_board, DRAW_WIN);
                sleep_ms(game_board.tempo * 10); // Pausa breve na vitória
                accumulated_points = game_board.pacmans[0].points;
                end_game = true;
            }
            else if(level_result == QUIT_GAME) {
                screen_refresh(&game_board, DRAW_GAME_OVER); 
                sleep_ms(game_board.tempo * 20);
                end_game = true;
            }
            else {
                screen_refresh(&game_board, DRAW_MENU); 
            }
        }
        
        unload_level(&game_board);
        free(namelist[i]);

        if (level_result == QUIT_GAME) break;
    }
    free(namelist);

    terminal_cleanup();
    close_debug_file();

    return 0;
}

