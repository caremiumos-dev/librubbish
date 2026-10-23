#include <jni.h>

#include <android/log.h>
#include <sys/ptrace.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define RUBBISH_EXPORT __declspec(dllexport)
#else
#define RUBBISH_EXPORT __attribute__((visibility("default")))
#endif

namespace {

constexpr char kTag[] = "secretapi";
constexpr char kWrappedClass[] = "com/xtc/secretapi/XTCGetUtilWrapper";
constexpr jint kPackageManagerGetSignatures = 0x40;

void log_error(const char* message) {
    __android_log_print(ANDROID_LOG_ERROR, kTag, "%s", message);
}

class LocalRef {
public:
    LocalRef(JNIEnv* env, jobject ref) : env_(env), ref_(ref) {}
    LocalRef(const LocalRef&) = delete;
    LocalRef& operator=(const LocalRef&) = delete;

    LocalRef(LocalRef&& other) noexcept
        : env_(std::exchange(other.env_, nullptr)),
          ref_(std::exchange(other.ref_, nullptr)) {}

    LocalRef& operator=(LocalRef&& other) noexcept {
        if (this != &other) {
            reset();
            env_ = std::exchange(other.env_, nullptr);
            ref_ = std::exchange(other.ref_, nullptr);
        }
        return *this;
    }

    ~LocalRef() { reset(); }

    jobject get() const { return ref_; }
    explicit operator bool() const { return ref_ != nullptr; }

    jobject release() {
        jobject value = ref_;
        ref_ = nullptr;
        return value;
    }

private:
    void reset() {
        if (env_ != nullptr && ref_ != nullptr) {
            env_->DeleteLocalRef(ref_);
        }
        env_ = nullptr;
        ref_ = nullptr;
    }

