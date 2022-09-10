
AM_CFLAGS = -Wall -Werror
AM_CXXFLAGS = -Wall -Werror -std=c++20
LDADD = $(libartnet_LIBS)

bin_PROGRAMS = artnet_openrdm_node $(NCURSES_PROGS)

artnet_openrdm_node_SOURCES = artnet_openrdm_node.cpp openrdm_device.cpp rdm.cpp openrdm.c
