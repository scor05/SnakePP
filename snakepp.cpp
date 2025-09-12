/*
    Juego de Snake realizado en ASCII
    Autores: Santiago Cordero, Juan Salguero, Diego Gudiel
*/

#include <ncurses.h>
#include <pthread.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <ctime>
#include <cstdlib>

using namespace std;


static const char SNAKE_HEAD_CH = 'O';
static const char SNAKE_BODY_CH = 'o';
static const char FOOD_CH = '*';
static const int HUD_HEIGHT = 4;


//Estados
enum class GameState { MENU, INSTRUCTIONS, HIGHSCORES, PLAYER_CREATE, PLAYER_SELECT, 
                        RUNNING, PAUSED, GAMEOVER, EXIT };

struct ScoreEntry { string name; int score; };
struct Point { int row, column; };

//Estructuras de juego
static WINDOW* win_board = nullptr; 
static WINDOW* win_hud = nullptr;
static int maxY = 0, maxX = 0;
static AppState state = AppState::MENU;
static bool running = true;
static pthread_t input_thread;
static pthread_mutex_t ui_mtx = PTHREAD_MUTEX_INITIALIZER;
static int maxY = 0, maxX = 0;

// Snake en sí
static static int dir = KEY_RIGHT
static vector<Point> snake;
static static Point food;
static int speed_us = 100000; // aprox 10 fps

// Para guardar puntajes destacados
static int score = 0;
static int best_score;
static static const char* HIGHSCORES_FILE = "highscores_snake.txt";
static static const char* PLAYERS_FILE    = "players_snake.txt";
static string currentPlayer = "Player";
static vector<string> players;
static string playerCreateBuf;
static int playerSelectIdx = 0;

// Funciones de UI
static void drawBoard() {
    werase(win_board);
    box(win_board, 0, 0);
    mvwaddch(win_board, food.y, food.x, FOOD_CH);
    for (int i = 0; i < snake.size(); i++) {
        char ch = (i == 0 ? SNAKE_HEAD_CH : SNAKE_BODY_CH);
        mvwaddch(win_board, snake[i].y, snake[i].x, ch);
    }
    wrefresh(win_board);
}

static void drawHUD() {
    werase(win_hud);
    box(win_hud, 0, 0);
    mvwprintw(win_hud, 1, 2, "Jugador: %s   Puntaje: %d   Mejor: %d",
              currentPlayer.c_str(), score, bestScore);
    mvwprintw(win_hud, 2, 2, "WASD/Flechas mover | P pausa | Q menu");
    wrefresh(win_hud);
}

static void drawMenu() {
    clear();
    box(stdscr, 0, 0);
    mvprintw(maxY/2 - 4, maxX/2 - 12, "      SNAKE ASCII");
    mvprintw(maxY/2 - 1, maxX/2 - 12, "1) Iniciar partida");
    mvprintw(maxY/2 + 0, maxX/2 - 12, "2) Instrucciones");
    mvprintw(maxY/2 + 1, maxX/2 - 12, "3) Puntajes");
    mvprintw(maxY/2 + 2, maxX/2 - 12, "4) Seleccionar jugador");
    mvprintw(maxY/2 + 3, maxX/2 - 12, "5) Crear jugador");
    mvprintw(maxY/2 + 4, maxX/2 - 12, "6) Salir");
    mvprintw(maxY - 3,2,           "Jugador actual: %s", currentPlayer.c_str());
    refresh();
}

static void drawInstructions() {
    clear();
    box(stdscr, 0,0);
    mvprintw(2,3, "Objetivo: mover la serpiente (cabeza 'O') y comer '*' para sumar puntos.");
    mvprintw(4,3, "Controles: Flechas o WASD para mover. 'Q' para volver al menu.");
    mvprintw(6,3, "Elementos ASCII:");
    mvprintw(6,3, "  O = Cabeza de serpiente");
    mvprintw(7,3, "  o = Cuerpo de serpiente (y cola)");
    mvprintw(8,3, "  * = Comida");
    mvprintw(9,3, "  Paredes delimitadas por líneas");
    mvprintw(maxY-3,3,"Presione Q para volver al menú.");
    refresh();
}

static void drawHighscores() {
    clear();
    box(stdscr, 0, 0);
    mvprintw(2, 3, "PUNTAJES DESTACADOS");
    if (highscores.empty()) {
        mvprintw(4, 5, "Sin registros. Juega y vuelve para ver tus puntajes.");
    } else {
        int row = 4;
        for (int i = 0; i < highscores.size() && row < maxY-3; i++) {
            // Colocar índice, nombre alineados a la izquierda, score a la derecha
            mvprintw(row++, 5, "%2d) %-14s  %5d", i+1,
                     highscores[i].name.c_str(), highscores[i].score);
        }
    }
    mvprintw(maxY-3, 3, "Presiona Q para volver.");
    refresh();
}

static void drawPlayerSelect() {
    pthread_mutex_lock(&ui_mtx);
    clear();
    box(stdscr, 0, 0);
    mvprintw(2, 3, "SELECCIONAR JUGADOR (Flechas Arriba/Abajo, Enter elegir, Q volver)");
    int startRow = 4;
    for (int i = 0; i < players.size() && startRow + i < maxY - 2; i++) {
        if (i == playerSelectIdx) attron(A_REVERSE); // A_REVERSE invierte colores de terminal del texto
        mvprintw(startRow + i, 5, "%s", players[i].c_str());
        if (i == playerSelectIdx) attroff(A_REVERSE);
    }
    refresh();
    pthread_mutex_unlock(&ui_mtx);
}

static void drawPlayerCreate() {
    pthread_mutex_lock(&ui_mtx);
    clear();
    box(stdscr, 0, 0);
    mvprintw(2, 3, "CREAR JUGADOR (Enter guardar, ESC/Q cancelar)");
    mvprintw(4, 3, "Ingrese el nombre del jugador (1 a 16 caracteres): ");
    mvprintw(6, 5, "%s", playerCreateBuf.c_str());
    refresh();
    pthread_mutex_unlock(&ui_mtx);
}

static void drawGameOver() {
    clear();
    box(stdscr, 0, 0);
    mvprintw(maxY/2 - 1, maxX/2 - 6, "GAME OVER");
    mvprintw(maxY/2 + 0, maxX/2 - 14, "Jugador: %s  Puntaje: %d",
             currentPlayer.c_str(), score);
    mvprintw(maxY/2 + 2, maxX/2 - 14, "R: Reiniciar  |  Q: Salir a menú");
    refresh();
}

int main(){
    setlocale(LC_ALL, "");
    srand(time(NULL));
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    getmaxyx(stdscr, maxY, maxX);

    win_board = newwin(maxY - HUD_HEIGHT, maxX, 0, 0);
    win_hud   = newwin(HUD_HEIGHT, maxX, maxY - HUD_HEIGHT, 0);

    return 0
}