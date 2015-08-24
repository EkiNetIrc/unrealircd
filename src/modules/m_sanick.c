/*
 *   Unreal Internet Relay Chat Daemon, src/modules/m_sanick.c
 *   (C) 2000-2001 Carsten V. Munk and the UnrealIRCd Team
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "config.h"
#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include "channel.h"
#include <time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#endif
#include <fcntl.h>
#include "h.h"
#include "proto.h"
#ifdef STRIPBADWORDS
#include "badwords.h"
#endif
#ifdef _WIN32
#include "version.h"
#endif

DLLFUNC int m_sanick(aClient *cptr, aClient *sptr, int parc, char *parv[]);

/* Place includes here */
#define MSG_SANICK     "SANICK"
#define TOK_SANICK     "SN"

ModuleHeader MOD_HEADER(m_sanick)
  = {
	"m_sanick",
	"Use to forcefully change a nickname, coded by chevyman2002 (tested on Unreal3.2.9-RC2)",
	"v1.0", 
	"3.2-b8-1",
	NULL 
    };

DLLFUNC int MOD_INIT(m_sanick)(ModuleInfo *modinfo)
{
	add_Command(MSG_SANICK, TOK_SANICK, m_sanick, MAXPARA);
	return MOD_SUCCESS;
}

DLLFUNC int MOD_LOAD(m_sanick)(int module_load)
{
	return MOD_SUCCESS;
}

DLLFUNC int MOD_UNLOAD(m_sanick)(int module_unload)
{
	if (del_Command(MSG_SANICK, TOK_SANICK, m_sanick) < 0)
	{
		sendto_realops("Failed to delete commands when unloading %s",
				MOD_HEADER(m_sanick).name);
	}
	return MOD_SUCCESS;
}


/*
** m_sanick() - PID - 08-08-2011
**
**      parv[0] - sender
**      parv[1] - nick to make join
**      parv[2] - channel(s) to join
*/
int m_sanick(aClient * cptr, aClient * sptr, int parc, char *parv[])  {
       aClient *acptr;
	char *param[3];
	int self = 0;
//	if (IsServer(sptr) || IsServices(sptr))
//		return 0; //Servers and Services should be invoking SVSNICK directly...
	if (!IsOper(sptr) && !IsAdmin(sptr) && !IsULine(sptr))   {
               sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
               return 0;
       }
	if (parv[1] == NULL || !parv[1] || !parv[2]) {
		sendnotice(sptr, "*** Usage: \2/sanick oldnick newnick\2");
		return 0;
	}
       if (parv[2] == NULL || !(acptr = find_person(parv[1], NULL))) {
		sendnotice(sptr, "*** No such user, %s.", parv[1]);
		return 0;
	}
	if (!strcmp(parv[2], parv[1]) || !strcmp(parv[2], parv[0])) {
		sendnotice(sptr, "*** Perhaps I should warn people that they have given an idiot IRCOp access?");
		return 0;
	}
	if (!strcmp(parv[0], parv[1]))
		self = 1;
	if (find_client(parv[2], NULL)) {
		sendnotice(sptr, "*** WARNING: User %s already exists!", parv[2]);
		return 0;
	}
	if(acptr->umodes & UMODE_REGNICK)
		acptr->umodes &= ~UMODE_REGNICK;
	param[0] = acptr->name;
	param[1] = parv[2];
	param[2] = NULL;
	sendnotice(acptr, "*** You were forced to change your nick to %s", parv[2]);
	do_cmd(acptr, acptr, "NICK", 2, param);
	if(self) {
		sendto_realops("%s used \2SANICK\2 to change their nick to %s.", parv[1], parv[2]);
		ircd_log(LOG_SACMDS,"SANICK: %s used SANICK to change their nick to %s", parv[1], parv[2]);
	} else {
		sendto_realops("%s used \2SANICK\2 to make %s change their nick to %s.", parv[0], parv[1], parv[2]);
		ircd_log(LOG_SACMDS,"SANICK: %s used SANICK to make %s change their nick to %s", acptr->name, parv[1], parv[2]);
	}
	return 0;
}
