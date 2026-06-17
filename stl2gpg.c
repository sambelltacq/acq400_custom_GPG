#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <assert.h>
#include <ctype.h>
#include <unistd.h>

#include "popt.h"

FILE *fp_out;
FILE *fp_out32;
FILE *fp_log;
FILE* fp_state;

#define MAXCOUNT 0x00ffffff

#define STARTUP	5	/* minimum count on start + 1 */
#define MINSTEP	2	/* minimum step size (by experiment) */

#define NSTATE 1
#define MAXSTATE 512	/* hardware limit */

void write_gpd(unsigned count, unsigned state, int line)
{
	unsigned gpd = (count<<8) | (state&0x0ff);

	fwrite(&gpd, sizeof(unsigned), 1, fp_out);
	if (fp_out32){
		unsigned state2 = state >> 8;
		fwrite(&state2, sizeof(unsigned), 1, fp_out32);
		fprintf(fp_log, "write_gpd %3d: %8u %08x %06x:%02x %08x\n", 
					line, count, state, state2, state&0x0ff, gpd);
	}else{
		fprintf(fp_log, "write_gpd %3d: %8u %02x %08x\n", line, count, state, gpd);
	}
	if (fp_state){
		fwrite(&state, sizeof(unsigned), 1, fp_state);
	}
}

long expand_state(unsigned state, long until_count)
{
	static unsigned count0;
	static int line;

	line += 1;

	if (until_count < MAXCOUNT){
		fprintf(fp_log, "expand_state() %d\n", __LINE__);
		if (until_count-count0 < MINSTEP){
			fprintf(fp_log, "ENFORCING MINSTEP %d\n", MINSTEP);
			until_count = count0 + MINSTEP;
		}
		write_gpd(until_count, state, line);
	}else{
		unsigned remain = until_count - count0;
		unsigned ontheclock = count0&MAXCOUNT;		/* what's already on the clock */
		if (remain < MINSTEP){
			fprintf(fp_log, "ENFORCING MINSTEP %d\n", MINSTEP);
			remain = MINSTEP;
		}
		if (MAXCOUNT-ontheclock > remain){
			fprintf(fp_log, "expand_state() %d\n", __LINE__);
			write_gpd(count0+remain, state, line);
		}else{
			fprintf(fp_log, "expand_state() %d\n", __LINE__);
			write_gpd(MAXCOUNT, state, line);
			remain -= (MAXCOUNT+1)-ontheclock;
			
			while(remain > MAXCOUNT){
				fprintf(fp_log, "expand_state() %d\n", __LINE__);
				write_gpd(MAXCOUNT, state, line);
				remain -= MAXCOUNT+1;
			}
			fprintf(fp_log, "expand_state() %d\n", __LINE__);
			write_gpd(remain, state, line);
		}
	}

	count0 = until_count;
	return until_count;
}



int FINAL = MINSTEP;		/* final state length */

void prompt(int state_count){
	printf("%d ok>\n", state_count);
	fflush(stdout);
}

char* cscale_def = "1";

struct poptOption opt_table[] = {
	{ "cscale", 'c', POPT_ARG_STRING, &cscale_def, 'c', "cscale on the command line" },
	POPT_AUTOHELP
	POPT_TABLEEND
};


int check_gpg32(const char* outfile)
{
	char f32[80];
	snprintf(f32, 80, "%s32", outfile);

	if (access(outfile, F_OK) == 0 && access(f32, F_OK) == 0){
		fp_out32 = fopen(f32, "w");
                if (fp_out32 == 0){
                        perror("failed to open gpg32");
                        return -1;
                }
	}

	return 0;
}

// https://stackoverflow.com/questions/744766/how-to-compare-ends-of-strings-in-c
int EndsWith(const char *str, const char *suffix)
{
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix >  lenstr)
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

