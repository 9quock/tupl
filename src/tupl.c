#include <stdlib.h>
#include <curses.h>
#include <menu.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <math.h>
#include <pwd.h>

#define NOB_TEMP_CAPACITY (15*sizeof(char*))
#define NOB_IMPLEMENTATION
#include "../nob.h"

#include "raudio/raudio.h"


#define MAX_MUSIC_VOLUME 200
#define MUSIC_PATH "Music/" /* Relative to $HOME */

#define TUPL_TEXT "TUPL  -  Simple TUI Music Player"
#define NO_MATCH_TEXT " NO MATCH FOUND "
#define PAUSED_TEXT " Paused  "
#define PLAYING_TEXT " Playing "
#define STOPED_TEXT " Stoped  "


typedef struct {
    ITEM **items;
    size_t count;
    size_t capacity;
} ItemsArray;
typedef struct {
    enum Mode {
        NORMAL,
        SEEK,
        FIND
    } mode;
    enum Shuffle {
        STOP,
        LOOP,
        NEXT,
        RANDOM,
    } shuffle;
    int volume;
    bool just_finished;

    MENU *menu;
    ITEM *current_selected_music;
    pthread_mutex_t *musicUpdaterThread_mut;

    Nob_String_Builder search_pattern;
    ItemsArray all_music_items;
    Music music;
} CTX;
CTX *ctx;

/* UI functions */
void makeHLine(int y) {
    mvhline(y, 1, 0, COLS - 2);
    mvaddch(y, 0, ACS_LTEE);
    mvaddch(y, COLS - 1, ACS_RTEE);
}
void drawBox() {
    box(stdscr, 0, 0);

    makeHLine(2);
    makeHLine(LINES-3);
    makeHLine(LINES-5);
    makeHLine(LINES-7);

    mvaddch(1, 2, ACS_VLINE);
    mvaddch(0, 2, ACS_TTEE);
    mvaddch(2, 2, ACS_BTEE);

    mvaddch(LINES-5+1, COLS-strlen(PLAYING_TEXT)-2, ACS_VLINE);
    mvaddch(LINES-5+1, COLS-strlen(PLAYING_TEXT)-4, ACS_VLINE);
}
void drawMode() {
    switch(ctx->mode) {
        case NORMAL:
            mvaddch(1, 1, 'N');
            break;
        case SEEK:
            mvaddch(1, 1, 'S');
            break;
        case FIND:
            mvaddch(1, 1, 'F');
            break;
    }
}
void drawTitle() {
    mvhline(1, 3, ' ', COLS - 4);
    mvaddnstr(1, 4, TUPL_TEXT,  COLS - 5);
}
void drawSearchPattern() {
    mvhline(1, 3, ACS_BULLET, COLS - 4);
    mvaddnstr(1, 3, ctx->search_pattern.items,  COLS - 5);
}
void drawNoMatchFound() {
    mvaddch(1, COLS-2-strlen(NO_MATCH_TEXT), ACS_VLINE);
    addstr(NO_MATCH_TEXT);
}
void drawMenu() {
    unpost_menu(ctx->menu);
    set_menu_sub(ctx->menu, derwin(stdscr, LINES-10, COLS - 2, 3, 1));
    set_menu_format(ctx->menu, LINES-10, 1);
    post_menu(ctx->menu);
}
float secToMin(int float_minutes) {
    return float_minutes/60+(float)(float_minutes%60)/100.f;
}
void drawProgressBar() {
    /* Time */
    float music_length = 0.f, music_played = 0.f;
    if(IsMusicReady(ctx->music)) {
        music_length = GetMusicTimeLength(ctx->music);
        music_played = GetMusicTimePlayed(ctx->music);
    }
    mvaddstr(LINES-6, 1, nob_temp_sprintf(" %.2f / %.2f ", secToMin(music_played), secToMin(music_length)));
    nob_temp_reset();
    addch(ACS_VLINE);

    /* Bar */
    int x = 0; getyx(stdscr, x, x);
    int max_length = COLS - x;
    int bar_length = 0; if(IsMusicReady(ctx->music)) bar_length = (float)music_played/music_length * max_length;
    hline(ACS_BULLET, max_length - 1);
    hline(ACS_CKBOARD, bar_length);
}
void drawCurrentMusic() {
    mvhline(LINES-4, 1, ' ', COLS-strlen(PLAYING_TEXT)-5);
    if(ctx->current_selected_music != NULL) {
        const char *name = item_name(ctx->current_selected_music);
        if(name != NULL) {
            mvaddnstr(LINES-4, 2, name, COLS-strlen(PLAYING_TEXT)-5);
            return;
        }
    }
    mvaddnstr(LINES-4, 2, "[NOTHING IS PLAYING]", COLS-strlen(PLAYING_TEXT)-5);
}
void drawShuffleMode() {
    switch(ctx->shuffle) {
        case STOP:
            mvaddch(LINES-4, COLS-strlen(PLAYING_TEXT)-3, 'S');
            break;
        case LOOP:
            mvaddch(LINES-4, COLS-strlen(PLAYING_TEXT)-3, 'L');
            break;
        case NEXT:
            mvaddch(LINES-4, COLS-strlen(PLAYING_TEXT)-3, 'N');
            break;
        case RANDOM:
            mvaddch(LINES-4, COLS-strlen(PLAYING_TEXT)-3, 'R');
            break;
    }
}
void drawMusicState() {
    move(LINES-5+1, COLS-strlen(PLAYING_TEXT)-1);
    if(IsMusicStreamPlaying(ctx->music)) {
        addstr(PLAYING_TEXT);
    } else {
        float music_played = 0.f; if(IsMusicReady(ctx->music)) music_played = GetMusicTimePlayed(ctx->music);
        if(music_played == 0.f) {
            addstr(STOPED_TEXT);
        } else {
            addstr(PAUSED_TEXT);
        }
    }
}
void drawVolume() {
    /* Volume */
    mvaddstr(LINES-2, 1, nob_temp_sprintf(" %3d%% / %d%% ", ctx->volume, MAX_MUSIC_VOLUME));
    nob_temp_reset();
    addch(ACS_VLINE);

    /* Bar */
    int x = 0; getyx(stdscr, x, x);
    int max_length = COLS - x;
    int bar_length = (float)ctx->volume/MAX_MUSIC_VOLUME * max_length;
    hline(ACS_BULLET, max_length - 1);
    hline(ACS_CKBOARD, bar_length - 1);
}
void drawUI() {
    clear();

    drawMode();
    drawTitle();
    drawMenu();
    drawProgressBar();
    drawCurrentMusic();
    drawShuffleMode();
    drawMusicState();
    drawVolume();
    drawBox();

    refresh();
}

