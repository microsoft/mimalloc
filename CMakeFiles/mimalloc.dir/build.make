# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.19

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Disable VCS-based implicit rules.
% : %,v


# Disable VCS-based implicit rules.
% : RCS/%


# Disable VCS-based implicit rules.
% : RCS/%,v


# Disable VCS-based implicit rules.
% : SCCS/s.%


# Disable VCS-based implicit rules.
% : s.%


.SUFFIXES: .hpux_make_needs_suffix_list


# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/aprokurov/Projects/mimalloc

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/aprokurov/Projects/mimalloc

# Include any dependencies generated for this target.
include CMakeFiles/mimalloc.dir/depend.make

# Include the progress variables for this target.
include CMakeFiles/mimalloc.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/mimalloc.dir/flags.make

CMakeFiles/mimalloc.dir/src/stats.c.o: CMakeFiles/mimalloc.dir/flags.make
CMakeFiles/mimalloc.dir/src/stats.c.o: src/stats.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/aprokurov/Projects/mimalloc/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object CMakeFiles/mimalloc.dir/src/stats.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/mimalloc.dir/src/stats.c.o -c /home/aprokurov/Projects/mimalloc/src/stats.c

CMakeFiles/mimalloc.dir/src/stats.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mimalloc.dir/src/stats.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/aprokurov/Projects/mimalloc/src/stats.c > CMakeFiles/mimalloc.dir/src/stats.c.i

CMakeFiles/mimalloc.dir/src/stats.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mimalloc.dir/src/stats.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/aprokurov/Projects/mimalloc/src/stats.c -o CMakeFiles/mimalloc.dir/src/stats.c.s

CMakeFiles/mimalloc.dir/src/random.c.o: CMakeFiles/mimalloc.dir/flags.make
CMakeFiles/mimalloc.dir/src/random.c.o: src/random.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/aprokurov/Projects/mimalloc/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Building C object CMakeFiles/mimalloc.dir/src/random.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/mimalloc.dir/src/random.c.o -c /home/aprokurov/Projects/mimalloc/src/random.c

CMakeFiles/mimalloc.dir/src/random.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mimalloc.dir/src/random.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/aprokurov/Projects/mimalloc/src/random.c > CMakeFiles/mimalloc.dir/src/random.c.i

CMakeFiles/mimalloc.dir/src/random.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mimalloc.dir/src/random.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/aprokurov/Projects/mimalloc/src/random.c -o CMakeFiles/mimalloc.dir/src/random.c.s

CMakeFiles/mimalloc.dir/src/os.c.o: CMakeFiles/mimalloc.dir/flags.make
CMakeFiles/mimalloc.dir/src/os.c.o: src/os.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/aprokurov/Projects/mimalloc/CMakeFiles --progress-num=$(CMAKE_PROGRESS_3) "Building C object CMakeFiles/mimalloc.dir/src/os.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/mimalloc.dir/src/os.c.o -c /home/aprokurov/Projects/mimalloc/src/os.c

CMakeFiles/mimalloc.dir/src/os.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mimalloc.dir/src/os.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/aprokurov/Projects/mimalloc/src/os.c > CMakeFiles/mimalloc.dir/src/os.c.i

CMakeFiles/mimalloc.dir/src/os.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mimalloc.dir/src/os.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/aprokurov/Projects/mimalloc/src/os.c -o CMakeFiles/mimalloc.dir/src/os.c.s

CMakeFiles/mimalloc.dir/src/bitmap.c.o: CMakeFiles/mimalloc.dir/flags.make
CMakeFiles/mimalloc.dir/src/bitmap.c.o: src/bitmap.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/aprokurov/Projects/mimalloc/CMakeFiles --progress-num=$(CMAKE_PROGRESS_4) "Building C object CMakeFiles/mimalloc.dir/src/bitmap.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/mimalloc.dir/src/bitmap.c.o -c /home/aprokurov/Projects/mimalloc/src/bitmap.c

CMakeFiles/mimalloc.dir/src/bitmap.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mimalloc.dir/src/bitmap.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/aprokurov/Projects/mimalloc/src/bitmap.c > CMakeFiles/mimalloc.dir/src/bitmap.c.i

CMakeFiles/mimalloc.dir/src/bitmap.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mimalloc.dir/src/bitmap.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/aprokurov/Projects/mimalloc/src/bitmap.c -o CMakeFiles/mimalloc.dir/src/bitmap.c.s

