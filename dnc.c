/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/*
 * Copyright (c) 2016, Joyent, Inc.
 */

/*
 * dnc.c: simple nc-like tool intended for demonstrating network failure modes
 */

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <libgen.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

char *dnc_arg0;
const char *dnc_usagefmt =
    "usage: %s [-n] -l -p LISTEN_PORT\n"
    "       %s [-n] HOST PORT\n";
static const char *dnc_optstr = ":lnp:";
static void dnc_usage(void);

typedef enum {
	DNC_F_LISTEN = 0x1,
} dncflags_t;

typedef struct {
	dncflags_t		dnc_flags;
	struct sockaddr_in	dnc_addr;
	FILE			*dnc_log;
} dnc_t;

static void dnc_usage(void);
static int dnc_port_parse(const char *, uint16_t *);
static int dnc_ipv4_parse(const char *, struct in_addr *);
static int dnc_listen(dnc_t *);
static int dnc_connect(dnc_t *);
static int dnc_connection(dnc_t *, int);
static void dnc_log(dnc_t *, const char *, ...);
static void dnc_vlog(dnc_t *, const char *, va_list);

int
main(int argc, char *argv[])
{
	char c;
	boolean_t resolve = B_TRUE;
	boolean_t haveport = B_FALSE;
	uint16_t port;
	int rv;
	dnc_t dnc;

	dnc_arg0 = basename(argv[0]);
	bzero(&dnc, sizeof (dnc));
	dnc.dnc_addr.sin_family = AF_INET;
	dnc.dnc_log = stdout;

	while ((c = getopt(argc, argv, dnc_optstr)) != -1) {
		switch (c) {
		case 'l':
			dnc.dnc_flags |= DNC_F_LISTEN;
			break;

		case 'n':
			resolve = B_FALSE;
			break;

		case 'p':
			haveport = B_TRUE;
			if (dnc_port_parse(optarg, &port) != 0) {
				warnx("invalid TCP port: %s\n", optarg);
				dnc_usage();
			} else {
				dnc.dnc_addr.sin_port = htons((uint16_t)port);
			}
			break;


		case ':':
			warnx("option requires an argument: -%c", optopt);
			dnc_usage();
			break;

		case '?':
			warnx("unrecognized option: -%c", optopt);
			dnc_usage();
			break;
		}
	}

	if (dnc.dnc_flags & DNC_F_LISTEN) {
		if (!haveport) {
			warnx("-l option requires -p");
			dnc_usage();
		}

		if (optind != argc) {
			warnx("extra arguments");
			dnc_usage();
		}

		rv = dnc_listen(&dnc);
	} else {
		if (optind > argc - 2) {
			warnx("missing arguments");
			dnc_usage();
		} else if (optind < argc - 2) {
			warnx("extra arguments");
			dnc_usage();
		}

		if (dnc_ipv4_parse(argv[optind], &dnc.dnc_addr.sin_addr) != 0) {
			warnx("invalid IP address: %s\n", argv[optind]);
			if (resolve) {
				warnx("note: hostnames not supported\n");
			}
			dnc_usage();
		}

		if (dnc_port_parse(argv[optind + 1], &port) != 0) {
			warnx("invalid TCP port: %s\n", argv[optind + 1]);
			dnc_usage();
		}

		dnc.dnc_addr.sin_port = htons(port);
		rv = dnc_connect(&dnc);
	}

	return (rv);
}

static void
dnc_usage(void)
{
	(void) fprintf(stderr, dnc_usagefmt, dnc_arg0, dnc_arg0);
	exit(2);
}

/*
 * Parse the given string as a TCP port.  No byte order translation is done.
 * Returns 0 on success or -1 on validation failure.
 */
static int
dnc_port_parse(const char *portstr, uint16_t *portp)
{
	char *endptr;
	unsigned long port;

	port = strtol(portstr, &endptr, 10);
	if (*endptr != '\0' || port > UINT16_MAX) {
		return (-1);
	}

	*portp = (uint16_t)port;
	return (0);
}

static int
dnc_ipv4_parse(const char *ip4str, struct in_addr *addrp)
{
	if (inet_pton(AF_INET, ip4str, addrp) != 1) {
		return (-1);
	}

	return (0);
}

