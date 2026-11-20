/**
 * @file tls_roots.c
 * @brief The platform's certificate authorities, as turborel-client takes
 * them through the crate rustls-native-certs [CLIENT Connecting]: those
 * of the files SSL_CERT_FILE and SSL_CERT_DIR name, if either is set,
 * SSL_CERT_DIR a list of directories apart by the platform's separator,
 * each regular file of each read as PEM, a dangling link passed over;
 * else the platform's own: on macOS the keychain's trust settings of the
 * user, the administrator and the system, the first to speak of a
 * certificate deciding; on Windows the system's store of roots; elsewhere
 * the TLS library's own file and directory.
 */
#include "net/tls_roots.h"

/* The TLS library's and the platforms' headers declare more than the
   include checker can map to their own names (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#include "format.h"
#include "settings/settings.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TRL_HAVE_TLS

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <wincrypt.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

/* The separator of SSL_CERT_DIR's list, as the platform's path lists. */
#ifdef _WIN32
#define TRL_PATH_LIST_SEPARATOR ';'
#else
#define TRL_PATH_LIST_SEPARATOR ':'
#endif

/* Notes the first reason no authority was read. */
static void note(char *why, size_t capacity, const char *what, const char *path) {
    if (why[0] == '\0') {
        trl_format(why, capacity, "%s %s", what, path);
    }
}

/* Adds each certificate of the PEM file at `path`; how many. */
static size_t add_pem_file(X509_STORE *store, const char *path, char *why, size_t capacity) {
    BIO *file = BIO_new_file(path, "r");
    if (file == NULL) {
        note(why, capacity, "cannot read", path);
        ERR_clear_error();
        return 0U;
    }
    size_t added = 0U;
    X509 *certificate = NULL;
    while ((certificate = PEM_read_bio_X509(file, NULL, NULL, NULL)) != NULL) {
        added += X509_STORE_add_cert(store, certificate) == 1 ? 1U : 0U;
        X509_free(certificate);
    }
    BIO_free(file);
    ERR_clear_error();
    return added;
}

/* Adds each certificate of each regular file of the directory `dir`, not
   within those it holds, a dangling link passed over; how many. */
static size_t add_pem_dir(X509_STORE *store, const char *dir, char *why, size_t capacity) {
    size_t added = 0U;
    char path[TRL_PATH_BYTES];
#ifdef _WIN32
    trl_format(path, sizeof path, "%s\\*", dir);
    WIN32_FIND_DATAA found;
    HANDLE listing = FindFirstFileA(path, &found);
    if (listing == INVALID_HANDLE_VALUE) {
        note(why, capacity, "cannot open the directory", dir);
        return 0U;
    }
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
            trl_format(path, sizeof path, "%s\\%s", dir, found.cFileName);
            added += add_pem_file(store, path, why, capacity);
        }
    } while (FindNextFileA(listing, &found));
    (void)FindClose(listing);
#else
    DIR *listing = opendir(dir);
    if (listing == NULL) {
        note(why, capacity, "cannot open the directory", dir);
        return 0U;
    }
    const struct dirent *entry = NULL;
    while ((entry = readdir(listing)) != NULL) { // NOLINT(concurrency-mt-unsafe): a listing of this call's own
        trl_format(path, sizeof path, "%s/%s", dir, entry->d_name);
        struct stat status;
        if (stat(path, &status) == 0 && S_ISREG(status.st_mode)) {
            added += add_pem_file(store, path, why, capacity);
        }
    }
    (void)closedir(listing);
#endif
    return added;
}

/* Adds the authorities of each directory of SSL_CERT_DIR's list, an
   empty one passed over; how many, `*named` whether it named any. */
static size_t add_pem_dirs(X509_STORE *store, const char *list, bool *named, char *why, size_t capacity) {
    size_t added = 0U;
    char dir[TRL_PATH_BYTES];
    for (const char *at = list; at != NULL && *at != '\0';) {
        const char *end = strchr(at, TRL_PATH_LIST_SEPARATOR);
        size_t length = end == NULL ? strlen(at) : (size_t)(end - at);
        if (length > 0U && length < sizeof dir) {
            memcpy(dir, at, length);
            dir[length] = '\0';
            *named = true;
            added += add_pem_dir(store, dir, why, capacity);
        }
        at = end == NULL ? NULL : end + 1;
    }
    return added;
}

#ifdef __APPLE__
/* What the keychain's trust settings say of a certificate for TLS. */
typedef enum trust {
    /* Its settings could not be read: no domain has spoken of it. */
    TRUST_UNREAD = 0,
    TRUST_ROOT,
    TRUST_NOT,
} trust;

/* What one domain's trust settings of `certificate` say for TLS, as the
   crate security-framework reads them: the first setting of no policy, or
   of the policy sslServer, whose result is neither unspecified nor
   invalid; a root of none such. */
static trust trust_of(SecCertificateRef certificate, SecTrustSettingsDomain domain) {
    CFArrayRef settings = NULL;
    if (SecTrustSettingsCopyTrustSettings(certificate, domain, &settings) != errSecSuccess || settings == NULL) {
        return TRUST_UNREAD;
    }
    SInt64 result = kSecTrustSettingsResultTrustRoot;
    for (CFIndex i = 0; i < CFArrayGetCount(settings); ++i) {
        CFDictionaryRef setting = (CFDictionaryRef)CFArrayGetValueAtIndex(settings, i);
        CFStringRef policy = (CFStringRef)CFDictionaryGetValue(setting, CFSTR("kSecTrustSettingsPolicyName"));
        if (policy != NULL && CFStringCompare(policy, CFSTR("sslServer"), 0) != kCFCompareEqualTo) {
            continue;
        }
        CFNumberRef number = (CFNumberRef)CFDictionaryGetValue(setting, CFSTR("kSecTrustSettingsResult"));
        SInt64 said = kSecTrustSettingsResultTrustRoot;
        if (number != NULL && !CFNumberGetValue(number, kCFNumberSInt64Type, &said)) {
            said = kSecTrustSettingsResultInvalid;
        }
        if (said >= kSecTrustSettingsResultTrustRoot && said < kSecTrustSettingsResultUnspecified) {
            result = said;
            break;
        }
    }
    CFRelease(settings);
    return result == kSecTrustSettingsResultTrustRoot || result == kSecTrustSettingsResultTrustAsRoot ? TRUST_ROOT
                                                                                                      : TRUST_NOT;
}

