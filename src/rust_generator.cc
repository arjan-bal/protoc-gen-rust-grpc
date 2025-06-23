#include "src/rust_generator.h"

#include "absl/strings/string_view.h"
#include <google/protobuf/compiler/rust/context.h>
#include <google/protobuf/compiler/rust/naming.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/stubs/common.h>
#include <utility>
#include <vector>

namespace rust_grpc_generator {
namespace protobuf = google::protobuf;
namespace rust = protobuf::compiler::rust;

using protobuf::Descriptor;
using protobuf::MethodDescriptor;
using protobuf::ServiceDescriptor;
using protobuf::SourceLocation;

// Writes the generated service interface into the given ZeroCopyOutputStream.
void GenerateService(
    const ServiceDescriptor *service, protobuf::io::ZeroCopyOutputStream *out,
    protobuf::compiler::rust::Context &rust_generator_context) {}

template <typename DescriptorType>
static std::string
GrpcGetCommentsForDescriptor(const DescriptorType *descriptor) {
  SourceLocation location;
  if (descriptor->GetSourceLocation(&location)) {
    return location.leading_comments.empty() ? location.trailing_comments
                                             : location.leading_comments;
  }
  return std::string();
}

/**
 * Method generation abstraction.
 *
 * Each service contains a set of generic methods that will be used by codegen
 * to generate abstraction implementations for the provided methods.
 */
class Method {
private:
  const MethodDescriptor *method_;

public:
  Method() = delete;

  Method(const MethodDescriptor *method) : method_(method) {}

  /// The name of the method in Rust style.
  std::string name() const {
    return rust::RsSafeName(rust::CamelToSnakeCase(method_->name()));
  };

  /// The fully-qualified name of the method, scope delimited by periods.
  absl::string_view full_name() const { return method_->full_name(); }

  /// The name of the method as it appears in the .proto file.
  absl::string_view identifier() const { return method_->name(); };

  /// Checks if the method is streamed by the client.
  bool is_client_streaming() const { return method_->client_streaming(); };

  /// Checks if the method is streamed by the server.
  bool is_server_streaming() const { return method_->server_streaming(); };

  /// Get comments about this method.
  std::string comment() const { return GrpcGetCommentsForDescriptor(method_); };

  /// Checks if the method is deprecated. Default is false.
  bool is_deprecated() const { return method_->options().deprecated(); }

  /**
   * Type name of request and response.
   * @param proto_path The path to the proto file, for context.
   * @return A pair of strings representing the generated request and response
   * type names.
   */
  std::pair<std::string, std::string>
  get_request_response_name(absl::string_view proto_path,
                            rust::Context &ctx) const {
    const Descriptor *input = method_->input_type();
    const Descriptor *output = method_->output_type();
    const std::string request_type = rust::RsTypePath(ctx, *input);
    const std::string response_type = rust::RsTypePath(ctx, *output);
    return std::make_pair(request_type, response_type);
  };
};

/**
 * Service generation abstraction.
 *
 * This class is an interface that can be implemented and consumed
 * by client and server generators to allow any codegen module
 * to generate service abstractions.
 */
class Service {
private:
  const ServiceDescriptor *service_;

public:
  Service() = delete;

  Service(const ServiceDescriptor *service) : service_(service) {}

  /// The name of the service, not including its containing scope.
  std::string name() const {
    return rust::RsSafeName(rust::SnakeToUpperCamelCase(service_->name()));
  };

  /// The fully-qualified name of the service, scope delimited by periods.
  absl::string_view full_name() const { return service_->full_name(); };

  /**
   * Methods provided by the service.
   * @return A span of non-owning pointers to the Method objects. The Service
   * implementation is expected to manage the lifetime of these objects.
   */
  std::vector<const Method *> methods() const {
    std::vector<const Method *> ret;
    int methods_count = service_->method_count();
    ret.reserve(methods_count);
    for (int i = 0; i < methods_count; ++i) {
      ret.push_back(new Method(service_->method(i)));
    }
    return ret;
  };

  /// Get comments about this service.
  virtual std::string comment() const {
    return GrpcGetCommentsForDescriptor(service_);
  };
};

/**
 * @brief Formats the full path for a method call.
 * @param service The service containing the method.
 * @param method The method to format the path for.
 * @param emit_package If true, the service name will include its package.
 * @return The formatted method path (e.g., "/package.MyService/MyMethod").
 */
static std::string FormatMethodPath(const Service &service,
                                    const Method &method, bool emit_package) {
  return absl::StrFormat("/%s/%s", service.full_name(), method.identifier());
}
} // namespace rust_grpc_generator
