#!/bin/sh
# Reproduce Hamlet's compiler and Qt (GCC 16.2, Qt 6.12.0) in a cloud container
# from conda-forge, to see the warnings Hamlet's build shows. About 2.2 GB.
# Usage: sh hamlet-toolchain.sh DIR, then `. DIR/envrc` before configuring.
# Configure native/ with -DSSTVAE_HAMLIB_DIR=native/build/_deps/hamlib-install-4.7.2
# and -DSSTVAE_ONNXRUNTIME_DIR=native/build/_deps/onnxruntime_prebuilt-src so
# nothing is downloaded. A full build is 482 steps, about 15 min at -j3.
# Reconstructed on 2026-10-15 from the recipe that built the first toolchain;
# this script itself has not been re-run.
set -e
TC=${1:?usage: hamlet-toolchain.sh DIR}
mkdir -p "$TC/mm"
cd "$TC"
curl -fsSL -o mm.tar.bz2 https://conda.anaconda.org/conda-forge/linux-64/micromamba-2.9.0-0.tar.bz2
tar -xjf mm.tar.bz2 -C mm
MAMBA_ROOT_PREFIX="$TC/root" ./mm/bin/micromamba create -y -p "$TC/env" -c conda-forge --override-channels \
    gxx_linux-64=16.2 gcc_linux-64=16.2 qt6-main=6.12.0 qt6-multimedia=6.12.0 cmake ninja pkg-config \
    libheif libwebp openjpeg zstd python=3.13 pybind11 numpy
cat > envrc <<ENV
export TC=$TC
export PATH=$TC/env/bin:\$PATH
export CC=$TC/env/bin/x86_64-conda-linux-gnu-gcc
export CXX=$TC/env/bin/x86_64-conda-linux-gnu-g++
export PKG_CONFIG_PATH=$TC/env/lib/pkgconfig:$TC/env/share/pkgconfig
export CMAKE_PREFIX_PATH=$TC/env
ENV
echo "toolchain ready: . $TC/envrc"
