#include "rust_generator.h"
#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/compiler/plugin.h>
#include <google/protobuf/compiler/rust/context.h>
#include <google/protobuf/compiler/rust/crate_mapping.h>
#include <google/protobuf/compiler/rust/naming.h>

namespace protobuf = google::protobuf;
namespace rust = google::protobuf::compiler::rust;

class RustGrpcGenerator : public protobuf::compiler::CodeGenerator {
public:
  // Protobuf 5.27 released edition 2023.
#if GOOGLE_PROTOBUF_VERSION >= 5027000
  uint64_t GetSupportedFeatures() const override {
    return Feature::FEATURE_PROTO3_OPTIONAL |
           Feature::FEATURE_SUPPORTS_EDITIONS;
  }
  protobuf::Edition GetMinimumEdition() const override {
    return protobuf::Edition::EDITION_PROTO2;
  }
  protobuf::Edition GetMaximumEdition() const override {
    return protobuf::Edition::EDITION_2023;
  }
#else
  uint64_t GetSupportedFeatures() const override {
    return Feature::FEATURE_PROTO3_OPTIONAL;
  }
#endif

  bool Generate(const protobuf::FileDescriptor *file,
                const std::string &parameter,
                protobuf::compiler::GeneratorContext *context,
                std::string *error) const override {
    // Return early to avoid creating an empty output file.
    if (file->service_count() == 0) {
      return true;
    }
    std::vector<std::pair<std::string, std::string>> options;
    protobuf::compiler::ParseGeneratorParameter(parameter, &options);

    // Copied from protobuf rust's generator.cc.
    absl::StatusOr<rust::Options> opts = rust::Options::Parse(parameter);
    if (!opts.ok()) {
      *error = std::string(opts.status().message());
      return false;
    }

    std::vector<const protobuf::FileDescriptor *> files_in_current_crate;
    context->ListParsedFiles(&files_in_current_crate);

    absl::StatusOr<absl::flat_hash_map<std::string, std::string>>
        import_path_to_crate_name = rust::GetImportPathToCrateNameMap(&*opts);
    if (!import_path_to_crate_name.ok()) {
      *error = std::string(import_path_to_crate_name.status().message());
      return false;
    }

    rust::RustGeneratorContext rust_generator_context(
        &files_in_current_crate, &*import_path_to_crate_name);
    std::vector<std::string> modules;

    modules.emplace_back(rust::RustInternalModuleName(*file));
    rust::Context ctx_without_printer(&*opts, &rust_generator_context, nullptr,
                                      std::move(modules));
    auto outfile = absl::WrapUnique(
        context->Open(rust_grpc_generator::GetRsGrpcFile(*file)));
    protobuf::io::Printer printer(outfile.get());
    rust::Context ctx = ctx_without_printer.WithPrinter(&printer);

    for (int i = 0; i < file->service_count(); ++i) {
      const protobuf::ServiceDescriptor *service = file->service(i);
      rust_grpc_generator::GenerateService(ctx, service);
    }
    return true;
  }
};

int main(int argc, char *argv[]) {
  RustGrpcGenerator generator;
  return protobuf::compiler::PluginMain(argc, argv, &generator);
  return 0;
}
