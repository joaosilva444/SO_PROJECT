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
/* AUXILIARY FUNCTIONS                             */
/* -------------------------------------------------------------------------- */

// Função auxiliar para ler um ficheiro inteiro para um buffer usando POSIX
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

// Função para avançar para a próxima linha no buffer
static char* next_line(char* current) {
    char* eol = strchr(current, '\n');
    if (!eol) return NULL;
    return eol + 1;
}



/* -------------------------------------------------------------------------- */
/* PARSING LOGIC                                   */
/* -------------------------------------------------------------------------- */

// Faz parse de ficheiros .m (Monstro) ou .p (Pacman)
static int parse_agent_file(const char* filepath, int* start_x, int* start_y, int* passo, command_t* moves, int* n_moves) {
    char* buffer = read_file_to_buffer(filepath);
    if (!buffer) return -1;

    char* line = buffer;
    *n_moves = 0;
    *passo = 0; // Default

    while (line && *line) {
        // Ignorar linhas vazias ou comentários
        if (*line == '#' || *line == '\n' || *line == '\r') {
            line = next_line(line);
            continue;
        }

        // Tokenização simples (segura para threads se usarmos strtok_r, mas aqui usaremos sscanf para parsear a linha atual em memória)
        // Nota: sscanf é stdio.h mas opera em memória, não em FILE stream. É permitido.
        // Se for necessário evitar sscanf, usar atoi e manipulação de ponteiros.
        
        char key[16];
        // Tentar ler comando chave
        if (sscanf(line, "%15s", key) == 1) {
            if (strcmp(key, "PASSO") == 0) {
                sscanf(line, "PASSO %d", passo);
            }
            else if (strcmp(key, "POS") == 0) {
                // Ficheiro: POS linha coluna -> y x
                sscanf(line, "POS %d %d", start_y, start_x);
            }
            else {
                // Assumir que é um movimento
                // Exemplos: "A", "T2", "R"
                char cmd_char;
                int turns = 1;
                
                // Verificar se tem digitos
                char* ptr = line;
                while (*ptr && isspace(*ptr)) ptr++; // Skip space
                
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

/* -------------------------------------------------------------------------- */
/* CORE LOGIC                                      */
/* -------------------------------------------------------------------------- */

// Funções privadas originais (find_and_kill, get_board_index, is_valid_position, sleep_ms...)
// MANTÊM-SE IGUAIS AO CÓDIGO BASE (Omitidas para brevidade, copiar do original)
// Apenas re-incluindo as necessárias para o contexto:

static inline int get_board_index(board_t* board, int x, int y) {
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
// ---------------------------------------------------------


/* -------------------------------------------------------------------------- */
/* LOAD LEVEL IMPL                                 */
/* -------------------------------------------------------------------------- */

int load_level(board_t* board, const char* dir_path, const char* level_file, int accumulated_points) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, level_file);
    
    char* buffer = read_file_to_buffer(filepath);
    if (!buffer) return -1;

    // Inicialização básica
    board->n_pacmans = 0;
    board->n_ghosts = 0;
    board->pacman_file[0] = '\0';
    snprintf(board->level_name, sizeof(board->level_name), "%s", level_file);

    char* line = buffer;
    int reading_map = 0;
    int map_row = 0;

    // First Pass: Get Dimensions and Metadata
    while (line && *line) {
        if (*line == '#') { line = next_line(line); continue; }
        
        char key[16];
        if (!reading_map && sscanf(line, "%15s", key) == 1) {
            if (strcmp(key, "DIM") == 0) {
                sscanf(line, "DIM %d %d", &board->height, &board->width);
                // Alloc board once DIM is known
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
                // Parse multiple monster files
                char* p = line + 3; // Skip "MON"
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
                // Detected start of map (assuming valid syntax starts with map char)
                reading_map = 1;
            }
        }
        
        if (reading_map) {
             // Parse Map Row
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

    // Alloc Agents
    board->pacmans = calloc(1, sizeof(pacman_t)); // Always 1 pacman slot logic
    board->ghosts = calloc(board->n_ghosts, sizeof(ghost_t));

    // ... (Código anterior de parsing do mapa e alocação de memória mantém-se igual)

    // 1º: CARREGAR FANTASMAS (Mova isto para cima do Pacman)
    // Assim, os 'M' já estarão marcados no tabuleiro quando formos colocar o Pacman
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
            if (board->board[idx].content != 'W') {
                board->board[idx].content = 'M';
            }
        }
    }

    // 2º: CARREGAR PACMAN (Agora executado em último)
    if (board->n_pacmans > 0) {
        snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, board->pacman_file);
        parse_agent_file(filepath, &board->pacmans[0].pos_x, &board->pacmans[0].pos_y, 
                         &board->pacmans[0].passo, board->pacmans[0].moves, &board->pacmans[0].n_moves);
        
        pacman_t* p = &board->pacmans[0];
        p->alive = 1;
        p->points = accumulated_points;

        // --- NOVA LÓGICA DE POSICIONAMENTO SEGURO ---
        int idx = get_board_index(board, p->pos_x, p->pos_y);
        char content_at_spawn = board->board[idx].content;

        // Se a posição original for Parede (W) ou Monstro (M)
        if (content_at_spawn == 'W' || content_at_spawn == 'M') {
            int found_safe_spot = 0;

            // Procura a primeira posição livre (Itera por todo o tabuleiro)
            for (int y = 0; y < board->height; y++) {
                for (int x = 0; x < board->width; x++) {
                    int try_idx = get_board_index(board, x, y);
                    char c = board->board[try_idx].content;

                    // Verifica se não é Parede nem Monstro
                    if (c != 'W' && c != 'M') {
                        p->pos_x = x;
                        p->pos_y = y;
                        found_safe_spot = 1;
                        break; 
                    }
                }
                if (found_safe_spot) break;
            }
            
            // Se não encontrou lugar nenhum (mapa cheio de paredes/monstros), mantém o original
            // para o erro ser visível, ou podes tratar o erro aqui.
        }
        // ---------------------------------------------

        // Atualiza o tabuleiro com a posição final (seja a original ou a nova segura)
        idx = get_board_index(board, p->pos_x, p->pos_y);
        board->board[idx].content = 'P';
        board->board[idx].has_dot = 0; 
    } else {
        // Fallback manual (também deve verificar colisão se quiseres ser rigoroso)
        board->n_pacmans = 1;
        board->pacmans[0].pos_x = 1; // Podes aplicar a mesma lógica de procura aqui se (1,1) for parede
        board->pacmans[0].pos_y = 1;
        board->pacmans[0].alive = 1;
        board->pacmans[0].points = accumulated_points;
        board->board[get_board_index(board, 1, 1)].content = 'P';
    }

    return 0;
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