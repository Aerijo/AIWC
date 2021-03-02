ARG LLVM_VERSION=11.0.1

ARG LLVM_INSTALL_DIR=/llvm

ARG DOWNLOADS_DIR=/tmp


###########################################################
FROM ubuntu:focal AS llvm-build
LABEL maintainer="Benjamin Gray <benjamin.gray@anu.edu.au>"

# Disable interactive apt install scripts
ARG DEBIAN_FRONTEND=noninteractive

ARG LLVM_ROOT=/src/llvm
ARG LLVM_DIR=${LLVM_ROOT}/llvm
ARG CLANG_DIR=${LLVM_ROOT}/clang
ARG LLD_DIR=${LLVM_ROOT}/lld
ARG COMPILER_RT_DIR=${LLVM_ROOT}/compiler-rt
ARG LIBCXX_DIR=${LLVM_ROOT}/libcxx
ARG LIBCXXABI_DIR=${LLVM_ROOT}/libcxxabi
ARG LIBUNWIND_DIR=${LLVM_ROOT}/libunwind
ARG OPENMP_DIR=${LLVM_ROOT}/openmp

RUN apt-get update -yq && apt-get install -yq \
    wget \
    xz-utils

ARG DOWNLOADS_DIR
ARG LLVM_VERSION

WORKDIR ${DOWNLOADS_DIR}
ARG WGET_FLAGS="-q --show-progress --progress=bar:force"
RUN wget ${WGET_FLAGS} https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/llvm-${LLVM_VERSION}.src.tar.xz
RUN wget ${WGET_FLAGS} https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/clang-${LLVM_VERSION}.src.tar.xz
RUN wget ${WGET_FLAGS} https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/lld-${LLVM_VERSION}.src.tar.xz
RUN wget ${WGET_FLAGS} https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/compiler-rt-${LLVM_VERSION}.src.tar.xz
RUN wget ${WGET_FLAGS} https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/libcxx-${LLVM_VERSION}.src.tar.xz
RUN wget ${WGET_FLAGS} https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/libcxxabi-${LLVM_VERSION}.src.tar.xz
RUN wget ${WGET_FLAGS} https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/libunwind-${LLVM_VERSION}.src.tar.xz
RUN wget ${WGET_FLAGS} https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/openmp-${LLVM_VERSION}.src.tar.xz

RUN mkdir -p ${LLVM_DIR}        && tar -xf ${DOWNLOADS_DIR}/llvm-${LLVM_VERSION}.src.tar.xz         --strip-components 1 -C ${LLVM_DIR}
RUN mkdir -p ${CLANG_DIR}       && tar -xf ${DOWNLOADS_DIR}/clang-${LLVM_VERSION}.src.tar.xz        --strip-components 1 -C ${CLANG_DIR}
RUN mkdir -p ${LLD_DIR}         && tar -xf ${DOWNLOADS_DIR}/lld-${LLVM_VERSION}.src.tar.xz          --strip-components 1 -C ${LLD_DIR}
RUN mkdir -p ${COMPILER_RT_DIR} && tar -xf ${DOWNLOADS_DIR}/compiler-rt-${LLVM_VERSION}.src.tar.xz  --strip-components 1 -C ${COMPILER_RT_DIR}
RUN mkdir -p ${LIBCXX_DIR}      && tar -xf ${DOWNLOADS_DIR}/libcxx-${LLVM_VERSION}.src.tar.xz       --strip-components 1 -C ${LIBCXX_DIR}
RUN mkdir -p ${LIBCXXABI_DIR}   && tar -xf ${DOWNLOADS_DIR}/libcxxabi-${LLVM_VERSION}.src.tar.xz    --strip-components 1 -C ${LIBCXXABI_DIR}
RUN mkdir -p ${LIBUNWIND_DIR}   && tar -xf ${DOWNLOADS_DIR}/libunwind-${LLVM_VERSION}.src.tar.xz    --strip-components 1 -C ${LIBUNWIND_DIR}
RUN mkdir -p ${OPENMP_DIR}      && tar -xf ${DOWNLOADS_DIR}/openmp-${LLVM_VERSION}.src.tar.xz       --strip-components 1 -C ${OPENMP_DIR}

RUN apt-get update -yq && apt-get install -yq \
    clang \
    cmake \
    git \
    python3 \
    python3-distutils

ARG LLVM_INSTALL_DIR

WORKDIR ${LLVM_DIR}
ENV CC=clang
ENV CXX=clang++
RUN cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=${LLVM_INSTALL_DIR} \
    -DLLVM_TARGETS_TO_BUILD="X86" \
    -DLLVM_ENABLE_PROJECTS="clang;compiler-rt;libcxx;libcxxabi;libunwind;lld;openmp"
WORKDIR ${LLVM_DIR}/build
RUN make -j$(nproc) install


###########################################################
FROM ubuntu:focal AS llvm-dist
LABEL maintainer="Benjamin Gray <benjamin.gray@anu.edu.au>"

ARG DEBIAN_FRONTEND=noninteractive

ARG LLVM_INSTALL_DIR

COPY --from=llvm-build ${LLVM_INSTALL_DIR} ${LLVM_INSTALL_DIR}
