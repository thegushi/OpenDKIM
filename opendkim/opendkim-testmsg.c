/*
**  Copyright (c) 2011, The OpenDKIM Project.  All rights reserved.
**
**  $Id: opendkim-testmsg.c,v 1.8 2010/06/19 15:29:12 cm-msk Exp $
*/

#ifndef lint
static char opendkim_testmsg_c_id[] = "@(#)$Id: opendkim-testadsp.c,v 1.8 2010/06/19 15:29:12 cm-msk Exp $";
#endif /* !lint */

/* system includes */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <sysexits.h>
#include <string.h>
#include <unistd.h>

/* libopendkim includes */
#include <dkim.h>

/* macros */
#define	BUFRSZ		1024
#define	CMDLINEOPTS	"d:k:s:"
#define STRORNULL(x)	((x) == NULL ? "(null)" : (x))
#define	TMPTEMPLATE	"/var/tmp/dkimXXXXXX"

/* prototypes */
int usage(void);

/* globals */
char *progname;

/*
**  USAGE -- print a usage message
**
**  Parameters:
**  	None.
**
**  Return value:
**  	EX_CONFIG
*/

int
usage(void)
{
	fprintf(stderr, "%s: usage: %s [-d domain] [-k key] [-s selector]\n",
	        progname, progname);

	return EX_CONFIG;
}

/*
**  MAIN -- program mainline
**
**  Parameters:
**  	argc, argv -- the usual
**
**  Return value:
**  	Exit status.
*/

