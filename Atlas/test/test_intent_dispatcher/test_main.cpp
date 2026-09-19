#include <unity.h>

#include "intent_dispatcher.h"

using TurnHub::Intent;
using TurnHub::IntentDispatcher;
using TurnHub::IntentResult;
using TurnHub::IntentStatus;
using TurnHub::IntentType;

namespace {

int handlerCalls = 0;
int alternateHandlerCalls = 0;
Intent capturedIntent;
void *capturedContext = nullptr;

IntentResult acceptingHandler(const Intent &intent, void *context) {
  ++handlerCalls;
  capturedIntent = intent;
  capturedContext = context;
  return IntentResult::accept("handled");
}

IntentResult alternateHandler(const Intent &, void *) {
  ++alternateHandlerCalls;
  return IntentResult::reject(IntentStatus::Conflict, "alternate");
}

}  // namespace

void setUp() {
  handlerCalls = 0;
  alternateHandlerCalls = 0;
  capturedIntent = Intent{};
  capturedContext = nullptr;
}

void tearDown() {}

void test_none_intent_is_rejected_as_unsupported() {
  IntentDispatcher dispatcher;
  Intent intent;

  const IntentResult result = dispatcher.dispatch(intent);

  TEST_ASSERT_FALSE(result.accepted());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(IntentStatus::Unsupported),
      static_cast<int>(result.status));
}

void test_valid_unbound_intent_is_rejected_as_unsupported() {
  IntentDispatcher dispatcher;
  Intent intent;
  intent.type = IntentType::Pass;

  const IntentResult result = dispatcher.dispatch(intent);

  TEST_ASSERT_FALSE(result.accepted());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(IntentStatus::Unsupported),
      static_cast<int>(result.status));
}

void test_bound_intent_dispatches_to_authoritative_handler() {
  IntentDispatcher dispatcher;
  int contextMarker = 42;

  TEST_ASSERT_TRUE(
      dispatcher.bind(IntentType::Pass, acceptingHandler, &contextMarker));
  TEST_ASSERT_TRUE(dispatcher.hasHandler(IntentType::Pass));

  Intent intent;
  intent.type = IntentType::Pass;
  intent.actor.moduleId = 3;
  intent.actor.slot = 1;
  intent.actor.playerNumber = 6;
  intent.payload.targetPlayer = 2;
  intent.payload.value = -10;
  intent.payload.flags = 0xA5;

  const IntentResult result = dispatcher.dispatch(intent);

  TEST_ASSERT_TRUE(result.accepted());
  TEST_ASSERT_EQUAL_INT(1, handlerCalls);
  TEST_ASSERT_EQUAL_PTR(&contextMarker, capturedContext);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(IntentType::Pass),
      static_cast<int>(capturedIntent.type));
  TEST_ASSERT_EQUAL_UINT8(3, capturedIntent.actor.moduleId);
  TEST_ASSERT_EQUAL_UINT8(1, capturedIntent.actor.slot);
  TEST_ASSERT_EQUAL_UINT8(6, capturedIntent.actor.playerNumber);
  TEST_ASSERT_EQUAL_UINT8(2, capturedIntent.payload.targetPlayer);
  TEST_ASSERT_EQUAL_INT32(-10, capturedIntent.payload.value);
  TEST_ASSERT_EQUAL_UINT32(0xA5, capturedIntent.payload.flags);
}

void test_duplicate_binding_is_refused_and_original_handler_remains_owner() {
  IntentDispatcher dispatcher;

  TEST_ASSERT_TRUE(dispatcher.bind(IntentType::Pass, acceptingHandler));
  TEST_ASSERT_FALSE(dispatcher.bind(IntentType::Pass, alternateHandler));

  Intent intent;
  intent.type = IntentType::Pass;
  const IntentResult result = dispatcher.dispatch(intent);

  TEST_ASSERT_TRUE(result.accepted());
  TEST_ASSERT_EQUAL_INT(1, handlerCalls);
  TEST_ASSERT_EQUAL_INT(0, alternateHandlerCalls);
}

void test_null_handler_cannot_be_bound() {
  IntentDispatcher dispatcher;

  TEST_ASSERT_FALSE(dispatcher.bind(IntentType::Pass, nullptr));
  TEST_ASSERT_FALSE(dispatcher.hasHandler(IntentType::Pass));
}

void test_none_and_count_cannot_be_bound() {
  IntentDispatcher dispatcher;

  TEST_ASSERT_FALSE(dispatcher.bind(IntentType::None, acceptingHandler));
  TEST_ASSERT_FALSE(dispatcher.bind(IntentType::Count, acceptingHandler));
}

void test_different_intent_types_can_have_independent_handlers() {
  IntentDispatcher dispatcher;

  TEST_ASSERT_TRUE(dispatcher.bind(IntentType::Pass, acceptingHandler));
  TEST_ASSERT_TRUE(dispatcher.bind(IntentType::Pause, alternateHandler));

  Intent passIntent;
  passIntent.type = IntentType::Pass;
  TEST_ASSERT_TRUE(dispatcher.dispatch(passIntent).accepted());

  Intent pauseIntent;
  pauseIntent.type = IntentType::Pause;
  const IntentResult pauseResult = dispatcher.dispatch(pauseIntent);

  TEST_ASSERT_FALSE(pauseResult.accepted());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(IntentStatus::Conflict),
      static_cast<int>(pauseResult.status));
  TEST_ASSERT_EQUAL_INT(1, handlerCalls);
  TEST_ASSERT_EQUAL_INT(1, alternateHandlerCalls);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_none_intent_is_rejected_as_unsupported);
  RUN_TEST(test_valid_unbound_intent_is_rejected_as_unsupported);
  RUN_TEST(test_bound_intent_dispatches_to_authoritative_handler);
  RUN_TEST(test_duplicate_binding_is_refused_and_original_handler_remains_owner);
  RUN_TEST(test_null_handler_cannot_be_bound);
  RUN_TEST(test_none_and_count_cannot_be_bound);
  RUN_TEST(test_different_intent_types_can_have_independent_handlers);
  return UNITY_END();
}
