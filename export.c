// ccache -- a fast C/C++ compiler cache
//
// Copyright (C) 2017 Darrell Schiebel
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "ccache.h"
#include "export.h"
#include <unistd.h>
#include <string.h>

extern struct conf *conf;

static struct {
    char *source_path;
    char **deps;
    unsigned int deps_size;
    unsigned int deps_available;
} export_info;

void export_begin( const char *path ) {
    export_info.deps_size = 0;
    export_info.deps_available = 256;
    export_info.deps = x_malloc(sizeof(char*)*export_info.deps_available);
    export_info.source_path = x_strdup(path);
}
void export_dependency( const char *path ) {
    if (export_info.deps_available == export_info.deps_size) {
        export_info.deps_available += 128;
        x_realloc(export_info.deps,export_info.deps_available);
    }
    export_info.deps[export_info.deps_size++] = x_strdup(path);
}

bool is_executable(mode_t m) {
    return S_ISREG(m) && ((S_IXUSR | S_IXGRP | S_IXOTH) & m);
}

void export_end( bool built ) {
    if (!str_eq(conf->export_exe, "")) {
        struct stat st_exe;
        if (stat(conf->export_exe, &st_exe) == 0) {
            if (is_executable(st_exe.st_mode)) {
                int infd[2];
                int errfd[2];
                if (pipe(infd) == 0 && pipe(errfd) == 0 ) {
                    pid_t pid = fork( );
                    if ( pid == -1 ) cc_log("export error, fork of %s failed", conf->export_exe );
                    else {
                        if ( pid == 0 ) {
                            close(infd[1]);                   /*** parent's ends ***/
                            close(errfd[0]);
                            dup2(infd[0],STDIN_FILENO);       /*** child's ends ***/
                            dup2(errfd[1],STDERR_FILENO);

                            char *argv[2] = {conf->export_exe, NULL};
                            _exit(execv(argv[0],argv));

                        } else {
                            close(infd[0]);                   /*** child's stdin ***/

                            dprintf( infd[1], "<src>\n" );
                            dprintf( infd[1], "    <file>%s</file>\n", export_info.source_path );
                            if ( export_info.deps_size > 0 ) {
                                dprintf( infd[1], "    <depend>\n" );
                                for ( unsigned int i=0; i < export_info.deps_size; ++i ) 
                                    dprintf( infd[1], "        <item>%s</item>\n", export_info.deps[i] );
                                dprintf( infd[1], "    </depend>\n" );
                            }
                            dprintf( infd[1], "    <rebuilt>%s</rebuilt>\n", built ? "true" : "false" );
                            dprintf( infd[1], "</src>\n" );

                            close(infd[1]);                   /*** close pipe to child's stdin ***/

                            fd_set set;
                            struct timeval timeout;
                            int rv;

                            FD_ZERO(&set);                    /* clear the set */
                            FD_SET(errfd[0], &set);           /* add our file descriptor to the set */

                            timeout.tv_sec = 0;
                            timeout.tv_usec = 100;

                            rv = select(errfd[0] + 1, &set, NULL, NULL, &timeout);
                            if(rv == -1)
                                cc_log("export error, select failed: %s", strerror(errno));
                            else if ( rv > 0 ) {
                                /*** rv == 0 implies timeout ***/
                                char errmsg[READ_BUFFER_SIZE];
                                int n = read(errfd[0], errmsg, sizeof(errmsg));
                                if ( n > 0 ) {
                                    /* trim error output for log... */
                                    char *p = strstr(errmsg,"\n");
                                    if (p) *p = '\0';
                                    cc_log("export error, error messages from executable: '%s'\n", errmsg );

                                }
                            }
                            close(errfd[0]);                  /*** close pipe from child's stderr if there ***/
                                                              /*** does not seem to be any output... ***/

                            int status = 0;
                            int ret = waitpid(pid,&status,0);
                            if ( ret != pid ) cc_log("export error, executable waitpid problem: %s", strerror(errno));
                            if ( status != 0 ) cc_log("export error, nonzero exit status from executable");

                        }
                    }
                } else cc_log("export error, pipe creation failed: %s", strerror(errno));
            } else cc_log("export error, executable does not exist or is not executable: %s", conf->export_exe);
        } else cc_log("export error, problem with executable: %s", strerror(errno));
    }
    export_info.deps_size = export_info.deps_available = 0;
    free(export_info.deps);
}
