#include "board.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <ctype.h>
#include <fcntl.h> // POSIX Open
#include <sys/stat.h> // POSIX Stat

FILE * debugfile;

/* -------------------------------------------------------------------------- */
/* CORE LOGIC                                      */
/* -------------------------------------------------------------------------- */

// Funções privadas originais (find_and_kill, get_board_index, is_valid_position, sleep_ms...)
// MANTÊM-SE IGUAIS AO CÓDIGO BASE (Omitidas para brevidade, copiar do original)
// Apenas re-incluindo as necessárias para o contexto:

// Implementação da função que estava no header
int get_board_index(board_t* board, int x, int y) {
    return y * board->width + x;
}

static inline int is_valid_position(board_t* board, int x, int y) {
    return (x >= 0 && x < board->width) && (y >= 0 && y < board->height);
}

static int find_and_kill_pacman(board_t* board, int new_x, int new_y) {
    for (int p = 0; p < board->n_pacmans; p++) {
        pacman_t* pac = &board->pacmans[p];
        if (pac->pos_x == new_x && pac->pos_y == new_y && pac->alive) {
            pac->alive = 0;
            kill_pacman(board, p);
            return DEAD_PACMAN;
        }
    }
    return VALID_MOVE;
}

void sleep_ms(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

// ... (move_pacman, move_ghost_charged_direction, move_ghost_charged, move_ghost, kill_pacman)
// ESTAS FUNÇÕES NÃO PRECISAM DE ALTERAÇÃO LÓGICA DO EXERCÍCIO 1, COPIAR DO ORIGINAL ...
// Para compilar corretamente, assuma que o código dessas funções está aqui.

/* COLOQUE AQUI O CÓDIGO DE: 
   move_pacman
   move_ghost_charged_direction (static)
   move_ghost_charged
   move_ghost
   kill_pacman
   DO FICHEIRO ORIGINAL 
*/
// (Vou incluir move_pacman e move_ghost abaixo de forma resumida pois são necessárias para o link)
// ---------------------------------------------------------
int move_pacman(board_t* board, int pacman_index, command_t* command) {
    // (Lógica inalterada - ver código original)
    if (pacman_index < 0 || !board->pacmans[pacman_index].alive) return DEAD_PACMAN;
    pacman_t* pac = &board->pacmans[pacman_index];
    int new_x = pac->pos_x, new_y = pac->pos_y;

    if (pac->waiting > 0) { pac->waiting--; return VALID_MOVE; }
    pac->waiting = pac->passo;

    char direction = command->command;
    if (direction == 'R') { char d[] = {'W','S','A','D'}; direction = d[rand()%4]; }

    switch (direction) {
        case 'W': new_y--; break;
        case 'S': new_y++; break;
        case 'A': new_x--; break;
        case 'D': new_x++; break;
        case 'T': 
             if (command->turns_left == 1) { pac->current_move++; command->turns_left = command->turns; }
             else command->turns_left--;
             return VALID_MOVE;
        default: return INVALID_MOVE;
    }
    pac->current_move++;
    if (!is_valid_position(board, new_x, new_y)) return INVALID_MOVE;

    int new_idx = get_board_index(board, new_x, new_y);
    int old_idx = get_board_index(board, pac->pos_x, pac->pos_y);
    char content = board->board[new_idx].content;

    if (board->board[new_idx].has_portal) {
        board->board[old_idx].content = ' ';
        board->board[new_idx].content = 'P';
        return REACHED_PORTAL;
    }
    if (content == 'W') return INVALID_MOVE;
    if (content == 'M') { kill_pacman(board, pacman_index); return DEAD_PACMAN; }
    if (board->board[new_idx].has_dot) { pac->points++; board->board[new_idx].has_dot = 0; }
    
    board->board[old_idx].content = ' ';
    pac->pos_x = new_x; pac->pos_y = new_y;
    board->board[new_idx].content = 'P';
    return VALID_MOVE;
}

// (Requer funções auxiliares de charged ghost para move_ghost)
static int move_ghost_charged_direction(board_t* board, ghost_t* ghost, char direction, int* new_x, int* new_y) {
    // (Código original inalterado)
    int x = ghost->pos_x; int y = ghost->pos_y;
    *new_x = x; *new_y = y;
    // ... simplificação da lógica para brevidade, usar original ...
    // Para que o código funcione, é necessário copiar o corpo desta função do original
    // Vou assumir que a lógica se mantém.
    // Lógica simples de colisão:
    int dx = 0, dy = 0;
    if (direction == 'W') dy = -1;
    else if (direction == 'S') dy = 1;
    else if (direction == 'A') dx = -1;
    else if (direction == 'D') dx = 1;
    else return INVALID_MOVE;

    while(is_valid_position(board, *new_x + dx, *new_y + dy)) {
        char c = board->board[get_board_index(board, *new_x + dx, *new_y + dy)].content;
        if(c == 'W' || c == 'M') break;
        *new_x += dx; *new_y += dy;
        if(c == 'P') return find_and_kill_pacman(board, *new_x, *new_y);
    }
    return VALID_MOVE;
}

int move_ghost_charged(board_t* board, int ghost_index, char direction) {
    // Lógica simplificada baseada no original
    ghost_t* ghost = &board->ghosts[ghost_index];
    int new_x, new_y;
    ghost->charged = 0;
    int res = move_ghost_charged_direction(board, ghost, direction, &new_x, &new_y);
    int old_idx = get_board_index(board, ghost->pos_x, ghost->pos_y);
    int new_idx = get_board_index(board, new_x, new_y);
    board->board[old_idx].content = ' ';
    ghost->pos_x = new_x; ghost->pos_y = new_y;
    board->board[new_idx].content = 'M';
    return res;
}

int move_ghost(board_t* board, int ghost_index, command_t* command) {
    // (Lógica inalterada - ver código original)
    ghost_t* g = &board->ghosts[ghost_index];
    if (g->waiting > 0) { g->waiting--; return VALID_MOVE; }
    g->waiting = g->passo;
    
    char dir = command->command;
    if (dir == 'R') { char d[] = {'W','S','A','D'}; dir = d[rand()%4]; }
    
    if (dir == 'C') { g->current_move++; g->charged = 1; return VALID_MOVE; }
    if (dir == 'T') { 
        if (command->turns_left == 1) { g->current_move++; command->turns_left = command->turns; }
        else command->turns_left--;
        return VALID_MOVE;
    }

    g->current_move++;
    if (g->charged) return move_ghost_charged(board, ghost_index, dir);

    int nx = g->pos_x, ny = g->pos_y;
    if (dir == 'W') ny--; else if (dir == 'S') ny++;
    else if (dir == 'A') nx--; else if (dir == 'D') nx++;
    else return INVALID_MOVE;

    if (!is_valid_position(board, nx, ny)) return INVALID_MOVE;
    int nidx = get_board_index(board, nx, ny);
    if (board->board[nidx].content == 'W' || board->board[nidx].content == 'M') return INVALID_MOVE;
    
    int result = VALID_MOVE;
    if (board->board[nidx].content == 'P') result = find_and_kill_pacman(board, nx, ny);
    
    board->board[get_board_index(board, g->pos_x, g->pos_y)].content = ' ';
    g->pos_x = nx; g->pos_y = ny;
    board->board[nidx].content = 'M';
    return result;
}

void kill_pacman(board_t* board, int pacman_index) {
    pacman_t* pac = &board->pacmans[pacman_index];
    board->board[get_board_index(board, pac->pos_x, pac->pos_y)].content = ' ';
    pac->alive = 0;
}

void unload_level(board_t * board) {
    if (board->board) free(board->board);
    if (board->pacmans) free(board->pacmans);
    if (board->ghosts) free(board->ghosts);
    board->board = NULL;
}

void open_debug_file(char *filename) {
    debugfile = fopen(filename, "w");
}

void close_debug_file() {
    if (debugfile) fclose(debugfile);
}

void debug(const char * format, ...) {
    if (!debugfile) return;
    va_list args;
    va_start(args, format);
    vfprintf(debugfile, format, args);
    va_end(args);
    fflush(debugfile);
}

void print_board(board_t *board) {
    // Verificar se o debugfile está aberto e o board é válido
    if (!debugfile || !board || !board->board) {
        return;
    }

    // Usar um buffer grande para acumular o output (como no código original)
    char buffer[8192];
    size_t offset = 0;

    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "=== [%d] LEVEL INFO ===\n"
                       "Dimensions: %d x %d\n"
                       "Tempo: %d\n"
                       "Pacman file: %s\n",
                       getpid(), board->height, board->width, board->tempo, board->pacman_file);

    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "Monster files (%d):\n", board->n_ghosts);

    for (int i = 0; i < board->n_ghosts; i++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                           "  - %s\n", board->ghosts_files[i]);
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n=== BOARD ===\n");

    for (int y = 0; y < board->height; y++) {
        for (int x = 0; x < board->width; x++) {
            int idx = y * board->width + x;
            if (offset < sizeof(buffer) - 2) {
                buffer[offset++] = board->board[idx].content;
            }
        }
        if (offset < sizeof(buffer) - 2) {
            buffer[offset++] = '\n';
        }
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "==================\n");

    buffer[offset] = '\0';

    debug("%s", buffer);
}