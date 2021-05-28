OBJDIR=./bin64/
OBJ=GXMPlayer.o GXMPatternView.o main.o

OBJPATH=$(patsubst %, $(OBJDIR)%, $(OBJ))

$(OBJDIR)%.o: %.cpp
	g++ -o $@ -c $< -O3 -s -lSDL2 -lSDL2main

main: $(OBJPATH)
	g++ -o $(OBJDIR)gxm $(OBJPATH) -lSDL2 -lSDL2main
	-mkdir ~/bin
	cp $(OBJDIR)gxm ~/bin

install:
	cp $(OBJDIR)gxm /bin

cleanup:
	-rm $(OBJDIR)*.o

.PHONY: cleanup
