## Usage example
```sh
# Build the plugin with Bazel
bazel build //src:protoc_gen_rust_grpc

# Set the plugin path
PLUGIN_PATH="$(pwd)/bazel-bin/src/protoc_gen_rust_grpc"

# Run protoc with the Rust and gRPC plugins
protoc \
  --plugin=protoc-gen-grpc-rust="$PLUGIN_PATH" \
  --rust_opt="experimental-codegen=enabled,kernel=upb" \
  --rust_out=./tmp \
  --grpc-rust_opt="experimental-codegen=enabled,kernel=upb" \
  --grpc-rust_out=./tmp \
  routeguide.proto
```
