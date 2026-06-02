# Buccaneer Test Container
# Runs tests with memory limits to prevent OOM issues

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    gcc \
    make \
    libc6-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /buccaneer

# Copy source files (build context is parent directory)
COPY . /buccaneer/

# Build the library first
RUN make clean && make

# Build tests
RUN cd tests && make clean && make

# Default command runs tests with timeout
CMD ["sh", "-c", "cd /buccaneer/tests && timeout 30 ./test_value && timeout 30 ./test_lexer && timeout 10 ./test_parser && timeout 30 ./test_package && timeout 30 ./test_vm && timeout 60 ./test_e2e && echo 'All tests completed'"]
