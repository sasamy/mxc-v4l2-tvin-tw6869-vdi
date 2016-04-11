OBJS = mxc_v4l2_tvin.o
TARGET = mxc_v4l2_tvin.out

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS) -lg2d

all: $(TARGET)

.PHONY: clean
clean:
	rm -f *.o *.out
