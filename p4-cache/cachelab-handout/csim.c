#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <ctype.h>
#include "cachelab.h"

#define LINE_LENGTH  20
#define LRU_INIT_NUM 9999

typedef unsigned cache_opt_res;
/**
 * NOTE: 
 * I: nop
 * L: miss; hit; miss eviction;
 * S: miss; hit; miss eviction;
 * M: miss, hit; miss eviction; hit, hit
 */

/* cache operation result type */
cache_opt_res    HIT         = 0x01;
cache_opt_res    MISS        = 0x10;
cache_opt_res    EVICTION    = 0x100;
    
/* cache line struct */
typedef struct cache_line_st{
    int valid; // valid field
    int tag;   // tag field
    int block; // block field
    int lrunum;
} cache_line;

/* cache set struct */
typedef struct cache_set_st {
    cache_line *cls;
} cache_set;

/* simulator cache statistics info struct */
typedef struct cache_stats_st {
    int hits;
    int misses;
    int evictions;
} cache_stats;

/* simulator cache struct */
typedef struct simulator_cache_st {
    int setcnt;
    int linecnt;
    int blockcnt;

    int s, E, b;

    char *tracefile;
    cache_set *sets;
    cache_stats cs;

    int verbose;
    int setmask;
} simulator_cache;

/* cache operation agrs struct */
typedef struct {
    char inst; // if 'I', then it's instruction operation, else data operation.
    char opttype;
    unsigned int addr;
    int size;
} cache_opt ;

/******************** custome function declaration ******************************************/
/* Print help options */
void print_help_options();

/* Parse simulator cache args */
void parse_cache_args(int argc, char *argv[], simulator_cache *sc);

/* Handle cache operations and record statistics */
void handle_cache_stuff(simulator_cache *sc);

/* Init simulator cache */
void init_cache_matrix(simulator_cache *sc);

/* Do base cache opt */
void do_base_opt(simulator_cache *sc, cache_opt co, cache_opt_res *optres);

/* Do normal cache opeartion */
void do_cache_opt(simulator_cache *sc, cache_opt co);

/* Do load data task */
void do_load_data(simulator_cache *sc, cache_opt co);

/* Do store data task */
void do_store_data(simulator_cache *sc, cache_opt co);

/* Do modify data task */
void do_modify_data(simulator_cache *sc, cache_opt co);

/* update cache data struct */
int update_cache(simulator_cache *sc, int setno, int tag);

/* Search the specific cache line index according LRU */
int search_lru_cache_line(simulator_cache *sc, int setno);

/* Free simulator cache memory */
void free_cache(simulator_cache *sc);

/* Trim white space for string */
char *trim_white_space(char *str);

/* Print cache opt result info */
void print_verbose(simulator_cache *sc, cache_opt co, cache_opt_res optres, int flag);

/******************** custome function declaration end ******************************************/

// returns a pointer to a substring of the original string.
// the return string can not be free.
char *trim_white_space(char *str)
{
  char *end;

  // Trim leading space
  while(isspace((unsigned char)*str)) str++;

  if(*str == 0)  // All spaces?
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace((unsigned char)*end)) end--;

  // Write new null terminator character
  end[1] = '\0';

  return str;
}

/* Print help options */
void print_help_options() 
{
    printf("Usage: ./csim [-hv] -s <num> -E <num> -b <num> -t <file>\n");
    printf("Options:\n");
    printf("  -h         Print this help message.\n");
    printf("  -v         Optional verbose flag.\n");
    printf("  -s <num>   Number of set index bits.\n");
    printf("  -E <num>   Number of lines per set.\n");
    printf("  -b <num>   Number of block offset bits.\n");
    printf("  -t <file>  Trace file.\n");
    printf("\n");
    printf("Examples:\n");
    printf("  linux>  ./csim -s 4 -E 1 -b 4 -t traces/yi.trace\n");
    printf("  linux>  ./csim -v -s 8 -E 2 -b 4 -t traces/yi.trace\n");
}

