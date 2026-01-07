#
# Cross Platform Makefile
# Compatible with MSYS2/MINGW, Ubuntu 14.04.1 and Mac OS X
#
# You will need GLFW (http://www.glfw.org):
# Linux:
#   apt-get install libglfw-dev
# Mac OS X:
#   brew install glfw
# MSYS2:
#   pacman -S --noconfirm --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-glfw
#

#CXX = g++
#CXX = clang++
CC = gcc

EXE = example_vgm_player
IMGUI_DIR = ../..
LIBVGM_DIR = ../../libvgm-player
MODIZER_VIZ_DIR = modizer_viz

LIBVGM_OBJS = \
    $(LIBVGM_DIR)/obj/player/helper.o \
    $(LIBVGM_DIR)/obj/utils/DataLoader.o \
    $(LIBVGM_DIR)/obj/utils/FileLoader.o \
    $(LIBVGM_DIR)/obj/utils/MemoryLoader.o \
    $(LIBVGM_DIR)/obj/utils/StrUtils-CPConv_IConv.o \
    $(LIBVGM_DIR)/obj/player/playerbase.o \
    $(LIBVGM_DIR)/obj/player/playera.o \
    $(LIBVGM_DIR)/obj/player/s98player.o \
    $(LIBVGM_DIR)/obj/player/droplayer.o \
    $(LIBVGM_DIR)/obj/player/gymplayer.o \
    $(LIBVGM_DIR)/obj/player/vgmplayer.o \
    $(LIBVGM_DIR)/obj/player/vgmplayer_cmdhandler.o \
    $(LIBVGM_DIR)/obj/player/dblk_compr.o \
    $(LIBVGM_DIR)/obj/utils/OSMutex_Win.o \
    $(LIBVGM_DIR)/obj/utils/OSSignal_Win.o \
    $(LIBVGM_DIR)/obj/utils/OSThread_Win.o

OBJS = \
    main.o \
    vgm_parser.o \
    modizer_viz.o \
    imgui.o \
    imgui_demo.o \
    imgui_draw.o \
    imgui_tables.o \
    imgui_widgets.o \
    imgui_impl_glfw.o \
    imgui_impl_opengl3.o

MODIZER_OBJS = \
    fmopn.o \
    ym2151.o

UNAME_S := $(shell uname -s)
LINUX_GL_LIBS = -lGL

CXXFLAGS = -std=c++11 -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -I$(LIBVGM_DIR) -I$(LIBVGM_DIR)/player -I$(LIBVGM_DIR)/audio -I$(LIBVGM_DIR)/emu -I$(LIBVGM_DIR)/utils -I$(MODIZER_VIZ_DIR) -DAUDDRV_DSOUND
CFLAGS = -I$(LIBVGM_DIR) -I$(LIBVGM_DIR)/player -I$(LIBVGM_DIR)/audio -I$(LIBVGM_DIR)/emu -I$(LIBVGM_DIR)/utils
CXXFLAGS += -g -Wall -Wformat
CFLAGS += -g -Wall -Wformat

MODIZER_CFLAGS = -I$(MODIZER_VIZ_DIR) -I$(MODIZER_VIZ_DIR)/libvgm -I$(MODIZER_VIZ_DIR)/libvgm/emu -D__CST_STDBOOL_H__
MODIZER_CFLAGS += -g -Wall -Wformat
LIBS = -lglfw3 -lgdi32 -lopengl32 -limm32 -ldsound

##---------------------------------------------------------------------
## BUILD FLAGS PER PLATFORM
##---------------------------------------------------------------------

ifeq ($(UNAME_S), Linux) #LINUX
	ECHO_MESSAGE = "Linux"
	LIBS += $(LINUX_GL_LIBS) `pkg-config --static --libs glfw3`
	CXXFLAGS += `pkg-config --cflags glfw3`
	CFLAGS = $(CXXFLAGS)
endif

ifeq ($(UNAME_S), Darwin) #APPLE
	ECHO_MESSAGE = "Mac OS X"
	LIBS += -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
	LIBS += -L/usr/local/lib -L/opt/local/lib -L/opt/homebrew/lib
	LIBS += -lglfw
	CXXFLAGS += -I/usr/local/include -I/opt/local/include -I/opt/homebrew/include
	CFLAGS = $(CXXFLAGS)
endif

ifeq ($(OS), Windows_NT)
	ECHO_MESSAGE = "MinGW"
	LIBS += -lglfw3 -lgdi32 -lopengl32 -limm32
	CXXFLAGS += `pkg-config --cflags glfw3`
	CFLAGS = $(CXXFLAGS)
endif

##---------------------------------------------------------------------
## BUILD RULES
##---------------------------------------------------------------------

all: $(EXE)

$(EXE): $(OBJS) $(MODIZER_OBJS)
	$(CXX) -o $@ $^ $(LIBVGM_OBJS) $(CXXFLAGS) $(LIBS) -lcomdlg32 -L$(LIBVGM_DIR)/obj -laudio -lemu -lz -lwinmm -ldsound -lole32 -liconv -luuid

main.o: main.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

vgm_parser.o: vgm_parser.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

modizer_viz.o: $(MODIZER_VIZ_DIR)/modizer_viz.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

fmopn.o: $(MODIZER_VIZ_DIR)/libvgm/emu/cores/fmopn.c
	$(CC) $(MODIZER_CFLAGS) -c -o $@ $<

ym2151.o: $(MODIZER_VIZ_DIR)/libvgm/emu/cores/ym2151.c
	$(CC) $(MODIZER_CFLAGS) -c -o $@ $<

imgui.o: $(IMGUI_DIR)/imgui.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
imgui_demo.o: $(IMGUI_DIR)/imgui_demo.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
imgui_draw.o: $(IMGUI_DIR)/imgui_draw.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
imgui_tables.o: $(IMGUI_DIR)/imgui_tables.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
imgui_widgets.o: $(IMGUI_DIR)/imgui_widgets.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
imgui_impl_glfw.o: $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
imgui_impl_opengl3.o: $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(EXE) $(OBJS)
