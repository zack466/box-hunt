b:
	g++ -g -o run main.cpp -lncursesw -lSDL2 -lSDL2_mixer

r:
	./run

cl:
	rm run

re:
	rm run
	g++ -o run main.cpp -lncursesw -lSDL2 -lSDL2_mixer