/* Parse simulator cache args */
void parse_cache_args(int argc, char *argv[], simulator_cache *sc)
{
    extern char *optarg;
    extern int optind, opterr, optopt;

    char opt;
    int argcnt = 0;
    while ((opt = getopt(argc, argv, "hvs:E:b:t:")) != -1) {
        switch (opt) {
        case 'h':
            print_help_options();
            exit(0);
        case 'v':
            sc->verbose = 1;
            break;
        case 's':
            sc->s = atoi(optarg);
            sc->setcnt = 0x01 << sc->s;
            argcnt++;
            break;
        case 'E':
            sc->linecnt = sc->E = atoi(optarg);
            argcnt++;
            break;
        case 'b':
            sc->b = atoi(optarg);
            sc->blockcnt = 0x01 << sc->b;
            argcnt++;
            break;
        case 't':
            sc->tracefile = optarg;
            argcnt++;
            break;
        default:
            print_help_options();
            exit(1);
        }
    }
    if (argcnt < 4) {
        print_help_options();
        exit(1);
    }
    // printf("v=%d, s=%d, E=%d, b=%d, t=%s.\n", sc->verbose, sc->setcnt, sc->linecnt, sc->blockcnt, sc->tracefile);
    return;
}

/* Init simulator cache */
void init_cache_matrix(simulator_cache *sc)
{
    // init cache matirx.
    int i, j;
    sc->sets = (cache_set *) malloc(sc->setcnt * sizeof(cache_set));
    if(!sc->sets){
        fprintf(stderr, "Set Memory allocation error!");
        exit(1);
    }
    for (i = 0; i < sc->setcnt; i++) {
        sc->sets[i].cls = (cache_line *) malloc(sc->linecnt * sizeof(cache_line));
        if(!sc->sets[i].cls){
            fprintf(stderr, "Cache line Memory allocation error!");
            exit(1);
        }
        for (j = 0; j < sc->linecnt; j++) {
            sc->sets[i].cls[j].valid  = 0;
            sc->sets[i].cls[j].tag    = -1;
            sc->sets[i].cls[j].lrunum = LRU_INIT_NUM;
            // sc->sets.cls[j].block = 0;
        }
    }
    // obtain set mask and cache line mask.
    int setmask = 1;
    i = 0;
    while (++i < sc->s) {
        setmask = setmask << 1;
        setmask |= 0x01;
    }
    setmask = setmask << sc->b;
    sc->setmask = setmask;
    sc->cs.evictions = sc->cs.hits = sc->cs.misses = 0;
    // printf("cache matrix init successfully!\n");
}

/* Handle cache operations and record statistics */
void handle_cache_stuff(simulator_cache *sc)
{
    FILE *fp = fopen(sc->tracefile, "r");
    if(NULL == fp) {
        fprintf(stderr, "%s: No such file or directory\n", sc->tracefile);
        exit(1);
    }
    // init simulator cache 
    init_cache_matrix(sc);
    char linestr[LINE_LENGTH] = {0};
    cache_opt co;
    while(!feof(fp))
    {
        memset(linestr, 0, sizeof(linestr));
        fgets(linestr, sizeof(linestr) - 1, fp); 
        sscanf(linestr,"%c%c %x,%d", &co.inst, &co.opttype, &co.addr, &co.size);
        char *t = trim_white_space(linestr);
        if (0 == strcmp(t, ""))  {break;}
        // if (0 == strcmp(linestr, " ") || 0 == strcmp(linestr, "")) {break;} // TODO: verify more carefully.
        // printf("%c%c %x,%d\n",  co.inst, co.opttype, co.addr, co.size);
        do_cache_opt(sc, co);
    }
    fclose(fp);
}   

/* Do normal cache opeartion */
void do_cache_opt(simulator_cache *sc, cache_opt co)
{
    // instruction or data opt.
    switch (co.inst) {
    case 'I': // do instruction related opt.
        return;
    case ' ': // do data releated opt.
        break;
    default:
        printf("Unreconginzed operation type %c!\n", co.inst);  // process continue.
        return;
    }
    // do task according to data operation type.
    switch (co.opttype) {
    case 'L': // do load data opt.
        // printf("Do load data task.\n");
        do_load_data(sc, co);
        break;
    case 'S': // do store data opt.
        // printf("Do store data task.\n");
        do_store_data(sc, co);
        break;
    case 'M': // do modify data opt.
        // printf("Do modify data task.\n");
        do_modify_data(sc, co);
        break;
    default:
        printf("Unreconginzed data operation type %c!\n", co.opttype); // process continue.
        return;
    }
}

