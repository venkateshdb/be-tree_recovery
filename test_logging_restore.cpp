// This test program performs a series of inserts, deletes, updates,
// and queries to a betree.  It performs the same sequence of
// operatons on a std::map.  It checks that it always gets the same
// result from both data structures.

// The program takes 1 command-line parameter -- the number of
// distinct keys it can use in the test.

// The values in this test are strings.  Since updates use operator+
// on the values, this test performs concatenation on the strings.

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

// INCLUDE YOUR LOGGING FILE HERE
#include "betree.hpp"

#include "logger.hpp"

#include "recovery.hpp"

void timer_start(uint64_t &timer) {
    struct timeval t;
    assert(!gettimeofday(&t, NULL));
    timer -= 1000000 * t.tv_sec + t.tv_usec;
}

void timer_stop(uint64_t &timer) {
    struct timeval t;
    assert(!gettimeofday(&t, NULL));
    timer += 1000000 * t.tv_sec + t.tv_usec;
}

int next_command(FILE *input, int *op, uint64_t *arg) {
    int ret;
    char command[64];

    ret = fscanf(input, "%s %ld", command, arg);
    if (ret == EOF)
        return EOF;
    else if (ret != 2) {
        fprintf(stderr, "Parse error\n");
        exit(3);
    }

    if (strcmp(command, "Inserting") == 0) {
        *op = 0;
    } else if (strcmp(command, "Updating") == 0) {
        *op = 1;
    } else if (strcmp(command, "Deleting") == 0) {
        *op = 2;
    } else if (strcmp(command, "Query") == 0) {
        *op = 3;
        if (1 != fscanf(input, " -> %s", command)) {
            fprintf(stderr, "Parse error\n");
            exit(3);
        }
    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        exit(1);
    }

    return 0;
}

#define DEFAULT_TEST_MAX_NODE_SIZE (1ULL << 6)
#define DEFAULT_TEST_MIN_FLUSH_SIZE (DEFAULT_TEST_MAX_NODE_SIZE / 4)
#define DEFAULT_TEST_CACHE_SIZE (4)
#define DEFAULT_TEST_NDISTINCT_KEYS (1ULL << 10)
#define DEFAULT_TEST_NOPS (1ULL << 12)

void usage(char *name) {
    std::cout
        << "Usage: " << name << " [OPTIONS]" << std::endl
        << "Tests the betree implementation" << std::endl
        << std::endl
        << "Options are" << std::endl
        << "  Required:" << std::endl
        << "    -d <backing_store_directory>                    [ default: "
           "none, parameter is required ]"
        << std::endl
        << "    -m  <mode>  (test or benchmark-<mode>)          [ default: "
           "none, parameter required ]"
        << std::endl
        << "        benchmark modes:" << std::endl
        << "          upserts    " << std::endl
        << "          queries    " << std::endl
        << "  Betree tuning parameters:" << std::endl
        << "    -N <max_node_size>            (in elements)     [ default: "
        << DEFAULT_TEST_MAX_NODE_SIZE << " ]" << std::endl
        << "    -f <min_flush_size>           (in elements)     [ default: "
        << DEFAULT_TEST_MIN_FLUSH_SIZE << " ]" << std::endl
        << "    -C <max_cache_size>           (in betree nodes) [ default: "
        << DEFAULT_TEST_CACHE_SIZE << " ]" << std::endl
        << "  Options for both tests and benchmarks" << std::endl
        << "    -k <number_of_distinct_keys>                    [ default: "
        << DEFAULT_TEST_NDISTINCT_KEYS << " ]" << std::endl
        << "    -t <number_of_operations>                       [ default: "
        << DEFAULT_TEST_NOPS << " ]" << std::endl
        << "    -s <random_seed>                                [ default: "
           "random ]"
        << std::endl
        << "  Test scripting options" << std::endl
        << "    -o <output_script>                              [ default: no "
           "output ]"
        << std::endl
        << "    -i <script_file>                                [ default: "
           "none ]"
        << std::endl
        << "  ====REQUIRED PARAMETERS FOR PROJECT 2====" << std::endl
        << "    -p <persistence_granularity>  (an integer)" << std::endl
        << "    -c <checkpoint_granularity>   (an integer)" << std::endl;
}

