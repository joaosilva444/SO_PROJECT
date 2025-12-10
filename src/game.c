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
    command_t manual_command;
    char action_char = '\0'; // Para guardar qual é a ação deste turno

    // -----------------------------------------------------------
    // 1. PROCESSAR INPUT E DETERMINAR COMANDO
    // -----------------------------------------------------------
    char input = get_input();

    // Se o jogador carrega em 'Q'
    if (input == 'Q') {
        if (has_active_save) {
            // Se sou o filho, tenho de avisar o pai para morrer também
            exit(EXIT_GAME_OVER); 
        }
        return QUIT_GAME;
    }

    // Verificar se o movimento é Manual ou Automático
    if (pacman->n_moves == 0) { 
        // Modo Manual
        if (input != '\0') {
            manual_command.command = input;
            manual_command.turns = 1;
            play = &manual_command;
            action_char = input; 
        }
    }
    else { 
        // Modo Automático (Ler do ficheiro)
        play = &pacman->moves[pacman->current_move % pacman->n_moves];
        action_char = play->command;
    }
    // -----------------------------------------------------------
    // 2. LÓGICA DE QUICKSAVE (Comando 'G')
    // -----------------------------------------------------------
    if (action_char == 'G') {
        
        // Se eu sou o Pai (has_active_save == 0), posso gravar.
        if (has_active_save == 0) {
            
            debug("Iniciando Quicksave via Teclado/Ficheiro (PID Pai: %d)...\n", getpid());
            
            pid_t pid = fork();

            if (pid < 0) {
                perror("Erro no fork");
            }
            else if (pid > 0) {
                // === PROCESSO PAI (SAVE STATE) ===
                int status;
                waitpid(pid, &status, 0); 

                if (WIFEXITED(status)) {
                    int exit_code = WEXITSTATUS(status);

                    if (exit_code == EXIT_RESTORE) {
                        debug("Save carregado! Pai retoma.\n");
                        has_active_save = 0; 
                        
                        // CORREÇÃO 1: Se o 'G' estava no ficheiro, temos de passar à frente
                        // para que, ao retomar, o Pacman não tente gravar outra vez imediatamente.
                        if (pacman->n_moves > 0 && play != NULL && play->command == 'G') {
                            pacman->current_move++;
                        }
                        
                        // Forçar refresh do ecrã
                        clear();
                        refresh();
                        screen_refresh(game_board, DRAW_MENU); 
                        return CONTINUE_PLAY;
                    }
                    else if (exit_code == EXIT_GAME_OVER) {
                        return QUIT_GAME;
                    }
                }
                // Se o filho morreu por outra razão
                return CONTINUE_PLAY; 
            }
            else {
                // === PROCESSO FILHO (JOGO ATIVO) ===
                has_active_save = 1; 
                
                // CORREÇÃO 2: O Filho acabou de ser criado a partir de um comando 'G'.
                // Ele TEM de avançar para a próxima instrução imediatamente, 
                // senão fica preso no 'G' para sempre.
                if (pacman->n_moves > 0 && play != NULL && play->command == 'G') {
                     pacman->current_move++;
                }
            }
        }
        else {
            // CORREÇÃO 3: CRUCIAL PARA O FILHO
            // Se eu já sou o Filho (has_active_save == 1) e encontro um 'G' no ficheiro,
            // tenho de o ignorar e AVANÇAR. Se não fizer isto, fico preso aqui.
            if (pacman->n_moves > 0 && play != NULL && play->command == 'G') {
                 pacman->current_move++;
            }
        }
        
        // Se a ação foi 'G', anulamos 'play' para não ir para o switch de movimento
        play = NULL; 
    }

    // -----------------------------------------------------------
    // 3. MOVIMENTO DO PACMAN
    // -----------------------------------------------------------
    if (play != NULL && play->command != 'G') {
        int result = move_pacman(game_board, 0, play);
        
        if (result == REACHED_PORTAL) return NEXT_LEVEL;

       // Dentro de if (play != NULL ...)
    if (result == DEAD_PACMAN) {
        debug("[MORTE ATIVA] Pacman bateu num monstro. has_active_save=%d\n", has_active_save); // <--- DEBUG
        
        if (has_active_save) {
            debug("[FILHO] A enviar EXIT_RESTORE (10)...\n"); // <--- DEBUG
            exit(EXIT_RESTORE);
        }
        debug("[MORTE] Sem save ativo. QUIT_GAME.\n"); // <--- DEBUG
        return QUIT_GAME; 
    }
    }

    // -----------------------------------------------------------
    // 4. MOVIMENTO DOS FANTASMAS
    // -----------------------------------------------------------
    for (int i = 0; i < game_board->n_ghosts; i++) {
        ghost_t* ghost = &game_board->ghosts[i];
        // Move fantasma se tiver movimentos definidos
        if (ghost->n_moves > 0) {
             move_ghost(game_board, i, &ghost->moves[ghost->current_move % ghost->n_moves]);
        }
    }

    // -----------------------------------------------------------
    // 5. VERIFICAÇÃO FINAL DE VIDA
    // -----------------------------------------------------------
    // (Caso o fantasma tenha atropelado o Pacman no turno dele)
    // No final da função play_board
    if (!game_board->pacmans[0].alive) {
        debug("[MORTE PASSIVA] Fantasma matou Pacman. has_active_save=%d\n", has_active_save); // <--- DEBUG
        
        if (has_active_save) {
            debug("[FILHO] A enviar EXIT_RESTORE (10)...\n"); // <--- DEBUG
            sleep_ms(100); // Dar tempo para escrever no log
            exit(EXIT_RESTORE); 
        }
        debug("[MORTE] Sem save ativo. QUIT_GAME.\n"); // <--- DEBUG
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

