#include <stdlib.h>
#include <dirent.h>
#include <curses.h>
#include <menu.h>
#include <string.h>
#include <math.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <pwd.h>
#include "raudio/raudio.h"

#define NOB_TEMP_CAPACITY (15*sizeof(char*))
#define NOB_IMPLEMENTATION
#include "../nob.h"

#define MAX_MUSIC_VOLUME 200
#define TUPL_TEXT "TUPL  -  Simple TUI Music Player"
#define NO_MATCH_TEXT " NO MATCH FOUND "
#define PAUSED_TEXT " Paused  "
#define PLAYING_TEXT " Playing "
#define STOPED_TEXT " Stoped  "


#define make_hline(y) \
    do { \
        mvhline(y, 1, 0, COLS - 2); \
        mvaddch(y, 0, ACS_LTEE); \
        mvaddch(y, COLS - 1, ACS_RTEE); \
    } while(0)

#define sec_to_min(float_minutes) (int)float_minutes/60+(float)((int)float_minutes%60)/100.f

typedef struct {
    ITEM **items;
    size_t count;
    size_t capacity;
} Items_Array;

typedef enum {
    NORMAL,
    SEEK,
    FIND
} Mode;

typedef enum {
    STOP,
    LOOP,
    NEXT,
    RANDOM,

    QUEUE_MODES_COUNT
} Queue_mode;


#define select_music_current_selection \
    do { \
        select_music(&music, item_description(current_item(basic_menu)), item_name(current_item(basic_menu))); \
        music.looping = queue_mode == LOOP; \
        current_music = current_item(basic_menu); \
        SetMusicVolume(music, music_volume/100.f); \
    } while(0)
void select_music(Music *music, const char *dir_path, const char *music_path) {
    mvhline(LINES-5+1, 1, ' ', COLS - 14);

    Nob_String_Builder path = {0};
    nob_sb_append_cstr(&path, dir_path);
    nob_sb_append_cstr(&path, music_path);
    nob_sb_append_null(&path);

    UnloadMusicStream(*music);
    mvaddnstr(LINES-5+1, 2, music_path, COLS - 15);
    *music = LoadMusicStream(path.items);

    nob_sb_free(path);

    PlayMusicStream(*music);
    mvaddch(LINES-5+1, COLS - strlen(STOPED_TEXT) - 2, ACS_VLINE);
    addstr(PLAYING_TEXT);
}

#define set_volume(volume) set_music_volume(&music, volume)
void set_music_volume(Music *music, int music_volume /* in percent */) {
    SetMusicVolume(*music, music_volume/100.f);

    mvaddstr(LINES-2, 1, nob_temp_sprintf(" %d%% / %d%% ", music_volume, MAX_MUSIC_VOLUME));
    nob_temp_reset();
    addch(ACS_VLINE);

    int x = 0;
    getyx(stdscr, x, x);
    int max_length = COLS - x;
    hline(ACS_BULLET, max_length - 1);
    int bar_length = (float)music_volume/MAX_MUSIC_VOLUME * max_length;
    hline(ACS_CKBOARD, bar_length - 1);
}

#define next_music \
    do { \
        if(item_count(basic_menu) > item_index(current_item(basic_menu)) + 1) \
            menu_driver(basic_menu, REQ_DOWN_ITEM); \
        else \
            menu_driver(basic_menu, REQ_FIRST_ITEM); \
    } while(0)

#define endsin(string, end) (strlen(string) >= strlen(end) && strcmp((end), &((string)[strlen(string) - strlen(end)])) == 0)