    JNIEnv* env_;
    jobject ref_;
};

bool clear_exception(JNIEnv* env, const char* operation) {
    if (env->ExceptionCheck() == JNI_FALSE) {
        return false;
    }

    __android_log_print(ANDROID_LOG_ERROR, kTag, "%s failed", operation);
    env->ExceptionDescribe();
    env->ExceptionClear();
    return true;
}

jobject package_signature_bytes(JNIEnv* env, jobject context) {
    LocalRef context_class(env, env->GetObjectClass(context));
    if (!context_class) {
        log_error("cannot get context class");
        return nullptr;
    }

    jmethodID get_package_manager = env->GetMethodID(
        static_cast<jclass>(context_class.get()),
        "getPackageManager",
        "()Landroid/content/pm/PackageManager;");
    if (get_package_manager == nullptr || clear_exception(env, "getPackageManager lookup")) {
        return nullptr;
    }

    LocalRef package_manager(env, env->CallObjectMethod(context, get_package_manager));
    if (!package_manager || clear_exception(env, "getPackageManager")) {
        return nullptr;
    }

    LocalRef package_manager_class(env, env->GetObjectClass(package_manager.get()));
    if (!package_manager_class) {
        log_error("cannot get PackageManager class");
        return nullptr;
    }

    jmethodID get_package_name = env->GetMethodID(
        static_cast<jclass>(package_manager_class.get()),
        "getPackageName",
        "()Ljava/lang/String;");
    jmethodID get_package_info = env->GetMethodID(
        static_cast<jclass>(package_manager_class.get()),
        "getPackageInfo",
        "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;");
    if (get_package_name == nullptr || get_package_info == nullptr ||
        clear_exception(env, "PackageManager method lookup")) {
        return nullptr;
    }

    LocalRef package_name(env, env->CallObjectMethod(package_manager.get(), get_package_name));
    if (!package_name || clear_exception(env, "getPackageName")) {
        return nullptr;
    }

    LocalRef package_info(
        env,
        env->CallObjectMethod(
            package_manager.get(),
            get_package_info,
            package_name.get(),
            kPackageManagerGetSignatures));
    if (!package_info || clear_exception(env, "getPackageInfo")) {
        return nullptr;
    }

    LocalRef package_info_class(env, env->GetObjectClass(package_info.get()));
    if (!package_info_class) {
        log_error("cannot get PackageInfo class");
        return nullptr;
    }

    jfieldID signatures_field = env->GetFieldID(
        static_cast<jclass>(package_info_class.get()),
        "signatures",
        "[Landroid/content/pm/Signature;");
    if (signatures_field == nullptr || clear_exception(env, "signatures field lookup")) {
        return nullptr;
    }

    auto signatures = static_cast<jobjectArray>(
        env->GetObjectField(package_info.get(), signatures_field));
    LocalRef signatures_ref(env, signatures);
    if (!signatures_ref || env->GetArrayLength(signatures) < 1) {
        log_error("package has no bundled signature");
        clear_exception(env, "signatures array");
        return nullptr;
    }

    LocalRef signature(env, env->GetObjectArrayElement(signatures, 0));
    if (!signature || clear_exception(env, "signature lookup")) {
        return nullptr;
    }

    LocalRef signature_class(env, env->GetObjectClass(signature.get()));
    if (!signature_class) {
        log_error("cannot get Signature class");
        return nullptr;
    }

    jmethodID to_byte_array = env->GetMethodID(
        static_cast<jclass>(signature_class.get()),
        "toByteArray",
        "()[B");
    if (to_byte_array == nullptr || clear_exception(env, "toByteArray lookup")) {
        return nullptr;
    }

    LocalRef certificate_bytes(env, env->CallObjectMethod(signature.get(), to_byte_array));
    if (!certificate_bytes || clear_exception(env, "toByteArray")) {
        return nullptr;
    }

    // Match the original CertificateFactory -> getEncoded round trip.
    LocalRef stream_class(env, env->FindClass("java/io/ByteArrayInputStream"));
    LocalRef factory_class(env, env->FindClass("java/security/cert/CertificateFactory"));
    if (!stream_class || !factory_class || clear_exception(env, "certificate class lookup")) {
        return nullptr;
    }

    jmethodID stream_ctor = env->GetMethodID(
        static_cast<jclass>(stream_class.get()),
        "<init>",
        "([B)V");
    jmethodID factory_get_instance = env->GetStaticMethodID(
        static_cast<jclass>(factory_class.get()),
        "getInstance",
        "(Ljava/lang/String;)Ljava/security/cert/CertificateFactory;");
    if (stream_ctor == nullptr || factory_get_instance == nullptr ||
        clear_exception(env, "certificate method lookup")) {
        return nullptr;
    }

    LocalRef stream(
        env,
        env->NewObject(static_cast<jclass>(stream_class.get()), stream_ctor, certificate_bytes.get()));
    if (!stream || clear_exception(env, "ByteArrayInputStream construction")) {
        return nullptr;
    }

    LocalRef algorithm(env, env->NewStringUTF("X.509"));
    if (!algorithm) {
        log_error("cannot allocate X.509 string");
        return nullptr;
    }

    LocalRef factory(
        env,
        env->CallStaticObjectMethod(
            static_cast<jclass>(factory_class.get()),
            factory_get_instance,
            algorithm.get()));
    if (!factory || clear_exception(env, "CertificateFactory.getInstance")) {
        return nullptr;
    }

    LocalRef factory_object_class(env, env->GetObjectClass(factory.get()));
    if (!factory_object_class) {
        log_error("cannot get CertificateFactory class");
        return nullptr;
    }

    jmethodID generate_certificate = env->GetMethodID(
        static_cast<jclass>(factory_object_class.get()),
        "generateCertificate",
        "(Ljava/io/InputStream;)Ljava/security/cert/Certificate;");
    if (generate_certificate == nullptr || clear_exception(env, "generateCertificate lookup")) {
        return nullptr;
    }

    LocalRef certificate(
        env,
        env->CallObjectMethod(factory.get(), generate_certificate, stream.get()));
    if (!certificate || clear_exception(env, "generateCertificate")) {
        return nullptr;
    }

    LocalRef certificate_class(env, env->GetObjectClass(certificate.get()));
    if (!certificate_class) {
        log_error("cannot get Certificate class");
        return nullptr;
    }

    jmethodID get_encoded = env->GetMethodID(
        static_cast<jclass>(certificate_class.get()),
        "getEncoded",
        "()[B");
    if (get_encoded == nullptr || clear_exception(env, "getEncoded lookup")) {
        return nullptr;
    }

    LocalRef encoded(env, env->CallObjectMethod(certificate.get(), get_encoded));
    if (!encoded || clear_exception(env, "getEncoded")) {
        return nullptr;
    }
    return encoded.release();
}

std::string sha1_hex(JNIEnv* env, jobject signature_bytes) {
    LocalRef digest_class(env, env->FindClass("java/security/MessageDigest"));
    if (!digest_class) {
        log_error("cannot get MessageDigest class");
        return {};
    }

    jmethodID get_instance = env->GetStaticMethodID(
        static_cast<jclass>(digest_class.get()),
        "getInstance",
        "(Ljava/lang/String;)Ljava/security/MessageDigest;");
    if (get_instance == nullptr || clear_exception(env, "MessageDigest.getInstance lookup")) {
        return {};
    }

    LocalRef algorithm(env, env->NewStringUTF("SHA1"));
    if (!algorithm) {
        log_error("cannot allocate SHA1 string");
        return {};
    }

    LocalRef digest(
        env,
        env->CallStaticObjectMethod(
            static_cast<jclass>(digest_class.get()),
            get_instance,
            algorithm.get()));
    if (!digest || clear_exception(env, "MessageDigest.getInstance")) {
        return {};
    }

    LocalRef digest_object_class(env, env->GetObjectClass(digest.get()));
    if (!digest_object_class) {
        log_error("cannot get MessageDigest instance class");
        return {};
    }

    jmethodID digest_method = env->GetMethodID(
        static_cast<jclass>(digest_object_class.get()),
        "digest",
        "([B)[B");
    if (digest_method == nullptr || clear_exception(env, "MessageDigest.digest lookup")) {
        return {};
    }

    auto digest_bytes = static_cast<jbyteArray>(
        env->CallObjectMethod(digest.get(), digest_method, signature_bytes));
    LocalRef digest_bytes_ref(env, digest_bytes);
    if (!digest_bytes_ref || clear_exception(env, "MessageDigest.digest")) {
        return {};
    }

    const jsize digest_length = env->GetArrayLength(digest_bytes);
    if (digest_length < 0) {
        log_error("invalid digest length");
        return {};
    }

    std::vector<jbyte> bytes(static_cast<std::size_t>(digest_length));
    if (digest_length > 0) {
        env->GetByteArrayRegion(digest_bytes, 0, digest_length, bytes.data());
        if (clear_exception(env, "GetByteArrayRegion")) {
            return {};
        }
    }

    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(static_cast<std::size_t>(digest_length) * 2U);
    for (const jbyte byte : bytes) {
        const auto value = static_cast<unsigned char>(byte);
        result.push_back(kHex[(value >> 4U) & 0x0FU]);
        result.push_back(kHex[value & 0x0FU]);
    }
    return result;
}

jstring result_string(JNIEnv* env, const char* value) {
    jstring result = env->NewStringUTF(value);
    if (result == nullptr) {
        clear_exception(env, "NewStringUTF");
    }
    return result;
}

}  // namespace

