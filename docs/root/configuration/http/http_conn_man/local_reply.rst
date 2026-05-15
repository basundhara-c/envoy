.. _config_http_conn_man_local_reply:

Local reply modification
========================

The :ref:`HTTP connection manager <arch_overview_http_conn_man>` supports modification of local reply which is response returned by Envoy itself.

Features:

* :ref:`Local reply content modification<config_http_conn_man_local_reply_modification>`.
* :ref:`Local reply format modification<config_http_conn_man_local_reply_format>`.

.. _config_http_conn_man_local_reply_modification:

Local reply content modification
--------------------------------

The local response content returned by Envoy can be customized. A list of :ref:`mappers <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.LocalReplyConfig.mappers>` can be specified. Each mapper must have a :ref:`filter <envoy_v3_api_field_config.accesslog.v3.AccessLog.filter>`. It may have following rewrite rules; a :ref:`status_code <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.status_code>` rule to rewrite response code, a :ref:`headers_to_add <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.headers_to_add>` rule to add/override/append response HTTP headers, a :ref:`body <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.body>` rule to rewrite the local reply body and a :ref:`body_format_override <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.body_format_override>` to specify the response body format. Envoy checks each ``mapper`` according to the specified order until the first one is matched. If a ``mapper`` is matched, all its rewrite rules will apply in the following order:

1. ``body`` — the static response body text is set.
2. ``headers_to_add`` — response headers are evaluated. Substitution variables such as ``%RESPONSE_CODE%`` resolve to the **original** response code at this point.
3. ``status_code`` — the response code is rewritten. This updates both the response headers and the stream info.
4. ``body_format_override`` (or the fallback ``body_format``) — the response body is formatted. Substitution variables such as ``%RESPONSE_CODE%`` resolve to the **overridden** response code.

Because of this ordering, ``%RESPONSE_CODE%`` can have different values in ``headers_to_add`` (original code) and ``body_format_override`` (overridden code). If you need the original response code in the body, you can capture it in a response header via ``headers_to_add`` and reference it in the body format using ``%RESP(header-name)%``.

Example of a LocalReplyConfig

.. code-block:: yaml

  mappers:
  - filter:
      status_code_filter:
        comparison:
          op: EQ
          value:
            default_value: 400
            runtime_key: key_b
    headers_to_add:
      - header:
          key: "foo"
          value: "bar"
        append_action: OVERWRITE_IF_EXISTS_OR_ADD
    status_code: 401
    body:
      inline_string: "not allowed"

In above example, if the status_code is 400,  it will be rewritten to 401, the response body will be rewritten to as "not allowed".

.. _config_http_conn_man_local_reply_format:

Local reply format modification
-------------------------------

The response body content type can be customized. If not specified, the content type is plain/text. There are two ``body_format`` fields; one is the :ref:`body_format <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.LocalReplyConfig.body_format>` field in the :ref:`LocalReplyConfig <envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.LocalReplyConfig>` message and the other :ref:`body_format_override <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.body_format_override>` field in the ``mapper``. The latter is only used when its mapper is matched. The former is used if there is no any matched mappers, or the matched mapper doesn't have the ``body_format`` specified.

Local reply format can be specified as :ref:`SubstitutionFormatString <envoy_v3_api_msg_config.core.v3.SubstitutionFormatString>`. It supports :ref:`text_format <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.text_format>` and :ref:`json_format <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.json_format>`.

Optionally, content-type can be modified further via :ref:`content_type <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.content_type>` field. If not specified, default content-type is ``text/plain`` for :ref:`text_format <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.text_format>` and ``application/json`` for :ref:`json_format <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.json_format>`.

Example of a LocalReplyConfig with ``body_format`` field.

.. code-block:: yaml

  mappers:
  - filter:
      status_code_filter:
        comparison:
          op: EQ
          value:
            default_value: 400
            runtime_key: key_b
    status_code: 401
    body_format_override:
      text_format: "<h1>%LOCAL_REPLY_BODY% %REQ(:path)%</h1>"
      content_type: "text/html; charset=UTF-8"
  - filter:
      status_code_filter:
        comparison:
          op: EQ
          value:
            default_value: 500
            runtime_key: key_b
    status_code: 501
  body_format:
    text_format: "%LOCAL_REPLY_BODY% %RESPONSE_CODE%"

In above example, there is a ``body_format_override`` inside the first ``mapper`` with a filter matching ``status_code == 400``. It generates the response body in plain text format by concatenating %LOCAL_REPLY_BODY% with the ``:path`` request header. It is only used when the first mapper is matched. There is a ``body_format`` at the bottom of the config and at the same level as field ``mappers``. It is used when non of the mappers is matched or the matched mapper doesn't have its own ``body_format_override`` specified.

.. _config_http_conn_man_local_reply_matcher:

Local reply matcher
-------------------

As an alternative to the ordered ``mappers`` list, ``LocalReplyConfig`` may use the unified
:ref:`Matcher API <arch_overview_matching_api>` to select a rewrite. The matcher is evaluated
against the HTTP matching data (request headers, response headers and ``StreamInfo``, which
includes the per-request :ref:`filter state <arch_overview_advanced_filter_state_sharing>`). The
matched action must be of type
:ref:`LocalReplyMapperAction <envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.LocalReplyMapperAction>`,
which carries the same ``status_code``, ``body``, ``body_format_override`` and ``headers_to_add``
fields as :ref:`ResponseMapper <envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.ResponseMapper>`.

When the matcher returns no match, the top-level ``body_format`` (if set) still applies. The
``mappers`` and ``matcher`` fields are mutually exclusive and setting both is a configuration
error.

Example using a :ref:`FilterStateInput <envoy_v3_api_msg_extensions.matching.common_inputs.network.v3.FilterStateInput>`
to dispatch on a filter state value previously written by an upstream filter:

.. code-block:: yaml

  matcher:
    matcher_tree:
      input:
        name: app
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.FilterStateInput
          key: route.tag
      exact_match_map:
        map:
          "alpha":
            action:
              name: alpha_local_reply
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.LocalReplyMapperAction
                status_code: 503
                body:
                  inline_string: "alpha error"
          "beta":
            action:
              name: beta_local_reply
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.LocalReplyMapperAction
                status_code: 503
                body:
                  inline_string: "beta error"
  body_format:
    text_format: "%LOCAL_REPLY_BODY%"