int test(betree<uint64_t, std::string> &b, uint64_t nops,
         uint64_t number_of_distinct_keys, FILE *script_input,
         FILE *script_output) {
    for (unsigned int i = 0; i < nops; i++) {

        printf("%u/%lu\n", i, nops);
        int op;
        uint64_t t;
        if (script_input) {
            int r = next_command(script_input, &op, &t);
            if (r == EOF)
                exit(0);
            else if (r < 0)
                exit(4);
        } else {
            op = rand() % 4;
            t = rand() % number_of_distinct_keys;
        }

        switch (op) {
            case 0:  // insert
                if (script_output){
                    //printf("Printing insert op!\n");
                    fprintf(script_output, "Inserting %lu\n", t);
                } 
                b.insert(t, std::to_string(t) + ":");
                break;
            case 1:  // update
                if (script_output) fprintf(script_output, "Updating %lu\n", t);
                b.update(t, std::to_string(t) + ":");
                break;
            case 2:  // delete
                if (script_output) fprintf(script_output, "Deleting %lu\n", t);
                b.erase(t);
                break;
            case 3:  // query
                try {
                    std::string bval = b.query(t);
                    if (script_output)
                        fprintf(script_output, "Query %lu -> %s\n", t,
                                bval.c_str());
                } catch (std::out_of_range & e) {
                    if (script_output)
                        fprintf(script_output, "Query %lu -> DNE\n", t);
                }
                break;
            default:
                abort();
        }
    }

    std::cout << "Test PASSED" << std::endl;

    return 0;
}

void benchmark_upserts(betree<uint64_t, std::string> &b, uint64_t nops,
                       uint64_t number_of_distinct_keys, uint64_t random_seed) {
    uint64_t overall_timer = 0;
    for (uint64_t j = 0; j < 100; j++) {
        uint64_t timer = 0;
        timer_start(timer);
        for (uint64_t i = 0; i < nops / 100; i++) {
            uint64_t t = rand() % number_of_distinct_keys;
            b.update(t, std::to_string(t) + ":");
        }
        timer_stop(timer);
        printf("%ld %ld %ld\n", j, nops / 100, timer);
        overall_timer += timer;
    }
    printf("# overall: %ld %ld\n", 100 * (nops / 100), overall_timer);
}

void benchmark_queries(betree<uint64_t, std::string> &b, uint64_t nops,
                       uint64_t number_of_distinct_keys, uint64_t random_seed) {
    // Pre-load the tree with data
    srand(random_seed);
    for (uint64_t i = 0; i < nops; i++) {
        uint64_t t = rand() % number_of_distinct_keys;
        b.update(t, std::to_string(t) + ":");
    }

    // Now go back and query it
    srand(random_seed);
    uint64_t overall_timer = 0;
    timer_start(overall_timer);
    for (uint64_t i = 0; i < nops; i++) {
        uint64_t t = rand() % number_of_distinct_keys;
        b.query(t);
    }
    timer_stop(overall_timer);
    printf("# overall: %ld %ld\n", nops, overall_timer);
}

