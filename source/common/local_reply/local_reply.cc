#include "source/common/local_reply/local_reply.h"

#include <string>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/config/datasource.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"
#include "source/common/matcher/validation_visitor.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/header_parser.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace LocalReply {

class BodyFormatter {
public:
  BodyFormatter() : content_type_(Http::Headers::get().ContentTypeValues.Text) {}

  static absl::StatusOr<std::unique_ptr<BodyFormatter>>
  create(const envoy::config::core::v3::SubstitutionFormatString& config,
         Server::Configuration::GenericFactoryContext& context) {
    auto formatter_or_error =
        Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config, context);
    RETURN_IF_NOT_OK_REF(formatter_or_error.status());
    return std::make_unique<BodyFormatter>(config, std::move(*formatter_or_error));
  }

  BodyFormatter(const envoy::config::core::v3::SubstitutionFormatString& config,
                Formatter::FormatterPtr&& formatter)
      : formatter_(std::move(formatter)),
        content_type_(
            !config.content_type().empty() ? config.content_type()
            : config.format_case() ==
                    envoy::config::core::v3::SubstitutionFormatString::FormatCase::kJsonFormat
                ? Http::Headers::get().ContentTypeValues.Json
                : Http::Headers::get().ContentTypeValues.Text) {}

  void format(const Http::RequestHeaderMap& request_headers,
              const Http::ResponseHeaderMap& response_headers,
              const Http::ResponseTrailerMap& response_trailers,
              const StreamInfo::StreamInfo& stream_info, std::string& body,
              absl::string_view& content_type) const {
    // No specific formatter is provided and the default formatter %LOCAL_REPLY_BODY% will
    // be used. That means the body will be the same as the original body and we don't need
    // to format it.
    if (formatter_ != nullptr) {
      body = formatter_->format({&request_headers, &response_headers, &response_trailers, body},
                                stream_info);
    }
    content_type = content_type_;
  }

private:
  const Formatter::FormatterPtr formatter_;
  const std::string content_type_;
};

using BodyFormatterPtr = std::unique_ptr<BodyFormatter>;
using HeaderParserPtr = std::unique_ptr<Envoy::Router::HeaderParser>;

// Applies the optional status/body/headers/formatter overrides from a single match result.
static void applyRewrite(absl::optional<Http::Code> status_code,
                         const absl::optional<std::string>& body_override,
                         const HeaderParserPtr& header_parser, BodyFormatter* override_formatter,
                         const Http::RequestHeaderMap& request_headers,
                         Http::ResponseHeaderMap& response_headers,
                         StreamInfo::StreamInfo& stream_info, Http::Code& code, std::string& body,
                         BodyFormatter*& final_formatter) {
  if (body_override.has_value()) {
    body = body_override.value();
  }
  if (header_parser != nullptr) {
    header_parser->evaluateHeaders(response_headers, {&request_headers, &response_headers},
                                   stream_info);
  }
  if (status_code.has_value() && code != status_code.value()) {
    code = status_code.value();
    response_headers.setStatus(std::to_string(enumToInt(code)));
    stream_info.setResponseCode(static_cast<uint32_t>(code));
  }
  if (override_formatter != nullptr) {
    final_formatter = override_formatter;
  }
}

class ResponseMapper {
public:
  static absl::StatusOr<std::unique_ptr<ResponseMapper>>
  create(const envoy::extensions::filters::network::http_connection_manager::v3::ResponseMapper&
             config,
         Server::Configuration::FactoryContext& context) {
    absl::Status creation_status = absl::OkStatus();
    auto ret = std::make_unique<ResponseMapper>(config, context, creation_status);
    RETURN_IF_NOT_OK(creation_status);
    return ret;
  }

  ResponseMapper(
      const envoy::extensions::filters::network::http_connection_manager::v3::ResponseMapper&
          config,
      Server::Configuration::FactoryContext& context, absl::Status& creation_status)
      : filter_(AccessLog::FilterFactory::fromProto(config.filter(), context)) {
    if (config.has_status_code()) {
      status_code_ = static_cast<Http::Code>(config.status_code().value());
    }
    if (config.has_body()) {
      auto body_or_error =
          Config::DataSource::read(config.body(), true, context.serverFactoryContext().api());
      SET_AND_RETURN_IF_NOT_OK(body_or_error.status(), creation_status);
      body_ = *body_or_error;
    }

    if (config.has_body_format_override()) {
      auto formatter_or_error = BodyFormatter::create(config.body_format_override(), context);
      SET_AND_RETURN_IF_NOT_OK(formatter_or_error.status(), creation_status);
      body_formatter_ = std::move(*formatter_or_error);
    }

    auto parser_or_error = Envoy::Router::HeaderParser::configure(config.headers_to_add());
    SET_AND_RETURN_IF_NOT_OK(parser_or_error.status(), creation_status);
    header_parser_ = std::move(*parser_or_error);
  }