void get_music_files(Items_Array *out, const char *path) {
    DIR *FD = NULL;
    if((FD = opendir(path)) == NULL) {
        printf("Directory \"%s\" not found", path);
        exit(-1);
    }

    struct dirent* in_file;
    while ((in_file = readdir(FD))) {
        if (in_file->d_name[0] == '.') continue; // Skip hidden files
        else if (in_file->d_type == DT_REG) {
            if (
                endsin(in_file->d_name, ".wav")  ||
                endsin(in_file->d_name, ".qoa")  ||
                endsin(in_file->d_name, ".ogg")  ||
                endsin(in_file->d_name, ".mp3")  ||
                endsin(in_file->d_name, ".flac") ||
                endsin(in_file->d_name, ".xm")   ||
                endsin(in_file->d_name, ".mod")
            ) {
                ITEM *it = new_item(in_file->d_name, path);
                if(it != NULL) // Please only use ASCII in your filenames
                    nob_da_append(out, it);
            }
        }
        else if (in_file->d_type == DT_DIR) {
            Nob_String_Builder new_path = {0};
            nob_sb_append_cstr(&new_path, path);
            nob_sb_append_cstr(&new_path, in_file->d_name);
            nob_sb_append_cstr(&new_path, "/");
            nob_sb_append_null(&new_path);

            get_music_files(out, new_path.items);

            // new_path.items should not get freed, because it's gonna get used later
        }
    }
}

#define redraw redraw_ui(basic_menu, music_volume, &music, current_music)
void redraw_ui(MENU *basic_menu, int music_volume, Music *music, ITEM *current_music) {
    unpost_menu(basic_menu);
    set_menu_sub(basic_menu, derwin(stdscr, LINES-5 - 5, COLS - 2, 3, 1));
    set_menu_format(basic_menu, LINES-5 - 5, 1);
    post_menu(basic_menu);

    box(stdscr, 0, 0);
    make_hline(2);
    make_hline(LINES-5-2);
    make_hline(LINES-5);
    make_hline(LINES-5+2);
    make_hline(LINES-5+2);
    mvaddch(1, 2, ACS_VLINE);
    mvaddch(0, 2, ACS_TTEE);
    mvaddch(2, 2, ACS_BTEE);

    mvaddstr(LINES-2, 1, nob_temp_sprintf(" %d%% / %d%% ", music_volume, MAX_MUSIC_VOLUME));
    nob_temp_reset();
    addch(ACS_VLINE);

    int x = 0;
    getyx(stdscr, x, x);
    int max_length = COLS - x;
    hline(ACS_BULLET, max_length - 1);
    int bar_length = (float)music_volume/MAX_MUSIC_VOLUME * max_length;
    hline(ACS_CKBOARD, bar_length - 1);

    mvaddch(LINES-5+1, COLS - strlen(STOPED_TEXT) - 2, ACS_VLINE);
    if(IsMusicStreamPlaying(*music))
        addstr(PLAYING_TEXT);
    else
        addstr(STOPED_TEXT);

    mvhline(LINES-5+1, 1, ' ', COLS - 14);
    mvaddnstr(LINES-5+1, 2, item_name(current_music), COLS - 15);
    set_current_item(basic_menu, current_music);

    mvaddch(1, 1, 'N');
    mvhline(1, 3, ' ', COLS - 4);
    mvaddnstr(1, 4, TUPL_TEXT,  COLS - 5);

    mvaddch(LINES-5+1, COLS - strlen(STOPED_TEXT) - 5, ' ');
    addch(ACS_VLINE);
    addch('S');
}

struct music_updater_thread_args {
    Music *music;
    pthread_mutex_t *mut;
};
void *music_updater_thread(void *args) {
    struct timespec sleep_time;
    sleep_time.tv_sec = 0;
    sleep_time.tv_nsec = 1000000;

    struct music_updater_thread_args *mut_args = (struct music_updater_thread_args *)args;

    while(1) {
        nanosleep(&sleep_time, NULL);
        pthread_mutex_lock(mut_args->mut);
        UpdateMusicStream(*(mut_args->music));
        pthread_mutex_unlock(mut_args->mut);
    }
    return NULL;
}