extern "C" {

RUBBISH_EXPORT const char* AUTH_KEY = "xtc_ok";
RUBBISH_EXPORT const char* ERROR_KEY = "xtc_error";

RUBBISH_EXPORT jstring JNICALL get6(JNIEnv* env) {
    return result_string(env, AUTH_KEY);
}

RUBBISH_EXPORT jobject JNICALL getApplication(JNIEnv* env) {
    LocalRef activity_thread_class(env, env->FindClass("android/app/ActivityThread"));
    if (!activity_thread_class) {
        log_error("cannot get ActivityThread class");
        clear_exception(env, "ActivityThread lookup");
        return nullptr;
    }

    jmethodID current_application = env->GetStaticMethodID(
        static_cast<jclass>(activity_thread_class.get()),
        "currentApplication",
        "()Landroid/app/Application;");
    if (current_application == nullptr || clear_exception(env, "currentApplication lookup")) {
        return nullptr;
    }

    jobject application = env->CallStaticObjectMethod(
        static_cast<jclass>(activity_thread_class.get()),
        current_application);
    if (application == nullptr || clear_exception(env, "currentApplication")) {
        return nullptr;
    }
    return application;
}

RUBBISH_EXPORT char* JNICALL jstringToChar(JNIEnv* env, jstring value) {
    if (value == nullptr) {
        return nullptr;
    }

    LocalRef string_class(env, env->FindClass("java/lang/String"));
    if (!string_class) {
        log_error("cannot get String class");
        clear_exception(env, "String lookup");
        return nullptr;
    }

    jmethodID get_bytes = env->GetMethodID(
        static_cast<jclass>(string_class.get()),
        "getBytes",
        "(Ljava/lang/String;)[B");
    if (get_bytes == nullptr || clear_exception(env, "String.getBytes lookup")) {
        return nullptr;
    }

    LocalRef charset(env, env->NewStringUTF("utf-8"));
    if (!charset) {
        log_error("cannot allocate utf-8 string");
        return nullptr;
    }

    auto bytes = static_cast<jbyteArray>(env->CallObjectMethod(value, get_bytes, charset.get()));
    LocalRef bytes_ref(env, bytes);
    if (!bytes_ref || clear_exception(env, "String.getBytes")) {
        return nullptr;
    }

    const jsize length = env->GetArrayLength(bytes);
    if (length <= 0) {
        return nullptr;
    }

    jbyte* raw = env->GetByteArrayElements(bytes, nullptr);
    if (raw == nullptr) {
        clear_exception(env, "GetByteArrayElements");
        return nullptr;
    }

    auto* result = static_cast<char*>(std::malloc(static_cast<std::size_t>(length) + 1U));
    if (result != nullptr) {
        std::memcpy(result, raw, static_cast<std::size_t>(length));
        result[length] = '\0';
    }

    env->ReleaseByteArrayElements(bytes, raw, JNI_ABORT);
    clear_exception(env, "ReleaseByteArrayElements");
    return result;
}

RUBBISH_EXPORT jobject JNICALL getSignArray(JNIEnv* env, jobject context) {
    __android_log_print(ANDROID_LOG_DEBUG, kTag, "start getSignArray");
    return package_signature_bytes(env, context);
}

RUBBISH_EXPORT jstring JNICALL
Java_com_xtc_secretapi_XTCGetUtilWrapper_init(JNIEnv* env, jobject thiz, jstring expected) {
    char* expected_raw = jstringToChar(env, expected);
    const char* expected_value = expected_raw != nullptr ? expected_raw : "";
    __android_log_print(ANDROID_LOG_DEBUG, kTag, "verifyCode = %s", expected_value);

    LocalRef signature_bytes(env, getSignArray(env, thiz));
    if (!signature_bytes) {
        std::free(expected_raw);
        return result_string(env, ERROR_KEY);
    }

    const std::string digest_hex = sha1_hex(env, signature_bytes.get());
    if (digest_hex.empty()) {
        std::free(expected_raw);
        return result_string(env, ERROR_KEY);
    }

    __android_log_print(ANDROID_LOG_DEBUG, kTag, "hexCodes = %s", digest_hex.c_str());
    const int comparison = std::strcmp(expected_value, digest_hex.c_str());
    __android_log_print(ANDROID_LOG_DEBUG, kTag, "strcmpResult %d", comparison);

    std::free(expected_raw);
    return result_string(env, comparison == 0 ? AUTH_KEY : ERROR_KEY);
}

RUBBISH_EXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    // Preserve the original best-effort anti-debug probe; its result was ignored.
    static_cast<void>(ptrace(PTRACE_TRACEME, 0, nullptr, nullptr));

    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_4) != JNI_OK || env == nullptr) {
        log_error("cannot obtain JNI environment");
        return JNI_ERR;
    }

    LocalRef wrapper_class(env, env->FindClass(kWrappedClass));
    if (!wrapper_class) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "cannot get class:%s", kWrappedClass);
        clear_exception(env, "FindClass");
        return JNI_ERR;
    }

    const JNINativeMethod methods[] = {
        {const_cast<char*>("gete"), const_cast<char*>("()Ljava/lang/String;"),
         reinterpret_cast<void*>(&get6)},
    };

    if (env->RegisterNatives(
            static_cast<jclass>(wrapper_class.get()),
            methods,
            static_cast<jint>(sizeof(methods) / sizeof(methods[0]))) != JNI_OK) {
        log_error("register native method failed!");
        clear_exception(env, "RegisterNatives");
        return JNI_ERR;
    }
    return JNI_VERSION_1_4;
}

}  // extern "C"
