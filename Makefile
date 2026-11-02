.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment: export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/3ds_rules

TARGET   := jimbos_roulette
CTRULIB  ?= $(DEVKITPRO)/libctru

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH) \
            -I$(CTRULIB)/include -I$(PORTLIBS)/include -D__3DS__ -std=gnu11
LDFLAGS  := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(TARGET).map
LIBDIRS  := -L$(CTRULIB)/lib -L$(PORTLIBS)/lib
LIBS     := -lcitro2d -lcitro3d -lctru -lm

all: $(TARGET).3dsx

$(TARGET).elf: source/main.c
	$(CC) $(CFLAGS) source/main.c $(LDFLAGS) $(LIBDIRS) $(LIBS) -o $@

%.3dsx: %.elf
	3dsxtool $< $@

clean:
	rm -f $(TARGET).elf $(TARGET).3dsx $(TARGET).map
