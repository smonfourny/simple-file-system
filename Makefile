CFLAGS = -c -g -Wall -std=gnu99 `pkg-config fuse --cflags --libs`
# CFLAGS = -c -g -Wall -std=gnu99 -fsanitize=address `pkg-config fuse --cflags --libs`

LDFLAGS = `pkg-config fuse --cflags --libs`
# LDFLAGS = -fsanitize=address `pkg-config fuse --cflags --libs`


# Uncomment on of the following three lines to compile
# SOURCES= disk_emu.c sfs_api.c sfs_test.c sfs_api.h
# SOURCES= disk_emu.c sfs_api.c sfs_test2.c sfs_api.h
SOURCES= disk_emu.c sfs_api.c fuse_wrappers.c sfs_api.h

#if you wish to create your own test - you can do it using this
# SOURCES= disk_emu.c sfs_api.c test.c sfs_api.h


OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE= 260692380_MONFOURNYDAIGNEAULT_SANDRINE

all: $(SOURCES) $(HEADERS) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	gcc $(OBJECTS) $(LDFLAGS) -o $@

.c.o:
	gcc $(CFLAGS) $< -o $@

clean:
	rm -rf *.o *~ $(EXECUTABLE)
