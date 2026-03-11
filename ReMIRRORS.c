#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include "cubiomes/generator.h"
#include "cubiomes/biomenoise.h"
#include "ringchecker_step.h"

#define THREADS 12
#define BATCH_SIZE 1024
#define B_SCALE (337.0 / 331.0)
#define RING_RADIUS 576
#define ALLOW_BROKEN_RINGS 0
#define WRITE_TO_FILE 0
#define FILE_PATH "ReMIRRORS-Output.txt"

#define COARSE_RADIUS 4096
#define COARSE_STEP 1024
#define MID_RADIUS 256
#define MID_STEP 64
#define FINE_RADIUS 128
#define FINE_STEP 32
#define ISLAND_SEARCH_RADIUS 32
#define LATTICE_RADIUS 7471104
#define LATTICE_STEP 131072

static FILE *f = NULL;
static atomic_long next_seed = 0;

typedef struct
{
    double values[12];
    double center;
    double score;
} Points;

static const int order[4] =
    {
        0, 2, 1, 3};

static double dx[12];
static double dz[12];
static double dx_b[12];
static double dz_b[12];

static void print_separator()
{
    for (int i = 0; i < 64; i++)
    {
        printf("-");
    }

    printf("\n");
}

static void print_result(long seed, int x, int z)
{
    int used = printf("[Seed: %ld]", seed);
    int length = snprintf(NULL, 0, "[X: %d Z: %d]", x, z);

    printf("%*s[X: %d Z: %d]\n", 64 - used - length, " ", x, z);
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
    double cx = ref_x * B_SCALE;
    double cz = ref_z * B_SCALE;

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

static void check(Generator *g, long seed)
{
    setClimateParaSeed(&g->bn, seed, 0, NP_CONTINENTALNESS, 1);

    for (int x1 = -COARSE_RADIUS; x1 <= COARSE_RADIUS; x1 += COARSE_STEP)
    {
        for (int z1 = -COARSE_RADIUS; z1 <= COARSE_RADIUS; z1 += COARSE_STEP)
        {
            if (sampleClimatePara(&g->bn, NULL, x1, z1) > -0.4)
            {
                continue;
            }

            for (int x2 = x1 - MID_RADIUS; x2 <= x1 + MID_RADIUS; x2 += MID_STEP)
            {
                for (int z2 = z1 - MID_RADIUS; z2 <= z1 + MID_RADIUS; z2 += MID_STEP)
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

                    for (int x3 = x2 - FINE_RADIUS; x3 <= x2 + FINE_RADIUS; x3 += FINE_STEP)
                    {
                        for (int z3 = z2 - FINE_RADIUS; z3 <= z2 + FINE_RADIUS; z3 += FINE_STEP)
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

                    for (int x4 = best_x - LATTICE_RADIUS; x4 <= best_x + LATTICE_RADIUS; x4 += LATTICE_STEP)
                    {
                        for (int z4 = best_z - LATTICE_RADIUS; z4 <= best_z + LATTICE_RADIUS; z4 += LATTICE_STEP)
                        {
                            if (!ring_b(oct_b, factor, x4, z4, &best))
                            {
                                continue;
                            }

                            setClimateParaSeed(&g->bn, seed, 0, NP_CONTINENTALNESS, -1);

                            double max = -1.0;

                            for (int x5 = x4 - ISLAND_SEARCH_RADIUS; x5 <= x4 + ISLAND_SEARCH_RADIUS; x5++)
                            {
                                for (int z5 = z4 - ISLAND_SEARCH_RADIUS; z5 <= z4 + ISLAND_SEARCH_RADIUS; z5++)
                                {
                                    double c = sampleClimatePara(&g->bn, NULL, x5, z5);

                                    if (c > -0.19 && c > max)
                                    {
                                        max = c;
                                    }
                                }
                            }

                            if (max > -0.19)
                            {
                                if (ALLOW_BROKEN_RINGS || is_surrounded(&g->bn, x4 * 4, z4 * 4, 16, -1.05))
                                {
                                    if (WRITE_TO_FILE)
                                    {
                                        fprintf(f, "%ld;%d;%d\n", seed, x4 * 4, z4 * 4);
                                        fflush(f);
                                    }
                                    else
                                    {
                                        print_result(seed, x4 * 4, z4 * 4);
                                        print_separator();
                                        map(g, x4, z4);
                                        print_separator();
                                    }
                                }
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
        long start = atomic_fetch_add(&next_seed, BATCH_SIZE);
        long end = start + BATCH_SIZE;

        for (long seed = start; seed < end; seed++)
        {
            check(&g, seed);
        }
    }

    return NULL;
}

static void precompute_offsets()
{
    double pi = acos(-1.0);
    double scaled_radius = RING_RADIUS / 4.0;

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

            dx_b[base + j] = x * B_SCALE;
            dz_b[base + j] = z * B_SCALE;
        }
    }

    print_separator();
    printf("[Precomputed ring point offsets for radius %d]\n", RING_RADIUS);
    print_separator();

    int length = snprintf(NULL, 0, "[X: %6.1f Z: %6.1f]", 0.0, 0.0);

    for (int i = 0; i < 12; i++)
    {
        double x = dx[i] * 4.0;
        double z = dz[i] * 4.0;

        printf("[%2d]", i + 1);
        printf("%*s[X: %6.1f Z: %6.1f]\n", 60 - length, " ", x, z);
    }

    print_separator();
}

int main(void)
{
    if (WRITE_TO_FILE)
    {
        f = fopen(FILE_PATH, "a");

        if (!f)
        {
            return 1;
        }
    }

    precompute_offsets();

    next_seed = (long)time(NULL) << 32;

    pthread_t threads[THREADS];

    for (int i = 0; i < THREADS; i++)
    {
        pthread_create(&threads[i], NULL, worker, NULL);
    }

    for (int i = 0; i < THREADS; i++)
    {
        pthread_join(threads[i], NULL);
    }

    if (f)
    {
        fclose(f);
    }

    return 0;
}