CMakeFiles/mimalloc.dir/src/arena.c.o: CMakeFiles/mimalloc.dir/flags.make
CMakeFiles/mimalloc.dir/src/arena.c.o: src/arena.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/aprokurov/Projects/mimalloc/CMakeFiles --progress-num=$(CMAKE_PROGRESS_5) "Building C object CMakeFiles/mimalloc.dir/src/arena.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/mimalloc.dir/src/arena.c.o -c /home/aprokurov/Projects/mimalloc/src/arena.c

CMakeFiles/mimalloc.dir/src/arena.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mimalloc.dir/src/arena.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/aprokurov/Projects/mimalloc/src/arena.c > CMakeFiles/mimalloc.dir/src/arena.c.i

CMakeFiles/mimalloc.dir/src/arena.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mimalloc.dir/src/arena.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/aprokurov/Projects/mimalloc/src/arena.c -o CMakeFiles/mimalloc.dir/src/arena.c.s

CMakeFiles/mimalloc.dir/src/region.c.o: CMakeFiles/mimalloc.dir/flags.make
CMakeFiles/mimalloc.dir/src/region.c.o: src/region.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/aprokurov/Projects/mimalloc/CMakeFiles --progress-num=$(CMAKE_PROGRESS_6) "Building C object CMakeFiles/mimalloc.dir/src/region.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/mimalloc.dir/src/region.c.o -c /home/aprokurov/Projects/mimalloc/src/region.c

CMakeFiles/mimalloc.dir/src/region.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mimalloc.dir/src/region.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/aprokurov/Projects/mimalloc/src/region.c > CMakeFiles/mimalloc.dir/src/region.c.i

CMakeFiles/mimalloc.dir/src/region.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mimalloc.dir/src/region.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/aprokurov/Projects/mimalloc/src/region.c -o CMakeFiles/mimalloc.dir/src/region.c.s

CMakeFiles/mimalloc.dir/src/segment.c.o: CMakeFiles/mimalloc.dir/flags.make
CMakeFiles/mimalloc.dir/src/segment.c.o: src/segment.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/aprokurov/Projects/mimalloc/CMakeFiles --progress-num=$(CMAKE_PROGRESS_7) "Building C object CMakeFiles/mimalloc.dir/src/segment.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/mimalloc.dir/src/segment.c.o -c /home/aprokurov/Projects/mimalloc/src/segment.c

CMakeFiles/mimalloc.dir/src/segment.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mimalloc.dir/src/segment.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/aprokurov/Projects/mimalloc/src/segment.c > CMakeFiles/mimalloc.dir/src/segment.c.i

CMakeFiles/mimalloc.dir/src/segment.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mimalloc.dir/src/segment.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/aprokurov/Projects/mimalloc/src/segment.c -o CMakeFiles/mimalloc.dir/src/segment.c.s

CMakeFiles/mimalloc.dir/src/page.c.o: CMakeFiles/mimalloc.dir/flags.make
CMakeFiles/mimalloc.dir/src/page.c.o: src/page.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/aprokurov/Projects/mimalloc/CMakeFiles --progress-num=$(CMAKE_PROGRESS_8) "Building C object CMakeFiles/mimalloc.dir/src/page.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/mimalloc.dir/src/page.c.o -c /home/aprokurov/Projects/mimalloc/src/page.c

CMakeFiles/mimalloc.dir/src/page.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mimalloc.dir/src/page.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/aprokurov/Projects/mimalloc/src/page.c > CMakeFiles/mimalloc.dir/src/page.c.i

CMakeFiles/mimalloc.dir/src/page.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mimalloc.dir/src/page.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/aprokurov/Projects/mimalloc/src/page.c -o CMakeFiles/mimalloc.dir/src/page.c.s

CMakeFiles/mimalloc.dir/src/alloc.c.o: CMakeFiles/mimalloc.dir/flags.make
CMakeFiles/mimalloc.dir/src/alloc.c.o: src/alloc.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/aprokurov/Projects/mimalloc/CMakeFiles --progress-num=$(CMAKE_PROGRESS_9) "Building C object CMakeFiles/mimalloc.dir/src/alloc.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/mimalloc.dir/src/alloc.c.o -c /home/aprokurov/Projects/mimalloc/src/alloc.c

CMakeFiles/mimalloc.dir/src/alloc.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mimalloc.dir/src/alloc.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/aprokurov/Projects/mimalloc/src/alloc.c > CMakeFiles/mimalloc.dir/src/alloc.c.i

CMakeFiles/mimalloc.dir/src/alloc.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mimalloc.dir/src/alloc.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/aprokurov/Projects/mimalloc/src/alloc.c -o CMakeFiles/mimalloc.dir/src/alloc.c.s

