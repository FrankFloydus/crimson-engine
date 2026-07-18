////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "operation_test_helpers.hpp"

#include <quader/modeling/neutral/polygon/document_validation.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>

namespace qdr_test {

namespace detail {

template <typename ValidationResult>
[[nodiscard]] bool polygon_document_validation_passed(const ValidationResult& result)
{
	if constexpr (requires { result.valid; }) {
		return result.valid;
	} else if constexpr (requires { result.ok(); }) {
		return result.ok();
	} else if constexpr (requires { result.ok; }) {
		return result.ok;
	} else if constexpr (requires { result.diagnostics; }) {
		return result.diagnostics.empty();
	} else if constexpr (requires { result.empty(); }) {
		return result.empty();
	} else {
		return static_cast<bool>(result);
	}
}

template <typename Diagnostic>
void append_polygon_document_diagnostic(std::ostringstream& stream, const Diagnostic& diagnostic)
{
	if constexpr (requires { diagnostic.message; }) {
		stream << diagnostic.message;
	} else if constexpr (requires { diagnostic.message(); }) {
		stream << diagnostic.message();
	} else {
		stream << "polygon document diagnostic";
	}
}

template <typename Diagnostics>
void append_polygon_document_diagnostics(std::ostringstream& stream, const Diagnostics& diagnostics)
{
	bool first = true;
	for (const auto& diagnostic : diagnostics) {
		if (!first) {
			stream << "; ";
		}
		first = false;
		append_polygon_document_diagnostic(stream, diagnostic);
	}
}

template <typename ValidationResult>
[[nodiscard]] std::string polygon_document_validation_summary(const ValidationResult& result)
{
	std::ostringstream stream;
	if constexpr (requires { result.diagnostics; }) {
		append_polygon_document_diagnostics(stream, result.diagnostics);
	} else if constexpr (requires { result.begin(); result.end(); }) {
		append_polygon_document_diagnostics(stream, result);
	} else if constexpr (requires { result.message; }) {
		stream << result.message;
	}
	const std::string summary = stream.str();
	return summary.empty() ? "central polygon validator reported an invalid document" : summary;
}

} // namespace detail

inline void expect_valid_polygon_document(const quader_poly::Document& document, std::string_view context)
{
	const auto validation = quader_poly::validate_polygon_document(document);
	EXPECT_TRUE(detail::polygon_document_validation_passed(validation))
		<< context << " failed central polygon document validation: "
		<< detail::polygon_document_validation_summary(validation);
}

inline void expect_polygon_document_validation_issue(
	const quader_poly::Document& document,
	quader_poly::PolygonDocumentDiagnosticCode code,
	std::string_view context)
{
	const auto validation = quader_poly::validate_polygon_document(document);
	const bool found = std::ranges::any_of(
		validation.diagnostics,
		[code](const quader_poly::PolygonDocumentDiagnostic& diagnostic) {
			return diagnostic.code == code;
		});
	EXPECT_TRUE(found) << context << " did not report the expected central polygon document diagnostic. "
					   << "Reported diagnostics: " << detail::polygon_document_validation_summary(validation);
}

inline void expect_valid_polygon_document_after_success(
	const quader_poly::Document& document,
	const quader_poly::OperationResult& result,
	std::string_view context = "successful polygon operation")
{
	if (!result.changed) {
		return;
	}
	expect_valid_polygon_document(document, context);
}

inline void expect_polygon_document_validation_issue_after_success(
	const quader_poly::Document& document,
	const quader_poly::OperationResult& result,
	quader_poly::PolygonDocumentDiagnosticCode code,
	std::string_view context = "successful polygon operation")
{
	if (!result.changed) {
		return;
	}
	expect_polygon_document_validation_issue(document, code, context);
}

inline void expect_valid_polygon_document_after_success(
	const quader_poly::Document& document,
	const quader_poly::ExtractFacesResult& result,
	std::string_view context = "successful extract-faces source document")
{
	if (!result.changed) {
		return;
	}
	expect_valid_polygon_document(document, context);
	expect_valid_polygon_document(result.extracted_document, "successful extract-faces extracted document");
}

inline void expect_valid_polygon_document_after_success(
	const quader_poly::PlaneCutResult& result,
	std::string_view context = "successful plane-cut result")
{
	if (!result.changed) {
		return;
	}
	std::string front_context { context };
	front_context += " front document";
	std::string back_context { context };
	back_context += " back document";
	expect_valid_polygon_document(result.front_document, front_context);
	expect_valid_polygon_document(result.back_document, back_context);
}

} // namespace qdr_test
