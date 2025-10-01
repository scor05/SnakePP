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
#include <sstream>
#include <cstdlib>
#include <locale.h>
#include <sys/time.h>

using namespace std;

static const char SNAKE_HEAD_CH = 'O';
static const char SNAKE_BODY_CH = 'o';
static const char FOOD_CH = '*';
static const char OBSTACLE_CH = '#';
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
static GameState state = GameState::MENU;
static pthread_t input_thread;
static pthread_mutex_t ui_mtx = PTHREAD_MUTEX_INITIALIZER;

// Snake
static int dir = KEY_RIGHT;
static vector<Point> snake;
static vector<Point> obstacles;
static Point food;
static int speed_us = 100000; // aprox 10 fps

// Obstáculos
static pthread_t obstacle_thread;
static pthread_mutex_t game_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t obst_mtx = PTHREAD_MUTEX_INITIALIZER; 
static pthread_cond_t obst_cond = PTHREAD_COND_INITIALIZER;
static volatile int obst_requests = 0;

// MODO TURBOO
static int SPEED_FPS_NORMAL = 10;
static int SPEED_FPS_BOOST = 25;
static int SPEED_US_NORMAL;
static int SPEED_US_BOOST;
static const int BOOST_TIMER_US = 120; // ms desde que se suelte la L
static long long boost_until_us = 0;

static vector<ScoreEntry> highscores;
static int bestScore = 0;
static volatile bool app_running = true;
static GameState last_drawn = GameState::EXIT;

// Para guardar puntajes destacados
static int score = 0;
static const char* HIGHSCORES_FILE = "highscores_snake.txt";
static const char* PLAYERS_FILE    = "players_snake.txt";
static string currentPlayer = "Player";
static vector<string> players;
static string playerCreateBuf;
static int playerSelectIdx = 0;

// Funciones de manejo de Jugadores y Puntajes
static void loadPlayers() {
    players.clear();
    ifstream f(PLAYERS_FILE);
    string name;
    while (getline(f, name)) {
        if (!name.empty()) players.push_back(name);
    }
    if (players.empty()) players.push_back("Player");
    if (find(players.begin(), players.end(), currentPlayer) == players.end())
        currentPlayer = players.front();
    playerSelectIdx = 0;
}

static void savePlayers() {
    ofstream f(PLAYERS_FILE, ios::trunc);
    for (auto &p : players) f << p << "\n";
}

static void loadHighscores() {
    highscores.clear();
    ifstream f(HIGHSCORES_FILE);
    if (!f) return;
    string line;
    while (getline(f, line)) {
        if (line.empty()) continue;
        istringstream iss(line);
        string name; int sc;
        if (getline(iss, name, ';') && (iss >> sc)) {
            highscores.push_back({name, sc});
        }
    }
    sort(highscores.begin(), highscores.end(),
         [](const ScoreEntry& a, const ScoreEntry& b){ return a.score > b.score; });
    if (highscores.size() > 20) highscores.resize(20);
}

static void appendHighscore(const string& name, int sc) {
    { ofstream f(HIGHSCORES_FILE, ios::app); if (f) f << name << ';' << sc << "\n"; }
    highscores.push_back({name, sc});
    sort(highscores.begin(), highscores.end(),
         [](const ScoreEntry& a, const ScoreEntry& b){ return a.score > b.score; });
    if (highscores.size() > 20) highscores.resize(20);
}

static int bestScoreFor(const string& name) {
    int best = 0;
    for (auto &e : highscores) if (e.name == name) best = max(best, e.score);
    return best;
}