  bool matchAndRewrite(const Http::RequestHeaderMap& request_headers,
                       Http::ResponseHeaderMap& response_headers,
                       const Http::ResponseTrailerMap& response_trailers,
                       StreamInfo::StreamInfo& stream_info, Http::Code& code, std::string& body,
                       BodyFormatter*& final_formatter) const {
    // If not matched, just bail out.
    if (filter_ == nullptr ||
        !filter_->evaluate({&request_headers, &response_headers, &response_trailers},
                           stream_info)) {
      return false;
    }
    applyRewrite(status_code_, body_, header_parser_, body_formatter_.get(), request_headers,
                 response_headers, stream_info, code, body, final_formatter);
    return true;
  }

private:
  const AccessLog::FilterPtr filter_;
  absl::optional<Http::Code> status_code_;
  absl::optional<std::string> body_;
  HeaderParserPtr header_parser_;
  BodyFormatterPtr body_formatter_;
};

using ResponseMapperPtr = std::unique_ptr<ResponseMapper>;

// Action used by the unified matcher path of ``LocalReplyConfig``.
class LocalReplyMatchAction
    : public Matcher::ActionBase<envoy::extensions::filters::network::http_connection_manager::v3::
                                     LocalReplyMapperAction> {
public:
  static absl::StatusOr<std::shared_ptr<LocalReplyMatchAction>>
  create(const envoy::extensions::filters::network::http_connection_manager::v3::
             LocalReplyMapperAction& config,
         Server::Configuration::FactoryContext& context) {
    absl::Status creation_status = absl::OkStatus();
    auto ret = std::make_shared<LocalReplyMatchAction>(config, context, creation_status);
    RETURN_IF_NOT_OK(creation_status);
    return ret;
  }

  LocalReplyMatchAction(const envoy::extensions::filters::network::http_connection_manager::v3::
                            LocalReplyMapperAction& config,
                        Server::Configuration::FactoryContext& context,
                        absl::Status& creation_status) {
    if (config.has_status_code()) {
      status_code_ = static_cast<Http::Code>(config.status_code().value());
    }
    if (config.has_body()) {
      auto body_or_error =
          Config::DataSource::read(config.body(), true, context.serverFactoryContext().api());
      SET_AND_RETURN_IF_NOT_OK(body_or_error.status(), creation_status);
      body_ = *body_or_error;
    }
    if (config.has_body_format_override()) {
      auto formatter_or_error = BodyFormatter::create(config.body_format_override(), context);
      SET_AND_RETURN_IF_NOT_OK(formatter_or_error.status(), creation_status);
      body_formatter_ = std::move(*formatter_or_error);
    }
    auto parser_or_error = Envoy::Router::HeaderParser::configure(config.headers_to_add());
    SET_AND_RETURN_IF_NOT_OK(parser_or_error.status(), creation_status);
    header_parser_ = std::move(*parser_or_error);
  }

  void apply(const Http::RequestHeaderMap& request_headers,
             Http::ResponseHeaderMap& response_headers, StreamInfo::StreamInfo& stream_info,
             Http::Code& code, std::string& body, BodyFormatter*& final_formatter) const {
    applyRewrite(status_code_, body_, header_parser_, body_formatter_.get(), request_headers,
                 response_headers, stream_info, code, body, final_formatter);
  }

private:
  absl::optional<Http::Code> status_code_;
  absl::optional<std::string> body_;
  HeaderParserPtr header_parser_;
  BodyFormatterPtr body_formatter_;
};

// Action factory context for ``LocalReplyMatchAction``.
struct LocalReplyMatchActionContext {
  Server::Configuration::FactoryContext& factory_context_;
};

class LocalReplyMatchActionFactory : public Matcher::ActionFactory<LocalReplyMatchActionContext> {
public:
  std::string name() const override { return "envoy.matching.actions.local_reply_mapper"; }

  Matcher::ActionConstSharedPtr
  createAction(const Protobuf::Message& config, LocalReplyMatchActionContext& context,
               ProtobufMessage::ValidationVisitor& validator) override {
    const auto& typed_config =
        MessageUtil::downcastAndValidate<const envoy::extensions::filters::network::
                                             http_connection_manager::v3::LocalReplyMapperAction&>(
            config, validator);
    auto action_or_error = LocalReplyMatchAction::create(typed_config, context.factory_context_);
    THROW_IF_NOT_OK_REF(action_or_error.status());
    return std::move(*action_or_error);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyMapperAction>();
  }
};

REGISTER_FACTORY(LocalReplyMatchActionFactory,
                 Matcher::ActionFactory<LocalReplyMatchActionContext>);

class MatchTreeValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Envoy::Http::HttpMatchingData> {
public:
  absl::Status
  performDataInputValidation(const Matcher::DataInputFactory<Envoy::Http::HttpMatchingData>&,
                             absl::string_view) override {
    return absl::OkStatus();
  }
};