CMakeFiles/mimalloc.dir/src/alloc-aligned.c.o: CMakeFiles/mimalloc.dir/flags.make
CMakeFiles/mimalloc.dir/src/alloc-aligned.c.o: src/alloc-aligned.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/aprokurov/Projects/mimalloc/CMakeFiles --progress-num=$(CMAKE_PROGRESS_10) "Building C object CMakeFiles/mimalloc.dir/src/alloc-aligned.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/mimalloc.dir/src/alloc-aligned.c.o -c /home/aprokurov/Projects/mimalloc/src/alloc-aligned.c

CMakeFiles/mimalloc.dir/src/alloc-aligned.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mimalloc.dir/src/alloc-aligned.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/aprokurov/Projects/mimalloc/src/alloc-aligned.c > CMakeFiles/mimalloc.dir/src/alloc-aligned.c.i

CMakeFiles/mimalloc.dir/src/alloc-aligned.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mimalloc.dir/src/alloc-aligned.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/aprokurov/Projects/mimalloc/src/alloc-aligned.c -o CMakeFiles/mimalloc.dir/src/alloc-aligned.c.s

CMakeFiles/mimalloc.dir/src/alloc-posix.c.o: CMakeFiles/mimalloc.dir/flags.make
CMakeFiles/mimalloc.dir/src/alloc-posix.c.o: src/alloc-posix.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/aprokurov/Projects/mimalloc/CMakeFiles --progress-num=$(CMAKE_PROGRESS_11) "Building C object CMakeFiles/mimalloc.dir/src/alloc-posix.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/mimalloc.dir/src/alloc-posix.c.o -c /home/aprokurov/Projects/mimalloc/src/alloc-posix.c

CMakeFiles/mimalloc.dir/src/alloc-posix.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mimalloc.dir/src/alloc-posix.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/aprokurov/Projects/mimalloc/src/alloc-posix.c > CMakeFiles/mimalloc.dir/src/alloc-posix.c.i

CMakeFiles/mimalloc.dir/src/alloc-posix.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mimalloc.dir/src/alloc-posix.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/aprokurov/Projects/mimalloc/src/alloc-posix.c -o CMakeFiles/mimalloc.dir/src/alloc-posix.c.s

CMakeFiles/mimalloc.dir/src/heap.c.o: CMakeFiles/mimalloc.dir/flags.make
CMakeFiles/mimalloc.dir/src/heap.c.o: src/heap.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/aprokurov/Projects/mimalloc/CMakeFiles --progress-num=$(CMAKE_PROGRESS_12) "Building C object CMakeFiles/mimalloc.dir/src/heap.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/mimalloc.dir/src/heap.c.o -c /home/aprokurov/Projects/mimalloc/src/heap.c

CMakeFiles/mimalloc.dir/src/heap.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mimalloc.dir/src/heap.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/aprokurov/Projects/mimalloc/src/heap.c > CMakeFiles/mimalloc.dir/src/heap.c.i

CMakeFiles/mimalloc.dir/src/heap.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mimalloc.dir/src/heap.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/aprokurov/Projects/mimalloc/src/heap.c -o CMakeFiles/mimalloc.dir/src/heap.c.s

CMakeFiles/mimalloc.dir/src/options.c.o: CMakeFiles/mimalloc.dir/flags.make
CMakeFiles/mimalloc.dir/src/options.c.o: src/options.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/aprokurov/Projects/mimalloc/CMakeFiles --progress-num=$(CMAKE_PROGRESS_13) "Building C object CMakeFiles/mimalloc.dir/src/options.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/mimalloc.dir/src/options.c.o -c /home/aprokurov/Projects/mimalloc/src/options.c

CMakeFiles/mimalloc.dir/src/options.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mimalloc.dir/src/options.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/aprokurov/Projects/mimalloc/src/options.c > CMakeFiles/mimalloc.dir/src/options.c.i

CMakeFiles/mimalloc.dir/src/options.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mimalloc.dir/src/options.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/aprokurov/Projects/mimalloc/src/options.c -o CMakeFiles/mimalloc.dir/src/options.c.s

CMakeFiles/mimalloc.dir/src/init.c.o: CMakeFiles/mimalloc.dir/flags.make
CMakeFiles/mimalloc.dir/src/init.c.o: src/init.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/aprokurov/Projects/mimalloc/CMakeFiles --progress-num=$(CMAKE_PROGRESS_14) "Building C object CMakeFiles/mimalloc.dir/src/init.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/mimalloc.dir/src/init.c.o -c /home/aprokurov/Projects/mimalloc/src/init.c

