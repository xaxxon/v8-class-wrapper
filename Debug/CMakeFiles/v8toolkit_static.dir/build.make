# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.6

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/local/Cellar/cmake/3.6.2/bin/cmake

# The command to remove a file.
RM = /usr/local/Cellar/cmake/3.6.2/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /Users/xaxxon/v8toolkit

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /Users/xaxxon/v8toolkit/Debug

# Include any dependencies generated for this target.
include CMakeFiles/v8toolkit_static.dir/depend.make

# Include the progress variables for this target.
include CMakeFiles/v8toolkit_static.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/v8toolkit_static.dir/flags.make

CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.o: CMakeFiles/v8toolkit_static.dir/flags.make
CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.o: ../src/javascript.cpp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/Users/xaxxon/v8toolkit/Debug/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.o"
	/Users/xaxxon/Downloads/clang+llvm-3.9.0-x86_64-apple-darwin/bin/clang++   $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -o CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.o -c /Users/xaxxon/v8toolkit/src/javascript.cpp

CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.i"
	/Users/xaxxon/Downloads/clang+llvm-3.9.0-x86_64-apple-darwin/bin/clang++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /Users/xaxxon/v8toolkit/src/javascript.cpp > CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.i

CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.s"
	/Users/xaxxon/Downloads/clang+llvm-3.9.0-x86_64-apple-darwin/bin/clang++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /Users/xaxxon/v8toolkit/src/javascript.cpp -o CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.s

CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.o.requires:

.PHONY : CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.o.requires

CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.o.provides: CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.o.requires
	$(MAKE) -f CMakeFiles/v8toolkit_static.dir/build.make CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.o.provides.build
.PHONY : CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.o.provides

CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.o.provides.build: CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.o


CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.o: CMakeFiles/v8toolkit_static.dir/flags.make
CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.o: ../src/v8helpers.cpp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/Users/xaxxon/v8toolkit/Debug/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Building CXX object CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.o"
	/Users/xaxxon/Downloads/clang+llvm-3.9.0-x86_64-apple-darwin/bin/clang++   $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -o CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.o -c /Users/xaxxon/v8toolkit/src/v8helpers.cpp

CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.i"
	/Users/xaxxon/Downloads/clang+llvm-3.9.0-x86_64-apple-darwin/bin/clang++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /Users/xaxxon/v8toolkit/src/v8helpers.cpp > CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.i

CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.s"
	/Users/xaxxon/Downloads/clang+llvm-3.9.0-x86_64-apple-darwin/bin/clang++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /Users/xaxxon/v8toolkit/src/v8helpers.cpp -o CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.s

CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.o.requires:

.PHONY : CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.o.requires

CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.o.provides: CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.o.requires
	$(MAKE) -f CMakeFiles/v8toolkit_static.dir/build.make CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.o.provides.build
.PHONY : CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.o.provides

CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.o.provides.build: CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.o


CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.o: CMakeFiles/v8toolkit_static.dir/flags.make
CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.o: ../src/v8toolkit.cpp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/Users/xaxxon/v8toolkit/Debug/CMakeFiles --progress-num=$(CMAKE_PROGRESS_3) "Building CXX object CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.o"
	/Users/xaxxon/Downloads/clang+llvm-3.9.0-x86_64-apple-darwin/bin/clang++   $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -o CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.o -c /Users/xaxxon/v8toolkit/src/v8toolkit.cpp

CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.i"
	/Users/xaxxon/Downloads/clang+llvm-3.9.0-x86_64-apple-darwin/bin/clang++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /Users/xaxxon/v8toolkit/src/v8toolkit.cpp > CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.i

CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.s"
	/Users/xaxxon/Downloads/clang+llvm-3.9.0-x86_64-apple-darwin/bin/clang++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /Users/xaxxon/v8toolkit/src/v8toolkit.cpp -o CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.s

CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.o.requires:

.PHONY : CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.o.requires

CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.o.provides: CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.o.requires
	$(MAKE) -f CMakeFiles/v8toolkit_static.dir/build.make CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.o.provides.build
.PHONY : CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.o.provides

CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.o.provides.build: CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.o


# Object files for target v8toolkit_static
v8toolkit_static_OBJECTS = \
"CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.o" \
"CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.o" \
"CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.o"

# External object files for target v8toolkit_static
v8toolkit_static_EXTERNAL_OBJECTS =

libv8toolkit.a: CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.o
libv8toolkit.a: CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.o
libv8toolkit.a: CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.o
libv8toolkit.a: CMakeFiles/v8toolkit_static.dir/build.make
libv8toolkit.a: CMakeFiles/v8toolkit_static.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/Users/xaxxon/v8toolkit/Debug/CMakeFiles --progress-num=$(CMAKE_PROGRESS_4) "Linking CXX static library libv8toolkit.a"
	$(CMAKE_COMMAND) -P CMakeFiles/v8toolkit_static.dir/cmake_clean_target.cmake
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/v8toolkit_static.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/v8toolkit_static.dir/build: libv8toolkit.a

.PHONY : CMakeFiles/v8toolkit_static.dir/build

CMakeFiles/v8toolkit_static.dir/requires: CMakeFiles/v8toolkit_static.dir/src/javascript.cpp.o.requires
CMakeFiles/v8toolkit_static.dir/requires: CMakeFiles/v8toolkit_static.dir/src/v8helpers.cpp.o.requires
CMakeFiles/v8toolkit_static.dir/requires: CMakeFiles/v8toolkit_static.dir/src/v8toolkit.cpp.o.requires

.PHONY : CMakeFiles/v8toolkit_static.dir/requires

CMakeFiles/v8toolkit_static.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/v8toolkit_static.dir/cmake_clean.cmake
.PHONY : CMakeFiles/v8toolkit_static.dir/clean

CMakeFiles/v8toolkit_static.dir/depend:
	cd /Users/xaxxon/v8toolkit/Debug && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /Users/xaxxon/v8toolkit /Users/xaxxon/v8toolkit /Users/xaxxon/v8toolkit/Debug /Users/xaxxon/v8toolkit/Debug /Users/xaxxon/v8toolkit/Debug/CMakeFiles/v8toolkit_static.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/v8toolkit_static.dir/depend