static absl::StatusOr<Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData>>
createMatchTree(const xds::type::matcher::v3::Matcher& matcher_config,
                Server::Configuration::FactoryContext& context) {
  LocalReplyMatchActionContext action_context{context};
  MatchTreeValidationVisitor validator;
  Matcher::MatchTreeFactory<Envoy::Http::HttpMatchingData, LocalReplyMatchActionContext> factory(
      action_context, context.serverFactoryContext(), validator);
  auto factory_cb = factory.create(matcher_config);
  if (!validator.errors().empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("requirement violation while creating local reply match tree: ",
                     validator.errors()[0].message()));
  }
  return factory_cb();
}

class LocalReplyImpl : public LocalReply {
public:
  LocalReplyImpl() : body_formatter_(std::make_unique<BodyFormatter>()) {}

  static absl::StatusOr<std::unique_ptr<LocalReplyImpl>>
  create(const envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig&
             config,
         Server::Configuration::FactoryContext& context) {
    absl::Status creation_status = absl::OkStatus();
    auto ret = std::make_unique<LocalReplyImpl>(config, context, creation_status);
    RETURN_IF_NOT_OK(creation_status);
    return ret;
  }

  LocalReplyImpl(
      const envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig&
          config,
      Server::Configuration::FactoryContext& context, absl::Status& creation_status) {
    if (!config.has_body_format()) {
      body_formatter_ = std::make_unique<BodyFormatter>();
    } else {
      auto formatter_or_error = BodyFormatter::create(config.body_format(), context);
      SET_AND_RETURN_IF_NOT_OK(formatter_or_error.status(), creation_status);
      body_formatter_ = std::move(*formatter_or_error);
    }

    if (config.has_matcher() && !config.mappers().empty()) {
      creation_status = absl::InvalidArgumentError(
          "Only one of 'mappers' or 'matcher' can be set in LocalReplyConfig.");
      return;
    }

    if (config.has_matcher()) {
      auto match_tree_or_error = createMatchTree(config.matcher(), context);
      SET_AND_RETURN_IF_NOT_OK(match_tree_or_error.status(), creation_status);
      match_tree_ = std::move(*match_tree_or_error);
      return;
    }

    mappers_.reserve(config.mappers().size());
    for (const auto& mapper : config.mappers()) {
      auto mapper_or_error = ResponseMapper::create(mapper, context);
      SET_AND_RETURN_IF_NOT_OK(mapper_or_error.status(), creation_status);
      mappers_.emplace_back(std::move(*mapper_or_error));
    }
  }

  void rewrite(const Http::RequestHeaderMap* request_headers,
               Http::ResponseHeaderMap& response_headers, StreamInfo::StreamInfo& stream_info,
               Http::Code& code, std::string& body,
               absl::string_view& content_type) const override {
    // Set response code to stream_info and response_headers due to:
    // 1) StatusCode filter is using response_code from stream_info,
    // 2) %RESP(:status)% is from Status() in response_headers.
    response_headers.setStatus(std::to_string(enumToInt(code)));
    stream_info.setResponseCode(static_cast<uint32_t>(code));

    if (request_headers == nullptr) {
      request_headers = Http::StaticEmptyHeaders::get().request_headers.get();
    }

    BodyFormatter* final_formatter{};
    if (match_tree_ != nullptr) {
      Envoy::Http::Matching::HttpMatchingDataImpl matching_data(stream_info);
      matching_data.onRequestHeaders(*request_headers);
      matching_data.onResponseHeaders(response_headers);
      auto result =
          Matcher::evaluateMatch<Envoy::Http::HttpMatchingData>(*match_tree_, matching_data);
      if (result.isMatch()) {
        const auto* action = dynamic_cast<const LocalReplyMatchAction*>(result.action().get());
        if (action != nullptr) {
          action->apply(*request_headers, response_headers, stream_info, code, body,
                        final_formatter);
        }
      }
    } else {
      for (const auto& mapper : mappers_) {
        if (mapper->matchAndRewrite(*request_headers, response_headers,
                                    *Http::StaticEmptyHeaders::get().response_trailers, stream_info,
                                    code, body, final_formatter)) {
          break;
        }
      }
    }

    if (!final_formatter) {
      final_formatter = body_formatter_.get();
    }
    return final_formatter->format(*request_headers, response_headers,
                                   *Http::StaticEmptyHeaders::get().response_trailers, stream_info,
                                   body, content_type);
  }

private:
  std::vector<ResponseMapperPtr> mappers_;
  Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree_;
  BodyFormatterPtr body_formatter_;
};

LocalReplyPtr Factory::createDefault() { return std::make_unique<LocalReplyImpl>(); }

absl::StatusOr<LocalReplyPtr> Factory::create(
    const envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig&
        config,
    Server::Configuration::FactoryContext& context) {
  return LocalReplyImpl::create(config, context);
}

} // namespace LocalReply
} // namespace Envoy
