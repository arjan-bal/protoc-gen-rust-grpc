#include "src/rust_generator.h"

#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
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
using protobuf::compiler::rust::Context;

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
  absl::string_view proto_field_name() const { return method_->name(); };

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
  request_response_name(rust::Context &ctx) const {
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
  std::vector<Method> methods() const {
    std::vector<Method> ret;
    int methods_count = service_->method_count();
    ret.reserve(methods_count);
    for (int i = 0; i < methods_count; ++i) {
      ret.push_back(Method(service_->method(i)));
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
                                    const Method &method) {
  return absl::StrFormat("/%s/%s", service.full_name(),
                         method.proto_field_name());
}

static std::string SanitizeForRustDoc(absl::string_view raw_comment) {
  // 1. Escape the escape character itself first.
  std::string sanitized = absl::StrReplaceAll(raw_comment, {{"\\", "\\\\"}});

  // 2. Escape Markdown and Rustdoc special characters.
  sanitized = absl::StrReplaceAll(sanitized, {
                                                 {"`", "\\`"},
                                                 {"*", "\\*"},
                                                 {"_", "\\_"},
                                                 {"[", "\\["},
                                                 {"]", "\\]"},
                                                 {"#", "\\#"},
                                                 {"<", "\\<"},
                                                 {">", "\\>"},
                                             });

  return sanitized;
}

static std::string ProtoCommentToRustDoc(absl::string_view proto_comment) {
  std::string rust_doc;
  std::vector<std::string> lines = absl::StrSplit(proto_comment, '\n');
  for (const std::string &line : lines) {
    // Preserve empty lines.
    if (line.empty()) {
      rust_doc += ("///\n");
    } else {
      rust_doc += absl::StrFormat("/// %s\n", SanitizeForRustDoc(line));
    }
  }
  return rust_doc;
}

static void GenerateDeprecated(Context &ctx) { ctx.Emit("#[deprecated]\n"); }

namespace client {

static void GenerateMethods(const Service &service, Context &ctx) {
  static std::string unary_format = R"rs(
    pub async fn $ident$(
        &mut self,
        request: impl tonic::IntoRequest<$request$>,
    ) -> std::result::Result<tonic::Response<tonic::codec::Streaming<$response$>>, tonic::Status> {
        self.inner.ready().await.map_err(|e| {
            tonic::Status::unknown(format!("Service was not ready: {}", e.into()))
        })?;
        let codec = $codec_name$::default();
        let path = http::uri::PathAndQuery::from_static("$path$");
        let mut req = request.into_request();
        req.extensions_mut().insert(GrpcMethod::new("$service_name$", "$method_name$"));
        self.inner.server_streaming(req, path, codec).await
    }
    )rs";

  static std::string server_streaming_format = R"rs(
        pub async fn $ident$(
            &mut self,
            request: impl tonic::IntoRequest<$request$>,
        ) -> std::result::Result<tonic::Response<tonic::codec::Streaming<$response$>>, tonic::Status> {
            self.inner.ready().await.map_err(|e| {
                tonic::Status::unknown(format!("Service was not ready: {}", e.into()))
            })?;
            let codec = $codec_name$::default();
            let path = http::uri::PathAndQuery::from_static("$path$");
            let mut req = request.into_request();
            req.extensions_mut().insert(GrpcMethod::new("$service_name$", "$method_name$"));
            self.inner.server_streaming(req, path, codec).await
        }
      )rs";

  static std::string client_streaming_format = R"rs(
        pub async fn $ident$(
            &mut self,
            request: impl tonic::IntoStreamingRequest<Message = $request$>
        ) -> std::result::Result<tonic::Response<$response$>, tonic::Status> {
            self.inner.ready().await.map_err(|e| {
                tonic::Status::unknown(format!("Service was not ready: {}", e.into()))
            })?;
            let codec = $codec_name$::default();
            let path = http::uri::PathAndQuery::from_static("$path$");
            let mut req = request.into_streaming_request();
            req.extensions_mut().insert(GrpcMethod::new("$service_name$", "$method_name$"));
            self.inner.client_streaming(req, path, codec).await
        }
      )rs";

  static std::string streaming_format = R"rs(
        pub async fn $ident$(
            &mut self,
            request: impl tonic::IntoStreamingRequest<Message = $request$>
        ) -> std::result::Result<tonic::Response<tonic::codec::Streaming<$response$>>, tonic::Status> {
            self.inner.ready().await.map_err(|e| {
                tonic::Status::unknown(format!("Service was not ready: {}", e.into()))
            })?;
            let codec = $codec_name$::default();
            let path = http::uri::PathAndQuery::from_static("$path$");
            let mut req = request.into_streaming_request();
            req.extensions_mut().insert(GrpcMethod::new("$service_name$", "$method_name$"));
            self.inner.streaming(req, path, codec).await
        }
      )rs";

  const std::vector<Method> methods = service.methods();
  for (const Method &method : methods) {
    ctx.Emit(ProtoCommentToRustDoc(method.comment()));
    if (method.is_deprecated()) {
      GenerateDeprecated(ctx);
    }
    std::pair<std::string, std::string> request_response_types =
        method.request_response_name(ctx);
    {
      auto vars =
          ctx.printer().WithVars({{"codec_name", "grpc::codec::ProtoCodec"},
                                  {"ident", method.name()},
                                  {"request", request_response_types.first},
                                  {"response", request_response_types.second},
                                  {"service_name", service.full_name()},
                                  {"path", FormatMethodPath(service, method)},
                                  {"method_name", method.proto_field_name()}});

      if (!method.is_client_streaming() && !method.is_server_streaming()) {
        ctx.Emit(unary_format);
      } else if (!method.is_client_streaming() &&
                 method.is_server_streaming()) {
        ctx.Emit(server_streaming_format);
      } else if (method.is_client_streaming() &&
                 !method.is_server_streaming()) {
        ctx.Emit(client_streaming_format);
      } else {
        ctx.Emit(streaming_format);
      }
      if (&method != &methods.back()) {
        ctx.Emit("\n");
      }
    }
  }
}

