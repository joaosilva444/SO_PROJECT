# Compiler variables
CC = gcc
# Adicionado -D_POSIX_C_SOURCE para garantir acesso a funções como fdopen, lstat, etc.
CFLAGS = -g -Wall -Wextra -Werror -std=c17 -D_POSIX_C_SOURCE=200809L -pthread
LDFLAGS = -lncurses -pthread

# Directory variables
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
INCLUDE_DIR = include

# executable 
TARGET = Pacmanist

# Objects variables
# ADICIONADO: loader.o à lista de objetos
OBJS = game.o display.o board.o files.o

# Dependencies
# Estas variáveis são expandidas na regra de compilação %.o
# Nota: Assume-se que os ficheiros .h estão em $(INCLUDE_DIR) ou no VPATH.
# Se o make não encontrar os headers, podes precisar de adicionar $(INCLUDE_DIR)/ antes do nome.

display.o = display.h board.h
board.o = board.h
files.o = files.h


# Object files path
vpath %.o $(OBJ_DIR)
vpath %.c $(SRC_DIR)
vpath %.h $(INCLUDE_DIR)

# Make targets
all: pacmanist

pacmanist: $(BIN_DIR)/$(TARGET)

$(BIN_DIR)/$(TARGET): $(OBJS) | folders
	$(CC) $(CFLAGS) $(addprefix $(OBJ_DIR)/,$(OBJS)) -o $@ $(LDFLAGS)

# dont include LDFLAGS in the end, to allow compilation on macos
# A variável $($@) expande para as dependências definidas acima (ex: loader.h para loader.o)
%.o: %.c $($@) | folders
	$(CC) -I $(INCLUDE_DIR) $(CFLAGS) -o $(OBJ_DIR)/$@ -c $<

# run the program
# Exemplo de uso: make run ARGS="levels"
run: pacmanist
	@./$(BIN_DIR)/$(TARGET) $(ARGS)

# Create folders
folders:
	mkdir -p $(OBJ_DIR)
	mkdir -p $(BIN_DIR)

# Clean object files and executable
clean:
	rm -f $(OBJ_DIR)/*.o
	rm -f $(BIN_DIR)/$(TARGET)
	rm -f *.log
	rm -f *.zip

# indentify targets that do not create files
.PHONY: all clean run folders