/* Do base cache opt */
void do_base_opt(simulator_cache *sc, cache_opt co, cache_opt_res *optres)
{
    // convert addr to hex.
    
    // locate set
    int setno = (co.addr & sc->setmask) >> sc->b ;
    // match cache line
    int linemask = co.addr >> (sc->b + sc->s) << (sc->b + sc->s); // logical right shift and then turn left.
    int tag = (co.addr & linemask);
    int i = 0;
    int miss = 1;
    for (; i < sc->linecnt; i++) {
        int valid = sc->sets[setno].cls[i].valid; // index check?
        int tagbit = sc->sets[setno].cls[i].tag;
        if (valid &&  tag == tagbit) {
            miss = 0;
            sc->cs.hits++;
            *optres |= HIT;
            // update access record.
            sc->sets[setno].cls[i].lrunum = LRU_INIT_NUM;
            for (int j = 0; j < sc->linecnt; j++) {
                if (j != i) {
                    sc->sets[setno].cls[j].lrunum--;
                }
            }
        }
    }
    if (miss) {
        sc->cs.misses++;
        *optres |= MISS;
        // read data from RAM...
        // update cache data
        if(update_cache(sc, setno, tag)) {
            sc->cs.evictions++;
            *optres |= EVICTION;
        }
    }
    return;
}

/* Do load data task */
void do_load_data(simulator_cache *sc, cache_opt co)
{
    cache_opt_res optres = 0;
    do_base_opt(sc, co, &optres);
    // print verbose if enable.
    print_verbose(sc, co, optres, 1);
    if (sc->verbose) printf("\n");
}

/* Do store data task */
void do_store_data(simulator_cache *sc, cache_opt co)
{
    cache_opt_res optres = 0;
    do_base_opt(sc, co, &optres);
    // print verbose if enable.
    print_verbose(sc, co, optres, 1);
    if (sc->verbose) printf("\n");
}

/* Do modify data task */
void do_modify_data(simulator_cache *sc, cache_opt co)
{
    cache_opt_res optres = 0;
    do_base_opt(sc, co, &optres);
    // print verbose if enable.
    print_verbose(sc, co, optres, 1);
    optres = 0;
    do_base_opt(sc, co, &optres);
    print_verbose(sc, co, optres, 0);
    if (sc->verbose) printf("\n");

}

/* Update cache data struct and evict cache line by lru if necessary */
int update_cache(simulator_cache *sc, int setno, int tag) 
{
    int i = 0;
    int evicted = 1;
    int lineno = 0;
    for (; i < sc->linecnt; i++) { // check whether there is any  empty cache line.
        if (sc->sets[setno].cls[i].valid == 0) {
            evicted = 0;
            break;
        }
    }
    if (!evicted) { // remain empy cache line.
        sc->sets[setno].cls[i].valid = 1;
        sc->sets[setno].cls[i].tag = tag;
        lineno = i;
    } else { // full set, need do eviction by LRU.
        int evindex = search_lru_cache_line(sc, setno);
        sc->sets[setno].cls[evindex].valid = 1;
        sc->sets[setno].cls[evindex].tag = tag;
        lineno = evindex;
    }
    // update cache record.
    sc->sets[setno].cls[lineno].lrunum = LRU_INIT_NUM;
    for (int j = 0; j < sc->linecnt; j++) {
        if (j != lineno) {
            sc->sets[setno].cls[j].lrunum--;
        }
    }
    return evicted;
}

/* Search the specific cache line index according LRU */
int search_lru_cache_line(simulator_cache *sc, int setno)
{
    int i = 0;
    int evindex = 0;
    int minlru = LRU_INIT_NUM;
    for(; i < sc->linecnt; i++) {
        if(sc->sets[setno].cls[i].lrunum < minlru){
            evindex = i;
            minlru = sc->sets[setno].cls[i].lrunum;
        }
    }
    return evindex;
}

/* Free simulator cache memory */
void free_cache(simulator_cache *sc)
{
    int i = 0;
    for (; i < sc->linecnt; i++) {
        free(sc->sets[i].cls);
    }
    free(sc->sets);
    // printf("Free simulator_cache successfully!\n");
}

/* Print cache opt result info */
void print_verbose(simulator_cache *sc, cache_opt co, cache_opt_res optres, int flag)
{
    if (sc->verbose) {
        if (flag) {
            printf("%c %x,%d", co.opttype, co.addr, co.size);
        }
        if (optres & HIT) {
            printf(" hit");
        }
        if (optres & MISS) {
            printf(" miss");
        }
        if (optres & EVICTION) {
            printf(" eviction");
        }
    }
}

int main(int argc, char *argv[])
{
    simulator_cache sc;
    parse_cache_args(argc, argv, &sc);
    // test_mask(&sc);
    handle_cache_stuff(&sc);
    printSummary(sc.cs.hits, sc.cs.misses, sc.cs.evictions);
    return 0;
}