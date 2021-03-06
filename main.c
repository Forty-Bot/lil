/*
 * LIL - Little Interpreted Language
 * Copyright (C) 2010 Kostas Michalopoulos
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Kostas Michalopoulos <badsector@runtimelegend.com>
 */

#define _DEFAULT_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lil.h"

static int running = 1;
static int exit_code = 0;

static void do_exit(struct lil *lil, struct lil_value *val)
{
	running = 0;
	exit_code = (int)lil_to_integer(val);
}

static char *do_system(size_t argc, char **argv)
{
	char *cmd = NULL;
	int cmdlen = 0;
	size_t i;
	FILE *p;
	for (i = 0; i < argc; i++) {
		size_t len = strlen(argv[i]);
		if (i != 0) {
			cmd = realloc(cmd, cmdlen + 1);
			cmd[cmdlen++] = ' ';
		}
		cmd = realloc(cmd, cmdlen + len);
		memcpy(cmd + cmdlen, argv[i], len);
		cmdlen += len;
	}
	cmd = realloc(cmd, cmdlen + 1);
	cmd[cmdlen] = 0;
	p = popen(cmd, "r");
	free(cmd);
	if (p) {
		char *retval = NULL;
		size_t size = 0;
		char buff[1024];
		ssize_t bytes;
		while ((bytes = fread(buff, 1, 1024, p))) {
			retval = realloc(retval, size + bytes);
			memcpy(retval + size, buff, bytes);
			size += bytes;
		}
		retval = realloc(retval, size + 1);
		retval[size] = 0;
		pclose(p);
		return retval;
	} else {
		return NULL;
	}
}

static struct lil_value *fnc_writechar(struct lil *lil, size_t argc,
				       struct lil_value **argv)
{
	if (!argc)
		return NULL;
	printf("%c", (char)lil_to_integer(argv[0]));
	return NULL;
}

static struct lil_value *fnc_system(struct lil *lil, size_t argc,
				    struct lil_value **argv)
{
	const char **sargv = malloc(sizeof(char *) * (argc + 1));
	struct lil_value *r = NULL;
	char *rv;
	size_t i;
	if (argc == 0)
		return NULL;
	for (i = 0; i < argc; i++)
		sargv[i] = lil_to_string(argv[i]);
	sargv[argc] = NULL;
	rv = do_system(argc, (char **)sargv);
	if (rv) {
		r = lil_alloc_string(rv);
		free(rv);
	}
	free(sargv);
	return r;
}

static struct lil_value *fnc_canread(struct lil *lil, size_t argc,
				     struct lil_value **argv)
{
	return (feof(stdin) || ferror(stdin)) ? NULL : lil_alloc_integer(1);
}

static struct lil_value *fnc_readline(struct lil *lil, size_t argc,
				      struct lil_value **argv)
{
	size_t len = 0, size = 64;
	char *buffer;
	int ch;
	struct lil_value *retval;
	if (feof(stdin)) {
		lil_set_error(lil, "End of file");
		return NULL;
	}
	buffer = malloc(size);
	for (;;) {
		ch = fgetc(stdin);
		if (ch == EOF) {
			if (ferror(stdin))
				lil_set_error(lil, "Error reading from stdin");
			break;
		}
		if (ch == '\r')
			continue;
		if (ch == '\n')
			break;
		if (len < size) {
			size += 64;
			buffer = realloc(buffer, size);
		}
		buffer[len++] = (char)ch;
	}
	buffer = realloc(buffer, len + 1);
	buffer[len] = 0;
	retval = lil_alloc_string(buffer);
	free(buffer);
	return retval;
}

static int repl(void)
{
	char buffer[16384];
	struct lil *lil = lil_new();
	lil_register(lil, "writechar", fnc_writechar);
	lil_register(lil, "system", fnc_system);
	lil_register(lil, "canread", fnc_canread);
	lil_register(lil, "readline", fnc_readline);
	printf("Little Interpreted Language Interactive Shell\n");
	lil_callback(lil, LIL_CALLBACK_EXIT, (lil_callback_proc_t)do_exit);
	while (running) {
		struct lil_value *result;
		const char *strres;
		const char *err_msg;
		size_t pos;
		buffer[0] = 0;
		printf("# ");
		if (!fgets(buffer, 16384, stdin))
			break;
		result = lil_parse(lil, buffer, 0, 0);
		strres = lil_to_string(result);
		if (strres[0])
			printf("%s\n", strres);
		lil_free_value(result);
		if (lil_error(lil, &err_msg, &pos)) {
			printf("error at %i: %s\n", (int)pos, err_msg);
		}
	}
	lil_free(lil);
	return exit_code;
}

static int nonint(int argc, const char *argv[])
{
	struct lil *lil = lil_new();
	const char *filename = argv[1];
	const char *err_msg;
	size_t pos;
	struct lil_list *arglist = lil_alloc_list();
	struct lil_value *args, *result;
	char *tmpcode;
	int i;
	lil_register(lil, "writechar", fnc_writechar);
	lil_register(lil, "system", fnc_system);
	lil_register(lil, "canread", fnc_canread);
	lil_register(lil, "readline", fnc_readline);
	for (i = 2; i < argc; i++) {
		lil_list_append(arglist, lil_alloc_string(argv[i]));
	}
	args = lil_list_to_value(arglist, 1);
	lil_free_list(arglist);
	lil_set_var(lil, "argv", args, LIL_SETVAR_GLOBAL);
	lil_free_value(args);
	tmpcode = malloc(strlen(filename) + 256);
	sprintf(tmpcode,
		"set __lilmain:code__ [read {%s}]\nif [streq $__lilmain:code__ ''] {print There is no code in the file or the file does not exist} {eval $__lilmain:code__}\n",
		filename);
	result = lil_parse(lil, tmpcode, 0, 1);
	free(tmpcode);
	lil_free_value(result);
	if (lil_error(lil, &err_msg, &pos)) {
		fprintf(stderr, "lil: error at %i: %s\n", (int)pos, err_msg);
	}
	lil_free(lil);
	return exit_code;
}

int main(int argc, const char *argv[])
{
	if (argc < 2)
		return repl();
	else
		return nonint(argc, argv);
}
