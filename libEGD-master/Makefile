.PHONY: all build build-shell pyintstaller aws-agent-launcher root-shell test coverage docs serve scan clean image-clean src-clean html-clean

# Build tools
#
# Targets (see each target for more information):
#   test:	runs lint, unit tests etc.
#   scan:	runs static analysis tools
#   clean:	removes build artifacts and images
#
#   all-build:	builds binaries for all target architectures
#

NAME := libEGD

# Version to tag
VERSION := 1.0.0
REVISION := 1

# Which architecture to build - see $(ALL_ARCH) for options.
ARCH ?= amd64

# Short description of the package
DESCRIPTION := "C/C++ library for commmunicating over EGD"

# Which BUILD_TYPE to build for (debug, release, coverage)
BUILD_TYPE ?= release

# Any directories that contain source code
SRC_DIRS := src

###
### These variables should not need tweaking.
###

# Platform specific USER  and proxy crud:
# On linux, run the container with the current uid, so files produced from
# within the container are owned by the current user, rather than root.
#
# On OSX, don't do anything with the container user, and let boot2docker manage
# permissions on the /Users mount that it sets up
DOCKER_USER := $(shell if [ "$$OSTYPE" != "darwin"* ]; then USER_ARG="--user=`id -u`"; fi; echo "$$USER_ARG")
PROXY_ARGS := $(shell if [ "$$http_proxy" != "" ]; then echo "-e http_proxy=$$http_proxy"; fi)
PROXY_ARGS += $(shell if [ "$$https_proxy" != "" ]; then echo " -e https_proxy=$$https_proxy"; fi)
PROXY_ARGS += $(shell if [ "$$no_proxy" != "" ]; then echo " -e no_proxy=$$no_proxy"; fi)

# Allow prepending to LOWER_NAME via PRE_NAME, this is needed for cases where multiple
# projects are going to build this one and you want the containers to have different names
# so they don't clean up each others artifacts
PRE_NAME ?=

LOWER_NAME := $(shell echo $(PRE_NAME)$(NAME) | tr A-Z a-z)

BUILDER_IMAGE := $(LOWER_NAME)-$(ARCH):builder
RUNTIME_IMAGE := registry.gear.ge.com/digital_ghost/$(LOWER_NAME)-$(ARCH):$(VERSION)
DEV_IMAGE := registry.gear.ge.com/digital_ghost/$(LOWER_NAME)-dev-$(ARCH):$(VERSION)

ALL_ARCH := amd64

