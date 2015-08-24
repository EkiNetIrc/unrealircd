/*
 * tkldb - TKL Database
 * (C) Copyright 2012-2014 Bram Matthys (Syzop)
 * Released under GNU GPL v2
 *
 * This module saves and loads all *LINES and Spamfilters in a text file so
 * they are not lost between IRCd restarts. See the README for full details.
 *
 * The development of this module was sponsored by Niklas Bivald.
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
#endif
#include <fcntl.h>
#include "h.h"
#ifdef STRIPBADWORDS
#include "badwords.h"
#endif
#ifdef _WIN32
#include "version.h"
#endif

#define TKLDB_VERSION "v1.1"

#ifdef DEBUGMODE
#define BENCHMARK
#endif

ModuleHeader MOD_HEADER(tkldb)
  = {
	"tkldb",
	TKLDB_VERSION,
	"TKL Database - by Syzop",
	"3.2-b8-1",
	NULL 
    };

struct cfgstruct {
	char *file;
};
static struct cfgstruct cfg;

static ModuleInfo ModInf;

static int recursion = 0;

#define CfgDup(a, b) do { if (a) free(a); a = strdup(b); } while(0)
#define safefree(a) do { if (a) free(a); a = NULL; } while(0)

/* file format version */
#define TKLDB_FILE_VERSION 1

/* Forward declarations */
DLLFUNC int tkldb_config_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
DLLFUNC int tkldb_config_run(ConfigFile *cf, ConfigEntry *ce, int type);
DLLFUNC int tkldb_tkl_add(aClient *, aClient *, aTKline *, int, char **);
DLLFUNC int tkldb_tkl_del(aClient *, aClient *, aTKline *, int, char **);

static void multi_log(char *fmt, ...)  __attribute__((format(printf,1,2)));

void load_tkls(void);
void save_tkls(aTKline *except_this_one);
void set_config_defaults(void);

int has_booted = 0;

DLLFUNC int MOD_TEST(tkldb)(ModuleInfo *modinfo)
{
	memcpy(&ModInf, modinfo, modinfo->size);
	memset(&cfg, 0, sizeof(cfg));
	HookAddEx(modinfo->handle, HOOKTYPE_CONFIGTEST, tkldb_config_test);
	return MOD_SUCCESS;
}

DLLFUNC int MOD_INIT(tkldb)(ModuleInfo *modinfo)
{
	HookAddEx(modinfo->handle, HOOKTYPE_CONFIGRUN, tkldb_config_run);
	has_booted = loop.ircd_booted; /* need to save here, since by the time MOD_LOAD is called we are fully booted */
	set_config_defaults();
	HookAddEx(modinfo->handle, HOOKTYPE_TKL_ADD, tkldb_tkl_add);
	HookAddEx(modinfo->handle, HOOKTYPE_TKL_DEL, tkldb_tkl_del);
	return MOD_SUCCESS;
}

DLLFUNC int MOD_LOAD(tkldb)(int module_load)
{
	load_tkls();
	return MOD_SUCCESS;
}

DLLFUNC int MOD_UNLOAD(tkldb)(int module_unload)
{
	save_tkls(NULL);
	return MOD_SUCCESS;
}

void set_config_defaults(void)
{
	CfgDup(cfg.file, "tkl.db");
}

DLLFUNC int tkldb_config_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
int errors = 0;
ConfigEntry *cep;

	if (type != CONFIG_SET)
		return 0;
	
	/* We are only interrested in set::tkldb.. */
	if (!ce || !ce->ce_varname || strcmp(ce->ce_varname, "tkldb"))
		return 0;
	
	for (cep = ce->ce_entries; cep; cep = cep->ce_next)
	{
		if (!cep->ce_varname)
		{
			config_error("%s:%i: blank set::tkldb item",
				cep->ce_fileptr->cf_filename, cep->ce_varlinenum);
			errors++;
			continue;
		} else
		if (!cep->ce_vardata)
		{
			config_error("%s:%i: blank set::tkldb::%s without value",
				cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_varname);
			errors++;
			continue;
		} else
		if (!strcmp(cep->ce_varname, "file"))
		{
			CfgDup(cfg.file, cep->ce_vardata);
		} else
		{
			config_error("%s:%i: unknown directive set::tkldb::%s",
				cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_varname);
			errors++;
			continue;
		}
	}
	
	*errs = errors;
	return errors ? -1 : 1;
}

DLLFUNC int tkldb_config_run(ConfigFile *cf, ConfigEntry *ce, int type)
{
ConfigEntry *cep;

	if (type != CONFIG_SET)
		return 0;
	
	/* We are only interrested in set::tkldb.. */
	if (!ce || !ce->ce_varname || strcmp(ce->ce_varname, "tkldb"))
		return 0;
	
	for (cep = ce->ce_entries; cep; cep = cep->ce_next)
	{
		if (!strcmp(cep->ce_varname, "file"))
		{
			CfgDup(cfg.file, cep->ce_vardata);
		}
	}
	return 1;
}

