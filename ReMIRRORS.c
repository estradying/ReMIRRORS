#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <getopt.h>
#include <pthread.h>
#include <inttypes.h>
#include <stdatomic.h>

#include "cubiomes/generator.h"
#include "cubiomes/biomenoise.h"

typedef struct
{
    double values[12];
    double center;
    double score;
} Points;

static atomic_uint_least64_t next_seed = 0;
static const int order[4] = {0, 2, 1, 3};

static int num_threads = 12;
static int64_t g_end_seed = -1;

static double dx[12];
static double dz[12];
static double dx_b[12];
static double dz_b[12];

static void print_usage(const char *prog)
{
    printf("Usage: %s [ACTION] [OPTIONS]\n", prog);
    printf("\n");
    printf("Searches for Mushroom Island ring patterns in Minecraft seeds.\n");
    printf("\n");
    printf("Actions:\n");
    printf("  search    Search for seeds (default)\n");
    printf("  help      Show this help message\n");
    printf("\n");
    printf("Options:\n");
    printf("  -t, --threads <N>       Number of worker threads (default: 12)\n");
    printf("  -s, --start-seed <N>    Starting seed (default: random)\n");
    printf("  -e, --end-seed <N>      End seed for a finite search range\n");
    printf("  -h, --help              Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s                          Search from a random seed with 12 threads\n", prog);
    printf("  %s search -t 8              Search with 8 threads\n", prog);
    printf("  %s search -s 12345          Start searching from seed 12345\n", prog);
    printf("  %s search -s 0 -e 1000000   Search seeds 0 through 1000000\n", prog);
    printf("  %s search -t 4 -s 99 -e 999  Search with 4 threads, seeds 99 through 999\n", prog);
}

static void print_separator(void)
{
    for (int i = 0; i < 64; i++)
    {
        printf("-");
    }

    printf("\n");
}

static void print_result(uint64_t seed, int x, int z)
{
    int used = printf("[Seed: %" PRId64 "]", (int64_t)seed);
    int length = snprintf(NULL, 0, "[X: %d Z: %d]", x, z);

    printf("%*s[X: %d Z: %d]\n", 64 - used - length, " ", x, z);

    print_separator();
}

static void map(Generator *g, int ref_x, int ref_z)
{
    for (int z = ref_z - 240; z < ref_z + 240; z += 16)
    {
        printf("[ ");

        for (int x = ref_x - 240; x < ref_x + 240; x += 16)
        {
            double c = sampleClimatePara(&g->bn, NULL, x, z);

            if (c > -0.19)
            {
                printf("&&");
            }
            else if (c > -0.455)
            {
                printf("::");
            }
            else if (c > -1.05)
            {
                printf("  ");
            }
            else
            {
                printf("@@");
            }
        }

        printf(" ]\n");
    }

    print_separator();
}

static int surrounded(Generator *g, int ref_x, int ref_z)
{
    int visited[128][128] = {0};
    int stack_x[16384];
    int stack_z[16384];
    int sp = 0;

    ref_x /= 4;
    ref_z /= 4;

    stack_x[sp] = ref_x;
    stack_z[sp] = ref_z;
    sp++;

    while (sp > 0)
    {
        sp--;

        int sx = stack_x[sp];
        int sz = stack_z[sp];

        int vx = sx - ref_x + 64;
        int vz = sz - ref_z + 64;

        if (vx < 0 || vz < 0 || vx >= 128 || vz >= 128)
        {
            return 0;
        }

        if (visited[vz][vx])
        {
            continue;
        }

        visited[vz][vx] = 1;

        if (sampleClimatePara(&g->bn, NULL, sx * 4, sz * 4) < -1.05)
        {
            continue;
        }

        stack_x[sp] = sx + 1;
        stack_z[sp] = sz;
        sp++;

        stack_x[sp] = sx - 1;
        stack_z[sp] = sz;
        sp++;

        stack_x[sp] = sx;
        stack_z[sp] = sz + 1;
        sp++;

        stack_x[sp] = sx;
        stack_z[sp] = sz - 1;
        sp++;
    }

    return 1;
}

static int ring_a(OctaveNoise *noise, double factor, int ref_x, int ref_z, Points *p)
{
    double center = p->center;

    for (int i = 0; i < 12; i++)
    {
        double c = sampleOctave(noise, ref_x + dx[i], 0, ref_z + dz[i]) * factor;

        p->values[i] = c;
        p->score += c - center;
    }

    return 1;
}

