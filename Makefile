build:
	g++ -g -o run main.cpp -lncursesw -lSDL2 -lSDL2_mixer

run:
	./run

clean:
	rm run
