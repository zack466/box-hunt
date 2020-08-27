#include <ncurses.h>
#include <SDL/SDL_mixer.h>
#include <stdlib.h>
#include <iostream>
#include <chrono>
#include <time.h>
#include <vector>
#include <math.h>
#include <tuple>
#include <string>
#include <memory>
#include <map>

const int MAX_COLUMNS = 54;
const int MAX_LINES = 18;
const int NANO = 1e9;
#define BLOCK L"\u2588"
const float RAND = 5.0;

class sample {
public:
	int channel;
    sample(const std::string &path, int volume, int channel);
    void play();
    void play(int times);
    void set_volume(int volume);
	void stop();
private:
    std::unique_ptr<Mix_Chunk, void (*)(Mix_Chunk *)> chunk;
};
sample::sample(const std::string &path, int volume, int channel)
    : chunk(Mix_LoadWAV(path.c_str()), Mix_FreeChunk) {
    if (!chunk.get()) {
		std::cout << "Couldn't load sample\n" << SDL_GetError() << std::endl;
        // LOG("Couldn't load audio sample: ", path);
    }
	this->channel = channel;
    Mix_VolumeChunk(chunk.get(), volume);
}
void sample::play() {
    Mix_PlayChannel(this->channel, chunk.get(), 0);
}
void sample::play(int times) {
    Mix_PlayChannel(this->channel, chunk.get(), times - 1);
}
void sample::set_volume(int volume) {
    Mix_VolumeChunk(chunk.get(), volume);
}
void sample::stop() {
	Mix_HaltChannel(this->channel);
}

std::map<std::string, sample*> sounds;

struct vec {
	float mag;
	float angle; //in radians
	std::pair<float, float> rect() {
		return {cos(angle) * mag, -1*sin(angle) * mag};
	}
	vec(float mag1, float angle1) {
		mag = mag1;
		angle = angle1;
	}
	vec() {}
};

struct GameOptions {
	std::wstring name = L"";
	std::vector<std::tuple<float,float,int>> attrs; // { {speed, escapetime}, ... }
	int special = 0; //0: standard, 1: small targets, 2: small shot, 3: 1 & 2
} Standard, Shotgun, Sniper, Impossible;

std::vector<GameOptions*> Gamemodes = {&Standard, &Shotgun, &Sniper, &Impossible};

std::tuple<float,float,int> attrGenerator(int r, int special) {
	float speed = pow(1.12, r+6) + 15;
	float escapetime = -4 * log10(r+4) + 9;
	return {speed, escapetime, special};
}

std::vector<std::tuple<float, float, int>> attrList(int special, int offset) {
	std::vector<std::tuple<float, float, int>> attrs;
	for (int i=0; i<5; i++) {
		attrs.push_back(attrGenerator(i+offset, special)); 
		attrs.push_back(attrGenerator(i+offset, special)); 
	}
	return attrs;
}

void setupGameOptions() {
	// "standard" 10-round duck hunt
	Standard.name = L"Standard";
	Standard.special = 0;
	Standard.attrs = attrList(Standard.special, 0);
	
	// Shotgun - ducks are smaller
	Shotgun.name = L"Shotgun";
	Shotgun.special = 1;
	Shotgun.attrs = attrList(Shotgun.special, 0);

	// Sniper - gunshot is smaller
	Sniper.name = L"Sniper";
	Sniper.special = 2;
	Sniper.attrs = attrList(Sniper.special, 0);

	// Impossible - small ducks, small gunshot
	Impossible.name = L"Impossible";
	Impossible.special = 3;
	Impossible.attrs = attrList(Impossible.special, 0);
}

float randomfloat(float high, float low=0) {
	//float x = static_cast <float> (rand()) / (static_cast <float> (RAND_MAX/high))+low;
	float x = low + static_cast <float> (rand()) /( static_cast <float> (RAND_MAX/(high-low)));
	return x;
}