# The project source .cc .h .cpp and .hpp files
SRC_FILES := $(foreach SRC_DIR, $(SRC_DIRS), $(shell find $(SRC_DIR) -type f -name "*.cc" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp"))

PROJ_ROOT := $(shell pwd)
DOCKER_ROOT := /home/edge/$(NAME)
DOCKER_SRC_DIRS := $(addprefix $(DOCKER_ROOT)/, $(SRC_DIRS))

BIN_DIR := bin/$(ARCH)/$(BUILD_TYPE)
LIB_DIR := lib/$(ARCH)/$(BUILD_TYPE)
COVERAGE_DIR := html/coverage
DOCS_DIR := html/docs
DOCKER_BIN_DIR := $(DOCKER_ROOT)/$(BIN_DIR)
DOCKER_LIB_DIR := $(DOCKER_ROOT)/$(LIB_DIR)
DOCKER_COVERAGE_DIR := $(DOCKER_ROOT)/$(COVERAGE_DIR)
DOCKER_DOCS_DIR := $(DOCKER_ROOT)/$(DOCS_DIR)
BUILD_DIRS := $(BIN_DIR) $(LIB_DIR) $(COVERAGE_DIR) $(DOCS_DIR)

# Determines the dependencies that will be installed when this package is installed
DEBIAN_DEPS_FILE := $(PROJ_ROOT)/hack/debian-deps.txt
DEBIAN_DEPS := $(shell cat $(DEBIAN_DEPS_FILE))
DEBIAN_PACKAGE_NAME := $(NAME)_$(VERSION)-$(REVISION).deb
DEBIAN_PACKAGE_FILE := $(PROJ_ROOT)/$(DEBIAN_PACKAGE_NAME)
DOCKER_DEBIAN_PACKAGE_FILE := $(DOCKER_ROOT)/$(DEBIAN_PACKAGE_NAME)

DOCKER_ENVIRONMENT_VARIABLES := -e ARCH=$(ARCH) -e DEB_FILE="$(DOCKER_DEBIAN_PACKAGE_FILE)" -e NAME=$(NAME) -e VERSION=$(VERSION) -e REVISION=$(REVISION) -e BUILD_TYPE=$(BUILD_TYPE) -e DEPENDENCIES="$(shell echo $(DEBIAN_DEPS) | tr ' ' ',')"
DOCKER_ENVIRONMENT_VARIABLES += -e SRC_DIRS=$(DOCKER_SRC_DIRS) -e ROOT_DIR=$(DOCKER_ROOT)
DOCKER_ENVIRONMENT_VARIABLES += -e BIN_DIR=$(DOCKER_BIN_DIR) -e LIB_DIR=$(DOCKER_LIB_DIR) -e COVERAGE_DIR=$(DOCKER_COVERAGE_DIR) -e DOCS_DIR=$(DOCKER_DOCS_DIR)
DOCKER_ENVIRONMENT_VARIABLES += -e LD_LIBRARY_PATH=$(DOCKER_ROOT)/$(LIB_DIR)
DOCKER_ENVIRONMENT_VARIABLES += -e DOCKER_ROOT=$(DOCKER_ROOT)
DOCKER_ENVIRONMENT_VARIABLES += $(PROXY_ARGS)


binaries := $(addprefix $(BIN_DIR)/, $(foreach SRC_DIR, $(SRC_DIRS), $(shell make --no-print-directory -C $(SRC_DIR) tasks)))

# Default target
all: build

build: $(binaries)

release:
	@$(MAKE) build BUILD_TYPE="release"

$(BIN_DIR):
	@mkdir -p $@

$(LIB_DIR):
	@mkdir -p $@

$(COVERAGE_DIR):
	@mkdir -p $@

$(DOCS_DIR):
	@mkdir -p $@

.builder-$(ARCH): hack/docker/Dockerfile.builder
	@echo "creating dev environment image ... "
	@docker build                                                          \
	    --build-arg ARCH=$(ARCH)                                           \
		--force-rm=true                                                    \
		-t $(BUILDER_IMAGE)                                                \
		-f $<                                                              \
		$(shell echo "$(PROXY_ARGS)" | sed s/-e/--build-arg/g)             \
		.
	@touch $@

image: .image-$(ARCH)
image_name:
	@echo $(RUNTIME_IMAGE)
.image-$(ARCH): hack/docker/Dockerfile.in .builder-$(ARCH) $(SRC_FILES)
	@echo "creating runtime image ... "
	@docker build                                                          \
		--force-rm=true                                                    \
		-t $(RUNTIME_IMAGE)                                                \
		--build-arg NAME=$(NAME)                                           \
		--build-arg ARCH=$(ARCH)                                           \
		--build-arg BUILDER_IMAGE=$(BUILDER_IMAGE)                         \
		-f $<                                                              \
		$(shell echo "$(PROXY_ARGS)" | sed s/-e/--build-arg/g)             \
		.
	@touch $@

dev_image: .dev_image-$(ARCH)
dev_image_name:
	@echo $(DEV_IMAGE)
.dev_image-$(ARCH): hack/docker/Dockerfile.dev .builder-$(ARCH) $(SRC_FILES)
	@echo "creating development image ... "
	@docker build                                                          \
		--force-rm=true                                                    \
		-t $(DEV_IMAGE)                                                \
		--build-arg NAME=$(NAME)                                           \
		--build-arg ARCH=$(ARCH)                                           \
		--build-arg BUILDER_IMAGE=$(BUILDER_IMAGE)                         \
		-f $<                                                              \
		$(shell echo "$(PROXY_ARGS)" | sed s/-e/--build-arg/g)             \
		.
	@touch $@

$(BIN_DIR)/%: .builder-$(ARCH) $(SRC_FILES) hack/build.sh | $(BUILD_DIRS)
	@echo "Building $@ ... "
	@docker run                                                            \
		-t                                                                 \
		--rm                                                               \
		$(DOCKER_USER)                                                     \
		$(DOCKER_ENVIRONMENT_VARIABLES)                                    \
		$(PROXY_ARGS)                                                      \
		-v $(PROJ_ROOT):$(DOCKER_ROOT)                                     \
		-w $(DOCKER_ROOT)                                                  \
		$(BUILDER_IMAGE)                                                   \
		/bin/sh -c "                                                       \
		    hack/build.sh                                                   \
		"

package: $(DEBIAN_PACKAGE_FILE)
$(DEBIAN_PACKAGE_FILE): .builder-$(ARCH) $(SRC_FILES) $(DEBIAN_DEPS_FILE) hack/package-deb.sh CMakeLists.txt | $(BUILD_DIRS)
	@echo "Building $@ ... "
	@docker run                                                            \
        -t                                                                 \
        --rm                                                               \
        $(DOCKER_USER)                                                     \
        $(DOCKER_ENVIRONMENT_VARIABLES)                                    \
        $(PROXY_ARGS)                                                      \
        -v $(PROJ_ROOT):$(DOCKER_ROOT)                                     \
        -w $(DOCKER_ROOT)                                                  \
        $(BUILDER_IMAGE)                                                   \
        /bin/sh -c "                                                       \
            hack/package-deb.sh                                            \
        "

build-shell: .builder-$(ARCH)
	@echo "entering build shell..."
	@docker run                                                            \
		-it                                                                \
		--rm                                                               \
		-p 8080:8080/udp                                                   \
		--cap-add SYS_PTRACE                                               \
		$(DOCKER_USER)                                                     \
		$(DOCKER_ENVIRONMENT_VARIABLES)                                    \
		$(PROXY_ARGS)                                                      \
		-v $(PROJ_ROOT):$(DOCKER_ROOT)                                     \
		-w $(DOCKER_ROOT)                                                  \
		$(BUILDER_IMAGE)                                                   \
		/bin/bash

root-shell: .builder-$(ARCH)
	@echo "entering build shell..."
	@docker run                                                            \
		-it                                                                \
		--rm                                                               \
		--cap-add SYS_PTRACE                                               \
		--user=root                                                        \
		$(DOCKER_ENVIRONMENT_VARIABLES)                                    \
		$(PROXY_ARGS)                                                      \
		-v $(PROJ_ROOT):$(DOCKER_ROOT)                                     \
		-w $(DOCKER_ROOT)                                                  \
		$(BUILDER_IMAGE)                                                   \
		/bin/bash

test: .builder-$(ARCH) hack/test.sh | $(BUILD_DIRS)
	@echo "Building $@ ... "
	@docker run                                                            \
		-t                                                                 \
		--rm                                                               \
		$(DOCKER_USER)                                                     \
		$(DOCKER_ENVIRONMENT_VARIABLES)                                    \
		$(PROXY_ARGS)                                                      \
		-v $(PROJ_ROOT):$(DOCKER_ROOT)                                     \
		-w $(DOCKER_ROOT)                                                  \
		$(BUILDER_IMAGE)                                                   \
		/bin/sh -c "                                                       \
		    hack/test.sh                                                   \
		"

coverage: .builder-$(ARCH) hack/coverage.sh | $(BUILD_DIRS)
	@echo "Running $@ ... "
	@docker run                                                            \
		-t                                                                 \
		--rm                                                               \
		$(DOCKER_USER)                                                     \
		$(DOCKER_ENVIRONMENT_VARIABLES)                                    \
		$(PROXY_ARGS)                                                      \
		-v $(PROJ_ROOT):$(DOCKER_ROOT)                                     \
		-w $(DOCKER_ROOT)                                                  \
		$(BUILDER_IMAGE)                                                   \
		/bin/sh -c "                                                       \
		    hack/coverage.sh                                               \
		"

docs: .builder-$(ARCH) doc/Doxyfile | $(BUILD_DIRS)
	@echo "Running $@ ... "
	@docker run                                                            \
		-t                                                                 \
		--rm                                                               \
		$(DOCKER_USER)                                                     \
		$(DOCKER_ENVIRONMENT_VARIABLES)                                    \
		$(PROXY_ARGS)                                                      \
		-v $(PROJ_ROOT):$(DOCKER_ROOT)                                     \
		-w $(DOCKER_ROOT)                                                  \
		$(BUILDER_IMAGE)                                                   \
		/bin/sh -c "                                                       \
		    hack/docs.sh                                                   \
		"

serve: .builder-$(ARCH) | $(BUILD_DIRS)
	@echo "Running $@ ... "
	@docker run                                                            \
		-it                                                                \
		--rm                                                               \
		$(DOCKER_USER)                                                     \
		$(DOCKER_ENVIRONMENT_VARIABLES)                                    \
		$(PROXY_ARGS)                                                      \
		-p 3002:3002                                                       \
		-v $(PROJ_ROOT):$(DOCKER_ROOT)                                     \
		-w $(DOCKER_ROOT)/html                                             \
		$(BUILDER_IMAGE)                                                   \
		/bin/sh -c "                                                       \
		    python -m SimpleHTTPServer 3002                                \
		"

scan: .builder-$(ARCH) hack/scan.sh | $(BUILD_DIRS)
	@echo "Building $@ ... "
	@docker run                                                            \
		-t                                                                 \
		--rm                                                               \
		$(DOCKER_USER)                                                     \
		$(DOCKER_ENVIRONMENT_VARIABLES)                                    \
		$(PROXY_ARGS)                                                      \
		-v $(PROJ_ROOT):$(DOCKER_ROOT)                                     \
		-w $(DOCKER_ROOT)                                                  \
		$(BUILDER_IMAGE)                                                   \
		/bin/sh -c "                                                       \
		    hack/scan.sh                                                   \
		"

# Clean everything; clean slate.
clean: image-clean src-clean examples-clean lib-clean html-clean bin-clean integration-test-clean

# Clean the build without rebuilding the images
build-clean: src-clean lib-clean bin-clean


image-clean:
	@if [ $(shell docker ps -a | grep $(NAME) | wc -l) != 0 ]; then \
		docker ps -a | grep $(NAME) | awk '{print $$1 }' | xargs docker rm -f; \
	fi
	@if [ $(shell docker images | grep $(NAME) | wc -l) != 0 ]; then \
		docker images | grep $(NAME) | grep -v "base" | awk '{print $$3}' | xargs docker rmi -f || true; \
	fi
	@if [ $(shell docker ps -a | grep $(LOWER_NAME) | wc -l) != 0 ]; then \
		docker ps -a | grep $(LOWER_NAME) | awk '{print $$1 }' | xargs docker rm -f; \
	fi
	@if [ $(shell docker images | grep $(LOWER_NAME) | wc -l) != 0 ]; then \
		docker images | grep $(LOWER_NAME) | grep -v "base" | awk '{print $$3}' | xargs docker rmi -f || true; \
	fi
	rm -f .builder-*
	rm -f .image-*
	rm -f .pyinstaller-*
	rm -rf $(LIB_DIR)

examples-clean:
	rm -f $(examples)

lib-clean:
	rm -f $(binaries)

html-clean:
	rm -rf html

src-clean:
	@for DIR in $(SRC_DIRS); do \
		$(MAKE) -C $${DIR} clean; \
	done

bin-clean:
	@rm -rf bin
	@rm -rf lib

package-clean:
	@rm -f *.deb

integration-test-clean:
	@rm -rf test/yocto-intel
	@rm -f report.txt
	@rm -f job_responses.txt