/* Multi-Threading */
#define mutexify(code) \
    do { \
        pthread_mutex_lock(ctx->musicUpdaterThread_mut); \
        code \
        pthread_mutex_unlock(ctx->musicUpdaterThread_mut); \
    } while(0)
void *musicUpdaterThread() {
    struct timespec sleep_time;
    sleep_time.tv_sec = 0;
    sleep_time.tv_nsec = 10 * 1000000;

    while(1) {
        nanosleep(&sleep_time, NULL);
        mutexify(
            ctx->music.looping = false; /* It seems like something keeps setting it to true */
            if(IsMusicReady(ctx->music)) UpdateMusicStream(ctx->music);
        );
    }
}

/* Stuff to work with menu */
void fillItemArrayWithMusicItemsFromPath(const char *path) {
    DIR *directory = NULL;
    if((directory = opendir(path)) == NULL) {
        endwin();
        fprintf(stderr, "Directory \"%s\" not found\n", path);
        exit(1);
    }

    struct dirent* in_file;
    while ((in_file = readdir(directory))) {
        if (in_file->d_name[0] == '.') continue; /* Skip hidden files */
        else if (in_file->d_type == DT_REG) {
            Nob_String_View file_name = nob_sv_from_cstr(in_file->d_name);
            if (
                nob_sv_end_with(file_name, ".wav")  ||
                nob_sv_end_with(file_name, ".qoa")  ||
                nob_sv_end_with(file_name, ".ogg")  ||
                nob_sv_end_with(file_name, ".mp3")  ||
                nob_sv_end_with(file_name, ".flac") ||
                nob_sv_end_with(file_name, ".xm")   ||
                nob_sv_end_with(file_name, ".mod")
            ) {
                ITEM *it = new_item(in_file->d_name, path);
                if(it != NULL)
                    nob_da_append(&ctx->all_music_items, it);
            }
        }
        else if (in_file->d_type == DT_DIR) {
            Nob_String_Builder new_path = {0};
            /* new_path.items should not get freed, because it's gonna get used later */
            nob_sb_append_cstr(&new_path, path);
            nob_sb_append_cstr(&new_path, in_file->d_name);
            nob_sb_append_cstr(&new_path, "/");
            nob_sb_append_null(&new_path);

            fillItemArrayWithMusicItemsFromPath(new_path.items);

        }
    }
}
void fillMenuWithMusic() {
    ctx->all_music_items.count = 0;
    unpost_menu(ctx->menu);

    Nob_String_Builder music_path = {0};
    /* music_path.items also should not get freed, because it's gonna get used later */
    nob_sb_append_cstr(&music_path, getpwuid(getuid())->pw_dir);
    nob_sb_append_cstr(&music_path, "/" MUSIC_PATH);
    nob_sb_append_null(&music_path);

    fillItemArrayWithMusicItemsFromPath(music_path.items);
    nob_da_append(&ctx->all_music_items, NULL);

    if(free_menu(ctx->menu) == E_SYSTEM_ERROR) {
        endwin();
        fprintf(stderr, "Could not free menu(SYSTEM_ERROR), aborting\n");
        exit(1);
    }
    ctx->menu = new_menu(ctx->all_music_items.items);
}
void selectItem(ITEM *menu_item) {
    ctx->current_selected_music = menu_item;
    UnloadMusicStream(ctx->music);

    Nob_String_Builder path = {0};
    nob_sb_append_cstr(&path, item_description(menu_item));
    nob_sb_append_cstr(&path, item_name(menu_item));
    nob_sb_append_null(&path);

    ctx->music = LoadMusicStream(path.items);
    nob_sb_free(path);

    drawCurrentMusic();
}
ITEM *nextItem(ITEM *item) {
    ITEM *it = menu_items(ctx->menu)[item_index(item)+1];
    if(it == NULL) it = menu_items(ctx->menu)[0];
    return it;
}
ITEM *prevItem(ITEM *item) {
    ITEM *it = NULL;
    if(item == menu_items(ctx->menu)[0]) it = menu_items(ctx->menu)[item_count(ctx->menu)-1];
    else it = menu_items(ctx->menu)[item_index(item)-1];
    return it;
}
ITEM *searchPatternInMenu(bool search_backwards, bool force_next_match) {
    if(ctx->search_pattern.count == 0) return NULL;

    ITEM *initial_item = current_item(ctx->menu);
    ITEM *item = force_next_match ? search_backwards ? prevItem(initial_item) : nextItem(initial_item) : initial_item;
    size_t pattern_cursor = 0, item_name_cursor = 0;

    while(1) {
        while(item_name_cursor < strlen(item_name(item))) {
            if(item_name(item)[item_name_cursor] == ctx->search_pattern.items[pattern_cursor]) ++pattern_cursor;
            ++item_name_cursor;
        }
        if(pattern_cursor == ctx->search_pattern.count) return item;

        pattern_cursor = 0; item_name_cursor = 0;
        item = search_backwards ? prevItem(item) : nextItem(item);
        if(item == initial_item) return NULL;
    }
}
ITEM *nextMatch() {
    return searchPatternInMenu(false, true);
}
ITEM *prevMatch() {
    return searchPatternInMenu(true, true);
}
ITEM *gotoMatch() {
    return searchPatternInMenu(false, false);
}

