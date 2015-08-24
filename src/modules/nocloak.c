/*
 *   IRC - Internet Relay Chat, src/modules/nocloak.c
 *   (C) 2004 The UnrealIRCd Team
 *
 *   See file AUTHORS in IRC package for additional names of
 *   the programmers.
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
#ifdef _WIN32
#include "version.h"
#endif

static char cloak_checksum[64];

DLLFUNC char *hidehost(char *host);
DLLFUNC char *cloakcsum();

Callback *cloak = NULL, *cloak_csum = NULL;

ModuleHeader MOD_HEADER(nocloak)
  = {
  "nocloak",
  "Unreal3.2.10.2+EkiNetIrc",
  "Cloaking module (nocloak)",
  "3.2-b8-1",
  NULL
  };

DLLFUNC int MOD_TEST(nocloak)(ModuleInfo *modinfo)
{
	cloak = CallbackAddPCharEx(modinfo->handle, CALLBACKTYPE_CLOAK, hidehost);
	if (!cloak)
	{
		config_error("cloak: Error while trying to install cloaking callback!");
		return MOD_FAILED;
	}
	cloak_csum = CallbackAddPCharEx(modinfo->handle, CALLBACKTYPE_CLOAKKEYCSUM, cloakcsum);
	if (!cloak_csum)
	{
		config_error("cloak: Error while trying to install cloaking checksum callback!");
		return MOD_FAILED;
	}
	return MOD_SUCCESS;
}

DLLFUNC int MOD_INIT(nocloak)(ModuleInfo *modinfo)
{
	ircsprintf(cloak_checksum, "NOCLOAK");
	return MOD_SUCCESS;
}

DLLFUNC int MOD_LOAD(nocloak)(int module_load)
{
	return MOD_SUCCESS;
}

DLLFUNC int MOD_UNLOAD(nocloak)(int module_unload)
{
	return MOD_SUCCESS;
}

DLLFUNC char *hidehost(char *host)
{
	static char cloak_host[HOSTLEN+1];
	ircsprintf(cloak_host, "%s", host);
	return cloak_host;
}

DLLFUNC char *cloakcsum()
{
	return cloak_checksum;
}
