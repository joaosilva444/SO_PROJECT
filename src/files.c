#include "files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

// Função auxiliar para filtro do scandir (movida do game.c)
int filter_levels(const struct dirent *entry) {
    const char *dot = strrchr(entry->d_name, '.');
    if (dot && strcmp(dot, ".lvl") == 0) {
        return 1;
    }
    return 0;
}

// Função auxiliar de leitura POSIX (movida do board.c)
static char* read_file_to_buffer(const char* filepath) {
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        perror("Erro ao abrir ficheiro");
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        close(fd);
        return NULL;
    }

    char* buffer = malloc(st.st_size + 1);
    if (!buffer) {
        close(fd);
        return NULL;
    }

    ssize_t bytes_read = read(fd, buffer, st.st_size);
    if (bytes_read != st.st_size) {
        free(buffer);
        close(fd);
        return NULL;
    }

    buffer[bytes_read] = '\0';
    close(fd);
    return buffer;
}

static char* next_line(char* current) {
    char* eol = strchr(current, '\n');
    if (!eol) return NULL;
    return eol + 1;
}

// Parser de Agentes (movido do board.c)
static int parse_agent_file(const char* filepath, int* start_x, int* start_y, int* passo, command_t* moves, int* n_moves) {
    char* buffer = read_file_to_buffer(filepath);
    if (!buffer) return -1;

    char* line = buffer;
    *n_moves = 0;
    *passo = 0; 

    while (line && *line) {
        if (*line == '#' || *line == '\n' || *line == '\r') {
            line = next_line(line);
            continue;
        }
        
        char key[16];
        if (sscanf(line, "%15s", key) == 1) {
            if (strcmp(key, "PASSO") == 0) {
                sscanf(line, "PASSO %d", passo);
            }
            else if (strcmp(key, "POS") == 0) {
                sscanf(line, "POS %d %d", start_y, start_x);
            }
            else {
                char cmd_char;
                int turns = 1;
                char* ptr = line;
                while (*ptr && isspace(*ptr)) ptr++; 
                cmd_char = *ptr;
                ptr++;
                if (*ptr && isdigit(*ptr)) {
                    turns = atoi(ptr);
                }

                if (*n_moves < MAX_MOVES) {
                    moves[*n_moves].command = cmd_char;
                    moves[*n_moves].turns = turns;
                    moves[*n_moves].turns_left = turns;
                    (*n_moves)++;
                }
            }
        }
        line = next_line(line);
    }
    free(buffer);
    return 0;
}

