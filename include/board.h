#ifndef BOARD_H
#define BOARD_H

#include <ncurses.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>

#define MAX_MOVES 100 // Aumentado para suportar ficheiros maiores
#define MAX_LEVELS 20
#define MAX_FILENAME 256
#define MAX_GHOSTS 25

typedef enum {
    REACHED_PORTAL = 1,
    VALID_MOVE = 0,
    INVALID_MOVE = -1,
    DEAD_PACMAN = -2,
} move_t;

typedef struct {
    char command;
    int turns;
    int turns_left;
} command_t;

typedef struct {
    int pos_x, pos_y; 
    int alive; 
    int points; 
    int passo; 
    command_t moves[MAX_MOVES];
    int current_move;
    int n_moves; 
    int waiting;
} pacman_t;

typedef struct {
    int pos_x, pos_y; 
    int passo; 
    command_t moves[MAX_MOVES];
    int n_moves; 
    int current_move;
    int waiting;
    int charged;
} ghost_t;

typedef struct {
    char content;   
    int has_dot;    
    int has_portal; 
} board_pos_t;

typedef struct {
    int width, height;      
    board_pos_t* board;     
    int n_pacmans;          
    pacman_t* pacmans;      
    int n_ghosts;           
    ghost_t* ghosts;        
    char level_name[256];   
    char pacman_file[256];  
    char ghosts_files[MAX_GHOSTS][256]; 
    int tempo;              
    
    // --- NOVO EXERCÍCIO 3 ---
    pthread_mutex_t board_lock; // O cadeado para proteger o tabuleiro
    int game_running;           // Flag: 1 = Jogo corre, 0 = Jogo deve parar
    char next_pacman_cmd; 
    int save_request;      // Comunicação entre Main (Teclado) e Thread Pacman
    // ------------------------
    int exit_status;
    pthread_mutex_t* row_locks; // Array dinâmico: tamanho = board->height
    pthread_mutex_t global_stats_lock;
} board_t;

/*Makes the current thread sleep for 'int milliseconds' miliseconds*/
void sleep_ms(int milliseconds);

/*Processes a command for Pacman or Ghost(Monster)*/
int move_pacman(board_t* board, int pacman_index, command_t* command);
int move_ghost(board_t* board, int ghost_index, command_t* command);

/*Process the death of a Pacman*/
void kill_pacman(board_t* board, int pacman_index);

/* Loads a level from a file into board 
   dir_path: path to the directory containing the files
   level_file: name of the .lvl file
*/

int get_board_index(board_t* board, int x, int y);

/*Unloads levels loaded by load_level*/

// DEBUG FILE
void open_debug_file(char *filename);
void close_debug_file();
void debug(const char * format, ...);
void print_board(board_t* board);

#endif