struct PointCh {
	std::wstring ch;
	bool isgun = false;
	bool visible = true;
	float lifetime = 0;
	float escapetime = -1; //time until escapes (sec)
	bool escaped = false;
	bool hit = false;
	float x,y;
	vec vect;
	int r = 1;
	PointCh(){}
	PointCh(std::tuple<float,float,int> attr) {
		float magnitude = randomfloat(std::get<0>(attr)+RAND, std::get<0>(attr)-RAND);
		float angle = static_cast <float> (rand()) / (static_cast <float> (RAND_MAX/2.9))+0.1;
		while (angle < 1.96 && angle > 1.17) //get rid of top pi/4 bc don't want ducks flying straight up and down
			angle = static_cast <float> (rand()) / (static_cast <float> (RAND_MAX/2.9))+0.1;
		vect = vec(magnitude, angle);
		
		escapetime = std::get<1>(attr);
		ch = L"\u25a1";
		x = (rand() % MAX_COLUMNS) / 2;
		y = MAX_LINES;

		if (std::get<2>(attr) == 1 || std::get<2>(attr) == 3) {
			r = 0;
		}
		
	}
	PointCh(float x1, float y1, std::wstring ch1) {
		x = x1;
		y = y1;
		ch = ch1;
	}
	void update(float f) {
		if (isgun)
			return;
		if (escapetime != -1 && lifetime > escapetime) {
			escaped = true;
		}
		if (hit) {
			if (y +r > MAX_LINES+2 && visible) {
				sounds["duck_hit"]->play();
				visible = false;
				return;
			} else {
				std::pair<float,float> v = {0, 20};
				x += f / NANO * v.first;
				y += f / NANO * v.second;
			}
		} else if (escaped) {
			if (y +r < 0-2) {
				visible = false;
				return;
			} else {
				std::pair<float,float> v = {0, -20};
				x += f / NANO * v.first;
				y += f / NANO * v.second;
			}
		} else if (visible) {
			std::pair<float,float> v = vect.rect();
			x += f / NANO * v.first;
			y += f / NANO * v.second;
			
			if ((x+1+r)*2 > MAX_COLUMNS) {
				turn(1);
			} else if ((x-1-r)*2 < 0) {
				turn(3);
			}
			if (y+1+r > MAX_LINES) {
				turn(0);
			} else if (y-1-r < 0) {
				turn(2);
			}
		}
	}
	void turn(int i) {
		// 0 up, 1 left, 2 down, 3 right
		auto v = vect.rect();
		switch (i) {
			case 0: {
				if (v.second > 0)
					vect.angle = -vect.angle;
				break;
			}
			case 1: {
				if (v.first > 0)
					vect.angle = 3.14f - vect.angle;
				break;
			}
			case 2: {
				if (v.second < 0)
					vect.angle = -vect.angle;
				break;
			}
			case 3: {
				if (v.first < 0)
					vect.angle = 3.14f - vect.angle;
				break;
			}
		}
	}
	void draw(WINDOW *win) {
		//mvwprintw(win, (int) y, (int) x, &ch);
		if (!visible) return;
		for (int i = -r; i <= r; i++) {
			for (int j = fmin(-(2*r), -1); j <= fmax(2*r, 1); j++) {
				mvwaddwstr(win, (int) y + i, 2*((int) x) + j, ch.c_str());
			}
		}
	}
	void clear(WINDOW *win) {
		for (int i = -r; i <= r; i++) {
			for (int j = fmin(-(2*r), -1); j <= fmax(2*r, 1); j++) {
				mvwprintw(win, (int) y + i, 2*((int) x) + j, " ");
			}
		}
	}
	void redraw(WINDOW *win, float f) {clear(win); update(f); draw(win);}
};

struct Scoreboard {
	int hit; int required;
	int hitthisround;
	int rounds = 3;
	int score;
	Scoreboard() {}
	Scoreboard(int hit1, int required1, int score1) {
		hit = hit1;
		required = required1;
		score = score1;
	}
	void draw(WINDOW *below, int gameround) {
		mvwprintw(below, 1, 1, "           ");
		//print rounds
		mvwprintw(below, 1, 1, "Rounds: ");
		for (int i=0; i<rounds; i++) {
			mvwprintw(below, 1, i+9, "\u258e ");
		}
		//print hits
		mvwprintw(below, 2, 1, "Hits: ");
		for (int i=0; i<hit; i++)
			mvwaddwstr(below, 2, 7+(2*i), L"\u2593");
		for (int i=hit; i<required; i++)
			mvwaddwstr(below, 2, 7+(2*i), L"\u2591");
		//print score
		mvwaddwstr(below, 1, 39, L"Score: ");
		mvwprintw(below, 1, 46, "         ");
		mvwprintw(below, 1, 46, "%d", score);
		// print round
		mvwprintw(below, 2, 39, "r = %d", gameround);
	}
};

bool intersecting(PointCh a, PointCh b) {
	if (fabs(a.x - b.x) <= 1 + fmax(a.r, b.r) && fabs(a.y - b.y) <= 1 + fmax(a.r, b.r)) {
		return true;
	}
	return false;
}

