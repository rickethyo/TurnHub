#pragma once

#include <stddef.h>

#include "intent.h"

namespace TurnHub {

// Fixed, heap-free routing table from semantic IntentType to one authoritative
// handler. Adapters submit requests here instead of calling game behavior
// directly. A second handler cannot silently replace the first one.
class IntentDispatcher {
 public:
  using Handler = IntentResult (*)(const Intent &intent, void *context);

  IntentDispatcher();

  bool bind(IntentType type, Handler handler, void *context = nullptr);
  bool hasHandler(IntentType type) const;
  IntentResult dispatch(const Intent &intent) const;

 private:
  struct Binding {
    Handler handler = nullptr;
    void *context = nullptr;
  };

  static constexpr size_t HANDLER_COUNT =
      static_cast<size_t>(IntentType::Count);

  Binding bindings_[HANDLER_COUNT]{};
};

}  // namespace TurnHub
