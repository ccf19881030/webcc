#include "webcc/http_request_parser.h"

#include <vector>

#include "boost/algorithm/string.hpp"

#include "webcc/http_request.h"
#include "webcc/logger.h"

namespace webcc {

HttpRequestParser::HttpRequestParser(HttpRequest* request)
    : HttpParser(request), request_(request) {
}

void HttpRequestParser::Init(HttpRequest* request) {
  HttpParser::Init(request);
  request_ = request;
}

bool HttpRequestParser::ParseStartLine(const std::string& line) {
  std::vector<std::string> strs;
  boost::split(strs, line, boost::is_any_of(" "), boost::token_compress_on);

  if (strs.size() != 3) {
    return false;
  }

  request_->set_method(std::move(strs[0]));
  request_->set_url(std::move(strs[1]));

  // HTTP version is ignored.

  return true;
}

bool HttpRequestParser::ParseContent(const char* data, std::size_t length) {
  if (chunked_) {
    return ParseChunkedContent(data, length);
  } else {
    if (request_->content_type().multipart()) {
      return ParseMultipartContent(data, length);
    } else {
      return ParseFixedContent(data, length);
    }
  }
}

bool HttpRequestParser::ParseMultipartContent(const char* data,
                                              std::size_t length) {
  // Append the new data to the pending data.
  // NOTE: It's more difficult to avoid this than normal fixed-length content.
  pending_data_.append(data, length);

  LOG_VERB("Parse multipart content (pending data size: %u).",
           pending_data_.size());

  if (!content_length_parsed_ || content_length_ == kInvalidLength) {
    // Invalid content length (syntax error).
    return false;
  }

  while (true) {
    if (pending_data_.empty()) {
      // Wait data from next read.
      break;
    }

    if (step_ == Step::kStart) {
      std::string line;
      if (!GetNextLine(0, &line, true)) {
        break;  // Not enough data
      }
      if (!IsBoundary(line)) {
        LOG_ERRO("Invalid boundary: %s", line.c_str());
        return false;
      }
      LOG_INFO("Boundary line: %s", line.c_str());
      // Go to next step.
      step_ = Step::kBoundaryParsed;
      continue;
    }

    if (step_ == Step::kBoundaryParsed) {
      bool need_more_data = false;
      if (ParsePartHeaders(&need_more_data)) {
        // Go to next step.
        step_ = Step::kHeadersParsed;
        LOG_INFO("Part headers just ended.");
        continue;
      } else {
        if (need_more_data) {
          // Need more data from next read.
          break;
        } else {
          return false;
        }
      }
    }

    if (step_ == Step::kHeadersParsed) {
      std::size_t off = 0;
      std::size_t count = 0;
      bool ended = false;
      if (!GetNextBoundaryLine(&off, &count, &ended)) {
        // All pending data belongs to this part.
        part_.AppendData(pending_data_);
        pending_data_.clear();
        break;
      }

      LOG_INFO("Next boundary has been found.");

      // This part has ended.
      if (off > 2) {
        // -2 for exluding the CRLF after the data.
        part_.AppendData(pending_data_.data(), off - 2);
      }

      request_->AddFormPart(std::move(part_));

      if (ended) {
        // Go to the end step.
        step_ = Step::kEnded;
        break;
      } else {
        // Go to next step.
        step_ = Step::kBoundaryParsed;
        continue;
      }
    }
  }

  if (step_ == Step::kEnded) {
    LOG_INFO("Multipart data has ended.");
    Finish();
  }

  return true;
}

bool HttpRequestParser::ParsePartHeaders(bool* need_more_data) {
  std::size_t off = 0;

  while (true) {
    std::string line;
    if (!GetNextLine(off, &line, false)) {
      // Need more data from next read.
      *need_more_data = true;
      return false;
    }

    off = off + line.size() + 2;  // +2 for CRLF

    if (line.empty()) {
      // Headers finished.
      break;
    }

    HttpHeader header;
    if (!Split2(line, ':', &header.first, &header.second)) {
      LOG_ERRO("Invalid part header line: %s", line.c_str());
      return false;
    }

    LOG_INFO("Part header (%s: %s).", header.first.c_str(),
             header.second.c_str());

    // Parse Content-Disposition.
    if (boost::iequals(header.first, http::headers::kContentDisposition)) {
      ContentDisposition content_disposition(header.second);
      if (!content_disposition.valid()) {
        LOG_ERRO("Invalid content-disposition header: %s",
                 header.second.c_str());
        return false;
      }
      part_.set_name(content_disposition.name());
      part_.set_file_name(content_disposition.file_name());
      LOG_INFO("Content-Disposition (name=%s; filename=%s)",
               part_.name().c_str(), part_.file_name().c_str());
    }

    // TODO: Parse other headers.
  }

  // Remove the data which has just been parsed.
  pending_data_.erase(0, off);

  return true;
}

bool HttpRequestParser::GetNextBoundaryLine(std::size_t* b_off,
                                            std::size_t* b_count,
                                            bool* ended) {
  std::size_t off = 0;

  while (true) {
    std::size_t pos = pending_data_.find(kCRLF, off);
    if (pos == std::string::npos) {
      break;
    }

    std::size_t count = pos - off;
    if (count == 0) {
      off = pos + 2;
      continue;  // Empty line
    }

    // TODO: Avoid temp string.
    std::string line = pending_data_.substr(off, count);

    if (IsBoundary(line)) {
      *b_off = off;
      *b_count = count;
      return true;
    }

    if (IsBoundaryEnd(line)) {
      *b_off = off;
      *b_count = count;
      *ended = true;
      return true;
    }

    off = pos + 2;
  }

  return false;
}

bool HttpRequestParser::IsBoundary(const std::string& line) const {
  if (line == "--" + request_->content_type().boundary()) {
    return true;
  }
  return false;
}

bool HttpRequestParser::IsBoundaryEnd(const std::string& line) const {
  if (line == "--" + request_->content_type().boundary() + "--") {
    return true;
  }
  return false;
}

}  // namespace webcc
