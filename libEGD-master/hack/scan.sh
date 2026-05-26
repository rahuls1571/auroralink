#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail

if [ -z "${SRC_DIRS}" ]; then
    echo "SRC_DIRS must be set"
    exit 1
fi

SOURCE_FILES=""
for SRC_DIR in ${SRC_DIRS}; do
  SOURCE_FILES="${SOURCE_FILES} $(find ${SRC_DIR} \( -name *.cc -o -name *.h -o -name *.cpp -o -name *.hpp -o -name *.c \) -a -not -path "*/test/*" -a -not -path "*/external/*" | tr '\n' ' ')"
done

EXIT_CODE=0

# Run cppcheck and if it fails, set exit code to 1, and continue on.
{
	cppcheck --error-exitcode=1 --enable=warning --force --language=c++ ${SOURCE_FILES} 2> check.log
} || {
	EXIT_CODE=1
	echo "------------------------------------------------------------------------------"
	echo "cppcheck failed, output can be found in check.log, continuing with cpplint..."
	echo "------------------------------------------------------------------------------"
}

# Run cpplint and if it fails, set exit code to 1, and continue on.
{
	python /edge-test/styleguide/cpplint/cpplint.py --filter=-whitespace/line_length,-runtime/references,-build/header_guard,-readability/todo ${SOURCE_FILES} 2> lint.log
} || {
	EXIT_CODE=1
	echo "-----------------------------------------------"
	echo "cpplint failed, output can be found in lint.log"
	echo "-----------------------------------------------"
}
exit ${EXIT_CODE}
