#!/usr/bin/env bash

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
BUILDDIR=${SCRIPT_DIR}/build
CONFIGDIR=config

while getopts ":b:c:" opt; do
  case ${opt} in
    b)
      BUILDDIR=${OPTARG}
      shift 2
      ;;
    c)
      CONFIGDIR=${OPTARG}
      ;;
    \?)
      echo "Invalid option: -${OPTARG}"
      exit -1
      ;;
    :)
      echo "Option -${OPTARG} requires an argument"
      exit -1
      ;;
  esac
done

shift $((OPTIND-1))

if [ ! -d ${BUILDDIR} ]; then
  echo "Invalid build directory: ${BUILDDIR}"
  exit -1
fi

export WIREPLUMBER_MODULE_DIR="${BUILDDIR}/modules"
export WIREPLUMBER_CONFIG_DIR="${SCRIPT_DIR}/src/${CONFIGDIR}"
export WIREPLUMBER_DATA_DIR="${SCRIPT_DIR}/src"
export PATH="${BUILDDIR}/src:${BUILDDIR}/src/tools:$PATH"
export LD_LIBRARY_PATH="${BUILDDIR}/lib/wp:$LD_LIBRARY_PATH"

exec $@
