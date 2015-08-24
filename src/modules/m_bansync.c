/*
 * This module implements an extended bantype to sync one channel's ban list
 * with anothers. It is up to the channel staff on the channel this mode is used
 * to trust the channel staff of the channel they are syncing to, permissions
 * are not checked by this module.
 * /mode #chan1 +b ~S:#chan2 will make it so that everyone on #chan1 is also
 * checked against #chan2's banlist as well as their own.
 *
 * Copyright (c) 2009, 2013 Ryan Schmidt <skizzerz@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "config.h"
#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include "proto.h"
#include "channel.h"
#include <time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <version.h>
#endif
#ifdef STRIPBADWORDS
#include "badwords.h"
#endif
#include <fcntl.h>
#include "h.h"

ModuleHeader MOD_HEADER(m_bansync) = {
    "m_bansync",
    "v1.1",
    "Sync channel banlists via extban ~S",
    "3.2-b8-1",
    NULL
};

#define BSBANCHAR 'S'
char* bs_param(char* param);
int bs_banned(aClient* sptr, aChannel* chptr, char* ban, int chktype);
Extban* bsExtBan;

int bs_recursion_guard_size;
char** bs_recursion_guard;

DLLFUNC int MOD_INIT(m_bansync)(ModuleInfo* modinfo) {
    ExtbanInfo bsInfo;
    memset(&bsInfo, 0, sizeof(ExtbanInfo));
    bsInfo.flag = BSBANCHAR;
    bsInfo.conv_param = bs_param;
    bsInfo.is_banned = bs_banned;
    bsExtBan = ExtbanAdd(modinfo->handle, bsInfo);

    bs_recursion_guard_size = 32;
    bs_recursion_guard = (char**)calloc(bs_recursion_guard_size, sizeof(char*));

    if (modinfo->handle->errorcode == MODERR_NOERROR)
        return MOD_SUCCESS;
    else
        return MOD_FAILED;
}

DLLFUNC int MOD_LOAD(m_bansync)(int module_load) {
    return MOD_SUCCESS;
}

DLLFUNC int MOD_UNLOAD(m_bansync)(int module_unload) {
    int i;
    for (i = 0; i < bs_recursion_guard_size; ++i) {
        if (bs_recursion_guard[i] != NULL) {
            free(bs_recursion_guard[i]);
        }
    }
    free(bs_recursion_guard);
    bs_recursion_guard = NULL;
    bs_recursion_guard_size = 0;

    ExtbanDel(bsExtBan);

    return MOD_SUCCESS;
}

char* bs_param(char* param) {
    if (match("~?:#*", param) || strchr(param, ',') != NULL) {
        return NULL;
    }

    return param;
}

int bs_banned(aClient* sptr, aChannel* chptr, char* ban, int chktype) {
    char banstr[64], *chantok;
    aChannel* chan;
    Ban* banned;
    int i, havespace;
    char** tmp_recursion_guard;

    /* recurse check, only sync with the channel at not to channels that that channel is synced to */
    for (i = 0; i < bs_recursion_guard_size; ++i) {
        if (bs_recursion_guard[i] != NULL && strcmp(bs_recursion_guard[i], chptr->chname) == 0) {
            return 0;
        }
    }

    strncpy(banstr, ban, sizeof(banstr));

    /* ignore the ~S part */
    (void) strtok(banstr, ":");

    /* grab the channel */
    chantok = strtok(NULL, ":");

    if (!chantok) {
        return 0;
    }

    chan = get_channel(sptr, chantok, !CREATE);

    if (!chan) {
        return 0;
    }

    /* chan exists, add to recursion guard and then check for bans on the next channel */
    havespace = 0;

    for (i = 0; i < bs_recursion_guard_size; ++i) {
        if (bs_recursion_guard[i] == NULL) {
            havespace = 1;
            bs_recursion_guard[i] = strdup(chantok); /* used in core modules, so presumably this is manually defined on non-posix systems */
            break;
        }
    }

    if (!havespace) {
        /* not enough space in the array, so grow it by another 32 channels */
        bs_recursion_guard_size += 32;
        tmp_recursion_guard = (char**)calloc(bs_recursion_guard_size, sizeof(char*));
        memcpy(tmp_recursion_guard, bs_recursion_guard, (bs_recursion_guard_size - 32) * sizeof(char*));
        free(bs_recursion_guard);
        bs_recursion_guard = tmp_recursion_guard;
        bs_recursion_guard[i] = strdup(chantok);
    }

    banned = is_banned(sptr, chan, chktype);

    free(bs_recursion_guard[i]);
    bs_recursion_guard[i] = NULL;

    if (banned) {
        return 1;
    }

    return 0;
}

