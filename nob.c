#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#define NOB_EXPERIMENTAL_DELETE_OLD
#include "nob.h"

#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))

#define SRC_DIR "src/"
#define BUILD_DIR "build/"

#define RUN_AFTER_BUILD

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    Cmd cmd = {0};
    if(!mkdir_if_not_exists(BUILD_DIR)) return 1;

    const char *raudio_files[] = {SRC_DIR"raudio/raudio.c"};
    if(needs_rebuild(BUILD_DIR"raudio.o", raudio_files, ARRAY_SIZE(raudio_files))) {
        cmd_append(&cmd, "cc", "-c", "-o", BUILD_DIR"raudio.o");
        da_append_many(&cmd, raudio_files, ARRAY_SIZE(raudio_files));
        cmd_append(&cmd, "-DTRACELOG(...)", "-DRAUDIO_STANDALONE", "-DSUPPORT_MODULE_RAUDIO");
        cmd_append(&cmd, "-DSUPPORT_FILEFORMAT_WAV", "-DSUPPORT_FILEFORMAT_OGG", "-DSUPPORT_FILEFORMAT_MP3", "-DSUPPORT_FILEFORMAT_QOA", "-DSUPPORT_FILEFORMAT_FLAC", "-DSUPPORT_FILEFORMAT_XM", "-DSUPPORT_FILEFORMAT_MOD");
        if (!cmd_run_sync_and_reset(&cmd)) return 1;
    }

    const char *tupl_files[] = {SRC_DIR"tupl.c", BUILD_DIR"raudio.o"};
    if(needs_rebuild(BUILD_DIR"tupl", tupl_files, ARRAY_SIZE(tupl_files))) {
        cmd_append(&cmd, "cc", "-Wall", "-Wextra", "-o", BUILD_DIR"tupl");
        da_append_many(&cmd, tupl_files, ARRAY_SIZE(tupl_files));
        cmd_append(&cmd, "-lncurses", "-lmenu", "-lm", "-lpthread");
        if (!cmd_run_sync_and_reset(&cmd)) return 1;
    }

#ifdef RUN_AFTER_BUILD
    nob_log(NOB_INFO, "Running \"tupl\"");
    cmd_append(&cmd, BUILD_DIR"tupl");
    if (!cmd_run_sync_and_reset(&cmd)) return 1;
#endif /* ifdef RUN_AFTER_BUILD */

    return 0;
}