/* Stuff to work with music */
void playNext() {
    switch(ctx->shuffle) {
        case STOP:
            PlayMusicStream(ctx->music);
            PauseMusicStream(ctx->music);
            break;
        case LOOP:
            PlayMusicStream(ctx->music);
            ctx->just_finished = true;
            break;
        case NEXT:
            set_current_item(ctx->menu, nextItem(ctx->current_selected_music));
            selectItem(current_item(ctx->menu));

            PlayMusicStream(ctx->music);
            ctx->just_finished = true;
            break;
        case RANDOM:
            {
                size_t next_item_index = (rand()%item_count(ctx->menu)-1 + item_index(ctx->current_selected_music)) % item_count(ctx->menu);
                set_current_item(ctx->menu, menu_items(ctx->menu)[next_item_index]);
            }
            selectItem(current_item(ctx->menu));

            PlayMusicStream(ctx->music);
            ctx->just_finished = true;
            break;
    }
}

/* Controll handlers */
void handleNormalModeInput(int c) {
    switch(c) {
        case 'q':
            endwin();
            exit(0);
        case 27: /* esc */
            ctx->search_pattern.count = 0;
            if(ctx->search_pattern.capacity > 0) ctx->search_pattern.items[0] = 0;
            drawTitle();
            break;
        case 'h': {
            ITEM *search_result = prevMatch();
            if(search_result == NULL) drawNoMatchFound();
            else set_current_item(ctx->menu, search_result);
            } break;
        case 'j':
            menu_driver(ctx->menu, REQ_DOWN_ITEM);
            break;
        case 'k':
            menu_driver(ctx->menu, REQ_UP_ITEM);
            break;
        case 'l': {
            ITEM *search_result = nextMatch();
            if(search_result == NULL) drawNoMatchFound();
            else set_current_item(ctx->menu, search_result);
            } break;
        case 13: /* enter */
            selectItem(current_item(ctx->menu));
            mutexify({
                PlayMusicStream(ctx->music);
                ctx->music.looping = false;
                ctx->just_finished = true;
            });
            drawMusicState();
            break;
        case 's':
            ctx->mode = SEEK;
            drawMode();
            break;
        case 'f':
            drawSearchPattern();
            ctx->mode = FIND;
            drawMode();
            break;
        case 'm':
            ctx->shuffle++;
            if(ctx->shuffle > RANDOM) ctx->shuffle = STOP;
            drawShuffleMode();
            break;
        case 'r':
            drawUI();
            break;
    }
}
void handleSeekModeInput(int c) {
    switch(c) {
        case 27: /* esc */
            ctx->mode = NORMAL;
            drawMode();
            break;
        case 'h':
            if(IsMusicReady(ctx->music)) SeekMusicStream(ctx->music, fmax(0.f, GetMusicTimePlayed(ctx->music) - 10.f)); /* Why is this so slow T-T (with MP3s at least) */
            break;
        case 'j':
            ctx->volume -= 5;
            if(ctx->volume < 0) ctx->volume = 0;
            drawVolume();
            break;
        case 'k':
            ctx->volume += 5;
            if(ctx->volume > MAX_MUSIC_VOLUME) ctx->volume = MAX_MUSIC_VOLUME;
            drawVolume();
            break;
        case 'l':
            if(IsMusicReady(ctx->music)) SeekMusicStream(ctx->music, fmin(GetMusicTimeLength(ctx->music), GetMusicTimePlayed(ctx->music) + 10.f));
            break;
        case ' ':
            IsMusicStreamPlaying(ctx->music) ? PauseMusicStream(ctx->music) : ResumeMusicStream(ctx->music);
            drawMusicState();
            break;
    }
}
void handleFindModeInput(int c) {
    switch(c) {
        case 27: /* esc */
            ctx->search_pattern.count = 0;
            if(ctx->search_pattern.capacity > 0) ctx->search_pattern.items[0] = 0;

            drawTitle();
            ctx->mode = NORMAL;
            drawMode();
            break;
        case 13: /* enter */
            ctx->mode = NORMAL;
            drawMode();
            break;
        case KEY_BACKSPACE:
            if(ctx->search_pattern.count > 0) {
                ctx->search_pattern.items[--ctx->search_pattern.count] = 0;
                
                {
                    ITEM *search_result = gotoMatch();
                    if(search_result == NULL) drawNoMatchFound();
                    else set_current_item(ctx->menu, search_result);
                }

                drawSearchPattern();
            }
            break;
        default:
            nob_da_append(&ctx->search_pattern, c);
            nob_da_append(&ctx->search_pattern, 0);
            ctx->search_pattern.count--;

            {
                ITEM *search_result = gotoMatch();
                if(search_result == NULL) drawNoMatchFound();
                else set_current_item(ctx->menu, search_result);
            }

            drawSearchPattern();
            break;
    }
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
    puts("\033[?12l"); /* disable cursor blinking */

    /* Allocate & Initialize CTX struct */
    ctx = (CTX *)malloc(sizeof(CTX));
    memset((void *)ctx, 0, sizeof(CTX));
    ctx->volume = 100;
    
    /* Setup musicUpdaterThread */
    pthread_t music_updater_thread_pid;
    pthread_mutex_t music_updater_thread_mut;
    ctx->musicUpdaterThread_mut = &music_updater_thread_mut;
    pthread_create(&music_updater_thread_pid, NULL, musicUpdaterThread, NULL);

    /* Sleep time */
    struct timespec sleep_time;
    sleep_time.tv_sec = 0;
    sleep_time.tv_nsec = 10 * 1000000;

    /* Init */
    InitAudioDevice();
    fillMenuWithMusic();
    drawUI();    

    while(1) {
        nanosleep(&sleep_time, NULL);

        drawProgressBar();
        mutexify(
            SetMusicVolume(ctx->music, ctx->volume/100.f);
        );

        if(IsMusicReady(ctx->music) && !IsMusicStreamPlaying(ctx->music) && ctx->just_finished) {
            ctx->just_finished = false;
            mutexify(
                playNext();
            );
            drawMusicState();
        }

        move(1, 1);
        int c = getch();
        if(c == -1) continue;

        switch(ctx->mode) {
            case NORMAL:
                handleNormalModeInput(c);
                break;
            case SEEK:
                mutexify(
                    handleSeekModeInput(c);
                );
                break;
            case FIND:
                handleFindModeInput(c);
                break;
        }
    }

    endwin();
    return 0;
}
