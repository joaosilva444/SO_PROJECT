#ifndef FILES_H
#define FILES_H

#include "board.h"
#include <dirent.h>

/* Carrega um nível a partir de ficheiros para a estrutura board */
int load_level(board_t* board, const char* dir_path, const char* level_file, int accumulated_points);

/* Filtro para o scandir encontrar ficheiros .lvl */
int filter_levels(const struct dirent *entry);

#endif