void shoot_anim(WINDOW *win, PointCh *p, int x, int y) {
	//mouse_trafo(&y, &x, true);
	p->x = (float) x/2;
	p->y = (float) y;
	p->visible = true;
	p->draw(win);
	return;
}

void draw_borders(WINDOW *screen) { int x, y, i;
	getmaxyx(screen, y, x);
	// 4 corners
	mvwprintw(screen, 0, 0, "+");
	mvwprintw(screen, y - 1, 0, "+");
	mvwprintw(screen, 0, x - 1, "+");
	mvwprintw(screen, y - 1, x - 1, "+");
	// side
	for (i = 1; i < (y - 1); i++) {
		mvwprintw(screen, i, 0, "|");
		mvwprintw(screen, i, x - 1, "|");
	}
	// top and bottom
	for (i = 1; i < (x - 1); i++) { mvwprintw(screen, 0, i, "-");
		mvwprintw(screen, y - 1, i, "-");
	}
}

int kbhit(WINDOW * win)
{
    int ch = wgetch(win);

    if (ch != ERR) {
        ungetch(ch);
        return 1;
    } else {
        return 0;
    }
}

int changeOptions(WINDOW *optionsmenu, int * currentgamemode) {
	int option = 0;
	while (1) {
		int numoptions = 2;
		int optionscoord = 4;
		draw_borders(optionsmenu);
		for (int i=0; i<numoptions; i++) {
			if (i == option)
				mvwaddwstr(optionsmenu, optionscoord+i, 11, L"\u25b8");
			else 
				mvwaddwstr(optionsmenu, optionscoord+i, 11, L" ");
		}
		mvwprintw(optionsmenu, optionscoord+0, 13, "Back");
		mvwaddwstr(optionsmenu, optionscoord+1, 13, L"          ");
		mvwaddwstr(optionsmenu, optionscoord+1, 13, Gamemodes[*currentgamemode]->name.c_str());
		if (option == 1) {
			mvwaddwstr(optionsmenu, optionscoord+1, 11, L" ");
			mvwaddwstr(optionsmenu, optionscoord+1, 24, L" ");
			if (*currentgamemode != 0)
				mvwaddwstr(optionsmenu, optionscoord+1, 11, L"\u25c2");
			if (*currentgamemode != Gamemodes.size()-1)
				mvwaddwstr(optionsmenu, optionscoord+1, 24, L"\u25b8");
		} else {
			mvwaddwstr(optionsmenu, optionscoord+1, 11, L" ");
			mvwaddwstr(optionsmenu, optionscoord+1, 24, L" ");
		}
		wnoutrefresh(optionsmenu);
		doupdate();
		auto ch = wgetch(optionsmenu);
		switch(ch) {
			case KEY_UP: {
				sounds["menu1"]->play();
				option -= option == 0 ? 0 : 1;
				break;
			}
			case KEY_DOWN: {
				sounds["menu1"]->play();
				option += option == numoptions-1 ? 0 : 1;
				break;
			}
			case (int) ' ': {
				sounds["menu2"]->play();
				switch (option) {
					case 0: {
						return 0;
					}
					default:
						break;
				}
			}
			case KEY_LEFT: {
				sounds["menu1"]->play();
				if (option == 1)
					*currentgamemode -= *currentgamemode == 0 ? 0 : 1;
				break;
			}
			case KEY_RIGHT: {
				sounds["menu1"]->play();
				if (option == 1)
					*currentgamemode += *currentgamemode == Gamemodes.size() - 1 ? 0 : 1;
				break;
			}
			default:
				break;
		}
	}
}

int playMenu(WINDOW *menu) {
	int option = 0;
	sounds["hihatloop"]->set_volume(0);
	while (1) {
		int numoptions = 2;
		int optionscoord = 4;
		draw_borders(menu);
		for (int i=0; i<4; i++) {
			if (i == option)
				mvwaddwstr(menu, optionscoord+i, 11, L"\u25b8");
			else 
				mvwaddwstr(menu, optionscoord+i, 11, L" ");
		}
		mvwprintw(menu, optionscoord+0, 13, "Resume");
		mvwprintw(menu, optionscoord+1, 13, "Exit");
		wnoutrefresh(menu);
		doupdate();
		auto ch1 = wgetch(menu);
		switch (ch1) {
			case KEY_UP: {
				sounds["menu1"]->play();
				option -= option == 0 ? 0 : 1;
				break;
			}
			case KEY_DOWN: {
				sounds["menu1"]->play();
				option += option == numoptions-1 ? 0 : 1;
				break;
			}
			case (int) ' ': { //spacebar
				sounds["menu2"]->play();
				switch (option) {
					case 0: {
						goto resume;
					}
					case 1: {
						goto quit;
					}
				}
				break;
			}
			case (int) 'p': {
				sounds["menu2"]->play();
				goto resume;
			}
			default:
				break;
		}
	}
resume:
	sounds["hihatloop"]->set_volume(30);
	return 0;
quit:
	sounds["hihatloop"]->set_volume(30);
	return 1;
}

