.PHONY: all build test coverage tasks blocks-test-clean clean
SHELL=/bin/sh

INCLUDE_BASE_DIR ?= .
SRC_DIRS := $(shell find . -type d -a -not -name "test")
TEST_DIRS := $(shell find . -type d -name "test")

ARCH ?= amd64

BUILD_TYPE ?= debug

NAME ?= libEGD

# Should be set in the parent Makefile
BIN_DIR ?= ../bin/$(ARCH)/$(BUILD_TYPE)
LIB_DIR ?= ../lib/$(ARCH)/$(BUILD_TYPE)
COVERAGE_DIR ?= ../html/coverage

TEST_BIN_DIR ?= $(BIN_DIR)/tests
GTEST_DIR = /edge-test/googletest/googletest

test_binary := $(TEST_BIN_DIR)/$(NAME)-tests

binary := $(BIN_DIR)/$(NAME)-tester
shared_library := $(LIB_DIR)/$(NAME).so
static_library := $(LIB_DIR)/$(NAME).a

sources_c := $(foreach SRC_DIR, $(SRC_DIRS), $(wildcard $(SRC_DIR)/*.c))
sources_cc := $(foreach SRC_DIR, $(SRC_DIRS), $(wildcard $(SRC_DIR)/*.cc))
sources_cpp := $(foreach SRC_DIR, $(SRC_DIRS), $(wildcard $(SRC_DIR)/*.cpp))

main_sources_c :=
main_sources_cc := examples/main.cc
main_sources_cpp :=

test_sources_c := $(foreach TEST_DIR, $(TEST_DIRS), $(wildcard $(TEST_DIR)/*.c))
test_sources_cc := $(foreach TEST_DIR, $(TEST_DIRS), $(wildcard $(TEST_DIR)/*.cc))
test_sources_cpp := $(foreach TEST_DIR, $(TEST_DIRS), $(wildcard $(TEST_DIR)/*.cpp))

headers := $(foreach SRC_DIR, $(SRC_DIRS), $(wildcard $(SRC_DIR)/*.h))
headers += $(foreach SRC_DIR, $(SRC_DIRS), $(wildcard $(SRC_DIR)/*.hpp))

ifeq ($(BUILD_TYPE), release)
objs_suffix := _$(ARCH)_release
else ifeq ($(BUILD_TYPE), coverage)
objs_suffix := _$(ARCH)_coverage
else
objs_suffix := _$(ARCH)
endif

objs := $(sources_c:.c=$(objs_suffix).o)
objs += $(sources_cc:.cc=$(objs_suffix).o)
objs += $(sources_cpp:.cpp=$(objs_suffix).o)

main_objs := $(main_sources_c:.c=$(objs_suffix).o)
main_objs += $(main_sources_cc:.cc=$(objs_suffix).o)
main_objs += $(main_sources_cpp:.cpp=$(objs_suffix).o)

test_objs := $(test_sources_c:.c=$(objs_suffix).o)
test_objs += $(test_sources_cc:.cc=$(objs_suffix).o)
test_objs += $(test_sources_cpp:.cpp=$(objs_suffix).o)

# Filter out any main files to make the list of library object files
lib_objs := $(filter-out $(main_objs),$(objs))

# Generate the list of dependency files that we need to generate
deps := $(objs:%.o=%.d)

debug_cc_flags = -g -O0 -rdynamic -ggdb -fstack-protector-all
coverage_cc_flags = --coverage
release_cc_flags = -O2

debug_cxx_flags = -g -O0 -rdynamic -ggdb -fstack-protector-all
coverage_cxx_flags = --coverage
release_cxx_flags = -O2

coverage_ld_flags = --coverage
curl_static_libs = $(shell curl-config --static-libs)
#link_libraries += -pthread -lcurl $(curl_static_libs) -lgss -lidn -lnsl -lnghttp2 -lssh2 -lssl -lcrypto -lz -luuid -ldl
link_libraries += -pthread -lcurl

cc_flags = -fPIC -I$(INCLUDE_BASE_DIR) -Wall
cxx_flags = -std=c++11 -fPIC -I$(INCLUDE_BASE_DIR) -Wall
shared_ld_flags = -shared $(link_libraries)
executable_ld_flags = $(link_libraries)
test_ld_flags = $(link_libraries)

ifeq ($(BUILD_TYPE), release)
cc_flags += $(release_cc_flags)
cxx_flags += $(release_cxx_flags)
else ifeq ($(BUILD_TYPE), coverage)
cc_flags += $(coverage_cc_flags)
cxx_flags += $(coverage_cxx_flags)
ld_flags += $(coverage_ld_flags)
test_ld_flags += $(coverage_ld_flags)
else
cc_flags += $(debug_cc_flags)
cxx_flags += $(debug_cxx_flags)
endif

INSTALL ?= install
INSTALL_PROGRAM = $(INSTALL) -D
INSTALL_DATA = $(INSTALL) -m 644 -D

prefix ?= /usr/local
exec_prefix ?= $(prefix)

includedir = $(prefix)/include
libdir = $(prefix)/lib
bindir = $(prefix)/bin

CC ?= gcc
CXX ?= g++

all := $(binary) $(shared_library) $(static_library)
all: $(all)
tasks:
	@echo $(notdir $(all))

# Include the dep files
-include $(deps)

%$(objs_suffix).o: %.c
	$(CC) -o $@ $(cc_flags) -MMD -c $<
%$(objs_suffix).o: %.cpp
	$(CXX) -o $@ $(cxx_flags) -MMD -c $<
%$(objs_suffix).o: %.cc
	$(CXX) -o $@ $(cxx_flags) -MMD -c $<

install: $(binary)
	$(INSTALL_PROGRAM) $(INSTALL_OPTS) $(binary) $(DESTDIR)$(bindir)/$(notdir $(binary))
	$(INSTALL_DATA) $(INSTALL_OPTS) $(static_library) $(DESTDIR)$(libdir)/$(notdir $(static_library))

$(LIB_DIR):
	mkdir -p $@

$(BIN_DIR):
	mkdir -p $@

$(COVERAGE_DIR):
	mkdir -p $@

$(TEST_BIN_DIR):
	mkdir -p $@

$(binary): $(objs) | $(BIN_DIR)
	$(CXX) -o $@ $^ $(cxx_flags) $(executable_ld_flags)
ifeq ($(BUILD_TYPE), release)
	strip $@
endif

$(shared_library): $(lib_objs) | $(LIB_DIR)
	$(CXX) -o $@ $^ $(cxx_flags) $(shared_ld_flags)
ifeq ($(BUILD_TYPE), release)
	strip $@
endif

$(static_library): $(lib_objs) | $(LIB_DIR)
	$(AR) rcs $@ $^

# Begin testing stuff
$(TEST_BIN_DIR)/gtest-all.o : | $(TEST_BIN_DIR)
	$(CXX) $(cxx_flags) -I$(GTEST_DIR) -I$(GTEST_DIR)/include -c $(GTEST_DIR)/src/gtest-all.cc -o $@

$(TEST_BIN_DIR)/gtest_main.o : | $(TEST_BIN_DIR)
	$(CXX) $(cxx_flags) -I$(GTEST_DIR) -I$(GTEST_DIR)/include -c $(GTEST_DIR)/src/gtest_main.cc -o $@

$(LIB_DIR)/gtest.a : $(TEST_BIN_DIR)/gtest-all.o | $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

$(LIB_DIR)/gtest_main.a : $(TEST_BIN_DIR)/gtest-all.o $(TEST_BIN_DIR)/gtest_main.o | $(TEST_BIN_DIR)
	$(AR) $(ARFLAGS) $@ -o $^

$(test_binary): $(lib_objs) $(test_objs) $(LIB_DIR)/gtest_main.a | $(TEST_BIN_DIR)
	$(CXX) -o $@ $^ $(cc_flags) $(test_ld_flags)

test: $(test_binary)
	@$^

ifeq ($(BUILD_TYPE), coverage)
coverage: test | $(COVERAGE_DIR)
	lcov $(addprefix --directory , $(dir $(lib_objs))) --capture --output-file $(COVERAGE_DIR)/$(NAME)_all.info
	lcov -r $(COVERAGE_DIR)/$(NAME)_all.info "/usr/include*" "/usr/local/include*" "*test*" "*external*" -o $(COVERAGE_DIR)/$(NAME).info
	genhtml -o $(COVERAGE_DIR) -t "$(NAME)" --demangle-cpp --legend $(COVERAGE_DIR)/$(NAME).info
else
coverage:
	@$(MAKE) coverage BUILD_TYPE="coverage"
endif

test-clean:
	rm -rf $(TEST_BIN_DIR)
	rm -f $(LIB_DIR)/gtest*
	rm -f $(foreach TEST_DIR, $(TEST_DIRS), $(wildcard $(TEST_DIR)/*.o))

coverage-clean:
	rm -f $(foreach GC_DIR, $(TEST_DIRS) $(SRC_DIRS), $(wildcard $(GC_DIR)/*.gc*))

clean: test-clean coverage-clean
	rm -f $(all)
	rm -f $(foreach SRC_DIR, $(SRC_DIRS), $(wildcard $(SRC_DIR)/*.d))
	rm -f $(foreach SRC_DIR, $(SRC_DIRS), $(wildcard $(SRC_DIR)/*.o))