int read_cscale_def(void)
/* we have to defer the read until the first data arrives, because this program is likely launched ahead of time */
{
	int cscale = 1;

	if (isdigit(cscale_def[0])){
        	cscale = atoi(cscale_def);
        }else{
                FILE* fp = fopen(cscale_def, "r");
        	if (fp){
                	int nc = fscanf(fp, "%d", &cscale);
                        if (nc != 1){
                        	fprintf(stderr, "ERROR %s scan failed\n", cscale_def);
                                exit(1);
                        }
                }else{
                	perror(cscale_def);
                        exit(1);
                }
	}
	return cscale;
}
int main(int argc, const char** argv)
{
	char aline[128];
	int delta_times = 0;
	long abs_count = 0;
	int nstate = 0;
	int nl = 0;
	int state_count = 0;
	unsigned state0 = 0;
	int cscale = 1;
	const char* arg1;
	const char* arg2;
	unsigned* input_counts = calloc(MAXSTATE, sizeof(unsigned));
	unsigned* input_states = calloc(MAXSTATE, sizeof(unsigned));
	unsigned count;
	unsigned state;


	if (getenv("FINAL")) FINAL = atoi(getenv("FINAL"));
	if (getenv("STL2GPG_LOG")){
		fp_log = fopen(getenv("STL2GPG_LOG"), "w");
		assert(fp_log);
	}else{
		fp_log = stderr;
	}
	if (getenv("CSCALE")){
		cscale = atoi(getenv("CSCALE"));
	}
        poptContext opt_context =
                        poptGetContext(argv[0], argc, argv, opt_table, 0);
        int rc;
        while ( (rc = poptGetNextOpt( opt_context )) >= 0 ){
                switch(rc){
                case 'c':
			cscale = read_cscale_def();
                        break;
                }
        }

        arg1 = poptGetArg(opt_context);
	if (arg1){
		char* outfile = strdup(arg1);
		if (EndsWith(outfile, ".stl")){
			fprintf(stderr, "refusing to overwrite file %s\n", outfile);
			return -1;
		}else{
			fp_out = fopen(outfile, "w");
			if (fp_out == 0){
				perror("failed to open outfile");
				return -1;
			}
			if (check_gpg32(outfile) == -1){
				return -1;
			}
			arg2 = poptGetArg(opt_context);
			if (arg2){
				char* statefile = strdup(arg2);
				fp_state = fopen(statefile, "w");
				if (fp_out == 0){
					perror("failed to open state file");
					return -1;
				}
				free(statefile);
			}
			snprintf(aline, 128, "/tmp/%s", basename(outfile));
			fp_log = fopen(aline, "w");
		}
		free(outfile);
	}else{
		fp_out = stdout;
	}
	for (; fgets(aline, 128, stdin) && ++nl; prompt(state_count)){
		char* pline = aline;

		if (nl == 1){
			cscale = read_cscale_def();
		}
		if (fp_log) {
			fputs(aline, fp_log);
		}
		if (aline[0] == '#' || strlen(aline) < 2){
			continue;
		}else if (strstr(aline, "EOFLOOP")){
			break;
		}else if (strstr(aline, "EOF")){
			fprintf(stderr, "quit on EOF\n");
			break;
		}else if (aline[0] == '+'){
			pline = aline + 1;
			delta_times = 1;	/* better make them all delta */
		}
		/* scan two numbers. IGNORE any trailing data same line */
		if ((nstate = sscanf(pline, "%u,%x", &count, &state) - 1) >= 1){
			count *= cscale;
			if (state_count+1 >= MAXSTATE-1){
				fprintf(stderr, "WARNING: state count limit %u exceeded\n", MAXSTATE);
				break;
			}
			input_counts[state_count] = count;
			input_states[state_count] = state;

			if (state_count++ == 0){
				state0 = state;			/* do nothing, but set state0 */
			}else{
				if (state_count++ == 1){
					if (count < STARTUP){
						fprintf(stderr, "STARTUP min count %d\n", STARTUP);
						count = STARTUP;
					}else if (delta_times){
						count -= 1;	/* first count from zero */
					}
				}
				/* abs times: all counts from zero */
				abs_count = expand_state(state0,
					delta_times? abs_count+count: count-1);
				state0 = state;
			}
		}else{
			fprintf(stderr, "scan failed\n");
			return -1;
		}
	}

        abs_count = expand_state(state0, delta_times? abs_count+count: count);

	if (state_count < 3){
		fprintf(stderr, "state_count < 3 add entry\n");
		abs_count = expand_state(input_states[0], 
                                delta_times? abs_count+FINAL: abs_count+FINAL);
	}

	fprintf(stderr, "return 0\n");
	return 0;
}