static int ring_b(OctaveNoise *noise, double factor, int ref_x, int ref_z, Points *p)
{
    double cx = ref_x * (337.0 / 331.0);
    double cz = ref_z * (337.0 / 331.0);

    double center = sampleOctave(noise, cx, 0, cz) * factor;

    if (center + p->center < -0.4)
    {
        return 0;
    }

    for (int i = 0; i < 12; i++)
    {
        double c = sampleOctave(noise, cx + dx_b[i], 0, cz + dz_b[i]) * factor;

        if (c + p->values[i] > -0.8)
        {
            return 0;
        }
    }

    return 1;
}

static void check(Generator *g, uint64_t seed)
{
    setClimateParaSeed(&g->bn, seed, 0, NP_CONTINENTALNESS, 1);

    for (int x1 = -4096; x1 <= 4096; x1 += 1024)
    {
        for (int z1 = -4096; z1 <= 4096; z1 += 1024)
        {
            if (sampleClimatePara(&g->bn, NULL, x1, z1) > -0.4)
            {
                continue;
            }

            for (int x2 = x1 - 256; x2 <= x1 + 256; x2 += 64)
            {
                for (int z2 = z1 - 256; z2 <= z1 + 256; z2 += 64)
                {
                    if (sampleClimatePara(&g->bn, NULL, x2, z2) > -0.6)
                    {
                        continue;
                    }

                    setClimateParaSeed(&g->bn, seed, 0, NP_CONTINENTALNESS, 8);

                    int nptype = g->bn.nptype;

                    OctaveNoise *oct_a = &g->bn.climate[nptype].octA;
                    OctaveNoise *oct_b = &g->bn.climate[nptype].octB;

                    double factor = g->bn.climate[nptype].amplitude;

                    Points best = {0};
                    int best_x = 0;
                    int best_z = 0;

                    for (int x3 = x2 - 128; x3 <= x2 + 128; x3 += 32)
                    {
                        for (int z3 = z2 - 128; z3 <= z2 + 128; z3 += 32)
                        {
                            double c = sampleOctave(oct_a, x3, 0, z3) * factor;

                            if (c < -0.2)
                            {
                                continue;
                            }

                            Points p = {0};
                            p.center = c;

                            ring_a(oct_a, factor, x3, z3, &p);

                            if (p.score < -6.0 && p.score < best.score)
                            {
                                best = p;
                                best_x = x3;
                                best_z = z3;
                            }
                        }
                    }

                    if (best.score == 0)
                    {
                        return;
                    }

                    for (int x4 = best_x - 7471104; x4 <= best_x + 7471104; x4 += 131072)
                    {
                        for (int z4 = best_z - 7471104; z4 <= best_z + 7471104; z4 += 131072)
                        {
                            if (!ring_b(oct_b, factor, x4, z4, &best))
                            {
                                continue;
                            }

                            setClimateParaSeed(&g->bn, seed, 0, NP_CONTINENTALNESS, -1);

                            double max = -1.0;

                            for (int x5 = x4 - 32; x5 <= x4 + 32; x5++)
                            {
                                for (int z5 = z4 - 32; z5 <= z4 + 32; z5++)
                                {
                                    double c = sampleClimatePara(&g->bn, NULL, x5, z5);

                                    if (c > -0.19 && c > max)
                                    {
                                        max = c;
                                    }
                                }
                            }

                            if (max > -0.19 && surrounded(g, x4, z4))
                            {
                                print_result(seed, x4 * 4, z4 * 4);
                                map(g, x4, z4);
                            }

                            setClimateParaSeed(&g->bn, seed, 0, NP_CONTINENTALNESS, 8);
                        }
                    }

                    return;
                }
            }
        }
    }
}

static void *worker(void *arg)
{
    Generator g;

    setupGenerator(&g, MC_NEWEST, 0);

    for (;;)
    {
        uint64_t start = atomic_fetch_add(&next_seed, 1024);

        if (g_end_seed >= 0 && start > (uint64_t)g_end_seed)
        {
            break;
        }

        uint64_t end = start + 1024;

        if (g_end_seed >= 0 && end > (uint64_t)g_end_seed + 1)
        {
            end = (uint64_t)g_end_seed + 1;
        }

        for (uint64_t seed = start; seed < end; seed++)
        {
            check(&g, seed);
        }
    }

    return NULL;
}