int playRound(WINDOW *win, WINDOW *below, int round, Scoreboard *score, WINDOW * menu, GameOptions gamemode, int gameround) {
	int k = 0;
	//draw  initial borders
	draw_borders(win);
	draw_borders(below);
	wnoutrefresh(win);
	wnoutrefresh(below);
	doupdate();
	//prepare game objects
	std::vector<PointCh*> gameObjects;
	for (int i=0; i<2; i++) {
		PointCh* duck1 = new PointCh(gamemode.attrs[round]);
		gameObjects.push_back(duck1);
	}
	PointCh* gun = new PointCh(-5, -5, BLOCK);
	gameObjects.push_back(gun);
	gun->visible = false;
	score->hitthisround = 0;
	gun->r = gamemode.special == 2 || gamemode.special == 3 ? 0 : 1;
	gun->isgun = true;
	score->rounds =3;
	score->draw(below, gameround);
	
	//prepare time
	auto t1 = std::chrono::system_clock::now();
	auto t2 = std::chrono::system_clock::now();
	float delta = -1;
	
	draw_borders(win);
	draw_borders(below);
	wnoutrefresh(win);
	wnoutrefresh(below);
	doupdate();
	//napms(rand() % 2000);
	
	sounds["hihatloop"]->play(0);

	while (1) {
		// time between loops for consistent movement
		t2 = std::chrono::system_clock::now();
		delta = delta == -1 ? 0 : (t2 - t1).count();
		t1 = t2;

		for (int i=0; i<gameObjects.size(); i++) {
			gameObjects[i]->redraw(win, delta);
			gameObjects[i]->lifetime += gameObjects[i]->visible ? delta/NANO : 0;
		}
		score->draw(below, gameround);
		//mvwprintw(win, 1, 1, "%f", gameObjects[0]->vect.mag);
		//mvwprintw(win, 2, 20, "%f", gameObjects[1]->vect.mag);
		//mvwprintw(win, 1, 1, "%f", gun->x);
		//mvwprintw(win, 2, 1, "%f", gun->y);

		if (gun->lifetime > 1e-1) { //gun flash effect
			gun->clear(win);
			gun->visible = false;
			gun->x = -5; gun->y = -5; //move offscreen
			gun->lifetime = 0;
		}
		if (kbhit(win)) {
			auto ch = wgetch(win);
			switch (ch) {
				case KEY_MOUSE: {
					MEVENT event;
					if (getmouse(&event) == OK) {
						if (event.bstate & BUTTON1_PRESSED) {
							if (score->rounds == 0) {
								sounds["noammo"]->play();
								break;
							}
							score->rounds -= score->rounds == 0 ? 0 : 1; // decrement rounds
							shoot_anim(win, gun, event.x, event.y);
							if (gamemode.special == 2 || gamemode.special == 3)
								sounds["gunshot2"]->play();
							else
								sounds["gunshot1"]->play();
							for (int i=0; i<gameObjects.size()-1; i++) { //check if gun hits any ducks
								if (intersecting(*gun, *gameObjects[i]) && gameObjects[i]->hit == false) {
									//mvwprintw(below, 1, 12, "HIT!");
									gameObjects[i]->hit = true;
									score->hit += 1;
									score->hitthisround += 1;
									//score increases by any value from 50 to 200 (rounded to 10s place) based on lifetime
									score->score += (200 - (int) (150.0/gameObjects[i]->escapetime*gameObjects[i]->lifetime)) / 10 * 10;
								}
							}
						}
					}
					break;
								}
				case (int) 'p': {//ESC key, pause
					sounds["menu2"]->play();
					k = playMenu(menu);
					if (k) {
						goto end;
					}
					delta = -1;
					break;
						 }
				// allows a second player to control ducks with wasd
				case (int) 'w' : {
					for (int i=0; i<gameObjects.size()-1; i++)
						gameObjects[i]->turn(0);
					break;
				}
				case (int) 'a' : {
					for (int i=0; i<gameObjects.size()-1; i++)
						gameObjects[i]->turn(1);
					break;
				}
				case (int) 's' : {
					for (int i=0; i<gameObjects.size()-1; i++)
						gameObjects[i]->turn(2);
					break;
				}
				case (int) 'd' : {
					for (int i=0; i<gameObjects.size()-1; i++)
						gameObjects[i]->turn(3);
					break;
				}
				case (int) '-': {
					goto end;
				}
				default: {
					break;
						 }
			}
		}

		//update screen
		draw_borders(win);
		draw_borders(below);
		wnoutrefresh(win);
		wnoutrefresh(below);
		doupdate();
		if (score->hitthisround == 2)
			sounds["hihatloop"]->stop();
		
		//check if game over
		bool roundover = true;
		for (int i=0; i<gameObjects.size()-1; i++) {
			if (!gameObjects[i]->visible) {
				continue;
			} else {
				roundover = false;
			}
		}
		if (roundover) {
			sounds["hihatloop"]->stop();
			napms(600);
			if (score->hitthisround == 0) {
				mvwprintw(win, 8, 20, "Great shots ;)");
			} else {
				mvwprintw(win, 7, 23, "Hit %d", score->hitthisround);
				sounds["success1"]->play();
			}
			draw_borders(win);
			draw_borders(below);
			wnoutrefresh(win);
			wnoutrefresh(below);
			doupdate();
			napms(1100);
			break;
		}
	}
	end:
	sounds["hihatloop"]->stop();
	wclear(win);
	wclear(below);
	wclear(menu);
	wnoutrefresh(win);
	wnoutrefresh(below);
	wnoutrefresh(menu);
	doupdate();
	return k;
}

