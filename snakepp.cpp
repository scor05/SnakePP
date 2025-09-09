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


//Estados
enum class AppState { MENU, INSTRUCTIONS, HIGHSCORES, RUNNING, PAUSED, GAMEOVER, EXIT };
enum class Dir { UP, DOWN, LEFT, RIGHT };
struct Point { int row, column; };


//Estructuras de juego
struct SnakeGame {
WINDOW* win_board = nullptr; 
WINDOW* win_hud = nullptr; 

AppState state = AppState::MENU;
bool running = true; 
bool tick_enabled = false; 
bool request_redraw = true; 

std::deque<Point> snake; 
Dir dir = Dir::RIGHT;
Point food{ -1, -1 };
int score = 0;
int best_score = 0;
int speed_ms = 140; 

pthread_t th_input{}; 
pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

int menu_idx = 0;
std::vector<std::string> menu_items{"Iniciar partida", "Instrucciones", "Puntajes", "Salir"};

time_t start_time = 0;
};
