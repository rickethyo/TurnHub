#include "intent_dispatcher.h"

namespace TurnHub {

namespace {

size_t intentIndex(IntentType type) {
  return static_cast<size_t>(type);
}

bool validIntentType(IntentType type) {
  const size_t index = intentIndex(type);
  return type != IntentType::None && index < static_cast<size_t>(IntentType::Count);
}

}  // namespace

IntentDispatcher::IntentDispatcher() = default;

bool IntentDispatcher::bind(
    IntentType type,
    Handler handler,
    void *context) {
  if (!validIntentType(type) || handler == nullptr) {
    return false;
  }

  Binding &binding = bindings_[intentIndex(type)];

  // Intent semantics get one owner. Refuse accidental replacement so duplicate
  // implementations become visible during integration rather than silently
  // changing behavior based on initialization order.
  if (binding.handler != nullptr) {
    return false;
  }

  binding.handler = handler;
  binding.context = context;
  return true;
}

bool IntentDispatcher::hasHandler(IntentType type) const {
  if (!validIntentType(type)) {
    return false;
  }
  return bindings_[intentIndex(type)].handler != nullptr;
}

IntentResult IntentDispatcher::dispatch(const Intent &intent) const {
  if (!validIntentType(intent.type)) {
    return IntentResult::reject(
        IntentStatus::Unsupported,
        "Unknown intent");
  }

  const Binding &binding = bindings_[intentIndex(intent.type)];
  if (binding.handler == nullptr) {
    return IntentResult::reject(
        IntentStatus::Unsupported,
        "Intent has no authoritative handler");
  }

  return binding.handler(intent, binding.context);
}

}  // namespace TurnHub
