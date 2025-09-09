#include <ncurses.h>
#include <pthread.h>
#include <unistd.h>
#include <deque>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <ctime>
#include <cstdlib>


static const int BOARD_H = 20; 
static const int BOARD_W = 40;
static const int HUD_HEIGHT = 4;


static const char WALL_CH = '#';
static const char SNAKE_HEAD_CH = 'O';
static const char SNAKE_BODY_CH = 'o';
static const char FOOD_CH = '*';