static void multi_log(char *fmt, ...)
{
va_list vl;
static char buf[2048];

	va_start(vl, fmt);
	vsnprintf(buf, sizeof(buf), fmt, vl);
	va_end(vl);

	if (!conf_log)
	{
		config_status("[tkldb] %s", buf);
	} else {
		sendto_realops("[tkldb] %s", buf);
		ircd_log(LOG_ERROR, "[tkldb] %s", buf);
	}
}

/* File format is as follows:
 * HEADER:
 * One line containing the words '# TKL Database v<version> <timestamp>'
 * The version is used to determine if we are compatible with it.
 * The timestamp is the UNIX timestamp of when the *LINES were saved
 * CONTENTS:
 * <type> <parameters...>
 * Note that the parameters depend on the type. For example, a spamfilter
 * entry has more fields than a ZLINE.
 */


/** Save all *LINES and spamfilters. */
void save_tkls(aTKline *except_this_one)
{
FILE *fd;
char tmpname[512];
aTKline *tkl;
int t;
char c;
#ifdef BENCHMARK
struct timeval tv, tv_old;
	gettimeofday(&tv_old, NULL);
#endif
    
	/* We write to a temporary file first, so the DB is not lost during a crash */
	snprintf(tmpname, sizeof(tmpname), "%s.tmp", cfg.file);

	fd = fopen(tmpname, "w");
	if (!fd)
	{
		/* This is a serious issue! */
		multi_log("Unable to open %s for writing: %s", tmpname, strerror(ERRNO));
		return;
	}
	
	if (fprintf(fd, "# TKL Database v%d %ld\n", TKLDB_FILE_VERSION, TStime()) < 0)
		goto ioerr;
	
	for (c = 'a'; c <= 'z'; c++)
	{
		for (tkl = tklines[tkl_hash(c)]; tkl; tkl = tkl->next)
		{
			if (tkl == except_this_one)
				continue; /* this *LINE is pending deletion */
			/* We don't save the following types (basically everything that was added by config already):
			 * local spamfilters & and local QLINES
			 */
			if ((tkl->type == TKL_SPAMF) || (tkl->type == TKL_NICK))
				continue; /* if it's a LOCAL spamfilter then it was added through config -- skip it ! */

			if (tkl->type & TKL_SPAMF)
			{
				/* Spamfilter */
				if (fprintf(fd, "%c %s %s %s %ld %ld %ld %s %s\n",
					tkl_typetochar(tkl->type), tkl->usermask, tkl->hostmask, tkl->setby, tkl->set_at, tkl->expire_at, tkl->ptr.spamf->tkl_duration, tkl->ptr.spamf->tkl_reason, tkl->reason) < 0)
						goto ioerr;
			} else
			{
				/* Regular */
				if (fprintf(fd, "%c %s %s %s %ld %ld %s\n",
					tkl_typetochar(tkl->type), tkl->usermask, tkl->hostmask, tkl->setby, tkl->set_at, tkl->expire_at, tkl->reason) < 0)
						goto ioerr;
			}
		}
	}

	if (fclose(fd))
		goto ioerr;
	
	/* Now, atomically rename the temp file to the db filename. If anything fails then
	 * the db file should either point to the new or old database.
	 */
	if (rename(tmpname, cfg.file) < 0)
		multi_log("Unable to move %s to %s: %s", tmpname, cfg.file, strerror(ERRNO));

#ifdef BENCHMARK
	gettimeofday(&tv, NULL);
	multi_log("tkldb: save took %ld useconds",
		((tv.tv_sec - tv_old.tv_sec) * 1000000) + (tv.tv_usec - tv_old.tv_usec));
#endif

	return;

ioerr:
	multi_log("Error writing to %s: %s",  tmpname, strerror(ERRNO));
	fclose(fd);
}

/** Cut off string on first occurance of CR or LF */
void stripcrlf(char *buf)
{
	for (; *buf; buf++)
	{
		if ((*buf == '\n') || (*buf == '\r'))
		{
			*buf = '\0';
			return;
		}
	}
}

/** This function loads all *LINES and Spamfilters from disk.
 * It uses the m_tkl() layer to add them.
 * If a *LINE/SPAMFILTER already exists then the m_tkl will silently ignore the add.
 */