/* Adds `certificate` if its domain trusts it as a root, unless an earlier
   domain spoke of it, as `decided` holds; how many. */
static size_t add_if_trusted(X509_STORE *store, SecCertificateRef certificate, SecTrustSettingsDomain domain,
                             CFMutableSetRef decided) {
    CFDataRef der = SecCertificateCopyData(certificate);
    if (der == NULL) {
        return 0U;
    }
    size_t added = 0U;
    if (!CFSetContainsValue(decided, der)) {
        trust said = trust_of(certificate, domain);
        if (said != TRUST_UNREAD) {
            CFSetAddValue(decided, der);
        }
        const unsigned char *at = CFDataGetBytePtr(der);
        X509 *x509 = said == TRUST_ROOT ? d2i_X509(NULL, &at, (long)CFDataGetLength(der)) : NULL;
        added = x509 != NULL && X509_STORE_add_cert(store, x509) == 1 ? 1U : 0U;
        X509_free(x509);
    }
    CFRelease(der);
    return added;
}

/* Adds each certificate of a domain's trust settings it trusts as a root,
   but those an earlier domain spoke of, which `decided` holds; how many. */
static size_t add_domain(X509_STORE *store, SecTrustSettingsDomain domain, CFMutableSetRef decided) {
    CFArrayRef certificates = NULL;
    if (SecTrustSettingsCopyCertificates(domain, &certificates) != errSecSuccess || certificates == NULL) {
        return 0U;
    }
    size_t added = 0U;
    for (CFIndex i = 0; i < CFArrayGetCount(certificates); ++i) {
        /* An array of Core Foundation hands out its items as const void *. */
        union {
            const void *item;
            SecCertificateRef certificate;
        } item = {CFArrayGetValueAtIndex(certificates, i)};
        added += add_if_trusted(store, item.certificate, domain, decided);
    }
    CFRelease(certificates);
    return added;
}

/* The keychain's trust settings, the user's over the administrator's over
   the system's. */
static size_t add_platform(X509_STORE *store, char *why, size_t capacity) {
    CFMutableSetRef decided = CFSetCreateMutable(NULL, 0, &kCFTypeSetCallBacks);
    if (decided == NULL) {
        note(why, capacity, "cannot read", "the keychain's trust settings");
        return 0U;
    }
    static const SecTrustSettingsDomain domains[] = {
        kSecTrustSettingsDomainUser,
        kSecTrustSettingsDomainAdmin,
        kSecTrustSettingsDomainSystem,
    };
    size_t added = 0U;
    for (size_t i = 0U; i < sizeof domains / sizeof domains[0]; ++i) {
        added += add_domain(store, domains[i], decided);
    }
    CFRelease(decided);
    ERR_clear_error();
    return added;
}
#elif defined(_WIN32)
/* The system's store of root authorities. */
static size_t add_platform(X509_STORE *store, char *why, size_t capacity) {
    HCERTSTORE roots = CertOpenSystemStoreW(0, L"ROOT");
    if (roots == NULL) {
        note(why, capacity, "cannot open", "the system's store of roots");
        return 0U;
    }
    size_t added = 0U;
    PCCERT_CONTEXT at = NULL;
    while ((at = CertEnumCertificatesInStore(roots, at)) != NULL) {
        const unsigned char *der = at->pbCertEncoded;
        X509 *x509 = d2i_X509(NULL, &der, (long)at->cbCertEncoded);
        added += x509 != NULL && X509_STORE_add_cert(store, x509) == 1 ? 1U : 0U;
        X509_free(x509);
    }
    (void)CertCloseStore(roots, 0U);
    ERR_clear_error();
    return added;
}
#else
/* The TLS library's own file and directory of authorities. */
static size_t add_platform(X509_STORE *store, char *why, size_t capacity) {
    char unused[TRL_FAILURE_MESSAGE_BYTES] = "";
    size_t added = add_pem_file(store, X509_get_default_cert_file(), unused, sizeof unused);
    added += add_pem_dir(store, X509_get_default_cert_dir(), unused, sizeof unused);
    if (added == 0U) {
        note(why, capacity, "found none in", X509_get_default_cert_file());
    }
    return added;
}
#endif

// NOLINTEND(misc-include-cleaner)

size_t trl_tls_roots_add(X509_STORE *store, char *why, size_t capacity) {
    why[0] = '\0';
    char *file = trl_variable_copy("SSL_CERT_FILE");
    char *dirs = trl_variable_copy("SSL_CERT_DIR");
    size_t added = 0U;
    bool named = file != NULL;
    if (file != NULL) {
        added += add_pem_file(store, file, why, capacity);
    }
    if (dirs != NULL) {
        added += add_pem_dirs(store, dirs, &named, why, capacity);
    }
    if (!named) {
        added = add_platform(store, why, capacity);
    }
    free(file);
    free(dirs);
    return added;
}

#endif
