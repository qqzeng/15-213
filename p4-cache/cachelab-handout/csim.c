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
 * I: nop
 * L: miss; hit; miss eviction;
 * S: miss; hit; miss eviction;
 * M: miss, hit; miss eviction; hit, hit
 */

/* cache operation type */
enum {
    TYPE_LOAD,
    TYPE_STORE,
    TYPE_MODIFY
};

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
typedef struct {
    int hits;
    int misses;
    int evictions;
    int verbose;
} cache_stats;


/* simulator cache struct */
typedef struct simulator_cache_st {
    int setcnt;
    int linecnt;
    int blockcnt;
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

/* Print cache opt result info */
void print_verbose(simulator_cache *sc, cache_opt co, cache_opt_res optres);

/******************** custome function declaration end ******************************************/

// Note: returns a pointer to a substring of the original string.
// If the given string was allocated dynamically, the caller must not overwrite
// that pointer with the returned value, since the original pointer must be
// deallocated using the same allocator with which it was allocated.  The return
// value must NOT be deallocated using free() etc.
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
            sc->cs.verbose = 1;
            break;
        case 's':
            sc->setcnt = atoi(optarg);
            argcnt++;
            break;
        case 'E':
            sc->linecnt = atoi(optarg);
            argcnt++;
            break;
        case 'b':
            sc->blockcnt = atoi(optarg);
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
    printf("v=%d, s=%d, E=%d, b=%d, t=%s.\n", sc->cs.verbose, sc->setcnt, sc->linecnt, sc->blockcnt, sc->tracefile);
    return;
}

/* Init simulator cache */
void init_cache_matrix(simulator_cache *sc)
{
    // init cache matirx.
    int i, j;
    i = j = 0;
    sc->sets = (cache_set *) malloc(sc->setcnt * sizeof(cache_set*));
    for (; i < sc->setcnt; i++) {
        sc->sets[i].cls = (cache_line *) malloc(sc->linecnt * sizeof(cache_line*));
        for (; j < sc->linecnt; j++) {
            sc->sets[i].cls[j].valid  = 0;
            sc->sets[i].cls[j].tag    = j;
            sc->sets[i].cls[j].lrunum = LRU_INIT_NUM;
            // sc->sets.cls[j].block = 0;
        }
    }
    // obtain set mask and cache line mask.
    int setmask = 0;
    i = 0;
    while (i < sc->setcnt) {
        setmask = setmask << 1;
        setmask |= setmask;
    }
    setmask = setmask << sc->blockcnt;
    sc->setmask = setmask;
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
        if (0 == strcmp(linestr, " ") || 0 == strcmp(linestr, "")) {break;} // TODO: verify more carefully.
        // printf("%c%c %x,%d\n", inst, opttype, addr, size);
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
        do_load_data(sc, co);
        break;
    case 'S': // do store data opt.
        do_store_data(sc, co);
        break;
    case 'M': // do store data opt.
        do_modify_data(sc, co);
        break;
    default:
        printf("Unreconginzed data operation type %c!\n", co.opttype); // process continue.
        return;
    }
}

/* Do load data task */
void do_load_data(simulator_cache *sc, cache_opt co)
{
    // convert addr to hex.
    
    // locate set
    int setno = co.addr & sc->setmask;
    // match cache line
    int linemask = co.addr >> (sc->blockcnt + sc->setcnt); // logical shift
    int tag = co.addr & linemask;
    cache_opt_res optres = 0;
    int i = 0;
    int miss = 1;
    for (; i < sc->linecnt; i++) {
        int valid = sc->sets[setno].cls[i].valid; // index check?
        int tagbit = sc->sets[setno].cls[i].tag;
        if (valid &&  tag == tagbit) {
            miss = 0;
            sc->cs.hits++;
            optres |= HIT;
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
        // sc->sets[setno].cls[lineno].valid = 1;
        optres |= MISS;
        // read data from RAM...
        // update cache data.
        if(update_cache(sc, setno, tag)){
            sc->cs.evictions++;
            optres |= EVICTION;
        }
    }
    // print verbose if enable.
    print_verbose(sc, co, optres);
    return;
}

/* Do store data task */
void do_store_data(simulator_cache *sc, cache_opt co)
{
    do_load_data(sc, co);
}

/* Do modify data task */
void do_modify_data(simulator_cache *sc, cache_opt co)
{
    
}

/* update cache data struct */
int update_cache(simulator_cache *sc, int setno, int tag) 
{
    int i = 0;
    int evicted = 1;
    for (; i < sc->linecnt; i++) {
        if (sc->sets[setno].cls[i].valid == 0) {
            evicted = 0;
            break;
        }
    }
    if (!evicted) {
        sc->sets[setno].cls[i].valid = 1;
        sc->sets[setno].cls[i].tag = tag;
        // update cache record.
        sc->sets[setno].cls[i].lrunum = LRU_INIT_NUM;
        for (int j = 0; j < sc->linecnt; j++) {
            if (j != i) {
                sc->sets[setno].cls[j].lrunum--;
            }
        }
    } else {
        int evindex = search_lru_cache_line(sc, setno);
        sc->sets[setno].cls[evindex].valid = 1;
        sc->sets[setno].cls[evindex].tag = tag;
         // update cache record.
        sc->sets[setno].cls[evindex].lrunum = LRU_INIT_NUM;
        for (int j = 0; j < sc->linecnt; j++) {
            if (j != evindex) {
                sc->sets[setno].cls[j].lrunum--;
            }
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
    printf("Free simulator_cache successfully!\n");
}

/* Print cache opt result info */
void print_verbose(simulator_cache *sc, cache_opt co, cache_opt_res optres)
{
    if (sc->verbose) {
        printf("%c %x,%d", co.opttype, co.addr, co.size);
        if (optres & HIT) {
            printf(" hit");
        }
        if (optres & MISS) {
            printf(" miss");
        }
        if (optres & EVICTION) {
            printf(" eviction");
        }
        printf("\n");
    }
}

int main(int argc, char *argv[])
{
    simulator_cache sc;
    parse_cache_args(argc, argv, &sc);
    handle_cache_stuff(&sc);
    printSummary(0, 0, 0);
    return 0;
}