CMakeFiles/mimalloc.dir/src/init.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mimalloc.dir/src/init.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/aprokurov/Projects/mimalloc/src/init.c > CMakeFiles/mimalloc.dir/src/init.c.i

CMakeFiles/mimalloc.dir/src/init.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mimalloc.dir/src/init.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/aprokurov/Projects/mimalloc/src/init.c -o CMakeFiles/mimalloc.dir/src/init.c.s

# Object files for target mimalloc
mimalloc_OBJECTS = \
"CMakeFiles/mimalloc.dir/src/stats.c.o" \
"CMakeFiles/mimalloc.dir/src/random.c.o" \
"CMakeFiles/mimalloc.dir/src/os.c.o" \
"CMakeFiles/mimalloc.dir/src/bitmap.c.o" \
"CMakeFiles/mimalloc.dir/src/arena.c.o" \
"CMakeFiles/mimalloc.dir/src/region.c.o" \
"CMakeFiles/mimalloc.dir/src/segment.c.o" \
"CMakeFiles/mimalloc.dir/src/page.c.o" \
"CMakeFiles/mimalloc.dir/src/alloc.c.o" \
"CMakeFiles/mimalloc.dir/src/alloc-aligned.c.o" \
"CMakeFiles/mimalloc.dir/src/alloc-posix.c.o" \
"CMakeFiles/mimalloc.dir/src/heap.c.o" \
"CMakeFiles/mimalloc.dir/src/options.c.o" \
"CMakeFiles/mimalloc.dir/src/init.c.o"

# External object files for target mimalloc
mimalloc_EXTERNAL_OBJECTS =

libmimalloc.so.1.7: CMakeFiles/mimalloc.dir/src/stats.c.o
libmimalloc.so.1.7: CMakeFiles/mimalloc.dir/src/random.c.o
libmimalloc.so.1.7: CMakeFiles/mimalloc.dir/src/os.c.o
libmimalloc.so.1.7: CMakeFiles/mimalloc.dir/src/bitmap.c.o
libmimalloc.so.1.7: CMakeFiles/mimalloc.dir/src/arena.c.o
libmimalloc.so.1.7: CMakeFiles/mimalloc.dir/src/region.c.o
libmimalloc.so.1.7: CMakeFiles/mimalloc.dir/src/segment.c.o
libmimalloc.so.1.7: CMakeFiles/mimalloc.dir/src/page.c.o
libmimalloc.so.1.7: CMakeFiles/mimalloc.dir/src/alloc.c.o
libmimalloc.so.1.7: CMakeFiles/mimalloc.dir/src/alloc-aligned.c.o
libmimalloc.so.1.7: CMakeFiles/mimalloc.dir/src/alloc-posix.c.o
libmimalloc.so.1.7: CMakeFiles/mimalloc.dir/src/heap.c.o
libmimalloc.so.1.7: CMakeFiles/mimalloc.dir/src/options.c.o
libmimalloc.so.1.7: CMakeFiles/mimalloc.dir/src/init.c.o
libmimalloc.so.1.7: CMakeFiles/mimalloc.dir/build.make
libmimalloc.so.1.7: /usr/lib/librt.so
libmimalloc.so.1.7: CMakeFiles/mimalloc.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/aprokurov/Projects/mimalloc/CMakeFiles --progress-num=$(CMAKE_PROGRESS_15) "Linking C shared library libmimalloc.so"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/mimalloc.dir/link.txt --verbose=$(VERBOSE)
	$(CMAKE_COMMAND) -E cmake_symlink_library libmimalloc.so.1.7 libmimalloc.so.1.7 libmimalloc.so

libmimalloc.so: libmimalloc.so.1.7
	@$(CMAKE_COMMAND) -E touch_nocreate libmimalloc.so

# Rule to build all files generated by this target.
CMakeFiles/mimalloc.dir/build: libmimalloc.so

.PHONY : CMakeFiles/mimalloc.dir/build

CMakeFiles/mimalloc.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/mimalloc.dir/cmake_clean.cmake
.PHONY : CMakeFiles/mimalloc.dir/clean

CMakeFiles/mimalloc.dir/depend:
	cd /home/aprokurov/Projects/mimalloc && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/aprokurov/Projects/mimalloc /home/aprokurov/Projects/mimalloc /home/aprokurov/Projects/mimalloc /home/aprokurov/Projects/mimalloc /home/aprokurov/Projects/mimalloc/CMakeFiles/mimalloc.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/mimalloc.dir/depend

