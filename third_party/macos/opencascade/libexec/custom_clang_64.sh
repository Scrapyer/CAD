#!/bin/bash

if [ "$1" == "" ]; then
  if [ "$2" == "64" ]; then
    # set environment variables used by OCCT
    export CSF_FPE=0

    export TCL_DIR="/opt/homebrew/opt/tcl-tk@8/lib"
    export TK_DIR="/opt/homebrew/opt/tcl-tk@8/lib"
    export FREETYPE_DIR="/opt/homebrew/opt/freetype/lib"
    export FREEIMAGE_DIR=""
    export TBB_DIR="/opt/homebrew/opt/tbb/lib"
    export VTK_DIR=""
    export FFMPEG_DIR=""
    export JEMALLOC_DIR=""

    if [ "x" != "x" ]; then
      export QTDIR=""
    fi

    export TCL_VERSION_WITH_DOT="8.6"
    export TK_VERSION_WITH_DOT="8.6"

    export CSF_OCCTBinPath="${CASROOT}/bin"
    export CSF_OCCTLibPath="${CASROOT}/lib"
    export CSF_OCCTIncludePath="${CASROOT}/include/opencascade"
    export CSF_OCCTResourcePath="${CASROOT}/share/opencascade/resources"
    export CSF_OCCTDataPath="${CASROOT}/share/opencascade/data"
    export CSF_OCCTSamplesPath="${CASROOT}/share/opencascade/samples"
    export CSF_OCCTTestsPath="${CASROOT}/share/opencascade/tests"
    export CSF_OCCTDocPath="${CASROOT}/share/doc/opencascade"
  fi
fi