// Funciones de UI
static void drawBoard() {
    werase(win_board);
    box(win_board, 0, 0);

    // obstáculos
    pthread_mutex_lock(&game_mtx);
    for (const auto& p : obstacles) mvwaddch(win_board, p.row, p.column, OBSTACLE_CH);
    pthread_mutex_unlock(&game_mtx);

    // snake y comida
    mvwaddch(win_board, food.row, food.column, FOOD_CH);
    for (size_t i = 0; i < snake.size(); i++) {
        char ch = (i == 0 ? SNAKE_HEAD_CH : SNAKE_BODY_CH);
        mvwaddch(win_board, snake[i].row, snake[i].column, ch);
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
    mvprintw(4,3, "Controles: Flechas o WASD para mover, 'L' para acelerar, 'Q' para volver al menu.");
    mvprintw(6,3, "Elementos ASCII:");
    mvprintw(6,3, "  O = Cabeza de serpiente");
    mvprintw(7,3, "  o = Cuerpo de serpiente (y cola)");
    mvprintw(8,3, "  * = Comida");
    mvprintw(9,3, "  # = Obstáculo (se genera cada vez que la serpiente come)");
    mvprintw(10,3, "  Paredes delimitadas por líneas");
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
        for (size_t i = 0; i < highscores.size() && row < maxY-3; i++) {
            mvprintw(row++, 5, "%2zu) %-14s  %5d", i+1,
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
    for (size_t i = 0; i < players.size() && startRow + i < maxY - 2; i++) {
        if ((int)i == playerSelectIdx) attron(A_REVERSE);
        mvprintw(startRow + i, 5, "%s", players[i].c_str());
        if ((int)i == playerSelectIdx) attroff(A_REVERSE);
    }
    refresh();
    pthread_mutex_unlock(&ui_mtx);
}

static void drawPlayerCreate() {
    pthread_mutex_lock(&ui_mtx);
    clear();
    box(stdscr, 0, 0);
    mvprintw(2, 3, "CREAR JUGADOR (Enter guardar, ESC/Q cancelar)");
    mvprintw(4, 3, "Nombre (1..16): ");
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

// ===== Cálculos =====
static int fps_to_us(int fps) {
    // 1s = 10^6 us
    return (fps > 0) ? (1000000 / fps) : 100000;
}

static long long now_us() {
    timeval tv; gettimeofday(&tv, nullptr);
    return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

static bool boostActive() {
    return now_us() < boost_until_us;
}

// ===== Juego =====
static bool isPointOnSnake(const Point& p) {
    for (const auto& s : snake) if (s.row == p.row && s.column == p.column) return true;
    return false;
}

static bool isPointOnObstacle(const Point& p) {
    for (const auto& o : obstacles) if (o.row == p.row && o.column == p.column) return true;
    return false;
}

// para llamar cada vez que se hace eat()
static void requestObstacle() {
    pthread_mutex_lock(&obst_mtx);
    obst_requests++;
    pthread_cond_signal(&obst_cond);
    pthread_mutex_unlock(&obst_mtx);
}

static void spawnFood() {
    int h, w; getmaxyx(win_board, h, w);
    Point p;
    // revisar que no se genere comida en un obstáculo
    for (int attempt = 0; attempt < 200; attempt++){
        p.column = (rand() % (w - 2)) + 1;
        p.row = (rand() % (h - 2)) + 1;

        pthread_mutex_lock(&game_mtx);
        bool bad = isPointOnObstacle(p) || isPointOnSnake(p);
        pthread_mutex_unlock(&game_mtx);

        if (!bad) { food = p; return; }
    }
}

static void initGame() {
    snake.clear();
    pthread_mutex_lock(&game_mtx);
    obstacles.clear();
    pthread_mutex_unlock(&game_mtx);
    int h, w; getmaxyx(win_board, h, w);
    snake.push_back({ h/2, w/2 }); // row, column
    score = 0;
    dir = KEY_RIGHT;
    spawnFood();
    bestScore = bestScoreFor(currentPlayer);
}

static bool advance() {
    int h, w; getmaxyx(win_board, h, w);
    Point head = snake[0];

    if      (dir == KEY_UP)    head.row--;
    else if (dir == KEY_DOWN)  head.row++;
    else if (dir == KEY_LEFT)  head.column--;
    else if (dir == KEY_RIGHT) head.column++;

    // paredes del juego
    if (head.column <= 0 || head.column >= w-1 || head.row <= 0 || head.row >= h-1)
        return false;

    // choque con obstáculo
    pthread_mutex_lock(&game_mtx);
    bool hit_obstacle = isPointOnObstacle(head);
    pthread_mutex_unlock(&game_mtx);
    if (hit_obstacle) return false;

    bool eating = (head.column == food.column && head.row == food.row);

    if (!eating && !snake.empty()) snake.pop_back();

    for (const auto& p : snake) if (p.column == head.column && p.row == head.row) return false;

    snake.insert(snake.begin(), head);

    if (eating) {
        score++;
        if (score > bestScore) bestScore = score;
        requestObstacle();
        spawnFood();
    }
    return true;
}

static void* obstacleThread(void*) {
    while (app_running) {
        // solo actúa el hilo cuando haya una solicitud de obstáculo (cuando come el snake)
        pthread_mutex_lock(&obst_mtx);
        while (app_running && obst_requests == 0)
            pthread_cond_wait(&obst_cond, &obst_mtx);

        if (!app_running) { pthread_mutex_unlock(&obst_mtx); break; }
        obst_requests--;
        pthread_mutex_unlock(&obst_mtx);

        int h, w;
        pthread_mutex_lock(&ui_mtx);
        getmaxyx(win_board, h, w);
        pthread_mutex_unlock(&ui_mtx);

        bool placed = false;
        for (int attempt = 0; attempt < 100 && !placed; attempt++) {
            int len = 2 + (rand() % 2); // 2 o 3 unidades de longitud aleatoriamente
            bool horizontal = (rand() % 2) == 0; // linea horizontal o vertical
            int r = (rand() % (h - 2)) + 1; // mod para que esté dentro del rango
            int c = (rand() % (w - 2)) + 1;

            if (horizontal) {
                if (c + len - 1 >= w - 1) c = (w - 1) - len;
            } else {
                if (r + len - 1 >= h - 1) r = (h - 1) - len;
            }

            vector<Point> cells;
            for (int i = 0; i < len; i++) {
                cells.push_back({ r + (horizontal ? 0 : i), c + (horizontal ? i : 0) });
            }

            pthread_mutex_lock(&game_mtx);
            bool ok = true;
            for (const auto& p : cells) {
                if (p.row <= 0 || p.row >= h - 1 || p.column <= 0 || p.column >= w - 1) { ok = false; break; }
                if (isPointOnSnake(p) || isPointOnObstacle(p) || (p.row == food.row && p.column == food.column)) { ok = false; break; }
            }
            if (ok) {
                obstacles.insert(obstacles.end(), cells.begin(), cells.end());
                placed = true;
            }
            pthread_mutex_unlock(&game_mtx);
        }
    }
    return nullptr;
}


static void* inputThread(void*) {
    pthread_mutex_lock(&ui_mtx);
    nodelay(stdscr, TRUE);
    pthread_mutex_unlock(&ui_mtx);

    while (app_running) {
        pthread_mutex_lock(&ui_mtx);
        int ch = getch();
        pthread_mutex_unlock(&ui_mtx);
        if (ch == ERR) { usleep(5000); continue; }

        bool isBack = (ch == KEY_BACKSPACE || ch == 127 || ch == 8);

        switch (state) {
            case GameState::MENU:
                if      (ch == '1') { initGame(); state = GameState::RUNNING; }
                else if (ch == '2') { state = GameState::INSTRUCTIONS; last_drawn = GameState::EXIT; }
                else if (ch == '3') { state = GameState::HIGHSCORES;   last_drawn = GameState::EXIT; }
                else if (ch == '4') { state = GameState::PLAYER_SELECT; last_drawn = GameState::EXIT; }
                else if (ch == '5') { playerCreateBuf.clear(); state = GameState::PLAYER_CREATE; last_drawn = GameState::EXIT; }
                else if (ch == '6' || ch == 'q' || ch == 'Q') { state = GameState::EXIT; }
                break;

            case GameState::INSTRUCTIONS:
            case GameState::HIGHSCORES:
                if (ch == 'q' || ch == 'Q') { state = GameState::MENU; last_drawn = GameState::EXIT; }
                break;

            case GameState::PLAYER_SELECT:
                if (ch == KEY_UP)    { if (playerSelectIdx > 0) playerSelectIdx--; last_drawn = GameState::EXIT; }
                if (ch == KEY_DOWN)  { if (playerSelectIdx < (int)players.size()-1) playerSelectIdx++; last_drawn = GameState::EXIT; }
                if (ch == '\n' || ch == KEY_ENTER) {
                    if (!players.empty()) {
                        currentPlayer = players[playerSelectIdx];
                        bestScore = bestScoreFor(currentPlayer);
                    }
                    state = GameState::MENU; last_drawn = GameState::EXIT;
                }
                if (ch == 'q' || ch == 'Q' || ch == 27 /*ESC*/) { state = GameState::MENU; last_drawn = GameState::EXIT; }
                break;

            case GameState::PLAYER_CREATE:
                if (ch == '\n' || ch == KEY_ENTER) {
                    string s = playerCreateBuf;
                    while (!s.empty() && (s.back()==' ' || s.back()=='\t')) s.pop_back();
                    size_t i0 = 0; while (i0 < s.size() && (s[i0]==' ' || s[i0]=='\t')) i0++;
                    s = s.substr(i0);
                    if (!s.empty()) {
                        if (find(players.begin(), players.end(), s) == players.end()) {
                            players.push_back(s);
                            savePlayers();
                        }
                        currentPlayer = s;
                        bestScore = bestScoreFor(currentPlayer);
                    }
                    playerCreateBuf.clear();
                    state = GameState::MENU; last_drawn = GameState::EXIT;
                } else if (isBack) {
                    if (!playerCreateBuf.empty()) playerCreateBuf.pop_back();
                    last_drawn = GameState::EXIT;
                } else if (ch == 'q' || ch == 'Q' || ch == 27 /*ESC*/) {
                    playerCreateBuf.clear();
                    state = GameState::MENU; last_drawn = GameState::EXIT;
                } else if (ch >= 32 && ch <= 126) {
                    if (ch == ';') ch = '_';
                    if ((int)playerCreateBuf.size() < 16) {
                        playerCreateBuf.push_back((char)ch);
                        last_drawn = GameState::EXIT;
                    }
                }
                break;

            case GameState::RUNNING:
                if ((ch == KEY_UP   || ch == 'w') && dir != KEY_DOWN)  dir = KEY_UP;
                if ((ch == KEY_DOWN || ch == 's') && dir != KEY_UP)    dir = KEY_DOWN;
                if ((ch == KEY_LEFT || ch == 'a') && dir != KEY_RIGHT) dir = KEY_LEFT;
                if ((ch == KEY_RIGHT|| ch == 'd') && dir != KEY_LEFT)  dir = KEY_RIGHT;

                if (ch == 'l'){
                    boost_until_us =  now_us() + (long long) BOOST_TIMER_US * 1000LL;
                }

                if (ch == 'p' || ch == 'P') { state = GameState::PAUSED; last_drawn = GameState::EXIT; }
                if (ch == 'q' || ch == 'Q') {
                    appendHighscore(currentPlayer, score);
                    state = GameState::MENU; last_drawn = GameState::EXIT;
                }
                break;

            case GameState::PAUSED:
                if (ch == 'p' || ch == 'P') { state = GameState::RUNNING; last_drawn = GameState::EXIT; }
                if (ch == 'q' || ch == 'Q') {
                    appendHighscore(currentPlayer, score);
                    state = GameState::MENU; last_drawn = GameState::EXIT;
                }
                break;

            case GameState::GAMEOVER:
                if (ch == 'r' || ch == 'R') { initGame(); state = GameState::RUNNING; last_drawn = GameState::EXIT; }
                if (ch == 'q' || ch == 'Q') { state = GameState::MENU; last_drawn = GameState::EXIT; }
                break;

            case GameState::EXIT:
            default:
                app_running = false;
                break;
        }
    }
    return nullptr;
}

int main(){
    setlocale(LC_ALL, "");
    srand(time(NULL));
    initscr();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    getmaxyx(stdscr, maxY, maxX);
    SPEED_US_BOOST = fps_to_us(SPEED_FPS_BOOST);
    SPEED_US_NORMAL = fps_to_us(SPEED_FPS_NORMAL);

    win_board = newwin(maxY - HUD_HEIGHT, maxX, 0, 0);
    win_hud = newwin(HUD_HEIGHT, maxX, maxY - HUD_HEIGHT, 0);

    loadPlayers();
    loadHighscores();
    bestScore = bestScoreFor(currentPlayer);

    pthread_create(&input_thread, nullptr, inputThread, nullptr);
    pthread_create(&obstacle_thread, nullptr, obstacleThread, nullptr);

    while (app_running) {
        if (state != last_drawn) {
            if      (state == GameState::MENU)          drawMenu();
            else if (state == GameState::INSTRUCTIONS)  drawInstructions();
            else if (state == GameState::HIGHSCORES)    drawHighscores();
            else if (state == GameState::PLAYER_SELECT) drawPlayerSelect();
            else if (state == GameState::PLAYER_CREATE) drawPlayerCreate();
            else if (state == GameState::GAMEOVER)      drawGameOver();
            last_drawn = state;
        }

        if (state == GameState::RUNNING) {
            if (!advance()) {
                appendHighscore(currentPlayer, score);
                state = GameState::GAMEOVER; last_drawn = GameState::EXIT;
            } else {
                drawBoard();
                drawHUD();
            }
            speed_us = boostActive() ? SPEED_US_BOOST: SPEED_US_NORMAL;
            usleep(speed_us);
        } else if (state == GameState::PLAYER_SELECT || state == GameState::PLAYER_CREATE) {
            if (state == GameState::PLAYER_SELECT)  drawPlayerSelect();
            if (state == GameState::PLAYER_CREATE)  drawPlayerCreate();
            usleep(16000);
        } else if (state == GameState::EXIT) {
            app_running = false;
        } else {
            usleep(16000);
        }
    }

    pthread_join(input_thread, nullptr);

    pthread_mutex_lock(&obst_mtx);
    app_running = false;
    pthread_cond_signal(&obst_cond);
    pthread_mutex_unlock(&obst_mtx);

    pthread_join(obstacle_thread, nullptr);
    delwin(win_hud);
    delwin(win_board);
    endwin();
    return 0;
}