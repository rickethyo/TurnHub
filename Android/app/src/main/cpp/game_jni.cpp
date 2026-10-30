#ifdef _MSC_VER
#define __attribute__(x)
#endif
#include <jni.h>
#ifdef _MSC_VER
#undef JNIEXPORT
#define JNIEXPORT __declspec(dllexport)
#endif
#include <stdexcept>
#include <string>
#include "turnhub_core.h"

namespace {
std::string bytes(JNIEnv *env, jbyteArray value) {
  if (!value) throw std::invalid_argument("Missing core input");
  std::string out(static_cast<size_t>(env->GetArrayLength(value)), '\0');
  env->GetByteArrayRegion(value, 0, static_cast<jsize>(out.size()), reinterpret_cast<jbyte *>(out.data()));
  return out;
}

class CallerRandom final : public turnhub::Random {
 public:
  CallerRandom(JNIEnv *env, jobject random) : env_(env), random_(random) {
    if (!random_) throw std::invalid_argument("Missing random source");
    const jclass cls = env_->GetObjectClass(random_);
    next_ = env_->GetMethodID(cls, "nextInt", "(I)I");
    coin_ = env_->GetMethodID(cls, "nextBoolean", "()Z");
    env_->DeleteLocalRef(cls);
    if (!next_ || !coin_) throw std::invalid_argument("Invalid random source");
  }
  int nextInt(int bound) override {
    const int value = env_->CallIntMethod(random_, next_, bound);
    if (env_->ExceptionCheck()) throw std::runtime_error("Random source failed");
    if (value < 0 || value >= bound) throw std::runtime_error("Random draw outside bound");
    return value;
  }
  bool nextBoolean() override {
    const bool value = env_->CallBooleanMethod(random_, coin_);
    if (env_->ExceptionCheck()) throw std::runtime_error("Random source failed");
    return value;
  }
 private:
  JNIEnv *env_;
  jobject random_;
  jmethodID next_;
  jmethodID coin_;
};
}

// UTF-8 byte arrays preserve supplementary Unicode characters across JNI.
extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_turnhub_android_standalone_NativeGameCore_dispatch(JNIEnv *env, jobject, jbyteArray state,
    jbyteArray action, jobject random) {
  try {
    bool valid = false;
    const auto input = turnhub::Json::parse(bytes(env, state), &valid);
    if (!valid || !input.isObject()) throw std::invalid_argument("Invalid core state JSON");
    const auto command = turnhub::Json::parse(bytes(env, action), &valid);
    if (!valid || !command.isObject()) throw std::invalid_argument("Invalid core action JSON");
    CallerRandom draws(env, random);
    const auto game = turnhub::gameFromJson(input);
    const auto outputValue = command.get("do").asString() == "record" ? turnhub::gameRecord(game) :
        turnhub::toJson(turnhub::applyAction(game, command, draws));
    const std::string output = outputValue.dump();
    auto result = env->NewByteArray(static_cast<jsize>(output.size()));
    if (result) env->SetByteArrayRegion(result, 0, static_cast<jsize>(output.size()),
        reinterpret_cast<const jbyte *>(output.data()));
    return result;
  } catch (const std::exception &e) {
    if (!env->ExceptionCheck()) env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), e.what());
    return nullptr;
  }
}
