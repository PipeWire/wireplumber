#!/usr/bin/env bash

set -e

# This is unset by meson
if [ -z "@MESON@" ]; then
  SOURCEDIR="@MESON_SOURCE_ROOT@"
  BUILDDIR="@MESON_BUILD_ROOT@"
else
  SOURCEDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
  BUILDDIR=$(find "${SOURCEDIR}" -maxdepth 2 -name build.ninja -printf "%h\n" -quit 2>/dev/null || echo "${SOURCEDIR}/build")
fi
CONFIGDIR=config

while getopts ":b:c:" opt; do
  case ${opt} in
    b)
      BUILDDIR=${OPTARG}
      ;;
    c)
      CONFIGDIR=${OPTARG}
      ;;
    \?)
      echo "Invalid option: -${OPTARG}"
      exit 1
      ;;
    :)
      echo "Option -${OPTARG} requires an argument"
      exit 1
      ;;
  esac
done

shift $((OPTIND-1))

if [ $# -eq 0 ]; then
  echo "Usage: $(basename ${BASH_SOURCE[0]}) [options] <wireplumber|wpctl|wpexec|...>"
  exit 1
fi

if [ ! -d ${BUILDDIR} ]; then
  echo "Invalid build directory: ${BUILDDIR}"
  exit 1
fi

export WIREPLUMBER_MODULE_DIR="${BUILDDIR}/modules"
export WIREPLUMBER_CONFIG_DIR="${SOURCEDIR}/src/${CONFIGDIR}"
export WIREPLUMBER_DATA_DIR="${SOURCEDIR}/src"
export PATH="${BUILDDIR}/src:${BUILDDIR}/src/tools:$PATH"
export LD_LIBRARY_PATH="${BUILDDIR}/lib/wp:$LD_LIBRARY_PATH"

exec $@
