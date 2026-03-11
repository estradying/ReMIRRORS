TARGET := ReMIRRORS
SRCS   := ReMIRRORS.c ringchecker_step.c \
          cubiomes/biomenoise.c cubiomes/generator.c \
          cubiomes/biomes.c cubiomes/layers.c cubiomes/util.c \
          cubiomes/noise.c
OBJS   := $(SRCS:.c=.o)
DEPS   := $(SRCS:.c=.d)

CC     := gcc
CFLAGS := -O3 -march=native -ffast-math -funroll-loops -flto -fomit-frame-pointer -fstrict-aliasing -pthread -Wall -MMD -MP
LDFLAGS := -pthread -lm -flto

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	rm -f $(TARGET) $(OBJS) $(DEPS)

.PHONY: clean