static int
dnc_listen(dnc_t *dncp)
{
	int sock, client;
	int rv = -1;

	assert(dncp->dnc_flags & DNC_F_LISTEN);
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		warn("socket");
		goto out;
	}

	if (bind(sock, (struct sockaddr *)&dncp->dnc_addr,
	    sizeof (dncp->dnc_addr)) != 0) {
		warn("bind");
		goto out;
	}

	if (listen(sock, 256) != 0) {
		warn("listen");
		goto out;
	}

	dnc_log(dncp, "listening");
	client = accept(sock, NULL, 0);
	if (client < 0) {
		goto out;
	}

	rv = dnc_connection(dncp, client);
	(void) close(client);

out:
	(void) close(sock);
	return (rv);
}

static int
dnc_connect(dnc_t *dncp)
{
	int sock;
	int rv = -1;

	assert(!(dncp->dnc_flags & DNC_F_LISTEN));
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		warn("socket");
		goto out;
	}

	dnc_log(dncp, "establishing connection");

	if (connect(sock, (struct sockaddr *)&dncp->dnc_addr,
	    sizeof (dncp->dnc_addr))) {
		warn("connect");
		goto out;
	}

	dnc_log(dncp, "connected");
	rv = dnc_connection(dncp, sock);

out:
	(void) close(sock);
	return (rv);
}

/* PRINTFLIKE2 */
static void
dnc_log(dnc_t *dncp, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	dnc_vlog(dncp, fmt, args);
	va_end(args);
}

static void
dnc_vlog(dnc_t *dncp, const char *fmt, va_list args)
{
	int errsave;
	time_t nowt;
	struct tm nowtm;
	char buf[sizeof ("2014-01-01T01:00:00Z")];

	errsave = errno;

	time(&nowt);
	gmtime_r(&nowt, &nowtm);
	if (strftime(buf, sizeof (buf), "%FT%TZ", &nowtm) == 0) {
		err(EXIT_FAILURE, "strftime");
	}

	(void) fprintf(dncp->dnc_log, "%s: ", buf);
	(void) vfprintf(dncp->dnc_log, fmt, args);
	(void) fprintf(dncp->dnc_log, "\n");

	errno = errsave;
}

/*
 * Once we've got an established connection, whether we're the server or the
 * client, we enter dnc_connection() to poll on the socket until data is
 * available.
 */
static int
dnc_connection(dnc_t *dncp, int sock)
{
	struct pollfd pollfds[2];
	char buf[128];
	int err;
	ssize_t nread, nwritten;

	pollfds[0].fd = STDIN_FILENO;
	pollfds[0].events = POLLIN;
	pollfds[0].revents = 0;
	pollfds[1].fd = sock;
	pollfds[1].events = POLLIN;
	pollfds[1].revents = 0;

	for (;;) {
		dnc_log(dncp, "entering poll()");

		if (pollfds[0].events == 0 && pollfds[1].events == 0) {
			break;
		}

		err = poll(pollfds, 2, -1);
		if (err < 0) {
			warn("poll");
			return (-1);
		}

		dnc_log(dncp, "poll returned events 0x%x/0x%x",
		    pollfds[0].revents, pollfds[1].revents);
		if ((pollfds[0].revents & (POLLIN | POLLHUP)) != 0) {
			dnc_log(dncp, "reading from stdin");
			nread = read(pollfds[0].fd, buf, sizeof (buf));
			if (nread < 0) {
				warn("read");
				return (-1);
			}

			if (nread == 0) {
				/*
				 * The user will be issuing no more commands,
				 * but we don't really care about that.
				 */
				pollfds[0].events = 0;
			}

			dnc_log(dncp, "writing %d bytes read from stdin "
			    "to socket", nread);
			nwritten = write(pollfds[1].fd, buf, nread);
			if (nwritten < 0) {
				warn("write");
				return (-1);
			}

			if (nwritten != nread) {
				warnx("short write: expected %d, wrote %d",
				    nread, nwritten);
				return (-1);
			}

		}

		if ((pollfds[1].revents & (POLLIN | POLLHUP)) != 0) {
			dnc_log(dncp, "reading from socket");
			nread = read(pollfds[1].fd, buf, sizeof (buf));
			if (nread < 0) {
				warn("read");
				return (-1);
			}

			if (nread == 0) {
				dnc_log(dncp, "read end-of-stream from socket");
				pollfds[1].events = 0;
			}

			dnc_log(dncp, "read %d bytes from socket", nread);
		}
	}

	dnc_log(dncp, "read end-of-stream from both socket and stdin");
	dnc_log(dncp, "pausing until signal");
	pause();

	return (0);
}
