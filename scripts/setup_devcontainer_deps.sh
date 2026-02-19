#!/usr/bin/env bash
set -euo pipefail

if command -v sudo >/dev/null 2>&1; then
  SUDO="sudo"
else
  SUDO=""
fi

${SUDO} apt-get update
${SUDO} apt-get install -y --no-install-recommends \
  liburing-dev \
  libboost-dev \
  libboost-system-dev \
  gcovr \
  clangd \
  clang-format \
  clang-tidy \
  hyperfine \
  valgrind \
  linux-perf \
  doxygen \
  graphviz \
  python3-matplotlib
${SUDO} apt-get clean
${SUDO} rm -rf /var/lib/apt/lists/*
