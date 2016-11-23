# Makefile for unix systems
# this requires GNU make

APPNAME=glteardetect

# Use pkg-config to search installed libraries
USE_PKGCONFIG=1

# Compiler flags

# enable all warnings in general
WARNFLAGS= -Wall -Wextra

# optimize flags, only used for RELEASE=1 builds
OPTIMIZEFLAGS = -ffast-math -mtune=native -march=native -O4 -DNDEBUG

ifeq ($(RELEASE), 1)
#
CFLAGS =   $(WARNFLAGS) $(OPTIMIZEFLAGS) -s
else
CFLAGS =   $(WARNFLAGS) -g
endif

# OpenGL Libraries
CPPFLAGS += -Iglad/include -DLINUX
LINK_GL =

# Try to find the system's GLFW3 library via pkg-config
ifeq ($(shell pkg-config --atleast-version=3 glfw3 && echo 1 || echo 0),1)
CPPFLAGS += $(shell pkg-config --cflags glfw3)
LINK_GL += $(shell pkg-config --static --libs glfw3)
else
$(error GLFW3 library not found via pkg-config, please install it)
endif

# Try to find the systems GL library via pkg-config
ifeq ($(shell pkg-config --exists gl && echo 1 || echo 0),1)
CPPFLAGS += $(shell pkg-config --cflags gl)
LINK_GL += $(shell pkg-config --libs gl) 
else
$(error OpenGL library not found via pkg-config, please install it)
endif

# all needed libraries
LINK = $(LINK_GL) -lX11 -lm -lrt -ldl

# Files

CFILES=glad/src/glad.c \
       glad/src/glad_glx.c \
       teardetect.c

INCFILES=$(wildcard *.h)
SRCFILES=$(CFILES)
OBJECTS =$(patsubst %.c,%.o,$(CFILES))
PRJFILES=Makefile


# build rules
.PHONY: all
all:	$(APPNAME) 

# build and start with "make run"
.PHONY: run
run:	$(APPNAME)
	./$(APPNAME)


# automatic dependency generation
# create $(DEPDIR) (and an empty file dir)
# create a .d file for every .c/.cpp source file which contains
# 		   all dependencies for that file
.PHONY: depend
depend:	$(DEPDIR)/dependencies
DEPDIR   = ./dep
DEPFILES = $(patsubst %.c,$(DEPDIR)/%.d,$(CFILES))
$(DEPDIR)/dependencies: $(DEPDIR)/dir $(DEPFILES)
	@cat $(DEPFILES) > $(DEPDIR)/dependencies
$(DEPDIR)/dir:
	@mkdir -p $(DEPDIR)
	@mkdir -p $(DEPDIR)/glad/src
	@touch $(DEPDIR)/dir
$(DEPDIR)/%.d: %.c $(DEPDIR)/dir
	@echo rebuilding dependencies for $*
	@set -e; $(CC) -M $(CPPFLAGS) $<	\
	| sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' \
	> $@; [ -s $@ ] || rm -f $@
$(DEPDIR)/%.d: %.cpp $(DEPDIR)/dir
	@echo rebuilding dependencies for $*
	@set -e; $(CXX) -M $(CPPFLAGS) $<	\
	| sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' \
	> $@; [ -s $@ ] || rm -f $@
-include $(DEPDIR)/dependencies

# rules to build applications
$(APPNAME): $(OBJECTS) $(DEPDIR)/dependencies
	$(CC) $(CFLAGS) $(OBJECTS) $(LDFLAGS) $(LINK) -o$(APPNAME)

# remove all unneeded files
.PHONY: clean
clean:
	@echo removing binaries: $(APPNAME)
	@rm -f $(APPNAME)
	@echo removing object files: $(OBJECTS)
	@rm -f $(OBJECTS)
	@echo removing dependency files
	@rm -rf $(DEPDIR)
	@echo removing tags
	@rm -f tags

# update the tags file
.PHONY: TAGS
TAGS:	tags

tags:	$(SRCFILES) $(INCFILES) 
	ctags $(SRCFILES) $(INCFILES)

# look for 'TODO' in all relevant files
.PHONY: todo
todo:
	-egrep -in "TODO" $(SRCFILES) $(INCFILES)