int main() {
    initscr();
    keypad(stdscr, TRUE);
    nonl();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    ESCDELAY = 100;
    srand(time(NULL));
    puts("\033[?12l"); // disable cursor blinking

    struct timespec sleep_time;
    sleep_time.tv_sec = 0;
    sleep_time.tv_nsec = 10 * 1000000;

    // This also can't be freed
    Nob_String_Builder music_path = {0};
    nob_sb_append_cstr(&music_path, getpwuid(getuid())->pw_dir);
    nob_sb_append_cstr(&music_path, "/Music/");
    nob_sb_append_null(&music_path);

    Items_Array paths = {0};
    get_music_files(&paths, music_path.items);
    nob_da_append(&paths, NULL);

    MENU *basic_menu = new_menu((ITEM **)paths.items);

    InitAudioDevice();

    Music music = {0};
    int music_volume = 100;
    Queue_mode queue_mode = STOP;
    ITEM *current_music = NULL;

    pthread_t music_updater_thread_pid;
    pthread_mutex_t music_updater_thread_mut;
    struct music_updater_thread_args mut_args = {&music, &music_updater_thread_mut};
    pthread_create(&music_updater_thread_pid, NULL, music_updater_thread, (void *)&mut_args);

    redraw;

    Mode mode = NORMAL;

    int isDone = 0;
    while(!isDone) {
        // Without this it uses ~100% of my CPU
        nanosleep(&sleep_time, NULL);

        float music_length = 0.f, music_played = 0.f;
        if(IsMusicReady(music)) {
            music_length = GetMusicTimeLength(music);
            music_played = GetMusicTimePlayed(music);
        }
        mvaddstr(LINES-5-1, 1, nob_temp_sprintf(" %.2f / %.2f ", sec_to_min(music_played), sec_to_min(music_length)));
        nob_temp_reset();
        addch(ACS_VLINE);

        int x = 0;
        getyx(stdscr, x, x);
        int max_length = COLS - x;
        hline(ACS_BULLET, max_length - 1);
        int bar_length = 0;
        if(IsMusicReady(music))
            bar_length = (float)music_played/music_length * max_length;
        hline(ACS_CKBOARD, bar_length);

        if(music_played == 0.f && !IsMusicStreamPlaying(music) && IsMusicReady(music)) {
            switch (queue_mode) {
                case STOP:
                    PlayMusicStream(music);
                    PauseMusicStream(music);
                    mvaddch(LINES-5+1, COLS - strlen(STOPED_TEXT) - 2, ACS_VLINE);
                    addstr(STOPED_TEXT);
                    break;
                case NEXT:
                    set_current_item(basic_menu, current_music);
                    next_music;
                    select_music_current_selection;
                    break;
                case RANDOM:
                    // This might potentially be a horrible way of implementing this, but idk (and also i don't care)
                    int rand_offset = rand()%item_count(basic_menu);
                    set_current_item(basic_menu, current_music);
                    for(int i = 0; i < rand_offset; ++i)
                        next_music;
                    select_music_current_selection;
                    break;
                // Stop compiler from complaining
                case LOOP:
                case QUEUE_MODES_COUNT:
                    break;
            }
        }

        move(1, 1);
        int c = getch();
        if(c == -1) continue;

        switch(mode) {
            case NORMAL:
                switch(c) {
                    case 'q':
                        isDone = 1;
                        break;
                    case 27: // esc
                        menu_driver(basic_menu, REQ_CLEAR_PATTERN);
                        mvhline(1, 3, ' ', COLS - 4);
                        mvaddnstr(1, 4, TUPL_TEXT,  COLS - 5);
                        break;
                    case 'k':
                    case KEY_UP:
                        menu_driver(basic_menu, REQ_UP_ITEM);
                        break;
                    case 'j':
                    case KEY_DOWN:
                        menu_driver(basic_menu, REQ_DOWN_ITEM);
                        break;
                    case 'h':
                    case KEY_LEFT:
                        menu_driver(basic_menu, REQ_PREV_MATCH);
                        break;
                    case 'l':
                    case KEY_RIGHT:
                        menu_driver(basic_menu, REQ_NEXT_MATCH);
                        break;
                    case 13: // enter
                        select_music_current_selection;

                        /* FALLTHRU */
                    case 's':
                       mode = SEEK;
                       mvaddch(1, 1, 'S');
                       break;
                    case 'f':
                       mode = FIND;
                       mvaddch(1, 1, 'F');
                       break;
                    case 'm':
                        if(++queue_mode >= QUEUE_MODES_COUNT) queue_mode = STOP;

                        music.looping = queue_mode == LOOP;

                        mvaddch(LINES-5+1, COLS - strlen(STOPED_TEXT) - 5, ' ');
                        addch(ACS_VLINE);
                        switch(queue_mode) {
                            case STOP:
                                addch('S');
                                break;
                            case LOOP:
                                addch('L');
                                break;
                            case NEXT:
                                addch('N');
                                break;
                            case RANDOM:
                                addch('R');
                                break;
                            case QUEUE_MODES_COUNT:
                                NOB_UNREACHABLE("How?");
                                break;
                        }
                        break;
                    case 'r':
                        redraw;
                        break;
                }
                break;
            case SEEK:
                switch(c) {
                    case 27: // esc
                        mode = NORMAL;
                        mvaddch(1, 1, 'N');
                        break;
                    case 'k':
                    case KEY_UP:
                        music_volume = MIN(music_volume+5, MAX_MUSIC_VOLUME);
                        set_volume(music_volume);
                        break;
                    case 'j':
                    case KEY_DOWN:
                        music_volume = MAX(music_volume-5, 0);
                        set_volume(music_volume);
                        break;
                    case 'h':
                    case KEY_LEFT:
                        pthread_mutex_lock(&music_updater_thread_mut);
                        if(IsMusicReady(music)) SeekMusicStream(music, fmax(0.07f, GetMusicTimePlayed(music) - 10.f));
                        pthread_mutex_unlock(&music_updater_thread_mut); // ^^^^^ This makes me wanna cry
                        break;
                    case 'l':
                    case KEY_RIGHT:
                        pthread_mutex_lock(&music_updater_thread_mut);
                        if(IsMusicReady(music)) SeekMusicStream(music, fmin(GetMusicTimeLength(music), GetMusicTimePlayed(music) + 10.f));
                        pthread_mutex_unlock(&music_updater_thread_mut);
                        break;
                    case ' ':
                        if(IsMusicStreamPlaying(music)) {
                            PauseMusicStream(music);
                            mvaddch(LINES-5+1, COLS - strlen(STOPED_TEXT) - 2, ACS_VLINE);
                            addstr(PAUSED_TEXT);
                        } else {
                            ResumeMusicStream(music);
                            mvaddch(LINES-5+1, COLS - strlen(STOPED_TEXT) - 2, ACS_VLINE);
                            addstr(PLAYING_TEXT);
                        }
                        break;
                }
                break;
            case FIND:
                switch(c) {
                    case 27: // esc
                        menu_driver(basic_menu, REQ_CLEAR_PATTERN);
                        mvhline(1, 3, ' ', COLS - 4);
                        mvaddnstr(1, 4, TUPL_TEXT,  COLS - 5);
                    case 13: // enter
                        mode = NORMAL;
                        mvaddch(1, 1, 'N');
                        break;
                    case KEY_BACKSPACE:
                        menu_driver(basic_menu, REQ_BACK_PATTERN);
                    /* FALLTHRU */
                    default:
                        int search_ret_code = 0;
                        if(c != KEY_BACKSPACE)
                            search_ret_code = menu_driver(basic_menu, c);
                        char *patern = menu_pattern(basic_menu);

                        mvhline(1, 3, ACS_BULLET, COLS - 4);
                        mvaddnstr(1, 3, patern, COLS - 4);
                        if(search_ret_code == E_NO_MATCH) {
                            mvaddch(1, COLS - 2 - strlen(NO_MATCH_TEXT), ACS_VLINE);
                            addstr(NO_MATCH_TEXT);
                        }
                }
                break;
        }
    }

    // No need to free anything - the OS does it for us
    endwin();
    exit(0);
}
