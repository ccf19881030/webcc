#include "gtest/gtest.h"

#include "boost/algorithm/string.hpp"
#include "json/json.h"

#include "webcc/http_client_session.h"
#include "webcc/logger.h"

// -----------------------------------------------------------------------------

#if (defined(WIN32) || defined(_WIN64))
// You need to set environment variable SSL_CERT_FILE properly to enable
// SSL verification.
bool kSslVerify = false;
#else
bool kSslVerify = true;
#endif

// -----------------------------------------------------------------------------
// JSON helper functions (based on jsoncpp).

// Parse a string to JSON object.
Json::Value StringToJson(const std::string& str) {
  Json::Value json;

  Json::CharReaderBuilder builder;
  std::stringstream stream(str);
  std::string errors;
  if (!Json::parseFromStream(builder, stream, &json, &errors)) {
    std::cerr << errors << std::endl;
  }

  return json;
}

// -----------------------------------------------------------------------------

#if 1

static void AssertGet(webcc::HttpResponsePtr r) {
  Json::Value json = StringToJson(r->content());

  Json::Value args = json["args"];

  EXPECT_EQ(2, args.size());
  EXPECT_EQ("value1", args["key1"].asString());
  EXPECT_EQ("value2", args["key2"].asString());

  Json::Value headers = json["headers"];

  EXPECT_EQ("application/json", headers["Accept"].asString());
  EXPECT_EQ("gzip, deflate", headers["Accept-Encoding"].asString());
  EXPECT_EQ("httpbin.org", headers["Host"].asString());
}

TEST(TestHttpClient, Get_RequestFunc) {
  webcc::HttpClientSession session;

  try {
    auto r = session.Request(webcc::HttpRequestBuilder{}.Get().
                             Url("http://httpbin.org/get").
                             Query("key1", "value1").
                             Query("key2", "value2").
                             Header("Accept", "application/json")
                             ());

    AssertGet(r);

  } catch (const webcc::Exception& e) {
    std::cerr << e.what() << std::endl;
  }
}

TEST(TestHttpClient, Get_Shortcut) {
  webcc::HttpClientSession session;

  try {
    // Use predefined shortcuts.

    auto r = session.Get("http://httpbin.org/get",
                         { "key1", "value1", "key2", "value2" },
                         { "Accept", "application/json" });

    AssertGet(r);

  } catch (const webcc::Exception& e) {
    std::cerr << e.what() << std::endl;
  }
}

#if WEBCC_ENABLE_SSL
TEST(TestHttpClient, Get_SSL) {
  webcc::HttpClientSession session;

  session.set_ssl_verify(kSslVerify);

  try {
    // HTTPS is auto-detected from the URL scheme.

    auto r = session.Request(webcc::HttpRequestBuilder{}.Get().
                             Url("https://httpbin.org/get").
                             Query("key1", "value1").
                             Query("key2", "value2").
                             Header("Accept", "application/json")
                             ());

    AssertGet(r);

  } catch (const webcc::Exception& e) {
    std::cerr << e.what() << std::endl;
  }
}
#endif  // WEBCC_ENABLE_SSL

// -----------------------------------------------------------------------------

// Test Gzip compressed response.
TEST(TestHttpClient, Compression_Gzip) {
  webcc::HttpClientSession session;

  try {
    auto r = session.Get("http://httpbin.org/gzip");

    Json::Value json = StringToJson(r->content());

    EXPECT_EQ(true, json["gzipped"].asBool());

  } catch (const webcc::Exception& e) {
    std::cerr << e.what() << std::endl;
  }
}

// Test Deflate compressed response.
TEST(TestHttpClient, Compression_Deflate) {
  webcc::HttpClientSession session;

  try {
    auto r = session.Get("http://httpbin.org/deflate");

    std::cout << r->content() << std::endl;

    Json::Value json = StringToJson(r->content());

    EXPECT_EQ(true, json["deflated"].asBool());

  } catch (const webcc::Exception& e) {
    std::cerr << e.what() << std::endl;
  }
}

// -----------------------------------------------------------------------------

// Test persistent connections.
TEST(TestHttpClient, KeepAlive) {
  webcc::HttpClientSession session;

  std::string url = "http://httpbin.org/get";
  try {

    // Keep-Alive by default.
    auto r = session.Get(url);

    using boost::iequals;

    EXPECT_TRUE(iequals(r->GetHeader("Connection"), "Keep-alive"));

    // Close by setting Connection header.
    r = session.Get(url, {}, { "Connection", "Close" });

    EXPECT_TRUE(iequals(r->GetHeader("Connection"), "Close"));

    // Close by using request builder.
    r = session.Request(webcc::HttpRequestBuilder{}.Get().
                        KeepAlive(false)
                        ());

    EXPECT_TRUE(iequals(r->GetHeader("Connection"), "Close"));

    // Keep-Alive explicitly by using request builder.
    r = session.Request(webcc::HttpRequestBuilder{}.Get().
                        KeepAlive(true)
                        ());

    EXPECT_TRUE(iequals(r->GetHeader("Connection"), "Keep-alive"));

  } catch (const webcc::Exception& e) {
    std::cerr << e.what() << std::endl;
  }
}

// -----------------------------------------------------------------------------

// Get a JPEG image.
TEST(TestHttpClient, GetImageJpeg) {
  webcc::HttpClientSession session;

  std::string url = "http://httpbin.org/get";
  try {

    auto r = session.Get("http://httpbin.org/image/jpeg");

    // Or
    //   auto r = session.Get("http://httpbin.org/image", {},
    //                        {"Accept", "image/jpeg"});

    //std::ofstream ofs(path, std::ios::binary);
    //ofs << r->content();

    // TODO: Verify the response is a valid JPEG image.

  } catch (const webcc::Exception& e) {
    std::cerr << e.what() << std::endl;
  }
}

#endif

// -----------------------------------------------------------------------------

// TODO: Post requests

// -----------------------------------------------------------------------------

int main(int argc, char* argv[]) {
  // Set webcc::LOG_CONSOLE to enable logging.
  WEBCC_LOG_INIT("", 0);

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
