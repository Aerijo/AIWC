ARG LLVM_INSTALL_DIR=/llvm

ARG OCLGRIND_INSTALL_DIR=/oclgrind

ARG OCLGRIND_DIR=/src/oclgrind

###########################################################
FROM benjamin/llvm AS oclgrind-build
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update -yq && apt-get install -yq \
    cmake \
    git \
    libreadline-dev \
    libstdc++-10-dev \
    libtinfo-dev \
    make \
    python3

ARG OCLGRIND_DIR
WORKDIR ${OCLGRIND_DIR}
COPY . .

ARG OCLGRIND_INSTALL_DIR
ARG LLVM_INSTALL_DIR

ENV CC=/llvm/bin/clang
ENV CXX=/llvm/bin/clang++
RUN cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DLLVM_DIR=${LLVM_INSTALL_DIR}/lib/cmake/llvm \
    -DCLANG_ROOT=${LLVM_INSTALL_DIR} \
    -DCMAKE_INSTALL_PREFIX=${OCLGRIND_INSTALL_DIR} \
    -DBUILD_SHARED_LIBS=On
WORKDIR ${OCLGRIND_DIR}/build
RUN make -j$(nproc) install


###########################################################
FROM benjamin/llvm AS oclgrind-dist
ARG DEBIAN_FRONTEND=noninteractive

ARG OCLGRIND_INSTALL_DIR

COPY --from=oclgrind-build ${OCLGRIND_INSTALL_DIR} ${OCLGRIND_INSTALL_DIR}

RUN mkdir -p /etc/OpenCL/vendors && echo "${OCLGRIND_INSTALL_DIR}/lib/liboclgrind-rt-icd.so\n" > /etc/OpenCL/vendors/oclgrind.icd
