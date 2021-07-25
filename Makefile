include psn00bsdk-setup.mk

# Project target name
TARGET		= orgplay

# Searches for C, C++ and S (assembler) files in specified directory
SRCDIR		= src
CFILES		= $(notdir $(wildcard $(SRCDIR)/*.c))
CPPFILES 	= $(notdir $(wildcard $(SRCDIR)/*.cpp))
AFILES		= $(notdir $(wildcard $(SRCDIR)/*.s))

# Create names for object files
OFILES		= $(addprefix build/,$(CFILES:.c=.o)) \
			$(addprefix build/,$(CPPFILES:.cpp=.o)) \
			$(addprefix build/,$(AFILES:.s=.o))

# Project specific include and library directories
# (use -I for include dirs, -L for library dirs)
INCLUDE	 	+=
LIBDIRS		+=

# Libraries to link
LIBS		= -lpsxgpu -lpsxspu -lpsxetc -lpsxapi -lpsxcd -lc

# C compiler flags
CFLAGS		= -g -O2 -fno-builtin -fdata-sections -ffunction-sections

# C++ compiler flags
CPPFLAGS	= $(CFLAGS) -fno-exceptions

# Assembler flags
AFLAGS		= -g

# Linker flags (-Ttext specifies the program text address)
LDFLAGS		= -g -Ttext=0x80010000 -gc-sections \
			-T $(GCC_BASE)/$(PREFIX)/lib/ldscripts/elf32elmip.x

all: $(TARGET).exe

iso: $(TARGET).iso

$(TARGET).iso: $(TARGET).exe
	mkpsxiso -y -q iso.xml

$(TARGET).exe: $(OFILES)
	$(LD) $(LDFLAGS) $(LIBDIRS) $(OFILES) $(LIBS) -o $(TARGET).elf
	elf2x -q $(TARGET).elf

build/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

build/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(AFLAGS) $(INCLUDE) -c $< -o $@

build/%.o: $(SRCDIR)/%.s
	@mkdir -p $(dir $@)
	$(CC) $(AFLAGS) $(INCLUDE) -c $< -o $@

clean:
	rm -rf build $(TARGET).elf $(TARGET).exe

.PHONY: all iso clean