int main(int argc, char **argv) {
    char *mode = NULL;
    uint64_t max_node_size = DEFAULT_TEST_MAX_NODE_SIZE;
    uint64_t min_flush_size = DEFAULT_TEST_MIN_FLUSH_SIZE;
    uint64_t cache_size = DEFAULT_TEST_CACHE_SIZE;
    char *backing_store_dir = NULL;
    uint64_t number_of_distinct_keys = DEFAULT_TEST_NDISTINCT_KEYS;
    uint64_t nops = DEFAULT_TEST_NOPS;
    char *script_infile = NULL;
    char *script_outfile = NULL;
    unsigned int random_seed = time(NULL) * getpid();

    // REQUIRED PARAMETERS FOR PERSISTENCE AND CHECKPOINTING GRANULARITY
    uint64_t persistence_granularity = UINT64_MAX;
    uint64_t checkpoint_granularity = UINT64_MAX;

    int opt;
    char *term;

    //////////////////////
    // Argument parsing //
    //////////////////////

    while ((opt = getopt(argc, argv, "m:d:N:f:C:o:k:t:s:i:p:c:")) != -1) {
        switch (opt) {
            case 'm':
                mode = optarg;
                break;
            case 'd':
                backing_store_dir = optarg;
                break;
            case 'N':
                max_node_size = strtoull(optarg, &term, 10);
                if (*term) {
                    std::cerr << "Argument to -N must be an integer"
                              << std::endl;
                    usage(argv[0]);
                    exit(1);
                }
                break;
            case 'f':
                min_flush_size = strtoull(optarg, &term, 10);
                if (*term) {
                    std::cerr << "Argument to -f must be an integer"
                              << std::endl;
                    usage(argv[0]);
                    exit(1);
                }
                break;
            case 'C':
                cache_size = strtoull(optarg, &term, 10);
                if (*term) {
                    std::cerr << "Argument to -C must be an integer"
                              << std::endl;
                    usage(argv[0]);
                    exit(1);
                }
                break;
            case 'o':
                script_outfile = optarg;
                break;
            case 'k':
                number_of_distinct_keys = strtoull(optarg, &term, 10);
                if (*term) {
                    std::cerr << "Argument to -k must be an integer"
                              << std::endl;
                    usage(argv[0]);
                    exit(1);
                }
                break;
            case 't':
                nops = strtoull(optarg, &term, 10);
                if (*term) {
                    std::cerr << "Argument to -t must be an integer"
                              << std::endl;
                    usage(argv[0]);
                    exit(1);
                }
                break;
            case 's':
                random_seed = strtoull(optarg, &term, 10);
                if (*term) {
                    std::cerr << "Argument to -s must be an integer"
                              << std::endl;
                    usage(argv[0]);
                    exit(1);
                }
                break;
            case 'i':
                script_infile = optarg;
                break;
            case 'p':
                // persistence granularity
                persistence_granularity = strtoull(optarg, &term, 10);
                if (*term) {
                    std::cerr << "Argument to -p must be an integer"
                              << std::endl;
                    usage(argv[0]);
                    exit(1);
                }
                break;
            case 'c':
                // checkpoint granularity
                checkpoint_granularity = strtoull(optarg, &term, 10);
                if (*term) {
                    std::cerr << "Argument to -c must be an integer"
                              << std::endl;
                    usage(argv[0]);
                    exit(1);
                }
                break;
            default:
                std::cerr << "Unknown option '" << (char)opt << "'"
                          << std::endl;
                usage(argv[0]);
                exit(1);
        }
    }

    // CHECK REQUIRED PARAMETERS
    if (persistence_granularity == UINT64_MAX) {
        std::cerr << "ERROR: Persistence granularity was not assigned through "
                     "-p! This is a requirement!";
        usage(argv[0]);
        exit(1);
    }
    if (checkpoint_granularity == UINT64_MAX) {
        std::cerr << "ERROR: Checkpoint granularity was not assigned through "
                     "-c! This is a requirement!";
        usage(argv[0]);
        exit(1);
    }

    FILE *script_input = NULL;
    FILE *script_output = NULL;

    if (mode == NULL ||
        (strcmp(mode, "test") != 0 && strcmp(mode, "benchmark-upserts") != 0 &&
         strcmp(mode, "benchmark-queries") != 0)) {
        std::cerr << "Must specify a mode of \"test\" or \"benchmark\""
                  << std::endl;
        usage(argv[0]);
        exit(1);
    }

    if (strncmp(mode, "benchmark", strlen("benchmark")) == 0) {
        if (script_infile) {
            std::cerr << "Cannot specify an input script in benchmark mode"
                      << std::endl;
            usage(argv[0]);
            exit(1);
        }
        if (script_outfile) {
            std::cerr << "Cannot specify an output script in benchmark mode"
                      << std::endl;
            usage(argv[0]);
            exit(1);
        }
    }

    if (script_infile) {
        script_input = fopen(script_infile, "r");
        if (script_input == NULL) {
            perror("Couldn't open input file");
            exit(1);
        }
    }

    if (script_outfile) {
        script_output = fopen(script_outfile, "w");
        if (script_output == NULL) {
            perror("Couldn't open output file");
            exit(1);
        }
    }

    srand(random_seed);

    if (backing_store_dir == NULL) {
        std::cerr << "-d <backing_store_directory> is required" << std::endl;
        usage(argv[0]);
        exit(1);
    }

    ////////////////////////////////////////////////////////
    // Construct a betree and run the tests or benchmarks //
    ////////////////////////////////////////////////////////

    one_file_per_object_backing_store ofpobs(backing_store_dir);

    //ofpobs.reset_ids();

    swap_space sspace(&ofpobs, cache_size, checkpoint_granularity);

    Logger logger(&ofpobs, persistence_granularity, checkpoint_granularity); // Initialze Logger here

    betree<uint64_t, std::string> b(&sspace, &logger, max_node_size, min_flush_size); // Add Logger pointer in betree constuctor
    
    // Recovery<uint64_t, std::string> recovery(&ofpobs, &sspace, &logger, &b);
    // recovery.recover();
    /**
     * STUDENTS: INITIALIZE YOUR CLASS HERE
     *
     * DON'T FORGET TO MODIFY THE FUNCTION DEFINITION FOR `test` ABOVE TO ACCEPT YOUR NEW
     * CLASS.
     *
     * REMINDER: persistence_granularity AND checkpoint_granularity SHOULD
     *           BE USED IN INITIALIZATION. THESE ARE EXPOSED
     *           THROUGH COMMAND LINE ARGUMENTS FOR TESTING (-p and -c
     * respectively). If your implementation does not use these, please
     *           note why.
     *
     */
    std::cout << "Starting test program..." << std::endl;


    if (strcmp(mode, "test") == 0)
        test(b, nops, number_of_distinct_keys, script_input, script_output);
    else if (strcmp(mode, "benchmark-upserts") == 0) {
        // std::cerr << "benchmark-upserts is not available for this testing program!" << std::endl;
        // return 0;
        benchmark_upserts(b, nops, number_of_distinct_keys, random_seed);
    }
        
    else if (strcmp(mode, "benchmark-queries") == 0) {
        std::cerr << "benchmark-queries is not available for this testing program!" << std::endl;
        return 0;
        // benchmark_queries(b, nops, number_of_distinct_keys, random_seed);
    }
        

    if (script_input) fclose(script_input);

    if (script_output) fclose(script_output);

    return 0;
}

// Test for running a minimal set of operations
// int main(int argc, char **argv) {
//     std::cout << "Starting the minimal test function..." << std::endl << std::flush;

//     // Set up minimal parameters
//     const uint64_t min_granularity = 2;
//     const uint64_t max_node_size = 16;
//     const uint64_t min_flush_size = 4;
    
//     // std::cout << "Initializing directory..." << std::endl << std::flush;
//     one_file_per_object_backing_store ofpobs("/home/u1480041/CS6350Proj3/temp_dir");
//     // std::cout << "Initializing classes..." << std::endl << std::flush;
//     swap_space sspace(&ofpobs, 4);
//     Logger logger(&ofpobs, min_granularity);
//     betree<uint64_t, std::string> b(&sspace, &logger, max_node_size, min_flush_size);
//     // std::cout << "Initializing Logger..." << std::endl << std::flush;
    
//     // std::cout << "Logger Class has been set up..." << std::endl << std::flush;
    
//     // std::cout << "Minimal test: inserting, updating, and querying..." << std::endl;
    
//     b.insert(1, "test_value");
//     b.update(1, "_updated");
//     std::string result = b.query(1);
//     std::cout << "Query result for key 1: " << result << std::endl;

//     return 0;
// }