void load_tkls(void)
{
FILE *fd;
char buf[2048];
char *s, *p;
char *type;
char *tkl[12];
TS exp;
#ifdef BENCHMARK
struct timeval tv, tv_old;
	gettimeofday(&tv_old, NULL);
#endif

	if (has_booted)
		return; /* only do this on-boot */
		
	multi_log("Loading *LINES and Spamfilters from '%s'...", cfg.file);

	fd = fopen(cfg.file, "r");
	if (!fd)
		return; /* Could be normal */

	buf[0] = '\0';
	fgets(buf, sizeof(buf), fd); /* read header */
	if (strncmp(buf, "# TKL Database", 14))
	{
		multi_log("File '%s' has invalid signature -- file not processed!", cfg.file);
		fclose(fd);
		return;
	}

	while (fgets(buf, sizeof(buf), fd))
	{
		stripcrlf(buf);
		if (*buf == '#')
			continue; /* Comments are ignored */

		type = strtoken(&p, buf, " ");
		if (!type) continue;
		
		tkl[0] = me.name;
		tkl[1] = "+";
		tkl[2] = type;
		
		if ((*type != 'F') && (*type != 'f'))
		{
			/* regular *LINE */
			char *usermask, *hostmask, *set_by, *set_at, *expire_at, *reason;
			
			usermask = strtoken(&p, NULL, " ");
			if (!usermask) continue;
			hostmask = strtoken(&p, NULL, " ");
			if (!hostmask) continue;
			set_by = strtoken(&p, NULL, " ");
			if (!set_by) continue;
			set_at = strtoken(&p, NULL, " ");
			if (!set_at) continue;
			expire_at = strtoken(&p, NULL, " ");
			if (!expire_at) continue;
			exp = atol(expire_at);
			if ((exp > 0) && (exp < TStime()))
				continue; /* already expired */
			reason = strtoken(&p, NULL, "");
			if (!reason) continue;
			/* from src/modules/m_tkl:
			 *           add:      remove:    spamfilter:    spamfilter+TKLEXT  sqline:
			 * parv[ 1]: +         -          +/-            +                  +/-
			 * parv[ 2]: type      type       type           type               type
			 * parv[ 3]: user      user       target         target             hold
			 * parv[ 4]: host      host       action         action             host
			 * parv[ 5]: setby     removedby  (un)setby      setby              setby
			 * parv[ 6]: expire_at            expire_at (0)  expire_at (0)      expire_at
			 * parv[ 7]: set_at               set_at         set_at             set_at
			 * parv[ 8]: reason               regex          tkl duration       reason
			 * parv[ 9]:                                     tkl reason [A]        
			 * parv[10]:                                     regex              
			 *           ^^^^^^                                                 ^^^^^^^
			 */
			tkl[3] = usermask;
			tkl[4] = hostmask;
			tkl[5] = set_by;
			tkl[6] = expire_at;
			tkl[7] = set_at;
			tkl[8] = reason;
			tkl[9] = NULL;
			recursion = 1;
			m_tkl(&me, &me, 9, tkl);
			recursion = 0;
		} else {
			/* Spamfilter */
			char *target, *action, *set_by, *set_at, *expire_at, *tkl_duration, *tkl_reason, *regex;
			
			target = strtoken(&p, NULL, " ");
			if (!target) continue;
			action = strtoken(&p, NULL, " ");
			if (!action) continue;
			set_by = strtoken(&p, NULL, " ");
			if (!set_by) continue;
			set_at = strtoken(&p, NULL, " ");
			if (!set_at) continue;
			expire_at = strtoken(&p, NULL, " "); /* currently always zero */
			if (!expire_at) continue;
			exp = atol(expire_at);
			if ((exp > 0) && (exp < TStime()))
				continue; /* already expired (currently spamfilters are always 0, but who knows about the future..) */
			tkl_duration = strtoken(&p, NULL, " ");
			if (!tkl_duration) continue;
			tkl_reason = strtoken(&p, NULL, " ");
			if (!tkl_reason) continue;
			regex = strtoken(&p, NULL, "");
			if (!regex) continue;
			
			/* from src/modules/m_tkl:
			 *           add:      remove:    spamfilter:    spamfilter+TKLEXT  sqline:
			 * parv[ 1]: +         -          +/-            +                  +/-
			 * parv[ 2]: type      type       type           type               type
			 * parv[ 3]: user      user       target         target             hold
			 * parv[ 4]: host      host       action         action             host
			 * parv[ 5]: setby     removedby  (un)setby      setby              setby
			 * parv[ 6]: expire_at            expire_at (0)  expire_at (0)      expire_at
			 * parv[ 7]: set_at               set_at         set_at             set_at
			 * parv[ 8]: reason               regex          tkl duration       reason
			 * parv[ 9]:                                     tkl reason [A]        
			 * parv[10]:                                     regex              
			 *                                               ^^^^^^^^^^^^^^^^^
			 */
			tkl[3] = target;
			tkl[4] = action;
			tkl[5] = set_by;
			tkl[6] = expire_at;
			tkl[7] = set_at;
			tkl[8] = tkl_duration;
			tkl[9] = tkl_reason;
			tkl[10] = regex;
			tkl[11] = NULL;
			
			recursion = 1;
			m_tkl(&me, &me, 11, tkl);
			recursion = 0;
		}
	}
	fclose(fd);
#ifdef BENCHMARK
	gettimeofday(&tv, NULL);
	multi_log("tkldb: load took %ld useconds",
		((tv.tv_sec - tv_old.tv_sec) * 1000000) + (tv.tv_usec - tv_old.tv_usec));
#endif
}

DLLFUNC int tkldb_tkl_add(aClient *cptr, aClient *sptr, aTKline *tk, int parc, char *parv[])
{
	if (recursion)
		return 0;
	save_tkls(NULL);
	return 0;
}

DLLFUNC int tkldb_tkl_del(aClient *cptr, aClient *sptr, aTKline *tk, int parc, char *parv[])
{
	save_tkls(tk);
	return 0;
}
