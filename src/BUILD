cc_binary(
    name = "protoc_gen_rust_grpc",
    srcs = [
    "rust_plugin.cc",
    "rust_generator.h",
    "rust_generator.cc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_protobuf//:protoc_lib",
    ],
)