static void precompute_offsets(void)
{
    double pi = acos(-1.0);
    double scaled_radius = 576 / 4.0;

    for (int i = 0; i < 3; i++)
    {
        int base = i * 4;
        int angle_offset = i * 30;

        for (int j = 0; j < 4; j++)
        {
            double radians = (order[j] * 90 + angle_offset) * (pi / 180.0);

            double x = cos(radians) * scaled_radius;
            double z = sin(radians) * scaled_radius;

            dx[base + j] = x;
            dz[base + j] = z;

            dx_b[base + j] = x * (337.0 / 331.0);
            dz_b[base + j] = z * (337.0 / 331.0);
        }
    }
}

int main(int argc, char *argv[])
{
    static const struct option long_opts[] = {
        {"threads",    required_argument, NULL, 't'},
        {"start-seed", required_argument, NULL, 's'},
        {"end-seed",   required_argument, NULL, 'e'},
        {"help",       no_argument,       NULL, 'h'},
        {NULL,         0,                 NULL,  0 }
    };

    /* Handle optional leading action word before option parsing */
    int arg_offset = 1;

    if (argc > 1 && argv[1][0] != '-')
    {
        if (strcmp(argv[1], "help") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[1], "search") == 0)
        {
            arg_offset = 2;
        }
        else
        {
            fprintf(stderr, "Unknown action: %s\n\n", argv[1]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Shift argv so getopt sees only the relevant arguments */
    int opt_argc = argc - arg_offset + 1;
    char **opt_argv = argv + arg_offset - 1;
    opt_argv[0] = argv[0];

    int64_t start_seed = -1;
    int opt;

    while ((opt = getopt_long(opt_argc, opt_argv, "t:s:e:h", long_opts, NULL)) != -1)
    {
        switch (opt)
        {
            case 't':
            {
                char *endptr;
                errno = 0;
                long val = strtol(optarg, &endptr, 10);

                if (errno != 0 || *endptr != '\0' || val < 1 || val > 256)
                {
                    fprintf(stderr, "Error: --threads must be an integer between 1 and 256\n");
                    return 1;
                }

                num_threads = (int)val;
                break;
            }

            case 's':
            {
                char *endptr;
                errno = 0;
                start_seed = strtoll(optarg, &endptr, 10);

                if (errno != 0 || *endptr != '\0')
                {
                    fprintf(stderr, "Error: --start-seed must be a valid integer\n");
                    return 1;
                }

                break;
            }

            case 'e':
            {
                char *endptr;
                errno = 0;
                g_end_seed = strtoll(optarg, &endptr, 10);

                if (errno != 0 || *endptr != '\0')
                {
                    fprintf(stderr, "Error: --end-seed must be a valid integer\n");
                    return 1;
                }

                break;
            }

            case 'h':
                print_usage(argv[0]);
                return 0;

            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    /* Determine starting seed */
    uint64_t seed;

    if (start_seed >= 0)
    {
        seed = (uint64_t)start_seed;
    }
    else
    {
        srand((unsigned int)time(NULL));
        seed = ((uint64_t)(rand() & 0xFFFF)) << 48;
    }

    if (g_end_seed >= 0 && (int64_t)seed > g_end_seed)
    {
        fprintf(stderr, "Error: --start-seed must be less than or equal to --end-seed\n");
        return 1;
    }

    atomic_store(&next_seed, seed);

    print_separator();
    printf("[Searching from seed %" PRId64 " with %d threads]\n",
           (int64_t)seed, num_threads);

    if (g_end_seed >= 0)
    {
        printf("[End seed: %" PRId64 "]\n", g_end_seed);
    }

    print_separator();

    precompute_offsets();

    pthread_t *threads = malloc((size_t)num_threads * sizeof(pthread_t));

    if (!threads)
    {
        fprintf(stderr, "Error: failed to allocate thread array\n");
        return 1;
    }

    for (int i = 0; i < num_threads; i++)
    {
        pthread_create(&threads[i], NULL, worker, NULL);
    }

    for (int i = 0; i < num_threads; i++)
    {
        pthread_join(threads[i], NULL);
    }

    free(threads);

    return 0;
}
