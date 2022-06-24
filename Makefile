CC = cc
LIBS = -lxcb -lxcb-xinerama -lxcb-icccm -I/usr/include/xcb
CFLAGS = -g
NAME = nyawm
SRCDIR = src
OBJDIR = obj
BINDIR = bin

SRC = $(wildcard $(SRCDIR)/*.c)
OBJ = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRC))
BIN = $(BINDIR)/$(NAME)

all: $(BINDIR) $(OBJDIR) $(BIN)

$(BIN): $(OBJ)
	$(CC) $^ $(CFLAGS) $(LIBS) -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $@

$(BINDIR):
	mkdir -p $@

run: all
	./$(BIN)

clean:
	rm -r $(OBJDIR)
	rm -r $(BINDIR)