void draw_title(WINDOW *mainmenu) {
	int titley = 2;
	int titlex = 4;
	mvwaddwstr(mainmenu, titley+0, titlex+0, L" _                   ");
	mvwaddwstr(mainmenu, titley+1, titlex+0, L"| |                 ");
	mvwaddwstr(mainmenu, titley+2, titlex+0, L"| |__    ___  __  __  _                    _   ");
	mvwaddwstr(mainmenu, titley+3, titlex+0, L"| '_ \\  / _ \\ \\ \\/ / | |                  | |  ");
	mvwaddwstr(mainmenu, titley+4, titlex+0, L"| |_) || (_) | >  <  | |__   _   _  _ __  | |_ ");
	mvwaddwstr(mainmenu, titley+5, titlex+0, L"|_.__/  \\___/ /_/\\_\\ | '_ \\ | | | || '_ \\ | __|");
	mvwaddwstr(mainmenu, titley+6, titlex+0, L"                     | | | || |_| || | | || |_");
	mvwaddwstr(mainmenu, titley+7, titlex+0, L"                     |_| |_| \\__,_||_| |_| \\__|");
}

int main() {
	setlocale(LC_ALL, "");
	initscr();
	cbreak();
	noecho();
	curs_set(0);
	mousemask(ALL_MOUSE_EVENTS, NULL);
	mouseinterval(0);
	srand(time(NULL));

	if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) < 0) {
    	endwin();
		std::cout << "Error initializing SDL audio - make sure SDL2 and SDL2_mixer installed\n";
		return 1;
	} Mix_AllocateChannels(16);
	sample duck_hit("audio/sfx_movement_jump13_landing.wav", 100, 0);
	sounds["duck_hit"] = &duck_hit;
	sample gunshot1("audio/sfx_weapon_shotgun1.wav", 60, 1);
	sounds["gunshot1"] = &gunshot1;
	sample gunshot2("audio/sfx_weapon_singleshot3.wav", 80, 2);
	sounds["gunshot2"] = &gunshot2;
	sample noammo("audio/sfx_wpn_noammo1.wav", 50, 3);
	sounds["noammo"] = &noammo;
	sample hihatloop("audio/KSHMR 128BPM Humanized Hat Loops 01.wav", 30, 4);
	sounds["hihatloop"] = &hihatloop;
	sample menu1("audio/sfx_menu_move1.wav", 50, 5);
	sounds["menu1"] = &menu1;
	sample menu2("audio/sfx_sounds_Blip4.wav", 50, 6);
	sounds["menu2"] = &menu2;
	sample success1("audio/KSHMR_Game_FX_29_Ready_Player_One.wav", 50, 7);
	sounds["success1"] = &success1;
	sample success2("audio/success.wav", 50, 8);
	sounds["success2"] = &success2;

	WINDOW * win = newwin(MAX_LINES, MAX_COLUMNS, 0, 0);
	keypad(win, TRUE);
	nodelay(win, TRUE);

	WINDOW * below = newwin(4, MAX_COLUMNS, MAX_LINES, 0);

	WINDOW * menu = newwin(MAX_LINES - 8, MAX_COLUMNS-20, 4, 10);
	keypad(menu, TRUE);
	
	WINDOW * mainmenu = newwin(MAX_LINES, MAX_COLUMNS, 0, 0);
	keypad(mainmenu, TRUE);
	
	WINDOW * optionsmenu = newwin(MAX_LINES-8, MAX_COLUMNS-20, 4, 10);
	keypad(optionsmenu, TRUE);
	
	setupGameOptions();
	int currentgamemode = 0;

	int option = 0;
	int numoptions = 3;
	int optionscoord = 12;
	int xcoord = 20;
	while (1) { //main menu loop
		draw_borders(mainmenu);
		draw_title(mainmenu);
		for (int i=0; i<numoptions; i++) {
			if (i == option)
				mvwaddwstr(mainmenu, optionscoord+i, xcoord, L"\u25b8");
			else 
				mvwaddwstr(mainmenu, optionscoord+i, xcoord, L" ");
		}
		mvwaddwstr(mainmenu, optionscoord+0, xcoord+2, L"Play");
		mvwaddwstr(mainmenu, optionscoord+1, xcoord+2, L"Options");
		mvwaddwstr(mainmenu, optionscoord+2, xcoord+2, L"Quit");
		wnoutrefresh(mainmenu);
		doupdate();
		auto ch = wgetch(mainmenu);
		switch (ch) {
			case KEY_UP: {
				option -= option == 0 ? 0 : 1;
				sounds["menu1"]->play();
				break;
						 }
			case KEY_DOWN: {
				option += option == numoptions-1 ? 0 : 1;
				sounds["menu1"]->play();
				break;
						 }
			case (int) ' ': { //spacebar
				sounds["menu2"]->play();
				switch (option) {
					case 0: {
						GameOptions gamemode = *Gamemodes[currentgamemode];
						Scoreboard* score = new Scoreboard(0, 6, 0);
						for (int i=3; i>0; i--) {
							draw_borders(win);
							mvwprintw(win, 8, 18, "Starting in %d", i);
							wnoutrefresh(win);
							doupdate();
							napms(500);
						}
						mvwprintw(win, 8, 18, "             ");
						for (int round = 1; ; round++) {
							gamemode.attrs = attrList(gamemode.special, (round-1)*5);
							for (int i=0; i < 5; ++i) {
								int k = playRound(win, below, i, score, menu, gamemode, round);
								if (k) {
									break;
								}
							}
							int c = 8;
							if (score->hit >= score->required) {
								sounds["success2"]->play();
								score->required += (round % 2 == 0 && score->required < 10) ? 1 : 0;
								napms(100);
								draw_borders(win);
								if (score->hit == 10) {
									mvwprintw(win, c-1, 15, "Perfect round! +750");
									score->score += 750;
								}
								score->hit = 0;
								mvwprintw(win, c, 18, "Next round: %d", round+1);
								mvwprintw(win, c+1, 13, "Press any key to continue");
								wnoutrefresh(win);
								doupdate();
								nodelay(win, FALSE);
								wgetch(win);
								nodelay(win, TRUE);
								mvwprintw(win, c-1, 15, "                     ");
								mvwprintw(win, c, 18, "              ");
								mvwprintw(win, c+1, 13, "                         ");
								napms(100);
							} else {
								napms(100);
								draw_borders(win);
								mvwprintw(win, c, 20, "Game Over");
								mvwprintw(win, c+1, 19, "Score: %d", score->score);
								if (score->score == 0)
									mvwaddwstr(win, c+1, 26, L"\u2639");
								mvwprintw(win, c+2, 13, "Press any key to continue");
								wnoutrefresh(win);
								doupdate();
								nodelay(win, FALSE);
								wgetch(win);
								nodelay(win, TRUE);
								mvwprintw(win, c, 16, "                ");
								mvwprintw(win, c+1, 15, "                       ");
								mvwprintw(win, c+2, 13, "                         ");
								break;
							}
						}
						break;
					}
					case 1: {
						changeOptions(optionsmenu, &currentgamemode);
						break;
					}
					case 2: {
						goto quit;
					}
				}
				break;
							}
			default:
				break;
		}
	}
	

quit:	
	endwin();
	return 0;
}