// A função Principal de carregamento (movida do board.c)
int load_level(board_t* board, const char* dir_path, const char* level_file, int accumulated_points) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, level_file);
    
    char* buffer = read_file_to_buffer(filepath);
    if (!buffer) return -1;

    board->n_pacmans = 0;
    board->n_ghosts = 0;
    board->pacman_file[0] = '\0';
    snprintf(board->level_name, sizeof(board->level_name), "%s", level_file);

    char* line = buffer;
    int reading_map = 0;
    int map_row = 0;

    // 1. Parsing do Cabeçalho e Mapa
    while (line && *line) {
        if (*line == '#' && !reading_map) { line = next_line(line); continue; }
        
        char key[16];
        if (!reading_map && sscanf(line, "%15s", key) == 1) {
            if (strcmp(key, "DIM") == 0) {
                sscanf(line, "DIM %d %d", &board->height, &board->width);
                board->board = calloc(board->width * board->height, sizeof(board_pos_t));
            }
            else if (strcmp(key, "TEMPO") == 0) {
                sscanf(line, "TEMPO %d", &board->tempo);
            }
            else if (strcmp(key, "PAC") == 0) {
                sscanf(line, "PAC %s", board->pacman_file);
                board->n_pacmans = 1;
            }
            else if (strcmp(key, "MON") == 0) {
                char* p = line;
                while (*p && isspace(*p)) p++; // Skip indent
                while (*p && !isspace(*p)) p++; // Skip MON word
                
                while (*p && board->n_ghosts < MAX_GHOSTS) {
                    while (*p && isspace(*p)) p++;
                    if (!*p) break;
                    
                    char mon_file[256];
                    int len = 0;
                    while (*p && !isspace(*p) && len < 255) {
                        mon_file[len++] = *p++;
                    }
                    mon_file[len] = '\0';
                    strcpy(board->ghosts_files[board->n_ghosts], mon_file);
                    board->n_ghosts++;
                }
            }
            else if (strchr("Xo@", *line)) {
                reading_map = 1;
            }
        }
        
        if (reading_map) {
             for (int i = 0; i < board->width && line[i] != '\0' && line[i] != '\n'; i++) {
                 int idx = map_row * board->width + i;
                 char c = line[i];
                 if (c == 'X') board->board[idx].content = 'W';
                 else if (c == '@') {
                     board->board[idx].content = ' ';
                     board->board[idx].has_portal = 1;
                 }
                 else if (c == 'o' || c == '0') {
                     board->board[idx].content = ' ';
                     board->board[idx].has_dot = 1;
                 }
                 else {
                     board->board[idx].content = ' ';
                 }
             }
             map_row++;
        }
        line = next_line(line);
    }
    free(buffer);

    board->pacmans = calloc(1, sizeof(pacman_t));
    board->ghosts = calloc(board->n_ghosts, sizeof(ghost_t));

    // 2. Carregar FANTASMAS (Com lógica de segurança)
    for (int i = 0; i < board->n_ghosts; i++) {
        board->ghosts[i].pos_x = -1;
        board->ghosts[i].pos_y = -1;

        snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, board->ghosts_files[i]);
        parse_agent_file(filepath, &board->ghosts[i].pos_x, &board->ghosts[i].pos_y, 
                         &board->ghosts[i].passo, board->ghosts[i].moves, &board->ghosts[i].n_moves);
        
        ghost_t* g = &board->ghosts[i];
        if (g->pos_x >= 0 && g->pos_x < board->width && 
            g->pos_y >= 0 && g->pos_y < board->height) {
            
            int idx = get_board_index(board, g->pos_x, g->pos_y);
            char content = board->board[idx].content;

            if (content == 'W' || content == 'M') {
                int found = 0;
                for (int y = 0; y < board->height; y++) {
                    for (int x = 0; x < board->width; x++) {
                        int try_idx = get_board_index(board, x, y);
                        if (board->board[try_idx].content != 'W' && board->board[try_idx].content != 'M') {
                            g->pos_x = x; g->pos_y = y; found = 1; break;
                        }
                    }
                    if (found) break;
                }
            }
            int final_idx = get_board_index(board, g->pos_x, g->pos_y);
            if (board->board[final_idx].content != 'W') board->board[final_idx].content = 'M';
        }
    }

    // 3. Carregar PACMAN (Com lógica de segurança)
    if (board->n_pacmans > 0) {
        snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, board->pacman_file);
        parse_agent_file(filepath, &board->pacmans[0].pos_x, &board->pacmans[0].pos_y, 
                         &board->pacmans[0].passo, board->pacmans[0].moves, &board->pacmans[0].n_moves);
        
        pacman_t* p = &board->pacmans[0];
        p->alive = 1;
        p->points = accumulated_points;

        int idx = get_board_index(board, p->pos_x, p->pos_y);
        if (board->board[idx].content == 'W' || board->board[idx].content == 'M') {
            int found = 0;
            for (int y = 0; y < board->height; y++) {
                for (int x = 0; x < board->width; x++) {
                    int try_idx = get_board_index(board, x, y);
                    char c = board->board[try_idx].content;
                    if (c != 'W' && c != 'M') {
                        p->pos_x = x; p->pos_y = y; found = 1; break;
                    }
                }
                if (found) break;
            }
        }
        idx = get_board_index(board, p->pos_x, p->pos_y);
        board->board[idx].content = 'P';
        board->board[idx].has_dot = 0; 
    } 
    else {
        // Fallback Manual
        board->n_pacmans = 1;
        board->pacmans[0].alive = 1;
        board->pacmans[0].points = accumulated_points;
        int sx = 1, sy = 1;
        if (board->board[get_board_index(board, sx, sy)].content == 'W') {
             // Procura simples se (1,1) for parede
             for(int i=0; i<board->width*board->height; i++) 
                if(board->board[i].content != 'W') { sx = i%board->width; sy = i/board->width; break; }
        }
        board->pacmans[0].pos_x = sx; board->pacmans[0].pos_y = sy;
        board->board[get_board_index(board, sx, sy)].content = 'P';
    }

    // Inicializar o Mutex
    board->row_locks = malloc(sizeof(pthread_mutex_t) * board->height);
    for (int i = 0; i < board->height; i++) {
        pthread_mutex_init(&board->row_locks[i], NULL);
    }
    pthread_mutex_init(&board->global_stats_lock, NULL);
    board->save_request = 0;    
    board->game_running = 1;      // Marcar jogo como ativo
    board->next_pacman_cmd = '\0'; // Limpar comando
    board->exit_status = 0;

    return 0;
}

void unload_level(board_t * board) {
    if (!board) return;

    // 1. Destruir e libertar mutexes das linhas
    if (board->row_locks) {
        for (int i = 0; i < board->height; i++) {
            pthread_mutex_destroy(&board->row_locks[i]);
        }
        free(board->row_locks);
        board->row_locks = NULL;
    }

    // 2. Destruir mutex global
    pthread_mutex_destroy(&board->global_stats_lock);

    // 3. Libertar o resto (como já tinhas)
    if (board->board) free(board->board);
    if (board->pacmans) free(board->pacmans);
    if (board->ghosts) free(board->ghosts);
    
    board->board = NULL;
    board->pacmans = NULL;
    board->ghosts = NULL;
    board->n_ghosts = 0;
    board->n_pacmans = 0;
}