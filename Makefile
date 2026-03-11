$TARGET := ReMIRRORS
SRCS   := ReMIRRORS.c ringchecker_step.c
OBJS   := $(SRCS:.c=.o)
DEPS   := $(SRCS:.c=.d)

CC := gcc
CFLAGS := "-O3 -march=native -ffast-math -funroll-loops -flto -fomit-frame-pointer -fstrict-aliasing -pthread -lm -Wall -MMD -MP"

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@

-include $(DEPS)

clean:
	rm -f $(TARGET) $(OBJS) $(DEPS)

.phony: $(TARGET) clean