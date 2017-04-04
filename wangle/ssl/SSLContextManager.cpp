/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <wangle/ssl/SSLContextManager.h>

#include <wangle/ssl/ClientHelloExtStats.h>
#include <wangle/ssl/PasswordInFile.h>
#include <wangle/ssl/SSLCacheOptions.h>
#include <wangle/ssl/ServerSSLContext.h>
#include <wangle/ssl/SSLSessionCacheManager.h>
#include <wangle/ssl/SSLUtil.h>
#include <wangle/ssl/TLSTicketKeyManager.h>
#include <wangle/ssl/TLSTicketKeySeeds.h>

#include <folly/Conv.h>
#include <folly/ScopeGuard.h>
#include <folly/String.h>
#include <folly/portability/OpenSSL.h>
#include <functional>
#include <openssl/asn1.h>
#include <openssl/ssl.h>
#include <string>
#include <folly/io/async/EventBase.h>

#define OPENSSL_MISSING_FEATURE(name) \
do { \
  throw std::runtime_error("missing " #name " support in openssl");  \
} while(0)


using folly::SSLContext;
using std::string;
using std::shared_ptr;
// Get OpenSSL portability APIs
using namespace folly::ssl;

/**
 * SSLContextManager helps to create and manage all SSL_CTX,
 * SSLSessionCacheManager and TLSTicketManager for a listening
 * VIP:PORT. (Note, in SNI, a listening VIP:PORT can have >1 SSL_CTX(s)).
 *
 * Other responsibilities:
 * 1. It also handles the SSL_CTX selection after getting the tlsext_hostname
 *    in the client hello message.
 *
 * Usage:
 * 1. Each listening VIP:PORT serving SSL should have one SSLContextManager.
 *    It maps to Acceptor in the wangle vocabulary.
 *
 * 2. Create a SSLContextConfig object (e.g. by parsing the JSON config).
 *
 * 3. Call SSLContextManager::addSSLContextConfig() which will
 *    then create and configure the SSL_CTX
 *
 * Note: Each Acceptor, with SSL support, should have one SSLContextManager to
 * manage all SSL_CTX for the VIP:PORT.
 */

namespace wangle {

namespace {

X509* getX509(SSL_CTX* ctx) {
  SSL* ssl = SSL_new(ctx);
  SSL_set_connect_state(ssl);
  X509* x509 = SSL_get_certificate(ssl);
  X509_up_ref(x509);
  SSL_free(ssl);
  return x509;
}

void set_key_from_curve(SSL_CTX* ctx, const std::string& curveName) {
#if OPENSSL_VERSION_NUMBER >= 0x0090800fL
#ifndef OPENSSL_NO_ECDH
  EC_KEY* ecdh = nullptr;
  int nid;

  /*
   * Elliptic-Curve Diffie-Hellman parameters are either "named curves"
   * from RFC 4492 section 5.1.1, or explicitly described curves over
   * binary fields. OpenSSL only supports the "named curves", which provide
   * maximum interoperability.
   */

  nid = OBJ_sn2nid(curveName.c_str());
  if (nid == 0) {
    LOG(FATAL) << "Unknown curve name:" << curveName.c_str();
    return;
  }
  ecdh = EC_KEY_new_by_curve_name(nid);
  if (ecdh == nullptr) {
    LOG(FATAL) << "Unable to create curve:" << curveName.c_str();
    return;
  }

  SSL_CTX_set_tmp_ecdh(ctx, ecdh);
  EC_KEY_free(ecdh);
#endif
#endif
}

// The following was auto-generated by
//  openssl dhparam -C 2048 with OepnSSL 1.1.0e
DH *get_dh2048()
{
    static unsigned char dhp_2048[] = {
        0xA2, 0x8B, 0xFC, 0x05, 0x95, 0x2D, 0xC8, 0xB5, 0x41, 0x0E,
        0x01, 0xA9, 0xDE, 0xF6, 0x4B, 0x6C, 0x36, 0x31, 0xAD, 0x07,
        0x0B, 0x8D, 0xCE, 0x0D, 0x71, 0x2A, 0xB8, 0x27, 0xD0, 0xC9,
        0x91, 0xB1, 0x13, 0x24, 0xCB, 0x35, 0x60, 0xA0, 0x83, 0xB1,
        0xE1, 0xEF, 0xA0, 0x9D, 0x9F, 0xA9, 0xAB, 0x56, 0x78, 0xBA,
        0xA6, 0xB4, 0xA5, 0xEC, 0x86, 0x80, 0xB4, 0x5A, 0xC5, 0x9E,
        0x30, 0x1E, 0xCC, 0xF8, 0x2D, 0x55, 0xF9, 0x0E, 0x74, 0x8F,
        0x72, 0x46, 0xF5, 0xFC, 0xD4, 0x5B, 0xBC, 0xC3, 0xBC, 0x89,
        0xCE, 0xB8, 0xD7, 0x1E, 0xC8, 0xD1, 0x46, 0xB7, 0xF3, 0xD3,
        0x1C, 0x3A, 0x62, 0xB4, 0x1E, 0x42, 0xEA, 0x79, 0x1C, 0x07,
        0x05, 0x46, 0x1A, 0x0F, 0x35, 0x79, 0xCB, 0xF8, 0xD1, 0x44,
        0xEE, 0x86, 0x7C, 0x34, 0xA8, 0x7D, 0x92, 0x67, 0x48, 0x2D,
        0x6E, 0xC2, 0x44, 0xA4, 0x93, 0x85, 0xF5, 0x2B, 0x79, 0x72,
        0x79, 0xB5, 0xF4, 0xB0, 0xC6, 0xE1, 0xF0, 0x9F, 0x00, 0x59,
        0x37, 0x09, 0xE8, 0x2C, 0xDB, 0xA7, 0x9B, 0x89, 0xEE, 0x49,
        0x55, 0x53, 0x48, 0xB4, 0x02, 0xC2, 0xFA, 0x7A, 0xBB, 0x28,
        0xFC, 0x0D, 0x06, 0xCB, 0xA5, 0xE2, 0x04, 0xFF, 0xDE, 0x5D,
        0x99, 0xE9, 0x55, 0xA0, 0xBA, 0x60, 0x1E, 0x5E, 0x47, 0x46,
        0x6C, 0x2A, 0x30, 0x8E, 0xBE, 0x71, 0x56, 0x85, 0x2E, 0x53,
        0xF9, 0x33, 0x5B, 0xC8, 0x8C, 0xC1, 0x80, 0xAF, 0xC3, 0x0B,
        0x89, 0xF5, 0x5A, 0x23, 0x97, 0xED, 0xB7, 0x8F, 0x2B, 0x0B,
        0x70, 0x73, 0x44, 0xD2, 0xE8, 0xEC, 0xF2, 0xDD, 0x80, 0x32,
        0x53, 0x9A, 0x17, 0xD6, 0xC7, 0x71, 0x7F, 0xA5, 0xD6, 0x45,
        0x06, 0x36, 0xCE, 0x7B, 0x5D, 0x77, 0xA7, 0x39, 0x5F, 0xC7,
        0x2A, 0xEA, 0x77, 0xE2, 0x8F, 0xFA, 0x8A, 0x81, 0x4C, 0x3D,
        0x41, 0x48, 0xA4, 0x7F, 0x33, 0x7B
    };
    static unsigned char dhg_2048[] = {
        0x02,
    };
    DH *dh = DH_new();
    BIGNUM *dhp_bn, *dhg_bn;

    if (dh == nullptr)
        return nullptr;
    dhp_bn = BN_bin2bn(dhp_2048, sizeof (dhp_2048), nullptr);
    dhg_bn = BN_bin2bn(dhg_2048, sizeof (dhg_2048), nullptr);
    // Note: DH_set0_pqg is defined only in OpenSSL 1.1.0; for
    // other versions, it is defined in the portability library
    // at folly/portability/OpenSSL.h
    if (dhp_bn == nullptr || dhg_bn == nullptr
            || !DH_set0_pqg(dh, dhp_bn, nullptr, dhg_bn)) {
        DH_free(dh);
        BN_free(dhp_bn);
        BN_free(dhg_bn);
        return nullptr;
    }
    return dh;
}

std::string flattenList(const std::list<std::string>& list) {
  std::string s;
  bool first = true;
  for (auto& item : list) {
    if (first) {
      first = false;
    } else {
      s.append(", ");
    }
    s.append(item);
  }
  return s;
}

}

SSLContextManager::~SSLContextManager() = default;

SSLContextManager::SSLContextManager(
  folly::EventBase* eventBase,
  const std::string& /* vipName */,
  bool strict,
  SSLStats* stats) :
    stats_(stats),
    eventBase_(eventBase),
    strict_(strict) {

}

void SSLContextManager::SslContexts::swap(SslContexts& other) noexcept {
  ctxs.swap(other.ctxs);
  defaultCtx.swap(other.defaultCtx);
  defaultCtxDomainName.swap(other.defaultCtxDomainName);
  dnMap.swap(other.dnMap);
}

void SSLContextManager::SslContexts::clear() {
  ctxs.clear();
  defaultCtx = nullptr;
  defaultCtxDomainName.clear();
  dnMap.clear();
}

void SSLContextManager::resetSSLContextConfigs(
  const std::vector<SSLContextConfig>& ctxConfigs,
  const SSLCacheOptions& cacheOptions,
  const TLSTicketKeySeeds* ticketSeeds,
  const folly::SocketAddress& vipAddress,
  const std::shared_ptr<SSLCacheProvider>& externalCache) {

  SslContexts contexts;
  TLSTicketKeySeeds oldTicketSeeds;
  // This assumes that all ctxs have the same ticket seeds. Which we assume in
  // other places as well
  if (!ticketSeeds) {
    // find first non null ticket manager and update seeds from it
    for (auto& ctx : contexts_.ctxs) {
      auto ticketManager = ctx->getTicketManager();
      if (ticketManager) {
        ticketManager->getTLSTicketKeySeeds(
          oldTicketSeeds.oldSeeds,
          oldTicketSeeds.currentSeeds,
          oldTicketSeeds.newSeeds);
        break;
      }
    }
  }

  for (const auto& ctxConfig : ctxConfigs) {
    addSSLContextConfig(ctxConfig,
                        cacheOptions,
                        ticketSeeds ? ticketSeeds : &oldTicketSeeds,
                        vipAddress,
                        externalCache,
                        &contexts);
  }
  contexts_.swap(contexts);
}

void SSLContextManager::addSSLContextConfig(
  const SSLContextConfig& ctxConfig,
  const SSLCacheOptions& cacheOptions,
  const TLSTicketKeySeeds* ticketSeeds,
  const folly::SocketAddress& vipAddress,
  const std::shared_ptr<SSLCacheProvider>& externalCache,
  SslContexts* contexts) {

  if (!contexts) {
    contexts = &contexts_;
  }

  unsigned numCerts = 0;
  std::string commonName;
  std::string lastCertPath;
  std::unique_ptr<std::list<std::string>> subjectAltName;
  auto sslCtx =
      std::make_shared<ServerSSLContext>(ctxConfig.sslVersion);
  for (const auto& cert : ctxConfig.certificates) {
    try {
      sslCtx->loadCertificate(cert.certPath.c_str());
    } catch (const std::exception& ex) {
      // The exception isn't very useful without the certificate path name,
      // so throw a new exception that includes the path to the certificate.
      string msg = folly::to<string>("error loading SSL certificate ",
                                     cert.certPath, ": ",
                                     folly::exceptionStr(ex));
      LOG(ERROR) << msg;
      throw std::runtime_error(msg);
    }

    // Verify that the Common Name and (if present) Subject Alternative Names
    // are the same for all the certs specified for the SSL context.
    numCerts++;
    X509* x509 = getX509(sslCtx->getSSLCtx());
    auto guard = folly::makeGuard([x509] { X509_free(x509); });
    auto cn = SSLUtil::getCommonName(x509);
    if (!cn) {
      throw std::runtime_error(folly::to<string>("Cannot get CN for X509 ",
                                                 cert.certPath));
    }
    auto altName = SSLUtil::getSubjectAltName(x509);
    VLOG(2) << "cert " << cert.certPath << " CN: " << *cn;
    if (altName) {
      altName->sort();
      VLOG(2) << "cert " << cert.certPath << " SAN: " << flattenList(*altName);
    } else {
      VLOG(2) << "cert " << cert.certPath << " SAN: " << "{none}";
    }
    if (numCerts == 1) {
      commonName = *cn;
      subjectAltName = std::move(altName);
    } else {
      if (commonName != *cn) {
        throw std::runtime_error(folly::to<string>("X509 ", cert.certPath,
                                          " does not have same CN as ",
                                          lastCertPath));
      }
      if (altName == nullptr) {
        if (subjectAltName != nullptr) {
          throw std::runtime_error(folly::to<string>("X509 ", cert.certPath,
                                            " does not have same SAN as ",
                                            lastCertPath));
        }
      } else {
        if ((subjectAltName == nullptr) || (*altName != *subjectAltName)) {
          throw std::runtime_error(folly::to<string>("X509 ", cert.certPath,
                                            " does not have same SAN as ",
                                            lastCertPath));
        }
      }
    }
    lastCertPath = cert.certPath;

    if (ctxConfig.isLocalPrivateKey
        || ctxConfig.keyOffloadParams.offloadType.empty()) {
      // The private key lives in the same process
      // This needs to be called before loadPrivateKey().
      if (!cert.passwordPath.empty()) {
        auto sslPassword = std::make_shared<PasswordInFile>(cert.passwordPath);
        sslCtx->passwordCollector(sslPassword);
      }

      try {
        sslCtx->loadPrivateKey(cert.keyPath.c_str());
      } catch (const std::exception& ex) {
        // Throw an error that includes the key path, so the user can tell
        // which key had a problem.
        string msg = folly::to<string>("error loading private SSL key ",
                                       cert.keyPath, ": ",
                                       folly::exceptionStr(ex));
        LOG(ERROR) << msg;
        throw std::runtime_error(msg);
      }
    } else {
      enableAsyncCrypto(sslCtx, ctxConfig, cert.certPath);
    }
  }

  overrideConfiguration(sslCtx, ctxConfig);

  // Let the server pick the highest performing cipher from among the client's
  // choices.
  //
  // Let's use a unique private key for all DH key exchanges.
  //
  // Because some old implementations choke on empty fragments, most SSL
  // applications disable them (it's part of SSL_OP_ALL).  This
  // will improve performance and decrease write buffer fragmentation.
  sslCtx->setOptions(SSL_OP_CIPHER_SERVER_PREFERENCE |
    SSL_OP_SINGLE_DH_USE |
    SSL_OP_SINGLE_ECDH_USE |
    SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS);

  // Configure SSL ciphers list
  if (!ctxConfig.tls11Ciphers.empty()) {
    // FIXME: create a dummy SSL_CTX for cipher testing purpose? It can
    //        remove the ordering dependency

    // Test to see if the specified TLS1.1 ciphers are valid.  Note that
    // these will be overwritten by the ciphers() call below.
    sslCtx->setCiphersOrThrow(ctxConfig.tls11Ciphers);
  }

  // Important that we do this *after* checking the TLS1.1 ciphers above,
  // since we test their validity by actually setting them.
  sslCtx->ciphers(ctxConfig.sslCiphers);

  // Use a fix DH param
  DH* dh = get_dh2048();
  SSL_CTX_set_tmp_dh(sslCtx->getSSLCtx(), dh);
  DH_free(dh);

  const string& curve = ctxConfig.eccCurveName;
  if (!curve.empty()) {
    set_key_from_curve(sslCtx->getSSLCtx(), curve);
  }

  if (!ctxConfig.clientCAFile.empty()) {
    try {
      sslCtx->loadTrustedCertificates(ctxConfig.clientCAFile.c_str());
      sslCtx->loadClientCAList(ctxConfig.clientCAFile.c_str());

      // Only allow over-riding of verification callback if one
      // isn't explicitly set on the context
      if (clientCertVerifyCallback_ == nullptr) {
        sslCtx->setVerificationOption(ctxConfig.clientVerification);
      } else {
        clientCertVerifyCallback_->attachSSLContext(sslCtx);
      }

    } catch (const std::exception& ex) {
      string msg = folly::to<string>("error loading client CA",
                                     ctxConfig.clientCAFile, ": ",
                                     folly::exceptionStr(ex));
      LOG(ERROR) << msg;
      throw std::runtime_error(msg);
    }
  }

  sslCtx->setupSessionCache(
      ctxConfig,
      cacheOptions,
      vipAddress,
      externalCache,
      commonName,
      eventBase_,
      stats_);

  sslCtx->setupTicketManager(ticketSeeds, ctxConfig, stats_);

  // finalize sslCtx setup by the individual features supported by openssl
  ctxSetupByOpensslFeature(sslCtx, ctxConfig, *contexts);

  try {
    insert(sslCtx,
           ctxConfig.isDefault,
           *contexts);
  } catch (const std::exception& ex) {
    string msg = folly::to<string>("Error adding certificate : ",
                                   folly::exceptionStr(ex));
    LOG(ERROR) << msg;
    throw std::runtime_error(msg);
  }

}

#ifdef PROXYGEN_HAVE_SERVERNAMECALLBACK
SSLContext::ServerNameCallbackResult
SSLContextManager::serverNameCallback(SSL* ssl) {
  shared_ptr<SSLContext> ctx;

  const char* sn = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  bool reqHasServerName = true;
  if (!sn) {
    VLOG(6) << "Server Name (tlsext_hostname) is missing, using default";
    if (clientHelloTLSExtStats_) {
      clientHelloTLSExtStats_->recordAbsentHostname();
    }
    reqHasServerName = false;

    sn = contexts_.defaultCtxDomainName.c_str();
  }
  size_t snLen = strlen(sn);
  VLOG(6) << "Server Name (SNI TLS extension): '" << sn << "' ";

  // FIXME: This code breaks the abstraction. Suggestion?
  folly::AsyncSSLSocket* sslSocket = folly::AsyncSSLSocket::getFromSSL(ssl);
  CHECK(sslSocket);

  // Check if we think the client is outdated and require weak crypto.
  CertCrypto certCryptoReq = CertCrypto::BEST_AVAILABLE;

  // TODO: use SSL_get_sigalgs (requires openssl 1.0.2).
  auto clientInfo = sslSocket->getClientHelloInfo();
  if (clientInfo) {
    certCryptoReq = CertCrypto::SHA1_SIGNATURE;
    for (const auto& sigAlgPair : clientInfo->clientHelloSigAlgs_) {
      if (sigAlgPair.first ==
          folly::ssl::HashAlgorithm::SHA256) {
        certCryptoReq = CertCrypto::BEST_AVAILABLE;
        break;
      }
    }

    // Assume the client supports SHA2 if it sent SNI.
    const auto& extensions = clientInfo->clientHelloExtensions_;
    if (std::find(extensions.begin(), extensions.end(),
          folly::ssl::TLSExtension::SERVER_NAME) != extensions.end()) {
      certCryptoReq = CertCrypto::BEST_AVAILABLE;
    }
  }

  DNString dnstr(sn, snLen);
  uint32_t count = 0;
  do {
    // First look for a context with the exact crypto needed. Weaker crypto will
    // be in the map as best available if it is the best we have for that
    // subject name.
    SSLContextKey key(dnstr, certCryptoReq);
    ctx = getSSLCtx(key);
    if (ctx) {
      sslSocket->switchServerSSLContext(ctx);
      if (clientHelloTLSExtStats_) {
        if (reqHasServerName) {
          clientHelloTLSExtStats_->recordMatch();
        }
        clientHelloTLSExtStats_->recordCertCrypto(certCryptoReq, certCryptoReq);
      }
      return SSLContext::SERVER_NAME_FOUND;
    }

    // If we didn't find an exact match, look for a cert with upgraded crypto.
    if (certCryptoReq != CertCrypto::BEST_AVAILABLE) {
      SSLContextKey fallbackKey(dnstr, CertCrypto::BEST_AVAILABLE);
      ctx = getSSLCtx(fallbackKey);
      if (ctx) {
        sslSocket->switchServerSSLContext(ctx);
        if (clientHelloTLSExtStats_) {
          if (reqHasServerName) {
            clientHelloTLSExtStats_->recordMatch();
          }
          clientHelloTLSExtStats_->recordCertCrypto(
              certCryptoReq, CertCrypto::BEST_AVAILABLE);
        }
        return SSLContext::SERVER_NAME_FOUND;
      }
    }

    // Give the noMatchFn one chance to add the correct cert
  }
  while (count++ == 0 && noMatchFn_ && noMatchFn_(sn));

  VLOG(6) << folly::stringPrintf("Cannot find a SSL_CTX for \"%s\"", sn);

  if (clientHelloTLSExtStats_ && reqHasServerName) {
    clientHelloTLSExtStats_->recordNotMatch();
  }
  return SSLContext::SERVER_NAME_NOT_FOUND;
}
#endif

// Consolidate all SSL_CTX setup which depends on openssl version/feature
void
SSLContextManager::ctxSetupByOpensslFeature(
  shared_ptr<ServerSSLContext> sslCtx,
  const SSLContextConfig& ctxConfig,
  SslContexts& contexts) {
  // Disable compression - profiling shows this to be very expensive in
  // terms of CPU and memory consumption.
  //
#ifdef SSL_OP_NO_COMPRESSION
  sslCtx->setOptions(SSL_OP_NO_COMPRESSION);
#endif

  // Enable early release of SSL buffers to reduce the memory footprint
#ifdef SSL_MODE_RELEASE_BUFFERS
  // Note: SSL_CTX_set_mode doesn't set, just ORs the arg with existing mode
  SSL_CTX_set_mode(sslCtx->getSSLCtx(), SSL_MODE_RELEASE_BUFFERS);

#endif
#ifdef SSL_MODE_EARLY_RELEASE_BBIO
  // Note: SSL_CTX_set_mode doesn't set, just ORs the arg with existing mode
  SSL_CTX_set_mode(sslCtx->getSSLCtx(), SSL_MODE_EARLY_RELEASE_BBIO);
#endif

  // This number should (probably) correspond to HTTPSession::kMaxReadSize
  // For now, this number must also be large enough to accommodate our
  // largest certificate, because some older clients (IE6/7) require the
  // cert to be in a single fragment.
#ifdef SSL_CTRL_SET_MAX_SEND_FRAGMENT
  SSL_CTX_set_max_send_fragment(sslCtx->getSSLCtx(), 8000);
#endif

  // Specify cipher(s) to be used for TLS1.1 client
  if (!ctxConfig.tls11Ciphers.empty() ||
      !ctxConfig.tls11AltCipherlist.empty()) {
#ifdef PROXYGEN_HAVE_SERVERNAMECALLBACK
    // Specified TLS1.1 ciphers are valid
    // XXX: this callback will be called for every new (TLS 1.1 or greater)
    // handshake, so it relies on ctxConfig.tls11Ciphers and
    // ctxConfig.tls11AltCipherlist not changing.
    sslCtx->addClientHelloCallback(
      std::bind(
        &SSLContext::switchCiphersIfTLS11,
        sslCtx.get(),
        std::placeholders::_1,
        ctxConfig.tls11Ciphers,
        ctxConfig.tls11AltCipherlist
      )
    );
#else
    OPENSSL_MISSING_FEATURE(SNI);
#endif
  }

  // NPN (Next Protocol Negotiation)
  if (!ctxConfig.nextProtocols.empty()) {
#ifdef OPENSSL_NPN_NEGOTIATED
    sslCtx->setRandomizedAdvertisedNextProtocols(ctxConfig.nextProtocols);
#else
    OPENSSL_MISSING_FEATURE(NPN);
#endif
  }

  // SNI
#ifdef PROXYGEN_HAVE_SERVERNAMECALLBACK
  noMatchFn_ = ctxConfig.sniNoMatchFn;
  if (ctxConfig.isDefault) {
    if (contexts.defaultCtx) {
      throw std::runtime_error(">1 X509 is set as default");
    }

    contexts.defaultCtx = sslCtx;
    contexts.defaultCtx->setServerNameCallback(
      std::bind(&SSLContextManager::serverNameCallback, this,
                std::placeholders::_1));
  }
#else
  if (contexts.ctxs.size() > 1) {
    OPENSSL_MISSING_FEATURE(SNI);
  }
#endif
}

void
SSLContextManager::insert(shared_ptr<ServerSSLContext> sslCtx,
                          bool defaultFallback,
                          SslContexts& contexts) {
  X509* x509 = getX509(sslCtx->getSSLCtx());
  auto guard = folly::makeGuard([x509] { X509_free(x509); });
  auto cn = SSLUtil::getCommonName(x509);
  if (!cn) {
    throw std::runtime_error("Cannot get CN");
  }

  /**
   * Some notes from RFC 2818. Only for future quick references in case of bugs
   *
   * RFC 2818 section 3.1:
   * "......
   * If a subjectAltName extension of type dNSName is present, that MUST
   * be used as the identity. Otherwise, the (most specific) Common Name
   * field in the Subject field of the certificate MUST be used. Although
   * the use of the Common Name is existing practice, it is deprecated and
   * Certification Authorities are encouraged to use the dNSName instead.
   * ......
   * In some cases, the URI is specified as an IP address rather than a
   * hostname. In this case, the iPAddress subjectAltName must be present
   * in the certificate and must exactly match the IP in the URI.
   * ......"
   */

  // Not sure if we ever get this kind of X509...
  // If we do, assume '*' is always in the CN and ignore all subject alternative
  // names.
  if (cn->length() == 1 && (*cn)[0] == '*') {
    if (!defaultFallback) {
      throw std::runtime_error("STAR X509 is not the default");
    }
    contexts.ctxs.emplace_back(sslCtx);
    return;
  }

  CertCrypto certCrypto;
  int sigAlg = X509_get_signature_nid(x509);
  if (sigAlg == NID_sha1WithRSAEncryption ||
      sigAlg == NID_ecdsa_with_SHA1) {
    certCrypto = CertCrypto::SHA1_SIGNATURE;
    VLOG(4) << "Adding SSLContext with SHA1 Signature";
  } else {
    certCrypto = CertCrypto::BEST_AVAILABLE;
    VLOG(4) << "Adding SSLContext with best available crypto";
  }

  // Insert by CN
  insertSSLCtxByDomainName(cn->c_str(),
                           cn->length(),
                           sslCtx,
                           contexts,
                           certCrypto);

  // Insert by subject alternative name(s)
  auto altNames = SSLUtil::getSubjectAltName(x509);
  if (altNames) {
    for (auto& name : *altNames) {
      insertSSLCtxByDomainName(name.c_str(),
                               name.length(),
                               sslCtx,
                               contexts,
                               certCrypto);
    }
  }

  if (defaultFallback) {
    contexts.defaultCtxDomainName = *cn;
  }

  contexts.ctxs.emplace_back(sslCtx);
}

void
SSLContextManager::insertSSLCtxByDomainName(const char* dn,
                                            size_t len,
                                            shared_ptr<SSLContext> sslCtx,
                                            SslContexts& contexts,
                                            CertCrypto certCrypto) {
  try {
    insertSSLCtxByDomainNameImpl(dn, len, sslCtx, contexts, certCrypto);
  } catch (const std::runtime_error& ex) {
    if (strict_) {
      throw ex;
    } else {
      LOG(ERROR) << ex.what() << " DN=" << dn;
    }
  }
}
void
SSLContextManager::insertSSLCtxByDomainNameImpl(const char* dn,
                                                size_t len,
                                                shared_ptr<SSLContext> sslCtx,
                                                SslContexts& contexts,
                                                CertCrypto certCrypto)
{
  VLOG(4) <<
    folly::stringPrintf("Adding CN/Subject-alternative-name \"%s\" for "
                        "SNI search", dn);

  // Only support wildcard domains which are prefixed exactly by "*." .
  // "*" appearing at other locations is not accepted.

  if (len > 2 && dn[0] == '*') {
    if (dn[1] == '.') {
      // skip the first '*'
      dn++;
      len--;
    } else {
      throw std::runtime_error(
        "Invalid wildcard CN/subject-alternative-name \"" + std::string(dn) + "\" "
        "(only allow character \".\" after \"*\"");
    }
  }

  if (len == 1 && *dn == '.') {
    throw std::runtime_error("X509 has only '.' in the CN or subject alternative name "
                    "(after removing any preceding '*')");
  }

  if (strchr(dn, '*')) {
    throw std::runtime_error("X509 has '*' in the the CN or subject alternative name "
                    "(after removing any preceding '*')");
  }

  DNString dnstr(dn, len);
  insertIntoDnMap(SSLContextKey(dnstr, certCrypto), sslCtx, true, contexts);
  if (certCrypto != CertCrypto::BEST_AVAILABLE) {
    // Note: there's no partial ordering here (you either get what you request,
    // or you get best available).
    VLOG(6) << "Attempting insert of weak crypto SSLContext as best available.";
    insertIntoDnMap(
        SSLContextKey(dnstr, CertCrypto::BEST_AVAILABLE),
        sslCtx,
        false,
        contexts);
  }
}

void SSLContextManager::insertIntoDnMap(SSLContextKey key,
                                        shared_ptr<SSLContext> sslCtx,
                                        bool overwrite,
                                        SslContexts& contexts)
{
  const auto v = contexts.dnMap.find(key);
  if (v == contexts.dnMap.end()) {
    VLOG(6) << "Inserting SSLContext into map.";
    contexts.dnMap.emplace(key, sslCtx);
  } else if (v->second == sslCtx) {
    VLOG(6)<< "Duplicate CN or subject alternative name found in the same X509."
      "  Ignore the later name.";
  } else if (overwrite) {
    VLOG(6) << "Overwriting SSLContext.";
    v->second = sslCtx;
  } else {
    VLOG(6) << "Leaving existing SSLContext in map.";
  }
}

void SSLContextManager::clear() {
  contexts_.clear();
}

shared_ptr<SSLContext>
SSLContextManager::getSSLCtx(const SSLContextKey& key) const
{
  auto ctx = getSSLCtxByExactDomain(key);
  if (ctx) {
    return ctx;
  }
  return getSSLCtxBySuffix(key);
}

shared_ptr<SSLContext>
SSLContextManager::getSSLCtxBySuffix(const SSLContextKey& key) const
{
  size_t dot;

  if ((dot = key.dnString.find_first_of(".")) != DNString::npos) {
    SSLContextKey suffixKey(DNString(key.dnString, dot),
        key.certCrypto);
    const auto v = contexts_.dnMap.find(suffixKey);
    if (v != contexts_.dnMap.end()) {
      VLOG(6) << folly::stringPrintf("\"%s\" is a willcard match to \"%s\"",
                                     key.dnString.c_str(),
                                     suffixKey.dnString.c_str());
      return v->second;
    }
  }

  VLOG(6) << folly::stringPrintf("\"%s\" is not a wildcard match",
                                 key.dnString.c_str());
  return shared_ptr<SSLContext>();
}

shared_ptr<SSLContext>
SSLContextManager::getSSLCtxByExactDomain(const SSLContextKey& key) const
{
  const auto v = contexts_.dnMap.find(key);
  if (v == contexts_.dnMap.end()) {
    VLOG(6) << folly::stringPrintf("\"%s\" is not an exact match",
                                   key.dnString.c_str());
    return shared_ptr<SSLContext>();
  } else {
    VLOG(6) << folly::stringPrintf("\"%s\" is an exact match",
                                   key.dnString.c_str());
    return v->second;
  }
}

shared_ptr<SSLContext>
SSLContextManager::getDefaultSSLCtx() const{
  return contexts_.defaultCtx;
}

void
SSLContextManager::reloadTLSTicketKeys(
  const std::vector<std::string>& oldSeeds,
  const std::vector<std::string>& currentSeeds,
  const std::vector<std::string>& newSeeds) {
#ifdef SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB
  for (auto& ctx : contexts_.ctxs) {
    auto tmgr = ctx->getTicketManager();
    if (tmgr) {
      tmgr->setTLSTicketKeySeeds(oldSeeds, currentSeeds, newSeeds);
    }
  }
#endif
}

} // namespace wangle