static void generate_client(const Service &service, Context &ctx) {
  std::string service_ident = absl::StrFormat("%sClient", service.name());
  std::string client_mod =
      absl::StrFormat("%s_client", rust::CamelToSnakeCase(service.name()));
  ctx.Emit(
      {
          {"client_mod", client_mod},
          {"service_ident", service_ident},
          {"service_doc",
           [&] { ctx.Emit(ProtoCommentToRustDoc(service.comment())); }},
          {"methods", [&] { GenerateMethods(service, ctx); }},
      },
      R"rs(
      /// Generated client implementations.
      pub mod $client_mod$ {
          #![allow(
              unused_variables,
              dead_code,
              missing_docs,
              clippy::wildcard_imports,
              // will trigger if compression is disabled
              clippy::let_unit_value,
          )]
          use tonic::codegen::*;
          use tonic::codegen::http::Uri;

          $service_doc$
          #[derive(Debug, Clone)]
          pub struct $service_ident$<T> {
              inner: tonic::client::Grpc<T>,
          }

          impl<T> $service_ident$<T>
          where
              T: tonic::client::GrpcService<tonic::body::Body>,
              T::Error: Into<StdError>,
              T::ResponseBody: Body<Data = Bytes> + std::marker::Send  +
              'static, <T::ResponseBody as Body>::Error: Into<StdError> +
              std::marker::Send,
          {
              pub fn new(inner: T) -> Self {
                  let inner = tonic::client::Grpc::new(inner);
                  Self { inner }
              }

              pub fn with_origin(inner: T, origin: Uri) -> Self {
                  let inner = tonic::client::Grpc::with_origin(inner, origin);
                  Self { inner }
              }

              pub fn with_interceptor<F>(inner: T, interceptor: F) ->
              $service_ident$<InterceptedService<T, F>> where
                  F: tonic::service::Interceptor,
                  T::ResponseBody: Default,
                  T: tonic::codegen::Service<
                      http::Request<tonic::body::Body>,
                      Response = http::Response<<T as
                      tonic::client::GrpcService<tonic::body::Body>>::ResponseBody>
                  >,
                  <T as
                  tonic::codegen::Service<http::Request<tonic::body::Body>>>::Error:
                  Into<StdError> + std::marker::Send + std::marker::Sync,
              {
                  $service_ident$::new(InterceptedService::new(inner, interceptor))
              }

              /// Compress requests with the given encoding.
              ///
              /// This requires the server to support it otherwise it might respond with an
              /// error.
              #[must_use]
              pub fn send_compressed(mut self, encoding: CompressionEncoding)
              -> Self {
                  self.inner = self.inner.send_compressed(encoding);
                  self
              }

              /// Enable decompressing responses.
              #[must_use]
              pub fn accept_compressed(mut self, encoding:
              CompressionEncoding) -> Self {
                  self.inner = self.inner.accept_compressed(encoding);
                  self
              }

              /// Limits the maximum size of a decoded message.
              ///
              /// Default: `4MB`
              #[must_use]
              pub fn max_decoding_message_size(mut self, limit: usize) ->
              Self {
                  self.inner = self.inner.max_decoding_message_size(limit);
                  self
              }

              /// Limits the maximum size of an encoded message.
              ///
              /// Default: `usize::MAX`
              #[must_use]
              pub fn max_encoding_message_size(mut self, limit: usize) ->
              Self {
                  self.inner = self.inner.max_encoding_message_size(limit);
                  self
              }

              $methods$
          }
      })rs");
}

} // namespace client

namespace server {} // namespace server

// Writes the generated service interface into the given
// ZeroCopyOutputStream.
void GenerateService(Context &rust_generator_context,
                     const ServiceDescriptor *service_desc) {
  const Service service = Service(service_desc);
  client::generate_client(service, rust_generator_context);
}

std::string GetRsGrpcFile(const protobuf::FileDescriptor &file) {
  absl::string_view basename = absl::StripSuffix(file.name(), ".proto");
  return absl::StrCat(basename, "_grpc.pb.rs");
}

} // namespace rust_grpc_generator