int
main(int argc, char **argv)
{
	_Bool testkey = NULL;
	int c;
	int n = 0;
	int tfd;
	dkim_policy_t pcode;
	int presult;
	DKIM_STAT status;
	ssize_t rlen;
	ssize_t wlen;
	ssize_t l = (ssize_t) -1;
	dkim_alg_t sa = DKIM_SIGN_RSASHA1;
	dkim_canon_t bc = DKIM_CANON_SIMPLE;
	dkim_canon_t hc = DKIM_CANON_RELAXED;
	DKIM_LIB *lib;
	DKIM *dkim;
	char *p;
	const char *domain = NULL;
	const char *selector = NULL;
	const char *keyfile = NULL;
	char *keydata = NULL;
	char buf[BUFRSZ];
	char fn[BUFRSZ];

	progname = (p = strrchr(argv[0], '/')) == NULL ? argv[0] : p + 1;

	memset(fn, '\0', sizeof fn);
	strncpy(fn, TMPTEMPLATE, sizeof fn);

	while ((c = getopt(argc, argv, CMDLINEOPTS)) != -1)
	{
		switch (c)
		{
		  case 'd':
			domain = optarg;
			n++;
			break;

		  case 'k':
			keyfile = optarg;
			n++;
			break;

		  case 's':
			selector = optarg;
			n++;
			break;

		  default:
			return usage();
		}
	}

	if (n != 0 && n != 3)
		return usage();

	if (n == 3)
	{
		int fd;
		struct stat s;

		fd = open(keyfile, O_RDONLY);
		if (fd < 0)
		{
			fprintf(stderr, "%s: %s: open(): %s\n", progname,
			        keyfile, strerror(errno));
			return EX_OSERR;
		}

		if (fstat(fd, &s) != 0)
		{
			fprintf(stderr, "%s: %s: fstat(): %s\n", progname,
			        keyfile, strerror(errno));
			close(fd);
			return EX_OSERR;
		}

		keydata = malloc(s.st_size + 1);
		if (keydata == NULL)
		{
			fprintf(stderr, "%s: malloc(): %s\n", progname,
			        strerror(errno));
			close(fd);
			return EX_OSERR;
		}

		memset(keydata, '\0', s.st_size + 1);
		rlen = read(fd, keydata, s.st_size);
		if (rlen == -1)
		{
			fprintf(stderr, "%s: %s: read(): %s\n", progname,
			        keyfile, strerror(errno));
			close(fd);
			free(keydata);
			return EX_OSERR;
		}
		else if (rlen < s.st_size)
		{
			fprintf(stderr,
			        "%s: %s: read() truncated (got %ud, expected %ud)\n",
			        progname, keyfile, rlen, s.st_size);
			close(fd);
			free(keydata);
			return EX_DATAERR;
		}

		close(fd);
	}

	lib = dkim_init(NULL, NULL);
	if (lib == NULL)
	{
		fprintf(stderr, "%s: dkim_init() failed\n", progname);
		return EX_SOFTWARE;
	}

	if (n == 0)
	{
		dkim = dkim_verify(lib, progname, NULL, &status);
		if (dkim == NULL)
		{
			fprintf(stderr, "%s: dkim_verify() failed: %s\n",
			        progname, dkim_getresultstr(status));
			dkim_close(lib);
			return EX_SOFTWARE;
		}
	}
	else
	{
		dkim = dkim_sign(lib, progname, NULL, keydata, selector,
		                 domain, hc, bc, sa, l, &status);
		if (dkim == NULL)
		{
			fprintf(stderr, "%s: dkim_sign() failed: %s\n",
			        progname, dkim_getresultstr(status));
			if (keydata != NULL)
				free(keydata);
			dkim_close(lib);
			return EX_SOFTWARE;
		}
	}

	tfd = mkstemp(fn);
	if (tfd < 0)
	{
		fprintf(stderr, "%s: mkstemp(): %s\n",
		        progname, strerror(errno));
		if (keydata != NULL)
			free(keydata);
		dkim_close(lib);
		return EX_SOFTWARE;
	}

	for (;;)
	{
		rlen = fread(buf, 1, sizeof buf, stdin);
		if (ferror(stdin))
		{
			fprintf(stderr, "%s: fread(): %s\n",
			        progname, strerror(errno));
			dkim_free(dkim);
			dkim_close(lib);
			close(tfd);
			if (keydata != NULL)
				free(keydata);
			return EX_SOFTWARE;
		}

		wlen = write(tfd, buf, rlen);
		if (wlen == -1)
		{
			fprintf(stderr, "%s: %s: write(): %s\n",
			        progname, fn, strerror(errno));
			dkim_free(dkim);
			dkim_close(lib);
			close(tfd);
			if (keydata != NULL)
				free(keydata);
			return EX_SOFTWARE;
		}

		status = dkim_chunk(dkim, buf, rlen);
		if (status != DKIM_STAT_OK)
		{
			fprintf(stderr, "%s: dkim_chunk(): %s\n",
			        progname, dkim_getresultstr(status));
			dkim_free(dkim);
			dkim_close(lib);
			close(tfd);
			if (keydata != NULL)
				free(keydata);
			return EX_SOFTWARE;
		}

		if (feof(stdin))
			break;
	}

	status = dkim_chunk(dkim, NULL, 0);
	if (status != DKIM_STAT_OK)
	{
		fprintf(stderr, "%s: dkim_chunk(): %s\n",
		        progname, dkim_getresultstr(status));
		dkim_free(dkim);
		dkim_close(lib);
		close(tfd);
		if (keydata != NULL)
			free(keydata);
		return EX_SOFTWARE;
	}

	status = dkim_eom(dkim, &testkey);
	if (status != DKIM_STAT_OK)
	{
		fprintf(stderr, "%s: dkim_eom(): %s\n",
		        progname, dkim_getresultstr(status));
		dkim_free(dkim);
		dkim_close(lib);
		close(tfd);
		if (keydata != NULL)
			free(keydata);
		return EX_SOFTWARE;
	}

	if (n == 0)
	{
		/* XXX -- do a policy query */
	}
	else
	{
		unsigned char *sighdr;
		size_t siglen;

		/* extract signature */
		status = dkim_getsighdr_d(dkim,
		                          strlen(DKIM_SIGNHEADER),
		                          &sighdr, &siglen);
		if (status != DKIM_STAT_OK)
		{
			fprintf(stderr, "%s: dkim_getsighdr_d(): %s\n",
			        progname, dkim_getresultstr(status));
			dkim_free(dkim);
			dkim_close(lib);
			close(tfd);
			if (keydata != NULL)
				free(keydata);
			return EX_SOFTWARE;
		}

		/* print it and the message */
		fprintf(stdout, "%s: %s\r\n", DKIM_SIGNHEADER, sighdr);
		(void) lseek(tfd, 0, SEEK_SET);
		for (;;)
		{
			rlen = read(tfd, buf, sizeof buf);
			(void) fwrite(buf, 1, rlen, stdout);
			if (rlen < sizeof buf)
				break;
		}
	}

	dkim_free(dkim);
	dkim_close(lib);
	close(tfd);
	if (keydata != NULL)
		free(keydata);

	return EX_OK;
}